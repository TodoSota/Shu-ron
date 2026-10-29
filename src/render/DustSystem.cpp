//==============================================================================
// DustSystem.cpp : 粉塵の発生・移動・描画の実装
//==============================================================================
#include "DustSystem.h"
#include "RenderSettings.h"
#include "../core/Shader.h"
#include "../mpm/MpmObject.h"
#include "../mpm/MpmPhysics.h"
#include <GLM/gtc/type_ptr.hpp>
#include <iostream>
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

DustSystem::DustSystem() {
	emitProgram = loadCompute("src/render/shaders/dust_emit.comp");
	updateProgram = loadCompute("src/render/shaders/dust_update.comp");
	drawProgram = loadProgram("src/render/shaders/dust.vert", "src/render/shaders/dust.frag");
	compositeProgram = loadProgram("src/render/shaders/fullscreen.vert", "src/render/shaders/wboit_composite.frag");

	// 粉塵のバッファ (位置 + 経過時間, 速度 + 寿命)。寿命 0 = 空きの印
	glGenBuffers(1, &dustBuffer);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, dustBuffer);
	glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(glm::vec4) * 2 * kCapacity, nullptr, GL_DYNAMIC_COPY);
	glGenBuffers(1, &counterBuffer);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, counterBuffer);
	glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(GLuint), nullptr, GL_DYNAMIC_COPY);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	clear();
}

DustSystem::~DustSystem() {
	glDeleteProgram(emitProgram);
	glDeleteProgram(updateProgram);
	glDeleteProgram(drawProgram);
	glDeleteProgram(compositeProgram);
	glDeleteBuffers(1, &dustBuffer);
	glDeleteBuffers(1, &counterBuffer);
	if (accumTex) glDeleteTextures(1, &accumTex);
	if (revealTex) glDeleteTextures(1, &revealTex);
	if (fbo) glDeleteFramebuffers(1, &fbo);
}

void DustSystem::clear() {
	// 全体を 0 で埋める (寿命 0 = すべて空き)
	const GLuint zero = 0;
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, dustBuffer);
	glClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, counterBuffer);
	glClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void DustSystem::update(const MpmObject& mpmObject, const MpmPhysics& physics, float dt, float particleRadius, const RenderSettings& settings) {
	if (dt <= 0.0f) return;
	time += dt;
	frameIndex++;

	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, mpmObject.getReadVbo());	// 砂の粒子 (読み出しのみ)
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, dustBuffer);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, counterBuffer);

	// --- (1) 発生: 砂の粒子 1 個 = 1 スレッド ---
	if (settings.dust && settings.dustEmitRate > 0.0f) {
		glUseProgram(emitProgram);
		glUniform1ui(glGetUniformLocation(emitProgram, "frameIndex"), frameIndex);
		setFloat(emitProgram, "emitRate", settings.dustEmitRate);
		setFloat(emitProgram, "speedThreshold", settings.dustSpeedThreshold);
		setFloat(emitProgram, "nearFloor", settings.dustNearFloor);
		setVec3(emitProgram, "floorNormal", physics.f_normal);
		setFloat(emitProgram, "floorHeight", physics.f_height);
		setFloat(emitProgram, "lifetime", settings.dustLifetime);
		setFloat(emitProgram, "jitter", particleRadius);
		glDispatchCompute((mpmObject.count + 63) / 64, 1, 1);
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
	}

	// --- (2) 移動と寿命: 粉塵 1 個 = 1 スレッド ---
	glUseProgram(updateProgram);
	setFloat(updateProgram, "dt", dt);
	setFloat(updateProgram, "time", time);
	setFloat(updateProgram, "drag", settings.dustDrag);
	setFloat(updateProgram, "sinkSpeed", settings.dustSinkSpeed);
	setFloat(updateProgram, "turbulence", settings.dustTurbulence);
	setVec3(updateProgram, "floorNormal", physics.f_normal);
	setFloat(updateProgram, "floorHeight", physics.f_height);
	glDispatchCompute(kCapacity / 64, 1, 1);
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

	glUseProgram(0);
}

