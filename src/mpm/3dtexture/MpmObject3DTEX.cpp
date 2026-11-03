#include "MpmObject3DTEX.h"

/// コンストラクタ
/// @param[in] count 頂点数
/// @param[in] gridSize グリッド(3Dテクスチャ)の解像度
MpmObject3DTEX::MpmObject3DTEX(GLsizei count, int gridSize) : IMpmObject(count, gridSize)
{
	// 3Dテクスチャ(MPMグリッド)の生成・設定
	auto createGridTex = [&](GLuint& texID) {
		glGenTextures(1, &texID);

		// RGBA32F を使用: RGB =運動量(mvx, mvy, mvz), A=質量(m)
		glBindTexture(GL_TEXTURE_3D, texID);
		// R32F で確保 Atomic のため R32UI としてバインド
		glTexImage3D(GL_TEXTURE_3D, 0, GL_R32F, gridSize, gridSize, gridSize, 0, GL_RED, GL_FLOAT, nullptr);

		// 正確なノードへの Read/Write のため NEAREST
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	};

	// 4枚のテクスチャを生成・設定
	createGridTex(gridTexX);
	createGridTex(gridTexY);
	createGridTex(gridTexZ);
	createGridTex(gridTexA);

	glBindTexture(GL_TEXTURE_3D, 0);
}

MpmObject3DTEX::~MpmObject3DTEX() {
	if (gridTexX != 0) glDeleteTextures(1, &gridTexX);
	if (gridTexY != 0) glDeleteTextures(1, &gridTexY);
	if (gridTexZ != 0) glDeleteTextures(1, &gridTexZ);
	if (gridTexA != 0) glDeleteTextures(1, &gridTexA);
}