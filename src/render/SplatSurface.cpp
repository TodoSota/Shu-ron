//==============================================================================
// SplatSurface.cpp : 格子による表面粒子の抽出の実装
//==============================================================================
#include "SplatSurface.h"
#include "RenderSettings.h"
#include "../core/Shader.h"
#include "../mpm/MpmObject.h"
#include <algorithm>
#include <cmath>

namespace {
	void setFloat(GLuint prog, const char* name, float v) { glUniform1f(glGetUniformLocation(prog, name), v); }
	void setInt(GLuint prog, const char* name, int v) { glUniform1i(glGetUniformLocation(prog, name), v); }
}

SplatSurface::SplatSurface() {
	splatProgram = loadCompute("src/render/shaders/density_splat.comp");
	resolveProgram = loadCompute("src/render/shaders/density_resolve.comp");
	blurProgram = loadCompute("src/render/shaders/density_blur.comp");
	surfelProgram = loadCompute("src/render/shaders/surfel_prepare.comp");
}

SplatSurface::~SplatSurface() {
	glDeleteProgram(splatProgram);
	glDeleteProgram(resolveProgram);
	glDeleteProgram(blurProgram);
	glDeleteProgram(surfelProgram);
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
