#include "SimulationSetup.h"

#include "mpm/MpmParticle.h"
#include "mpm/3dtexture/MpmSimulator3DTEX.h"
#include "mpm/ssbo/MpMSimulatorSSBO.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
const float kBaseParticleVolume = std::pow(kWorldScale * 0.2f, 3.0f) / static_cast<float>(baseParticleCount);
constexpr int maxTimelineFrames = 600;

void validateSimulatorSettings(const SimulationSettings& settings) {
	const auto [mode, interpolation, count, grid] = settings;
	if ((mode != 0 && mode != 1) || (interpolation != 0 && interpolation != 1)) {
		throw std::invalid_argument("Select a valid memory mode and interpolation method.");
	}
	if (count < 1 || count > 125 * baseParticleCount || grid < 8) {
		throw std::invalid_argument("Particle count must be 1 to 1250000 and grid size at least 8.");
	}

	GLint64 blockLimit = 0;
	GLint groups[3]{};
	glGetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &blockLimit);
	for (GLuint axis = 0; axis < 3; ++axis) {
		glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, axis, &groups[axis]);
		if ((static_cast<int64_t>(grid) + 7) / 8 > groups[axis]) {
			throw std::invalid_argument("Grid size exceeds the GPU dispatch limit.");
		}
	}
	const auto bufferLimit = (std::min)(blockLimit, static_cast<GLint64>((std::numeric_limits<GLsizeiptr>::max)()));
	if (static_cast<int64_t>(count) * static_cast<int64_t>(sizeof(MpmParticle)) > bufferLimit ||
		(count + 63) / 64 > groups[0]) {
		throw std::invalid_argument("Particle count exceeds the GPU buffer or dispatch limit.");
	}
	if (mode == 1) {
		if (grid > 1290 || 16LL * grid * grid * grid > bufferLimit) {
			throw std::invalid_argument("Grid size exceeds the GPU SSBO limit.");
		}
	} else {
		GLint textureLimit = 0;
		glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &textureLimit);
		if (grid > textureLimit) throw std::invalid_argument("Grid size exceeds the GPU 3D texture limit.");
	}
}

void checkSimulatorAllocation() {
	const GLenum error = glGetError();
	if (error != GL_NO_ERROR) {
		while (glGetError() != GL_NO_ERROR) {}
		throw std::runtime_error("Simulator initialization failed (OpenGL " + std::to_string(error) + ").");
	}
}

MpmPhysics resetMpmSimParam(MpmPhysics mpmSimParam, int currentGrid) {
	const float currentGridSpacing = kWorldScale / static_cast<float>(currentGrid);	// グリッドの間隔
	const float invGridSpacing = 1.0f / currentGridSpacing;

	const float particleRadius = currentGridSpacing * 0.5f;
	const float particleVolume = kBaseParticleVolume;
	const float particleMass = particleVolume * 400.0f; // 密度 400.0f

	mpmSimParam.dx = currentGridSpacing;
	mpmSimParam.inv_dx = invGridSpacing;
	mpmSimParam.p_vol = particleVolume;
	mpmSimParam.p_mass = particleMass;
	mpmSimParam.p_radius = particleRadius;
	mpmSimParam.p_overlap = particleRadius * 0.01f;

	return mpmSimParam;
}

} // namespace

MpmPhysics defaultSimulationPhysics(int grid) {
	//--------------------------------------------------------------------------
	// 3. 材料定数とシミュレーションのパラメータ
	//   E, ν からラメ定数 μ = E / 2(1+ν), λ = Eν / (1+ν)(1-2ν) を求める。
	//   コメントアウトされている E = 3.537e5 は Klár et al. 2016 の砂の値
	//   (同論文の密度は 2200 kg/m³)。
	//   時間刻みは工程2の基準条件でΔt=2.5e-4 sを採用。弾性波のCFL条件
	//     Δt ≲ C·dx / sqrt((λ + 2μ)/ρ)   (C ≈ 0.3〜0.5)
	//   を E = 5e4, ν = 0.3, ρ = 400, dx = 1.5/128 で見積もると約 2.7e-4 s になり、
	//   旧dt=1e-3 sは刻み比較のAABB基準を超えた。材料・速度・格子変更時の自動dt調整はない。
	//   [点検 B-1] 基準条件以外の安定性は未保証。docs/maintenance/stage2-validation.mdを参照。
	//--------------------------------------------------------------------------
	// 各種材料の特性値とシミュレーションの設定
	//const float E = 3.537e5f;	// ヤング率
	const float E = 5e4f;	// ヤング率
	const float Nu = 0.3f;	// ポアソン比
	const float currentGridSpacing = kWorldScale / static_cast<float>(grid);	// グリッドの間隔

	// 空間における設定
	return {
		{0.0f, -9.8f, 0.0f},// 重力
		1.0f / 4000.0f,		// 時間間隔（工程2の基準dt=0.00025秒）
		{0.0f, 1.0f, 0.0f},	// 地面の法線
		0.1f,				// 地面の高さ
		0.5f,				// 地面の反発係数
		0.6f,				// 地面の摩擦係数
		currentGridSpacing,		// グリッドの間隔
		1.0f / currentGridSpacing,// 間隔の逆数

		// 粒子
		0.2f,											// 粒子の反発係数
		kBaseParticleVolume,					// 粒子の体積
		E / (2.0f * (1.0f + Nu)),						// 粒子のラメ係数
		E * Nu / ((1.0f + Nu) * (1.0f - 2.0f * Nu)),	// 粒子のラメ係数
		kBaseParticleVolume * 400.0f,// 粒子の質量
		currentGridSpacing * 0.5f,											// 粒子の半径
		currentGridSpacing * 0.005f,										// 予約スロット（旧p_overlap）
		0,												// 調整

		// 障害物
		{0.5f, 0.1f, 0.5f, 0.1f},
		{0.0f, 0.0f, 0.0f, 0.0f}
	};

}

float initialParticleScale(const SimulationSettings& settings) {
	return std::cbrt(static_cast<float>(settings.particleCount) / baseParticleCount);
}

float particleSpacing(const SimulationSettings& settings) {
	return kWorldScale * 0.2f * initialParticleScale(settings) / std::cbrt(static_cast<float>(settings.particleCount));
}

SimulationResources prepareSimulation(const SimulationSettings& settings, MpmPhysics& physics,
	const EvaluatorOptions* evaluatorOptions) {
	validateSimulatorSettings(settings);
	const auto [mode, interpolation, count, grid] = settings;
	const bool linear = interpolation == 1;
	SimulationResources resources;
	if (mode == 1) {
		std::cout << "Creating Simulator: SSBO mode" << std::endl;
		resources.simulator = std::make_unique<MpmSimulatorSSBO>(count, grid, kWorldScale, linear);
	} else {
		std::cout << "Creating Simulator: 3D Texture mode" << std::endl;
		resources.simulator = std::make_unique<MpmSimulator3DTEX>(count, grid, kWorldScale, linear);
	}
	resources.simulator->resetParticles(initialParticleScale(settings), false);
	const auto preparedPhysics = resetMpmSimParam(physics, grid);
	resources.simulator->setPhysics(preparedPhysics);
	checkSimulatorAllocation();
	if (evaluatorOptions) resources.simulator->getEvaluator().setOptions(*evaluatorOptions);
	resources.timeline = std::make_unique<TimelineManager>(maxTimelineFrames, count, 1);
	physics = preparedPhysics;
	return resources;
}
