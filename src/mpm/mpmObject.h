#pragma once

//==============================================================================
// mpmObject.h : MPM の GPU データ (粒子バッファと格子テクスチャ) の定義
// 目的: ・粒子 1 個分の状態 MpmParticle を、GLSL の std430 構造体と
//         バイト単位で一致するように定義する (計 192 バイト)。
//       ・粒子バッファを 2 本 (ping-pong) と、それぞれを描画する VAO を持つ。
//       ・格子を 3D テクスチャ 4 枚 (運動量 X/Y/Z、質量) として持つ。
//==============================================================================

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
/// メモリ配置 (バイトオフセット): position 0, velocity 16, affineC 32, deformation 96,
///   alpha 160, q 164, vc 168, state 172, scale 176, padding 180–191 → 合計 192 (16 の倍数)
/// GLSL 側 (std430) の配置と一致していることが前提。メンバを変更するときは全シェーダーの struct も変更すること
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
	alignas(16) int scale;	//スケール(テクスチャへの書き込み用) ※ 現在のシェーダーでは未使用
	alignas(4) int padding[3]; // 調整用 (padding[0] は材質フラグ: 1 = 石 として描画の色分けに流用)
};

/// 頂点配列オブジェクト
struct MpmObject {

	GLuint vao[2];			// vao:描画
	GLuint vbo[2];			// vbo:粒子のデータ本体
	int readBuffer{ 0 };	// 計算対象を表現するインデックス

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