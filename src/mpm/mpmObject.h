#pragma once

// バッファオブジェクト関連の宣言はgl.h は含まれていないので glew.h を使う
#include <GL/glew.h>

// GLM 関連
#if !defined(_USE_MATH_DEFINES)
#	define _USE_MATH_DEFINES
#endif
#if !defined(GLM_FORCE_RADIANS)
#	define GLM_FORCE_RADIANS
#endif
#include <glm/glm.hpp>

#include "MpmPhysics.h"

/// 粒子の物理量
struct MpmParticle {

	alignas(16) glm::vec4 position;		// 位置
	alignas(16) glm::vec4 velocity;		// 速度 (内部的には vec3 分を使用)
	alignas(16) glm::mat4 affineC;		//アフィン速度行列 (内部的には mat3 分を使用)
	alignas(16) glm::mat4 deformation;	// 変形勾配 (内部的には mat3 分を使用)

	// 砂の塑性変形パラメーター
	alignas(4) float alpha;	//降伏面の大きさ
	alignas(4) float q;		//硬化状態
	alignas(4) float vc;	//変化の際の体積変化
	alignas(4) int state;	//状態(変化)
	alignas(16) int scale;	//スケール(テクスチャへの書き込み用)
	alignas(4) int padding[3]; // 調整用
};

/// 頂点配列オブジェクト
struct MpmObject {

	GLuint vao[2];		// vao:描画
	GLuint vbo[2];		// vbo:粒子のデータ本体
	int readBuffer{ 0 };		// 計算対象を表現するインデックス

	const GLsizei count;	// 頂点数

	// MPMグリッドのフィールド
	GLuint gridTexX, gridTexY, gridTexZ, gridTexA;	// 3DテクスチャID | 生成して後から変更するので const はなし
	const int gridSize;		// グリッドのサイズ(テクスチャの解像度)

	/// コンストラクタ
	/// @param[in] count 頂点の数
	/// @param[in] gridSize グリッド(3Dテクスチャ)の解像度
	MpmObject(GLsizei count, int gridSize);

	MpmObject(const MpmObject& MpmObject) = delete;					// コピーコンストラクタを禁止
	virtual ~MpmObject();											// デストラクタ
	MpmObject& operator = (const MpmObject& MpmObject) = delete;	// 代入演算子は使用しない
	MpmObject& operator=(MpmObject&& MpmObject) = default;			// ムーブ代入演算子はデフォルトを使用

	// ゲッター
	GLuint getReadVbo() const { return vbo[readBuffer]; }
	GLuint getWriteVbo() const { return vbo[1 - readBuffer]; }
	GLuint getRenderVao() const { return vao[readBuffer]; }

	/// @brief ダブルバッファのターゲットを切り替える
	void swapBuffers() { readBuffer = 1 - readBuffer; }
};