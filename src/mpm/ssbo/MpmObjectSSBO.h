#pragma once

#include "../IMpmObject.h"

/// SSBOを用いた頂点・格子データ管理オブジェクト
class MpmObjectSSBO : public IMpmObject {
private:
	// MPMグリッド (3Dテクスチャの代わりに1つのSSBOを保持)
	GLuint gridSsbo{ 0 };   // 格子データ用SSBO

public:
	/// コンストラクタ
	/// @param[in] count 頂点の数
	/// @param[in] gridSize グリッドの解像度
	MpmObjectSSBO(GLsizei count, int gridSize);

	/// デストラクタ
	~MpmObjectSSBO() override;

	// ゲッター
	GLuint getGridSsbo() const { return gridSsbo; }
};