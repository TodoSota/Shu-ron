#include "SimulationRun.h"

#include "controller/InteractionController.h"
#include "core/Window.h"
#include "render/Renderer.h"
#include "mpm/IMpmObject.h"

SimulationRun::SimulationRun(SimulationResources& resources, SimulationSettings& settings, MpmPhysics& physics,
	Window& window, InteractionController& controller, SdfInstance& obstacle, Renderer& renderer)
	: resources(resources), settings(settings), physics(physics), window(window),
	  controller(controller), obstacle(obstacle), renderer(renderer), initialObstacle(obstacle.captureSnapshot()) {
	recordFrame();
}

void SimulationRun::recordFrame() {
	const auto& simulator = resources.simulator;
	resources.timeline->recordFrame(simulator->getMpmObject().getReadVbo(), &obstacle,
		{ physics, controller, simulator->getStepCount(), simulator->getSimulationTime() });
}

void SimulationRun::resetTimeline() {
	obstacle.restoreSnapshot(initialObstacle);
	controller.resetMotion();
	controller.syncMouse(window);
	resources.timeline->clear();
	recordFrame();
	renderer.clearDust();
	renderFrame = 0;
	paused = true;
	stepRequested = false;
}

void SimulationRun::advance(const Camera& camera) {
	auto& simulator = resources.simulator;
	auto& timeline = resources.timeline;
	float currentDt = (paused && !stepRequested) ? 0.0f : physics.timestep;

	if (!simulator->getSimulationError().empty()) { paused = true; stepRequested = false; }
	if (!paused || stepRequested) {
		if (timeline->getPlaybackFrame() + 1 < timeline->getCurrentFrameCount()) {
			timeline->discardFuture();
			simulator->getEvaluator().discardStepsAfter(simulator->getStepCount());
		}
		int completedSubSteps = 0;
		// サブステップ
		for (int i = 0; i < subSteps; i++) {
			const auto previousObstacle = obstacle.captureSnapshot();
			const auto previousController = controller;
			// 入力とカメラ更新
			controller.update(window, camera, obstacle, currentDt, physics, kWorldScale);

			// 値の共有と物理計算の進行
			simulator->setPhysics(physics);
			simulator->step(obstacle);		// *** 計算の主体 ***
			if (!simulator->getSimulationError().empty()) {
				obstacle.restoreSnapshot(previousObstacle);
				controller = previousController;
				paused = true;
				break;
			}
			++completedSubSteps;
		}
		// 粉塵を、このフレームで進めたシミュレーション時間だけ進める
		renderer.updateDust(simulator->getMpmObject(), physics, physics.timestep * completedSubSteps);
		// コマ送り要求完了のためリセット
		stepRequested = false;
		if (completedSubSteps > 0) recordFrame();
	}
	else {
		// 一時停止中もプレビューなどは更新する
		if (simulator->getSimulationError().empty()) controller.update(window, camera, obstacle, currentDt, physics, kWorldScale);
	}
}

void SimulationRun::recordFps(float fps) {
	if (fps > 0.0f) {
		resources.simulator->getEvaluator().recordAppFps(renderFrame, fps, 1000.0f / fps);
	}
	++renderFrame;
}

void SimulationRun::seek(int frame) {
	auto& simulator = resources.simulator;
	auto& timeline = resources.timeline;
	paused = true;
	timeline->setPlaybackFrame(frame);

	// 指定した論理フレームの状態をGPU/CPUに復元
	//   (読み出し側の粒子バッファだけを上書きする。次の step() はこのバッファから計算を始める)
	TimelineState state;
	timeline->restoreToFrame(frame, simulator->getMpmObject().getReadVbo(), &obstacle, &state);
	controller = state.controller;
	controller.cancelDrag();
	controller.syncMouse(window);
	physics = state.physics;
	simulator->setPhysics(physics);
	simulator->restoreSimulationClock(state.step, state.time);
	stepRequested = false;
	renderer.clearDust();	// 粉塵の状態は記録していないので、巻き戻したら消す
}

void SimulationRun::restart() {
	resources.simulator->resetParticles(initialParticleScale(settings), false);
	resources.simulator->setPhysics(physics);
	resetTimeline();
}

void SimulationRun::rebuild(const SimulationSettings& nextSettings) {
	const auto options = resources.simulator->getEvaluator().getOptions();
	auto candidate = prepareSimulation(nextSettings, physics, &options);
	resources = std::move(candidate);
	settings = nextSettings;
	renderer.settings.particleSpacing = particleSpacing(settings);
	resetTimeline();
}
