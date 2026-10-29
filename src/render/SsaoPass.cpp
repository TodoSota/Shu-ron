//==============================================================================
// SsaoPass.cpp : SSAO の実装
//==============================================================================
#include "SsaoPass.h"
#include "RenderSettings.h"
#include "../core/Shader.h"
#include <GLM/gtc/type_ptr.hpp>
#include <random>
#include <vector>

SsaoPass::SsaoPass() {
	ssaoProgram = loadProgram("src/render/shaders/fullscreen.vert", "src/render/shaders/ssao.frag");
	blurProgram = loadProgram("src/render/shaders/fullscreen.vert", "src/render/shaders/ssao_blur.frag");

	// 法線方向 (+z) の半球内のサンプル。中心に近いほど密になるように長さを調整する
	// 接平面すれすれのサンプルは、斜めから見た平面で自己遮蔽 (縞模様) を起こすので、
	// z を 0.3 以上にして、天頂角が約 73° 以内の円錐に限定する
	std::mt19937 rng(1234);	// 毎回同じ結果になるように固定シード
	std::uniform_real_distribution<float> u(0.0f, 1.0f);
	for (int i = 0; i < kKernelSize; ++i) {
		glm::vec3 s(u(rng) * 2.0f - 1.0f, u(rng) * 2.0f - 1.0f, 0.3f + 0.7f * u(rng));
		s = glm::normalize(s) * u(rng);
		float t = static_cast<float>(i) / kKernelSize;
		s *= 0.1f + 0.9f * t * t;
		kernel[i] = s;
	}

	// 4×4 の回転用ノイズ (接平面内のランダムな向き)。画面全体に敷き詰めて使う
	std::vector<glm::vec2> noise(16);
	for (auto& n : noise) n = glm::vec2(u(rng) * 2.0f - 1.0f, u(rng) * 2.0f - 1.0f);
	glGenTextures(1, &noiseTex);
	glBindTexture(GL_TEXTURE_2D, noiseTex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, 4, 4, 0, GL_RG, GL_FLOAT, noise.data());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glBindTexture(GL_TEXTURE_2D, 0);
}

SsaoPass::~SsaoPass() {
	glDeleteProgram(ssaoProgram);
	glDeleteProgram(blurProgram);
	glDeleteTextures(1, &noiseTex);
}

GLuint SsaoPass::run(GLuint depthTex, GLuint normalTex, int fullWidth, int fullHeight,
	const glm::mat4& projection, float radius, const RenderSettings& settings, GLuint emptyVao) {
	// 半解像度のターゲットを用意 (線形補間で拡大して使う)
	const int w = (fullWidth + 1) / 2, h = (fullHeight + 1) / 2;
	const std::vector<ColorAttachmentDesc> fmt = { { GL_R8, GL_RED, GL_UNSIGNED_BYTE, GL_LINEAR } };
	if (aoTarget.framebuffer() == 0) { aoTarget.create(w, h, fmt, false); blurTarget.create(w, h, fmt, false); }
	else { aoTarget.resize(w, h); blurTarget.resize(w, h); }

	const glm::mat4 invProj = glm::inverse(projection);

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glBindVertexArray(emptyVao);

	// --- 1. AO の計算 ---
	aoTarget.bind();
	glUseProgram(ssaoProgram);
	glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, depthTex);
	glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, normalTex);
	glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, noiseTex);
	glUniform1i(glGetUniformLocation(ssaoProgram, "gDepth"), 0);
	glUniform1i(glGetUniformLocation(ssaoProgram, "gNormal"), 1);
	glUniform1i(glGetUniformLocation(ssaoProgram, "noiseTex"), 2);
	glUniform3fv(glGetUniformLocation(ssaoProgram, "kernel"), kKernelSize, glm::value_ptr(kernel[0]));
	glUniformMatrix4fv(glGetUniformLocation(ssaoProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
	glUniformMatrix4fv(glGetUniformLocation(ssaoProgram, "invProjection"), 1, GL_FALSE, glm::value_ptr(invProj));
	glUniform2f(glGetUniformLocation(ssaoProgram, "noiseScale"), w / 4.0f, h / 4.0f);
	glUniform1f(glGetUniformLocation(ssaoProgram, "radius"), radius);
	glUniform1f(glGetUniformLocation(ssaoProgram, "bias"), settings.ssaoBias);
	glUniform1f(glGetUniformLocation(ssaoProgram, "power"), settings.ssaoPower);
	glDrawArrays(GL_TRIANGLES, 0, 3);

	// --- 2. ノイズの縞を消すぼかし (深度が大きく違う画素は混ぜない) ---
	blurTarget.bind();
	glUseProgram(blurProgram);
	glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, aoTarget.colorTexture(0));
	glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, depthTex);
	glUniform1i(glGetUniformLocation(blurProgram, "aoTex"), 0);
	glUniform1i(glGetUniformLocation(blurProgram, "gDepth"), 1);
	glUniformMatrix4fv(glGetUniformLocation(blurProgram, "invProjection"), 1, GL_FALSE, glm::value_ptr(invProj));
	glUniform1f(glGetUniformLocation(blurProgram, "depthThreshold"), radius);
	glDrawArrays(GL_TRIANGLES, 0, 3);

	glBindVertexArray(0);
	glActiveTexture(GL_TEXTURE0);
	return blurTarget.colorTexture(0);
}
