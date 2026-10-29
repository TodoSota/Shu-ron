#pragma once

//==============================================================================
// MpmPhysics.h : シミュレーション全体で共通の物理パラメータ (UBO)
// 目的: GLSL の uniform ブロック MPMPhysics (std140, binding = 1) とメモリ配置を一致させ、
//       glBufferSubData で一度に GPU へ転送する。
//       std140 では vec3 が 16 バイト境界に揃えられるため、vec3 の直後に float を 1 個置いて
//       16 バイトを埋める配置にしている (gravity + timestep, f_normal + f_height)。
// 単位: SI 単位系 (m, s, kg, Pa) を想定している。
// 未使用: p_restitution, p_overlap, obstacle_sphere, obstacle_velocity は現在どのシェーダーでも使われていない。
//==============================================================================
#include <GL/glew.h>
#include <GLM/glm.hpp>

// MPMの粒子群の物理パラメータ
struct MpmPhysics {
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
	alignas(4) GLfloat p_mu;			// 粒子のラメ係数 (第 2 定数 μ = せん断弾性係数 E / 2(1+ν))
	alignas(4) GLfloat p_lambda;		// 粒子のラメ係数 (第 1 定数 λ = Eν / (1+ν)(1-2ν))

	alignas(4) GLfloat p_mass;			// 粒子の質量
	alignas(4) GLfloat p_radius;		// 粒子の半径
	alignas(4) GLfloat p_overlap;		// 粒子の重なり
	alignas(4) GLfloat p_padding;		// 調整

	// 障害物
	alignas(16) glm::vec4 obstacle_sphere; // 静的物体の仮設
	alignas(16) glm::vec4 obstacle_velocity; // 静的物体の仮設
};