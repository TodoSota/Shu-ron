#pragma once

#include <memory>
#include "mpm/IMpmSimulator.h"
#include "mpm/TimelineManager.h"

inline constexpr float kWorldScale = 1.5f;
inline constexpr int baseParticleCount = 10000;

struct SimulationSettings {
	int mode = 0;
	int interpolation = 1;
	int particleCount = baseParticleCount;
	int grid = 128;
};

struct SimulationResources {
	std::unique_ptr<IMpmSimulator> simulator;
	std::unique_ptr<TimelineManager> timeline;
};

MpmPhysics defaultSimulationPhysics(int grid);
float initialParticleScale(const SimulationSettings& settings);
float particleSpacing(const SimulationSettings& settings);
SimulationResources prepareSimulation(const SimulationSettings& settings, MpmPhysics& physics,
	const EvaluatorOptions* evaluatorOptions = nullptr);
