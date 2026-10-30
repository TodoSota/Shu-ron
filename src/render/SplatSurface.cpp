//==============================================================================
// SplatSurface.cpp : 格子による表面粒子の抽出の実装
//==============================================================================
#include "SplatSurface.h"
#include "RenderSettings.h"
#include "../core/Shader.h"
#include "../mpm/MpmObject.h"
#include <GLM/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {
	void setMat4(GLuint prog, const char* name, const glm::mat4& m) { glUniformMatrix4fv(glGetUniformLocation(prog, name), 1, GL_FALSE, glm::value_ptr(m)); }
	void setVec3(GLuint prog, const char* name, const glm::vec3& v) { glUniform3fv(glGetUniformLocation(prog, name), 1, glm::value_ptr(v)); }
	void setFloat(GLuint prog, const char* name, float v) { glUniform1f(glGetUniformLocation(prog, name), v); }
	void setInt(GLuint prog, const char* name, int v) { glUniform1i(glGetUniformLocation(prog, name), v); }

	constexpr GLenum kPointSprite = 0x8861;	// 互換プロファイルで gl_PointCoord を使うため (Renderer.cpp と同じ)
	bool isCompatibilityProfile() {
		GLint mask = 0;
		glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &mask);
		return (mask & GL_CONTEXT_CORE_PROFILE_BIT) == 0;
	}
}

SplatSurface::SplatSurface() {
	splatProgram = loadCompute("src/render/shaders/density_splat.comp");
	resolveProgram = loadCompute("src/render/shaders/density_resolve.comp");
	blurProgram = loadCompute("src/render/shaders/density_blur.comp");
	surfelProgram = loadCompute("src/render/shaders/surfel_prepare.comp");
	drawProgram = loadProgram("src/render/shaders/splat.vert", "src/render/shaders/splat.frag");
	normalizeProgram = loadProgram("src/render/shaders/fullscreen.vert", "src/render/shaders/splat_normalize.frag");
}

SplatSurface::~SplatSurface() {
	glDeleteProgram(splatProgram);
	glDeleteProgram(resolveProgram);
	glDeleteProgram(blurProgram);
	glDeleteProgram(surfelProgram);
	glDeleteProgram(drawProgram);
	glDeleteProgram(normalizeProgram);
	if (densityBuffer) glDeleteBuffers(1, &densityBuffer);
	if (densityTex[0]) glDeleteTextures(2, densityTex);
	if (surfels) glDeleteBuffers(1, &surfels);
}

// 一辺 n セルの格子を用意する (大きさが変わったときだけ作り直す)
void SplatSurface::ensureGrid(int n) {
	if (n == gridN && densityBuffer != 0) return;
	if (densityBuffer) glDeleteBuffers(1, &densityBuffer);
	if (densityTex[0]) glDeleteTextures(2, densityTex);
	gridN = n;

	glGenBuffers(1, &densityBuffer);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, densityBuffer);
	glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(GLuint) * static_cast<GLsizeiptr>(n) * n * n, nullptr, GL_DYNAMIC_COPY);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

	glGenTextures(2, densityTex);
	const GLfloat border[4] = { 0.0f, 0.0f, 0.0f, 0.0f };	// 格子の外は空 (密度 0)
	for (int i = 0; i < 2; ++i) {
		glBindTexture(GL_TEXTURE_3D, densityTex[i]);
		glTexStorage3D(GL_TEXTURE_3D, 1, GL_R32F, n, n, n);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_BORDER);
		glTexParameterfv(GL_TEXTURE_3D, GL_TEXTURE_BORDER_COLOR, border);
	}
	glBindTexture(GL_TEXTURE_3D, 0);
}

