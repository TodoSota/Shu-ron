//==============================================================================
// RenderTarget.cpp : オフスクリーン描画用のフレームバッファの実装
//==============================================================================
#include "RenderTarget.h"
#include <iostream>

void RenderTarget::create(int w, int h, const std::vector<ColorAttachmentDesc>& colors, bool withDepth) {
	release();
	width = (w > 0) ? w : 1;
	height = (h > 0) ? h : 1;
	colorDescs = colors;
	hasDepth = withDepth;

	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);

	// カラーテクスチャを作成して location 順に取り付ける
	std::vector<GLenum> drawBuffers;
	for (size_t i = 0; i < colors.size(); ++i) {
		GLuint tex;
		glGenTextures(1, &tex);
		glBindTexture(GL_TEXTURE_2D, tex);
		glTexImage2D(GL_TEXTURE_2D, 0, colors[i].internalFormat, width, height, 0, colors[i].format, colors[i].type, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, colors[i].filter);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, colors[i].filter);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(i), GL_TEXTURE_2D, tex, 0);
		colorTextures.push_back(tex);
		drawBuffers.push_back(GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(i));
	}

	// 深度テクスチャ (後のパスでサンプリングするので、レンダーバッファではなくテクスチャにする)
	if (withDepth) {
		glGenTextures(1, &depthTex);
		glBindTexture(GL_TEXTURE_2D, depthTex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTex, 0);
	}

	if (drawBuffers.empty()) {
		// 深度だけのターゲット
		glDrawBuffer(GL_NONE);
		glReadBuffer(GL_NONE);
	}
	else {
		glDrawBuffers(static_cast<GLsizei>(drawBuffers.size()), drawBuffers.data());
	}

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		std::cerr << "Error: RenderTarget is incomplete (" << width << "x" << height << ")" << std::endl;
	}

	glBindTexture(GL_TEXTURE_2D, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool RenderTarget::resize(int w, int h) {
	if (fbo != 0 && w == width && h == height) return false;
	create(w, h, colorDescs, hasDepth);
	return true;
}

void RenderTarget::bind() const {
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glViewport(0, 0, width, height);
}

void RenderTarget::release() {
	if (!colorTextures.empty()) glDeleteTextures(static_cast<GLsizei>(colorTextures.size()), colorTextures.data());
	colorTextures.clear();
	if (depthTex != 0) glDeleteTextures(1, &depthTex);
	depthTex = 0;
	if (fbo != 0) glDeleteFramebuffers(1, &fbo);
	fbo = 0;
}
