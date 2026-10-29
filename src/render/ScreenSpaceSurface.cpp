//==============================================================================
// ScreenSpaceSurface.cpp : スクリーン空間で粒子の面を張る処理の実装
//==============================================================================
#include "ScreenSpaceSurface.h"
#include "RenderSettings.h"
#include "../core/Shader.h"
#include <GLM/gtc/type_ptr.hpp>
#include <vector>

ScreenSpaceSurface::ScreenSpaceSurface() {
	smoothProgram = loadProgram("src/render/shaders/fullscreen.vert", "src/render/shaders/smooth_depth.frag");
	normalProgram = loadProgram("src/render/shaders/fullscreen.vert", "src/render/shaders/normal_from_depth.frag");
	compositeProgram = loadProgram("src/render/shaders/fullscreen.vert", "src/render/shaders/surface_composite.frag");
}

ScreenSpaceSurface::~ScreenSpaceSurface() {
	glDeleteProgram(smoothProgram);
	glDeleteProgram(normalProgram);
	glDeleteProgram(compositeProgram);
}

void ScreenSpaceSurface::run(const RenderTarget& gbuffer, const glm::mat4& projection, const glm::mat4& view,
	float particleRadius, const RenderSettings& settings, GLuint emptyVao) {
	const int w = gbuffer.getWidth(), h = gbuffer.getHeight();

	// 作業用のターゲットを G-buffer と同じ大きさで用意する
	const std::vector<ColorAttachmentDesc> depthFmt = { { GL_R32F, GL_RED, GL_FLOAT, GL_NEAREST } };
	const std::vector<ColorAttachmentDesc> surfFmt = { { GL_RGBA32F, GL_RGBA, GL_FLOAT, GL_NEAREST } };
	if (pingTarget.framebuffer() == 0) {
		pingTarget.create(w, h, depthFmt, false);
		pongTarget.create(w, h, depthFmt, false);
		surfaceTarget.create(w, h, surfFmt, false);
	}
	else {
		pingTarget.resize(w, h);
		pongTarget.resize(w, h);
		surfaceTarget.resize(w, h);
	}

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glBindVertexArray(emptyVao);

	// --- (D) 深度の平滑化: 横 → 縦 を smoothIterations 回繰り返す ---
	//   入力は G-buffer の線形深度 (location 2)。結果は pingTarget に残る
	glUseProgram(smoothProgram);
	glUniform1i(glGetUniformLocation(smoothProgram, "depthTex"), 0);
	glUniform1f(glGetUniformLocation(smoothProgram, "worldRadius"), settings.smoothRadiusScale * particleRadius);
	// 深度 1 m の位置で 1 m が何ピクセルになるか (= P[1][1] · H / 2)
	glUniform1f(glGetUniformLocation(smoothProgram, "pixelsPerMeter"), projection[1][1] * h * 0.5f);
	glUniform1f(glGetUniformLocation(smoothProgram, "depthThreshold"), settings.smoothDepthScale * particleRadius);
	GLuint src = gbuffer.colorTexture(2);
	const int iterations = (settings.smoothIterations > 0) ? settings.smoothIterations : 0;
	for (int it = 0; it < iterations; ++it) {
		// 横方向: src → pong
		pongTarget.bind();
		glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, src);
		glUniform2i(glGetUniformLocation(smoothProgram, "direction"), 1, 0);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		// 縦方向: pong → ping
		pingTarget.bind();
		glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, pongTarget.colorTexture(0));
		glUniform2i(glGetUniformLocation(smoothProgram, "direction"), 0, 1);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		src = pingTarget.colorTexture(0);
	}
	if (iterations == 0) src = gbuffer.colorTexture(2);	// 平滑化しない (段階 1 とほぼ同じ見た目になる)

	// --- (E) 法線の再構成 ---
	surfaceTarget.bind();
	glUseProgram(normalProgram);
	glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, src);
	glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, gbuffer.colorTexture(1));
	glUniform1i(glGetUniformLocation(normalProgram, "smoothDepth"), 0);
	glUniform1i(glGetUniformLocation(normalProgram, "gNormal"), 1);
	glUniformMatrix4fv(glGetUniformLocation(normalProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
	glUniformMatrix4fv(glGetUniformLocation(normalProgram, "invView"), 1, GL_FALSE, glm::value_ptr(glm::inverse(view)));
	glUniform1f(glGetUniformLocation(normalProgram, "sphereNormalMix"), settings.sphereNormalMix);
	glUniform1f(glGetUniformLocation(normalProgram, "grainStrength"), settings.grainStrength);
	glUniform1f(glGetUniformLocation(normalProgram, "grainSize"), settings.grainScale * particleRadius);
	glDrawArrays(GL_TRIANGLES, 0, 3);

	// --- (E') G-buffer への書き戻し: 粒子の画素だけ、法線 (location 1) と深度を置き換える ---
	glBindFramebuffer(GL_FRAMEBUFFER, gbuffer.framebuffer());
	glViewport(0, 0, w, h);
	const GLenum onlyNormal[3] = { GL_NONE, GL_COLOR_ATTACHMENT1, GL_NONE };
	glDrawBuffers(3, onlyNormal);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_ALWAYS);
	glDepthMask(GL_TRUE);
	glUseProgram(compositeProgram);
	glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, surfaceTarget.colorTexture(0));
	glUniform1i(glGetUniformLocation(compositeProgram, "surfaceTex"), 0);
	glUniformMatrix4fv(glGetUniformLocation(compositeProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
	glDrawArrays(GL_TRIANGLES, 0, 3);
	const GLenum all[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
	glDrawBuffers(3, all);
	glDepthFunc(GL_LESS);

	glBindVertexArray(0);
	glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, 0);
}