// 粒子 count 個分の surfel バッファを用意する
void SplatSurface::ensureSurfels(GLsizei count) {
	if (count <= surfelCapacity && surfels != 0) return;
	if (surfels) glDeleteBuffers(1, &surfels);
	surfelCapacity = count;
	glGenBuffers(1, &surfels);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, surfels);
	glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(GLfloat) * 8 * static_cast<GLsizeiptr>(count), nullptr, GL_DYNAMIC_COPY);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void SplatSurface::prepare(const MpmObject& mpmObject, float worldScale, const RenderSettings& settings) {
	const GLsizei count = mpmObject.count;
	if (count <= 0) return;

	// --- 格子の大きさ: セルの一辺 h = densityCellScale × 粒子間隔。セル数が上限を超えたら h を広げる ---
	const float spacing = std::max(settings.particleSpacing, 1e-5f);
	float h = std::max(settings.densityCellScale, 0.5f) * spacing;
	int n = static_cast<int>(std::ceil(worldScale / h));
	if (n > kMaxGrid) { n = kMaxGrid; h = worldScale / kMaxGrid; }
	n = std::max(n, 8);
	ensureGrid(n);
	ensureSurfels(count);
	cell = h;
	rhoRef = std::pow(h / spacing, 3.0f);

	const GLuint groups3 = static_cast<GLuint>((n + 7) / 8);
	const GLuint groupsZ = static_cast<GLuint>((n + 3) / 4);
	const GLuint groupsP = static_cast<GLuint>((count + 255) / 256);

	// --- S1: 粒子 → 密度格子 ---
	const GLuint zero = 0;
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, densityBuffer);
	glClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, mpmObject.getReadVbo());
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kDensityBinding, densityBuffer);
	glUseProgram(splatProgram);
	setInt(splatProgram, "particleCount", count);
	setInt(splatProgram, "gridN", n);
	setFloat(splatProgram, "cellSize", h);
	glDispatchCompute(groupsP, 1, 1);
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

	// --- S2: float への変換 (→ densityTex[0]) ---
	glUseProgram(resolveProgram);
	setInt(resolveProgram, "gridN", n);
	glBindImageTexture(0, densityTex[0], 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R32F);
	glDispatchCompute(groups3, groups3, groupsZ);
	glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
	finalTex = 0;

	// --- S2: ガウスぼかし x → y → z (0 → 1 → 0 → 1) ---
	if (settings.densityBlurSigma > 0.0f) {
		glUseProgram(blurProgram);
		setInt(blurProgram, "gridN", n);
		setFloat(blurProgram, "sigma", settings.densityBlurSigma);
		setInt(blurProgram, "src", 0);
		const GLint dirs[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
		for (int d = 0; d < 3; ++d) {
			const int from = finalTex, to = 1 - finalTex;
			glUniform3iv(glGetUniformLocation(blurProgram, "direction"), 1, dirs[d]);
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_3D, densityTex[from]);
			glBindImageTexture(0, densityTex[to], 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R32F);
			glDispatchCompute(groups3, groups3, groupsZ);
			glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
			finalTex = to;
		}
	}
	glBindImageTexture(0, 0, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R32F);

	// --- S3: 粒子ごとの表面らしさ・法線・密度 ---
	glUseProgram(surfelProgram);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_3D, densityTex[finalTex]);
	setInt(surfelProgram, "density", 0);
	setInt(surfelProgram, "particleCount", count);
	setFloat(surfelProgram, "gridExtent", h * n);
	setFloat(surfelProgram, "cellSize", h);
	setFloat(surfelProgram, "rhoRef", rhoRef);
	setFloat(surfelProgram, "outsideOffset", settings.outsideOffsetScale * h);
	// しきい値は ρ_ref に対する割合で持ち、lo < hi になるように並べる
	const float lo = std::min(settings.surfaceLo, settings.surfaceHi - 0.01f);
	setFloat(surfelProgram, "surfaceLo", lo * rhoRef);
	setFloat(surfelProgram, "surfaceHi", settings.surfaceHi * rhoRef);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kSurfelBinding, surfels);
	glDispatchCompute(groupsP, 1, 1);
	// 次に surfel バッファを読むのは頂点シェーダー (SSBO)
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

	glBindTexture(GL_TEXTURE_3D, 0);
	glUseProgram(0);
}

