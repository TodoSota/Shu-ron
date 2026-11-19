#pragma once
#include <cstddef>
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
	alignas(16) int scale; // Reserved (C-6)
	alignas(4) int padding[3];	// 調整用
};

static_assert(sizeof(MpmParticle) == 192, "MPM particle SSBO stride");
static_assert(offsetof(MpmParticle, affineC) == 32 && offsetof(MpmParticle, deformation) == 96 &&
              offsetof(MpmParticle, alpha) == 160 && offsetof(MpmParticle, state) == 172 &&
              offsetof(MpmParticle, scale) == 176, "MPM particle std430 offsets");