void DustSystem::ensureTargets(int width, int height, GLuint sceneDepthTex) {
	if (fbo != 0 && width == fboWidth && height == fboHeight && sceneDepthTex == attachedDepth) return;
	if (accumTex) glDeleteTextures(1, &accumTex);
	if (revealTex) glDeleteTextures(1, &revealTex);
	if (fbo) glDeleteFramebuffers(1, &fbo);
	fboWidth = width; fboHeight = height; attachedDepth = sceneDepthTex;

	auto makeTex = [&](GLenum internalFormat, GLenum format) {
		GLuint t;
		glGenTextures(1, &t);
		glBindTexture(GL_TEXTURE_2D, t);
		glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, GL_FLOAT, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		return t;
	};
	// accum は 16 ビット浮動小数点 (最大 65504)。Σ w·α があふれないように、dust.frag の重み w の
	// 上限を 30 にしている (1 個あたり最大 30 × 0.15 = 4.5 なので、約 15,000 個重なるまであふれない)
	// ただし有効桁は約 3 桁なので、1 画素に数千個重なると 1 個分の寄与が丸めで消え、縞が出る。
	// 極端に濃い粉塵を使う場合は GL_RGBA32F にする (メモリと帯域は 2 倍)
	accumTex = makeTex(GL_RGBA16F, GL_RGBA);
	revealTex = makeTex(GL_R16F, GL_RED);
	glBindTexture(GL_TEXTURE_2D, 0);

	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, accumTex, 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, revealTex, 0);
	// 深度バッファは付けない。G-buffer の深度はシェーダーでサンプリングして前後判定と soft particle に使う
	// (同じテクスチャを深度バッファとして付けたままサンプリングすると、OpenGL では動作が未定義になる)
	const GLenum bufs[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
	glDrawBuffers(2, bufs);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		std::cerr << "Error: dust (WBOIT) framebuffer is incomplete" << std::endl;
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void DustSystem::draw(GLuint sceneDepthTex, int width, int height, const glm::mat4& view, const glm::mat4& projection,
	GLuint shadowTex, const glm::mat4& lightViewProjection, float particleRadius, const RenderSettings& settings, GLuint emptyVao) {
	ensureTargets(width, height, sceneDepthTex);

	// --- (H-1) WBOIT の蓄積: accum = Σ w·(α·c, α), revealage = Π(1 - α) ---
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glViewport(0, 0, width, height);
	const float clearAccum[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	const float clearReveal[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
	glClearBufferfv(GL_COLOR, 0, clearAccum);
	glClearBufferfv(GL_COLOR, 1, clearReveal);

	glDisable(GL_DEPTH_TEST);	// 前後判定はシェーダーで G-buffer の深度と比べて行う
	glEnable(GL_BLEND);
	glBlendFunci(0, GL_ONE, GL_ONE);
	glBlendFunci(1, GL_ZERO, GL_ONE_MINUS_SRC_COLOR);

	glUseProgram(drawProgram);
	setMat4(drawProgram, "view", view);
	setMat4(drawProgram, "projection", projection);
	setMat4(drawProgram, "lightViewProjection", lightViewProjection);
	setFloat(drawProgram, "viewportHeight", static_cast<float>(height));
	setFloat(drawProgram, "baseSize", settings.dustSize * particleRadius);
	setFloat(drawProgram, "growth", settings.dustGrowth);
	setFloat(drawProgram, "opacity", settings.dustOpacity);
	setVec3(drawProgram, "dustColor", settings.dustColor);
	setVec3(drawProgram, "sunDirection", settings.sunDirection());
	setVec3(drawProgram, "sunColor", settings.sunColor);
	setFloat(drawProgram, "sunIntensity", settings.sunIntensity);
	setVec3(drawProgram, "skyColor", settings.skyColor);
	setFloat(drawProgram, "ambientIntensity", settings.ambientIntensity);
	setFloat(drawProgram, "exposure", settings.exposure);
	setFloat(drawProgram, "softRange", 4.0f * particleRadius);
	setInt(drawProgram, "useShadow", (settings.shadows && shadowTex != 0) ? 1 : 0);
	glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, sceneDepthTex);
	glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, shadowTex);
	setInt(drawProgram, "sceneDepth", 0);
	setInt(drawProgram, "shadowTex", 1);

	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, dustBuffer);
	const bool compat = isCompatibilityProfile();
	glEnable(GL_PROGRAM_POINT_SIZE);
	if (compat) glEnable(kPointSprite);
	glBindVertexArray(emptyVao);
	glDrawArrays(GL_POINTS, 0, kCapacity);	// 空きの粉塵は頂点シェーダーで画面外に出す
	if (compat) glDisable(kPointSprite);
	glDisable(GL_PROGRAM_POINT_SIZE);

	// --- (H-2) 合成: 平均の色を、不透明度 (1 - revealage) で画面に重ねる ---
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, width, height);
	glDisable(GL_DEPTH_TEST);
	glBlendFunc(GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA);
	glUseProgram(compositeProgram);
	glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, accumTex);
	glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, revealTex);
	setInt(compositeProgram, "accumTex", 0);
	setInt(compositeProgram, "revealTex", 1);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glBindVertexArray(0);

	// 後始末 (この後の枠線・ImGui のために既定の状態へ戻す)
	glDisable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ZERO);
	glEnable(GL_DEPTH_TEST);
	glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, 0);
	glUseProgram(0);
}
