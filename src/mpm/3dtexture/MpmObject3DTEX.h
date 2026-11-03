#pragma once

#include "../IMpmObject.h"

/// 頂点配列オブジェクト
struct MpmObject3DTEX : public IMpmObject {
private :
	// MPMグリッドのフィールド
	GLuint gridTexX{ 0 }, gridTexY{ 0 }, gridTexZ{ 0 }, gridTexA{ 0 };	// 3DテクスチャID | 生成して後から変更するので const はなし

public :
	/// コンストラクタ
	/// @param[in] count 頂点の数
	/// @param[in] gridSize グリッド(3Dテクスチャ)の解像度
	MpmObject3DTEX(GLsizei count, int gridSize);
	~MpmObject3DTEX() override;

	// ゲッター
	GLuint getGridTexX() const { return gridTexX; }
	GLuint getGridTexY() const { return gridTexY; }
	GLuint getGridTexZ() const { return gridTexZ; }
	GLuint getGridTexA() const { return gridTexA; }
};