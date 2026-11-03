#pragma once
#include <glm/glm.hpp>

// 粒子の物理量
struct MpmParticle {
	alignas(16) glm::vec4 position;		// 位置
	alignas(16) glm::vec4 velocity;		// 速度 (内部的には vec3 分を使用)
	alignas(16) glm::mat4 affineC;		//アフィン速度行列 (内部的には mat3 分を使用)
	alignas(16) glm::mat4 deformation;	// 変形勾配 (内部的には mat3 分を使用)

	// 砂の塑性変形パラメーター
	alignas(4) float alpha;		//降伏面の大きさ
	alignas(4) float q;			//硬化状態
	alignas(4) float vc;		//変化の際の体積変化
	alignas(4) int state;		//状態(変化)
	alignas(16) int scale;		//スケール(テクスチャへの書き込み用)
	alignas(4) int padding[3];	// 調整用
};