/// 図形関連の処理
#include "Object.h"

/// コンストラクタ
/// @param[in] count 頂点数
/// @param[in] data データ
Object::Object(GLsizei count, const void* data) :
	// 頂点配列オブジェクトを作成して vao を作成する
	vao{ []() {GLuint vao; glGenVertexArrays(1, &vao); return vao; }() },
	// 頂点バッファオブジェクトを作成して vbo を作成する
	vbo{ []() {GLuint vbo; glGenBuffers(1, &vbo); return vbo; }() },
	// 頂点の数を保存
	count{ count }
{
	// 頂点配列オブジェクトを結合
	glBindVertexArray(vao);

	// 頂点バッファオブジェクトを決都合して頂点配列オブジェクトに組み込む
	glBindBuffer(GL_ARRAY_BUFFER, vbo);

	// 頂点バッファオブジェクトのメモリを確保し頂点位置データを転送
	glBufferData(GL_ARRAY_BUFFER, sizeof(Particle) * count, data, GL_DYNAMIC_DRAW);

	/// [in]変数 0 番に position
	/// [in]変数 1 番に velocity

	// 結合されている頂点バッファオブジェクトのインデックスを 0 番に
	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(Particle), static_cast<char*>(0) + offsetof(Particle, position));

	// 0 番の頂点バッファオブジェクトを有効に
	glEnableVertexAttribArray(0);

	// 結合されている頂点バッファオブジェクトの velocity のインデックスを 1 番にする
	glVertexAttribPointer(1, 3, GL_FLOAT, GLU_FALSE, sizeof(Particle), static_cast<char*>(0) + offsetof(Particle, velocity));

	// 1 番の頂点バッファオブジェクトを有効に
	glEnableVertexAttribArray(1);

	// 頂点配列オブジェクトの結合を解除する
	glBindVertexArray(0);

	// 頂点バッファオブジェクトの結合を解除する
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

Object::~Object() {
	// 頂点配列オブジェクトを削除
	glDeleteVertexArrays(1, &vao);

	// 頂点バッファオブジェクトを削除
	glDeleteBuffers(1, &vbo);
}