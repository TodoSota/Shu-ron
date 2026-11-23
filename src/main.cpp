//==============================================================================
// main.cpp : 初期化、方式選択とメインループ
// 入力・描画・UIを処理し、物理更新と履歴の制御をSimulationRunへ渡す。
//==============================================================================
// Windowsの OpenGL ライブラリをリンクする
#pragma comment(lib, "opengl32.lib")

#include "core/Window.h"		// ウィンドウの生成から入力などの処理
#include "core/Camera.h"		// 3D空間におけるカメラ位置
#include "core/Errorcheck.h"	// OpenGL のエラーチェック
#include "core/MeshResource.h"	// SDFに存在するオブジェクトのデータ
#include "sdf/SDFInstance.h"	// SDFに登録するオブジェクト
#include "controller/InteractionController.h" // プレイヤー操作の処理
#include "render/Renderer.h"

// --- MPM シミュレーションに関するインクルード
#include "SimulationRun.h"
#include "mpm/TimelineManager.h"// MPM シミュレーションの履歴
#include "mpm/IMpmObject.h"		// MPM 用の描画データパッケージ
#include "mpm/IMpmSimulator.h"	// MPM のシミュレーション実行クラス

// オブジェクトロード・ライブラリ
#define TINYOBJLOADER_IMPLEMENTATION // 必ずインクルードの前に書く
#include "tiny_obj_loader/tiny_obj_loader.h"

// 標準ライブラリ
#include <iostream>
#include <random>
#include <memory>
#include <cmath>
#include <algorithm>
#include <limits>

// GLM関連
#include <GLM/gtc/type_ptr.hpp>
#include <GLM/gtc/matrix_transform.hpp>

