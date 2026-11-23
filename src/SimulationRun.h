#pragma once

#include "SimulationSetup.h"
#include "sdf/SDFInstance.h"

struct Camera;
class Renderer;
class Window;
class InteractionController;

// The window and the simulation's GL resources outlive this controller.
class SimulationRun {
public:
	SimulationRun(SimulationResources& resources, SimulationSettings& settings, MpmPhysics& physics,
		Window& window, InteractionController& controller, SdfInstance& obstacle, Renderer& renderer);

	bool isPaused() const { return paused; }
	void togglePause() { paused = !paused; }
	void requestStep() { stepRequested = true; }
	int subStepCount() const { return subSteps; }
	void setSubStepCount(int count) { subSteps = count; }

	void advance(const Camera& camera);
	void recordFps(float fps);
	void seek(int frame);
	void restart();
	void rebuild(const SimulationSettings& nextSettings);

private:
	void recordFrame();
	void resetTimeline();

	SimulationResources& resources;
	SimulationSettings& settings;
	MpmPhysics& physics;
	Window& window;
	InteractionController& controller;
	SdfInstance& obstacle;
	Renderer& renderer;
	const SdfSnapshot initialObstacle;
	bool paused = true;
	bool stepRequested = false;
	int subSteps = 1;
	int renderFrame = 0;
};
