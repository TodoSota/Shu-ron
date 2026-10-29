//==============================================================================
// ShadowMap.cpp : 太陽のシャドウマップの実装
//==============================================================================
#include "ShadowMap.h"
#include <GLM/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>

ShadowMap::~ShadowMap() {
	if (depthTex) glDeleteTextures(1, &depthTex);
	if (fbo) glDeleteFramebuffers(1, &fbo);
}

void ShadowMap::ensure(int res) {
	if (res == resolution && fbo != 0) return;
	if (depthTex) glDeleteTextures(1, &depthTex);
	if (fbo) glDeleteFramebuffers(1, &fbo);
	resolution = res;

	glGenTextures(1, &depthTex);
	glBindTexture(GL_TEXTURE_2D, depthTex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, res, res, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
	// sampler2DShadow で比較しながら線形補間する (ハードウェアの 2×2 PCF)
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
	// 範囲外は「影なし」になるように、境界の深度を最大 (1.0) にする
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	const float border[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
	glBindTexture(GL_TEXTURE_2D, 0);

	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTex, 0);
	glDrawBuffer(GL_NONE);
	glReadBuffer(GL_NONE);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		std::cerr << "Error: shadow map framebuffer is incomplete" << std::endl;
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void ShadowMap::update(const glm::vec3& sunDir, float worldScale) {
	// 領域の中心と、領域 (立方体) を包む球の半径
	const glm::vec3 center(0.5f * worldScale);
	const float radius = 0.5f * std::sqrt(3.0f) * worldScale;

	// 太陽の方向から領域の中心を見る。真上からの光でも lookAt が退化しないように上方向を選ぶ
	const glm::vec3 up = (std::abs(sunDir.y) > 0.99f) ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
	lightView = glm::lookAt(center + sunDir * (2.0f * radius), center, up);
	lightProj = glm::ortho(-radius, radius, -radius, radius, 0.01f, 4.0f * radius);
}

void ShadowMap::bind() const {
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glViewport(0, 0, resolution, resolution);
}
