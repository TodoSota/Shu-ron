#pragma once

//==============================================================================
// RenderTarget.h : オフスクリーン描画用のフレームバッファ (FBO) とその付属テクスチャ
// 目的: G-buffer、SSAO などの中間結果を書き込むレンダーターゲットを、
//       カラーテクスチャ (複数可) と深度テクスチャの組としてまとめて管理する。
//       ウィンドウ (フレームバッファ) の大きさが変わったら resize() で作り直す。
//==============================================================================

#include <GL/glew.h>
#include <vector>

/// カラーテクスチャ 1 枚分の形式
struct ColorAttachmentDesc {
	GLenum internalFormat;	// 例: GL_RGBA8, GL_RGBA16F, GL_R8
	GLenum format;			// 例: GL_RGBA, GL_RED
	GLenum type;			// 例: GL_UNSIGNED_BYTE, GL_FLOAT
	GLenum filter{ GL_NEAREST };	// サンプリング時のフィルタ
};

class RenderTarget {
public:
	RenderTarget() = default;
	~RenderTarget() { release(); }

	RenderTarget(const RenderTarget&) = delete;
	RenderTarget& operator=(const RenderTarget&) = delete;

	/// レンダーターゲットを作成する (作成済みなら作り直す)
	/// @param[in] width, height 大きさ [px]
	/// @param[in] colors カラーテクスチャの形式の並び (location 0, 1, ... の順)
	/// @param[in] withDepth 深度テクスチャ (GL_DEPTH_COMPONENT32F) を付けるなら true
	void create(int width, int height, const std::vector<ColorAttachmentDesc>& colors, bool withDepth);

	/// 大きさが変わっていれば、同じ形式で作り直す
	/// @return 作り直した場合 true
	bool resize(int width, int height);

	/// このターゲットを描画先にし、ビューポートを全体に合わせる
	void bind() const;

	GLuint colorTexture(int index) const { return colorTextures[index]; }
	GLuint depthTexture() const { return depthTex; }
	GLuint framebuffer() const { return fbo; }
	int getWidth() const { return width; }
	int getHeight() const { return height; }

private:
	void release();

	GLuint fbo{ 0 };
	std::vector<GLuint> colorTextures;
	GLuint depthTex{ 0 };
	std::vector<ColorAttachmentDesc> colorDescs;
	bool hasDepth{ false };
	int width{ 0 }, height{ 0 };
};
