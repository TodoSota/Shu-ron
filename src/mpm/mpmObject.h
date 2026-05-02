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
#include <GLM/glm.hpp>

/// 粒子の物理量
struct mpmParticle {

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

// MPMの粒子群の物理パラメータ
struct MPMPhysics {
	// シミュレーション空間
	alignas(16) glm::vec3 gravity;		// 重力
	alignas(4) GLfloat timestep;		// 時間間隔
	alignas(16) glm::vec3 f_normal;		// 地面の法線
	alignas(4) GLfloat f_height;		// 地面の高さ

	alignas(4) GLfloat f_restitution;	// 地面の反発係数
	alignas(4) GLfloat f_friction;		// 地面の摩擦係数
	alignas(4) GLfloat dx;				// グリッドの間隔
	alignas(4) GLfloat inv_dx;			// 間隔の逆数

	// 粒子
	alignas(4) GLfloat p_restitution;	// 粒子の反発係数
	alignas(4) GLfloat p_vol;			// 粒子の体積
	alignas(4) GLfloat p_mu;			// 粒子のラメ係数
	alignas(4) GLfloat p_lambda;		// 粒子のラメ係数

	alignas(4) GLfloat p_mass;			// 粒子の質量
	alignas(4) GLfloat p_radius;		// 粒子の半径
	alignas(4) GLfloat p_overlap;		// 粒子の重なり
	alignas(4) GLfloat p_padding;		// 調整

	// 障害物
	alignas(16) glm::vec4 obstacle_sphere; // 静的物体の仮説
	alignas(16) glm::vec4 obstacle_velocity; // 静的物体の仮説
};

/// 頂点配列オブジェクト
struct mpmObject {

	const GLuint vao;		// vao:描画
	const GLuint vbo;		// vbo:粒子のデータ本体
	const GLsizei count;	// 頂点数

	// MPMグリッドのフィールド
	GLuint gridTexX, gridTexY, gridTexZ, gridTexA;	// 3DテクスチャID | 生成して後から変更するので const はなし
	const int gridSize;		// グリッドのサイズ(テクスチャの解像度)

	/// コンストラクタ
	/// @param[in] count 頂点の数
	/// @param[in] gridSize グリッド(3Dテクスチャ)の解像度
	mpmObject(GLsizei count, int gridSize);

	mpmObject(const mpmObject& mpmObject) = delete;					// コピーコンストラクタを禁止
	virtual ~mpmObject();											// デコンストラクタ
	mpmObject& operator = (const mpmObject& mpmObject) = delete;	// 代入演算子は使用しない
	mpmObject& operator=(mpmObject&& mpmObject) = default;			// ムーブ代入演算子はデフォルトを使用


};