/// メインプログラム
/// @return プログラムが正常終了した場合 0
auto main() -> int try {
	//--------------------------------------------------------------------------
	// 1. ウィンドウ・OpenGL・ImGui の初期化
	//--------------------------------------------------------------------------
	// GLFW の初期化
	if (glfwInit() == GL_FALSE) {
		// 初期化に失敗したのでエラーメッセージ出力
		std::cerr << "Can't initialize GLFW" << std::endl;

		// 終了
		return EXIT_FAILURE;
	}

	// プログラム終了時の処理を登録
	atexit(glfwTerminate);

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	// ウィンドウを作成
	Window window(1280, 720, "MPM Simulator");

	// 作成失敗すれば
	if (window.get() == nullptr) {
		// エラーメッセージ出力
		std::cerr << "Can't create GLFW window" << std::endl;

		// プログラム終了
		return EXIT_FAILURE;
	}

	// GLEW の初期化時にすべてのAPIエントリポイントを見つけるように(OpenGLの機能をアクティブに)
	glewExperimental = GL_TRUE;

	// GLEW 初期化
	// こちらでフルアクティブ完了
	if (glewInit() != GLEW_OK) {
		// エラーメッセージ出力
		std::cerr << "Can't initialize GLEW" << std::endl;

		// プログラム終了
		return EXIT_FAILURE;
	}

	if (!GLEW_VERSION_4_3) throw std::runtime_error("OpenGL 4.3 is required.");
	std::cout << "OpenGL: " << glGetString(GL_VERSION) << " / " << glGetString(GL_RENDERER) << '\n';

#if defined(IMGUI_VERSION)
	// ImGui のバージョンをチェックする
	IMGUI_CHECKVERSION();

	// ImGui のコンテキストを作成する
	ImGui::CreateContext();
	struct ImGuiSession {
		bool glfwReady = false;
		bool glReady = false;
		~ImGuiSession() {
			if (glReady) ImGui_ImplOpenGL3_Shutdown();
			if (glfwReady) ImGui_ImplGlfw_Shutdown();
			ImGui::DestroyContext();
		}
	} imguiSession;
	imguiSession.glfwReady = ImGui_ImplGlfw_InitForOpenGL(window.get(), true);
	if (imguiSession.glfwReady) imguiSession.glReady = ImGui_ImplOpenGL3_Init();
	if (!imguiSession.glReady) throw std::runtime_error("Cannot initialize ImGui backends.");

	// ImGui のスタイルを設定する
	auto& io{ ImGui::GetIO() };
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
	//ImGui::StyleColorsDark();
	//ImGui::StyleColorsClassic();

	// 日本語を表示できるメニューフォントを読み込む
	if (!io.Fonts->AddFontFromFileTTF("ImGui/Mplus1-Regular.ttf", 18,
		nullptr, io.Fonts->GetGlyphRangesJapanese()))
	{
		// GLEW の初期化に失敗したのでエラーメッセージを出して
		std::cerr << "Can't load menu font." << std::endl;

		// 終了する
		return EXIT_FAILURE;
	}

	// メニューを表示するなら true
	bool showMenu{ true };
#endif

	//--------------------------------------------------------------------------
	// 2. 障害物 (SDF) の準備
	//   メッシュを読み込み、ローカル空間で 64³ の SDF を一度だけ CPU で計算する (起動時に時間がかかる)。
	//   初期位置・拡大率は下のSdfInstance設定で指定し、Shoot / Swing操作で更新する。
	//--------------------------------------------------------------------------
	// シミュレーション空間内に存在するオブジェクトリソースのロード
	std::shared_ptr<MeshResource> sdfResource;
	try {
		sdfResource = std::make_shared<MeshResource>("src/assets/object.obj");
		sdfResource->generateSDF(64);
	}
	catch (const std::exception& error) {
		std::cerr << "SDF generation rejected the mesh: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}

	// インスタンスを作成
	SdfInstance obstacle(sdfResource);
	obstacle.position = glm::vec3(kWorldScale * 0.5f, 0.3f, kWorldScale * 0.5f);
	obstacle.scale = glm::vec3(0.2f);
	obstacle.updateMatrices();

	SimulationSettings settings;
	auto mpmSimParam = defaultSimulationPhysics(settings.grid);

	//--------------------------------------------------------------------------
	// 4. 描画状態の初期設定
	//--------------------------------------------------------------------------
	// 背景色指定
	glClearColor(0.8f, 0.8f, 0.8f, 1.0f);

	// デプステスト（前後関係の計算）を有効化
	glEnable(GL_DEPTH_TEST);

	// カリング（裏面の描画省略）を有効化（影の計算がおかしくなる対策）
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK); // 裏面は描画しない

	glPointSize(static_cast<GLfloat>(window.getSize().y * 0.01));
	GLint profileMask = 0;
	glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profileMask);
	if ((profileMask & GL_CONTEXT_COMPATIBILITY_PROFILE_BIT) != 0) glEnable(GL_POINT_SMOOTH);

	//--------------------------------------------------------------------------
	// 5. シミュレーション関連オブジェクトの生成
	//--------------------------------------------------------------------------
	// 起動時にコンソールから シミュレーション種類 and 補間方式を選択させる
	std::cout << "Select MPM Memory Mode:\n  0: 3D Texture (Base)\n  1: SSBO (Test)\n> ";
	if (!(std::cin >> settings.mode)) throw std::runtime_error("Invalid memory mode input.");

	std::cout << "Select MPM Interpolation Method:\n";
	std::cout << "  0: Quadratic B-Spline\n" << "  1: Linear\n";
	std::cout << "> ";
	if (!(std::cin >> settings.interpolation)) throw std::runtime_error("Invalid interpolation input.");
	auto resources = prepareSimulation(settings, mpmSimParam);
	auto& mpmSimulator = resources.simulator;
	auto& timeline = resources.timeline;
	// 画面描画クラスのインスタンス生成
	Renderer renderer(kWorldScale);
	renderer.settings.particleSpacing = particleSpacing(settings);
	// プレイヤー操作処理のインスタンス生成
	InteractionController controller;
	// 描画空間におけるカメラ生成
	Camera camera;

	SimulationRun run(resources, settings, mpmSimParam, window, controller, obstacle, renderer);
	auto pendingSettings = settings;
	std::string restartError;

	while (window) {
		// 更新処理を行う
		window.update();

		// マウスでの視点移動を獲得
		glm::dvec2 delta = window.getMouseDelta();
		// 右クリックドラッグ時のみカメラ視点移動
		if (glfwGetMouseButton(window.get(), GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
			camera.rotate((float)delta.x, (float)delta.y);
		}
		// スクロール量を取り出してズームに変換
		double scrollDelta = window.getScrollDelta();
		if (scrollDelta != 0.0) {
			camera.zoom((float)scrollDelta);
		}
		// カメラ行列の更新
		glm::mat4 model = glm::mat4(1.0);// モデル変換行列を設定・モデルは回転させずに固定
		glm::mat4 view = camera.getView();// ビュー変換行列を設定・カメラの現在位置を取得
		glm::mat4 projection = camera.getProjection(window.getAspect());// 投影変換行列を設定

		run.advance(camera);
		const float currentFramerate = ImGui::GetIO().Framerate;
		run.recordFps(currentFramerate);

		// --- 描画 ---
		// 画面の描画
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);// ウィンドウを消去(カラー/デプスバッファを初期状態に)
		if (renderer.usesPointMode()) {
			// 従来の点描画 (陰影なし)
			renderer.drawFloor(view,projection,model,mpmSimParam);
			renderer.drawObstacle(obstacle,view,projection,model);
			renderer.drawMpm(mpmSimulator->getMpmObject(), view, projection, model, window.getUseDebugColor());
		}
		else {
			// 球インポスタ + 陰影 + 影 + AO (画面の色と深度を書く)
			const glm::ivec2 fb = window.getFramebufferSize();
			renderer.drawScene(mpmSimulator->getMpmObject(), obstacle, mpmSimParam, view, projection, fb.x, fb.y, window.getUseDebugColor());
		}
		renderer.drawBoundary(view,projection,glm::mat4(1.0f));

		// Swing モードのプレビュー描画
		if (controller.isDragging()) {
			// Renderer にデータを渡して描画させる
			renderer.drawPreview(view, projection, model, controller.calcPreviewPoints());
		}

		// OpenGL 周りのエラーがないかチェック
		Errorcheck();

		// --- UI (ImGui): 評価・ログ、パラメータ、カメラ、タイムライン、再起動の設定 ---
#if defined(IMGUI_VERSION)
		ImGui::Begin("Simulation Control");
		if (!mpmSimulator->getSimulationError().empty()) {
			ImGui::TextUnformatted("Simulation stopped. Restart to clear the deformation fault.");
			ImGui::TextWrapped("%s", mpmSimulator->getSimulationError().c_str());
		}

		// 描画の設定 (描画モード・光源・影・AO)
		renderer.drawSettingsUi();

		ImGui::Separator();
		
		// 評価機能のコントロールパネル
		ImGui::Text("Evaluation & Logging:");
		EvaluatorOptions evalOptions = mpmSimulator->getEvaluator().getOptions();	// 現在のオプションを取得
		bool optionsChanged = false;

		ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.0f, 1.0f), "[ GPU Time Measurement ]");
		if (ImGui::Checkbox("GPU Timer Query", &evalOptions.enableGpuTimer)) optionsChanged = true;
		ImGui::BeginDisabled(!evalOptions.enableGpuTimer);
		if (ImGui::Checkbox("Timer Readback (Sync)", &evalOptions.enableGpuTimerReadback)) optionsChanged = true;
		ImGui::EndDisabled();
		ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.0f, 1.0f), "[ State Evaluation ]");
		if (ImGui::Checkbox("Evaluation Compute", &evalOptions.enableStateEvaluation)) optionsChanged = true;
		ImGui::BeginDisabled(!evalOptions.enableStateEvaluation);
		if (ImGui::Checkbox("State Readback (Sync)", &evalOptions.enableStateReadback)) optionsChanged = true;
		ImGui::EndDisabled();
		if (ImGui::Checkbox("Rep Particle Readback (Sync)", &evalOptions.enableRepParticleReadback)) optionsChanged = true;

		if (ImGui::SliderInt("Eval Interval", &evalOptions.stateEvaluationInterval, 1, 100)) optionsChanged = true;

		ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.0f, 1.0f), "[ APP FPS Logging ]");
		if (ImGui::Checkbox("Record App FPS", &evalOptions.enableFpsLogging)) optionsChanged = true;

		// 変更があれば反映
		if (optionsChanged) {
			mpmSimulator->getEvaluator().setOptions(evalOptions);
		}

		ImGui::Spacing();	// --- セーブ / リセット ---
		ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "[ File Output Settings ]");

		// 実験ID（再起動してもコード上で初期値を設定するか、UIで直接いじれる）
		static int expId = 1;
		ImGui::InputInt("Experiment ID (E**)", &expId);

		// ノート欄（ベースライン、現象発生条件など）
		static char note[64] = "baseline";
		ImGui::InputText("Case / Note", note, IM_ARRAYSIZE(note));

		static std::string logStatus;
		if (ImGui::Button("Save Logs to CSV")) {
			// --- ExperimentID のフォーマット (例: 1 -> "E01") ---
			char expIdStr[16];
			snprintf(expIdStr, sizeof(expIdStr), "E%02d", expId);

			// --- 補間方式の文字列化 ---
			std::string simModeStr = (settings.mode == 0) ? "TEX" : "SSBO";
			std::string interpStr = (settings.interpolation == 0) ? "QuadraticBSpline" : "Linear";

			// --- ベースファイル名の構築 ---
			// 形式: [ExperimentID]_[Particle]_[Grid]_[Interpolation]_[Case/Note]
			std::string safeNote(note);
			for (auto& ch : safeNote) {
				if (!(ch >= 'a' && ch <= 'z') && !(ch >= 'A' && ch <= 'Z') &&
					!(ch >= '0' && ch <= '9') && ch != '-' && ch != '_') ch = '_';
			}
			std::string baseName = std::string(expIdStr) + "_" + simModeStr + "_p" + std::to_string(settings.particleCount) + "_g" + std::to_string(settings.grid) + "_" + interpStr + "_" + safeNote;

			// 各種CSVのファイル名を構築
			std::string perfFile = baseName + "_perf.csv";
			std::string detailFile = baseName + "_detail.csv";
			std::string fpsFile = baseName + "_fps.csv";

			// 保存処理の実行
			logStatus = mpmSimulator->getEvaluator().saveLogToCSV(perfFile, detailFile, fpsFile);
			if (logStatus.empty()) {
				logStatus = "Saved logs: " + baseName;
				if (expId < (std::numeric_limits<int>::max)()) ++expId;
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Clear Log")) {
			mpmSimulator->getEvaluator().clearLogs();
			logStatus.clear();
		}
		
		if (!logStatus.empty()) ImGui::TextWrapped("%s", logStatus.c_str());
		ImGui::Separator(); // 区切り線

		ImGui::Text("Timeline Control:");

		// 現在のFPSと1フレームあたりの処理時間を表示
		ImGui::Text("FPS: %.1f (%.3f ms/frame)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);
		ImGui::Separator();

		// シミュレーションパラメータの表示と編集
		ImGui::Text("Physics Parameters:");
		ImGui::SliderFloat("Floor Height", &mpmSimParam.f_height, -5.0f, 5.0f);
		auto floorNormal = mpmSimParam.f_normal;
		if (ImGui::SliderFloat3("Floor Normal", &floorNormal[0], 0.0f, 1.0f) && glm::length(floorNormal) > 1e-6f) {
			mpmSimParam.f_normal = glm::normalize(floorNormal);
		}

		ImGui::Separator(); // デバック範囲のため区切り線
		bool debugFlag = window.getUseDebugColor();
		if (ImGui::Checkbox("Debug Color Mode (Space key)", &debugFlag)) {
			window.setUseDebugColor(debugFlag);
		}

		// インタラクションモードの切り替え
		ImGui::Separator();
		ImGui::Text("Interaction Mode:");
		int interactionMode = controller.getMode();
		ImGui::RadioButton("Camera Control", &interactionMode, 0); ImGui::SameLine();
		ImGui::RadioButton("Shoot Sphere", &interactionMode, 1); ImGui::SameLine();
		ImGui::RadioButton("Swing", &interactionMode, 2);
		controller.setMode(interactionMode);
		ImGui::Separator(); // 区切り線

		ImGui::Text("Camera Settings:");
		const char* modes[] = { u8"Orbit (俯瞰)", u8"FPS (主観)" };
		int currentMode = (int)camera.mode;
		if (ImGui::Combo("Camera Mode", &currentMode, modes, IM_ARRAYSIZE(modes))) {
			camera.mode = (CameraMode)currentMode;
			// モード切り替え時に位置を再計算
			camera.updateVectors();
		}
		if (camera.mode == CameraMode::ORBIT) {
			if (ImGui::SliderFloat("Orbit Radius", &camera.radius, 0.5f, 10.0f)) {
				camera.updateVectors();	// 向いている方向をリセット
			}
		}
		ImGui::Separator(); // 区切り線

		ImGui::Text("Timeline Control:");

		// 再生 / 一時停止のトグルボタン
		if (ImGui::Button(run.isPaused() ? u8"▶ Play" : u8"■ Pause")) {
			run.togglePause();
		}
		ImGui::SameLine();

		// コマ送りボタン（一時停止中のみ押せるように制御）
		if (!run.isPaused()) {
			ImGui::BeginDisabled(); // 稼働中はグレーアウト
		}
		ImGui::PushButtonRepeat(true);	// 長押しでリピート可能
		if (ImGui::Button(u8"▶▶ Step Forward (1 Frame)")) {
			run.requestStep();
		}
		ImGui::PopButtonRepeat();		// 長押しリピート設定解除
		if (!run.isPaused()) {
			ImGui::EndDisabled();
		}

		ImGui::Spacing();

		// 再生中はスライダーをグレーアウトして操作不能にする
		if (!run.isPaused()) {
			ImGui::BeginDisabled();
		}

		// 1フレームあたり1〜32ステップの範囲で調整可能
		int subSteps = run.subStepCount();
		if (ImGui::SliderInt("Sub-steps", &subSteps, 1, 32)) run.setSubStepCount(subSteps);
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Number of physics calculations per frame (16 is roughly the actual speed)");
		}

		if (!run.isPaused()) {
			ImGui::EndDisabled();
		}

		ImGui::Separator(); // 区切り線

		// 操作記録の再実行ボタン
		ImGui::Separator();
		ImGui::Text("Action Macro:");

		// 記録がない場合はボタンを無効化する
		if (!controller.hasLastAction()) {
			ImGui::BeginDisabled();
		}
		if (ImGui::Button("Repeat Last Action")) {
			// 最後に実行したShoot or Swingをもう一度走らせる
			controller.fireLastAction(obstacle);
		}
		if (!controller.hasLastAction()) {
			ImGui::EndDisabled();
		}
		ImGui::Separator();

		// タイムラインシークバー
		ImGui::Spacing();
		int maxFrameIndex = timeline->getCurrentFrameCount() > 0 ? timeline->getCurrentFrameCount() - 1 : 0;
		int currentPlaybackFrame = timeline->getPlaybackFrame();

		// スライダーを操作したとき
		if (timeline->getCapacity() == 0) ImGui::TextUnformatted("Timeline unavailable");
		else ImGui::Text("Timeline capacity: %d frames", timeline->getCapacity());
		if (timeline->getCurrentFrameCount() > 0 &&
			ImGui::SliderInt("Timeline (Frames)", &currentPlaybackFrame, 0, maxFrameIndex)) {
			run.seek(currentPlaybackFrame);
		}

		// リスタート
		if (ImGui::Button("Restart Simulation")) {
			run.restart();
		}

		// --- 設定変更とリスタート用UI ---
		ImGui::Separator();
		ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "[ Simulation Restart Settings ]");

		const char* memoryModes[] = { "3D Texture", "SSBO" };
		ImGui::Combo("Memory Mode", &pendingSettings.mode, memoryModes, IM_ARRAYSIZE(memoryModes));
		ImGui::InputInt("Particle Count", &pendingSettings.particleCount);
		ImGui::InputInt("Grid Size (ex: 32, 64, 128)", &pendingSettings.grid);
		ImGui::InputInt("Interpolation \n(0 : B spline, 1 : Linear)", &pendingSettings.interpolation);

		if (ImGui::Button("Apply & Restart Simulation")) {
			try {
				run.rebuild(pendingSettings);
				restartError.clear();
			}
			catch (const std::exception& error) {
				restartError = error.what();
			}
		}
		if (!restartError.empty()) ImGui::TextWrapped("%s", restartError.c_str());

		ImGui::End();
#endif

		// カラーバッファを入れ替えてイベントを取り出す
		window.swapBuffers();
	}
}
catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
}
