/// 図形関連の処理
#include "mpmObject.h"

/// コンストラクタ
/// @param[in] count 頂点数
/// @param[in] gridSize グリッド(3Dテクスチャ)の解像度
mpmObject::mpmObject(GLsizei count, int gridSize) :
	// 頂点配列オブジェクトを作成して vao を作成する
	vao{ []() {GLuint vao; glGenVertexArrays(1, &vao); return vao; }() },
	// 頂点バッファオブジェクトを作成して vbo を作成する
	vbo{ []() {GLuint vbo; glGenBuffers(1, &vbo); return vbo; }() },
	// 頂点の数を保存
	count{ count },
	// テクスチャを作成
	gridTexX{ 0 },
	gridTexY{ 0 },
	gridTexZ{ 0 },
	gridTexA{ 0 },
	// グリッドサイズを保存
	gridSize{ gridSize }
{
	// バッファの設定
	glBindVertexArray(vao);// vaoを結合
	glBindBuffer(GL_ARRAY_BUFFER, vbo);// vboを結合しvaoに組み込む
	glBufferData(GL_ARRAY_BUFFER, sizeof(mpmParticle) * count, nullptr, GL_DYNAMIC_DRAW);// vboのメモリを確保し頂点位置データを転送

	/// [in]変数 0 番に position
	/// [in]変数 1 番に state(色分けで状態を表示)
	 
	// 結合されているvboの position を 0 番として設定
	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(mpmParticle), (void*)offsetof(mpmParticle, position)) ;
	glEnableVertexAttribArray(0); // 0 番のvboを有効に

	// 結合されているvboの state を 1 番として設定
	glVertexAttribIPointer(1, 1, GL_INT, sizeof(mpmParticle), (void*)offsetof(mpmParticle, state)); // int なので 1 成分
	glEnableVertexAttribArray(1); // 1 番のvboを有効に

	// 3Dテクスチャ(MPMグリッド)の生成・設定
	auto createGridTex = [&](GLuint& texID) {
		glGenTextures(1, &texID);

		// RGBA32F を使用: RGB =運動量(mvx, mvy, mvz), A=質量(m)
		glBindTexture(GL_TEXTURE_3D, texID);
		// R32F で確保 Atomic のため R32UI としてバインド
		glTexImage3D(GL_TEXTURE_3D, 0, GL_R32F, gridSize, gridSize, gridSize, 0, GL_RED, GL_FLOAT, nullptr);

		// 正確なノードへの Read/Write のため NEAREST で
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

	// 処理終了の後始末
	glBindTexture(GL_TEXTURE_3D, 0);	// テクスチャの選択を解除
	glBindVertexArray(0);				// vaoの結合を解除する
	glBindBuffer(GL_ARRAY_BUFFER, 0);	// vboの結合を解除する
}

mpmObject::~mpmObject() {
	glDeleteVertexArrays(1, &vao);	// vaoを削除
	glDeleteBuffers(1, &vbo);		// vboを削除
	glDeleteTextures(1, &gridTexX);	//3Dテクスチャを削除
	glDeleteTextures(1, &gridTexY);	//3Dテクスチャを削除
	glDeleteTextures(1, &gridTexZ);	//3Dテクスチャを削除
	glDeleteTextures(1, &gridTexA);	//3Dテクスチャを削除
}