void SplatSurface::draw(const MpmObject& mpmObject, const RenderTarget& gbuffer, const glm::mat4& view, const glm::mat4& projection,
	bool useDebugColor, const RenderSettings& settings, GLuint emptyVao) {
	if (surfels == 0 || mpmObject.count <= 0) return;
	const int w = gbuffer.getWidth(), h = gbuffer.getHeight();

	// 蓄積用のターゲット (深度は G-buffer と同じ D32F。P1 でコピーするため形式を揃える)
	if (accumTarget.framebuffer() == 0) {
		accumTarget.create(w, h, {
			{ GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_NEAREST },	// Σw·色, Σw (重み w ≤ 1、重なりは数十枚なので 16F で足りる)
			{ GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_NEAREST },	// Σw·法線, Σw·あいまいさ
			{ GL_R32F,    GL_RED,  GL_FLOAT, GL_NEAREST },	// Σw·線形深度 (精度のため 32F)
			{ GL_R16F,    GL_RED,  GL_FLOAT, GL_NEAREST },	// 視線上のすべての円盤の Σw (被覆率)
		}, true);
	}
	else {
		accumTarget.resize(w, h);
	}
	// splat 層 (P4 の出力。陰影の計算で背景の上に重ねる)
	if (layerTarget.framebuffer() == 0) {
		layerTarget.create(w, h, {
			{ GL_RGBA8,   GL_RGBA, GL_UNSIGNED_BYTE, GL_NEAREST },	// 色 + 被覆率 α
			{ GL_RGBA16F, GL_RGBA, GL_FLOAT,         GL_NEAREST },	// 法線 + あいまいさ
			{ GL_R32F,    GL_RED,  GL_FLOAT,         GL_NEAREST },	// 線形深度 (0 = なし)
		}, false);
	}
	else {
		layerTarget.resize(w, h);
	}

	// --- P1: G-buffer の深度 (床・障害物) をコピーし、蓄積バッファを 0 にする ---
	//   G-buffer の深度を直接書き換えないのは、段階 3 で半透明の輪郭の背後にある床の深度を残すため
	glBindFramebuffer(GL_READ_FRAMEBUFFER, gbuffer.framebuffer());
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, accumTarget.framebuffer());
	glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
	accumTarget.bind();
	const GLfloat zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	// glClearBufferfv の番号は描画バッファの番号なので、4 枚すべてを描画バッファにしてから消す
	// (前のフレームの最後に、被覆率の 3 番を外した設定が残っている)
	const GLenum allBuffers[4] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3 };
	glDrawBuffers(4, allBuffers);
	for (int i = 0; i < 4; ++i) glClearBufferfv(GL_COLOR, i, zero);

	// --- 円盤の描画の準備 ---
	const float radius = settings.splatRadiusScale * settings.particleSpacing;
	glUseProgram(drawProgram);
	setMat4(drawProgram, "view", view);
	setMat4(drawProgram, "projection", projection);
	setFloat(drawProgram, "splatRadius", radius);
	setFloat(drawProgram, "viewportHeight", static_cast<float>(h));
	glUniform2f(glGetUniformLocation(drawProgram, "viewport"), static_cast<float>(w), static_cast<float>(h));
	setFloat(drawProgram, "surfaceCutoff", settings.surfaceCutoff);
	setFloat(drawProgram, "rhoRef", rhoRef);
	setInt(drawProgram, "surfelDebug", static_cast<int>(settings.surfelDebug));
	setFloat(drawProgram, "splatSharpness", settings.splatSharpness);
	setFloat(drawProgram, "depthOffset", settings.splatDepthOffsetScale * radius);
	setVec3(drawProgram, "sandColor", settings.sandColor);
	setVec3(drawProgram, "stoneColor", settings.stoneColor);
	setFloat(drawProgram, "colorVariation", settings.colorVariation);
	setInt(drawProgram, "useDebugColor", useDebugColor ? 1 : 0);

	const bool compat = isCompatibilityProfile();
	const GLboolean smoothWasOn = glIsEnabled(GL_POINT_SMOOTH);
	glEnable(GL_PROGRAM_POINT_SIZE);
	if (compat) { glEnable(kPointSprite); glDisable(GL_POINT_SMOOTH); }
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, mpmObject.getReadVbo());
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kSurfelBinding, surfels);
	glBindVertexArray(mpmObject.getRenderVao());
	glEnable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);

	// --- 被覆率: 床・障害物の深度とだけ比べ、視線上のすべての円盤の重みを足す (裏向きも含む) ---
	const GLenum coverageOnly[4] = { GL_NONE, GL_NONE, GL_NONE, GL_COLOR_ATTACHMENT3 };
	glDrawBuffers(4, coverageOnly);
	setInt(drawProgram, "pass", 2);
	setInt(drawProgram, "cullBackFacing", 0);
	glDepthMask(GL_FALSE);
	glDepthFunc(GL_LEQUAL);
	glEnable(GL_BLEND);
	glBlendEquation(GL_FUNC_ADD);
	glBlendFunc(GL_ONE, GL_ONE);
	glDrawArrays(GL_POINTS, 0, mpmObject.count);
	glDisable(GL_BLEND);
	const GLenum accumOnly[4] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_NONE };
	glDrawBuffers(4, accumOnly);
	setInt(drawProgram, "cullBackFacing", 1);

	// --- P2: 可視性 (深度だけ。奥へずらした深度で、手前の表面の厚みを決める) ---
	setInt(drawProgram, "pass", 0);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	glDepthMask(GL_TRUE);
	glDepthFunc(GL_LESS);
	glDrawArrays(GL_POINTS, 0, mpmObject.count);

	// --- P3: 蓄積 (厚みの内側の円盤を加算。深度は書かない) ---
	setInt(drawProgram, "pass", 1);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDepthMask(GL_FALSE);
	glDepthFunc(GL_LEQUAL);
	glEnable(GL_BLEND);
	glBlendEquation(GL_FUNC_ADD);
	glBlendFunc(GL_ONE, GL_ONE);
	glDrawArrays(GL_POINTS, 0, mpmObject.count);
	glDisable(GL_BLEND);
	glDepthMask(GL_TRUE);

	glBindVertexArray(0);
	glDisable(GL_PROGRAM_POINT_SIZE);
	if (compat) { glDisable(kPointSprite); if (smoothWasOn) glEnable(GL_POINT_SMOOTH); }
	glEnable(GL_CULL_FACE);

	// --- P4/P5: 正規化 ---
	glUseProgram(normalizeProgram);
	for (int i = 0; i < 4; ++i) {
		glActiveTexture(GL_TEXTURE0 + i);
		glBindTexture(GL_TEXTURE_2D, accumTarget.colorTexture(i));
	}
	setInt(normalizeProgram, "accum0", 0);
	setInt(normalizeProgram, "accum1", 1);
	setInt(normalizeProgram, "accum2", 2);
	setInt(normalizeProgram, "coverageTex", 3);
	setMat4(normalizeProgram, "projection", projection);
	setFloat(normalizeProgram, "coverageK", settings.coverageK);
	setFloat(normalizeProgram, "opaqueAlpha", settings.opaqueAlpha);
	glBindVertexArray(emptyVao);

	// P4: splat 層へ (円盤がかかった画素すべて。深度は使わない)
	layerTarget.bind();
	for (int i = 0; i < 3; ++i) glClearBufferfv(GL_COLOR, i, zero);
	glDisable(GL_DEPTH_TEST);
	setInt(normalizeProgram, "target", 0);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glEnable(GL_DEPTH_TEST);

	// P5: G-buffer へ (不透明な画素だけ。床・障害物より手前の画素だけ: GL_LESS)
	gbuffer.bind();
	glDepthFunc(GL_LESS);
	setInt(normalizeProgram, "target", 1);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glBindVertexArray(0);
	for (int i = 3; i >= 0; --i) { glActiveTexture(GL_TEXTURE0 + i); glBindTexture(GL_TEXTURE_2D, 0); }
	glUseProgram(0);
}
