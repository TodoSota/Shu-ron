#pragma once
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