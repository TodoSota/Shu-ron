// Windowsの OpenGL ライブラリをリンクする
#pragma comment(lib, "opengl32.lib")

#include "core/Window.h"		// ウィンドウの生成から入力などの処理
#include "core/Camera.h"		// 3D空間におけるカメラ位置
#include "core/Errorcheck.h"	// OpenGL のエラーチェック
#include "core/MeshResource.h"	// SDFに存在するオブジェクトのデータ
#include "sdf/SdfInstance.h"	// SDFに登録するオブジェクト
#include "mpm/MpmObject.h"		// MPM 用の描画データパッケージ
#include "mpm/MpmSimulator.h"	// MPM のシミュレーション実行クラス
#include "controller/InteractionController.h" // プレイヤー操作の処理
#include "render/Renderer.h"

// オブジェクトロード・ライブラリ
#define TINYOBJLOADER_IMPLEMENTATION // 必ずインクルードの前に書く
#include "tiny_obj_loader/tiny_obj_loader.h"

// 標準ライブラリ
#include <iostream>
#include <random>
#include <memory>

// GLM関連
#include <GLM/gtc/type_ptr.hpp>
#include <GLM/gtc/matrix_transform.hpp>

// 粒子数
const auto kParticleCount{ 10000 }; // ノートPCでやるには10000重いので
const float kWorldScale = 0.67f;

/// メインプログラム
/// @return プログラムが正常終了した場合 0
auto main() -> int {
	// GLFW の初期化
	if (glfwInit() == GL_FALSE) {
		// 初期化に失敗したのでエラーメッセージ出力
		std::cerr << "Can't initialize GLFW" << std::endl;

		// 終了
		return EXIT_FAILURE;
	}

	// プログラム終了時の処理を登録
	atexit(glfwTerminate);

	// ウィンドウを作成
	Window window;

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

#if defined(IMGUI_VERSION)
	// ImGui のバージョンをチェックする
	IMGUI_CHECKVERSION();

	// ImGui のコンテキストを作成する
	ImGui::CreateContext();

	// ImGui のバックエンドに OpenGL / GLFW を使う
	ImGui_ImplGlfw_InitForOpenGL(window.get(), true);
	ImGui_ImplOpenGL3_Init();

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

	// MPM シミュレーション領域を生成
	const int kGrid = 128; // グリッドの解像度

	// シミュレーション空間内に存在するオブジェクトリソースのロード
	auto sdfResource = std::make_shared<MeshResource>("src/assets/object.obj");
	sdfResource->generateSDF(64);// 64^3の解像度でSDFを生成

	// インスタンスを作成
	SdfInstance obstacle(sdfResource);
	obstacle.position = glm::vec3(0.5f, 0.3f, 0.5f);
	obstacle.scale = glm::vec3(-0.2f);
	obstacle.updateMatrices();

	// 各種材料の特性値とシミュレーションの設定
	//const float E = 3.537e5f;	// ヤング率
	const float E = 5e4f;	// ヤング率
	const float Nu = 0.3f;	// ポアソン比
	const float kGridSpacing = kWorldScale / (float)kGrid;	// グリッドの間隔

	// 空間における設定
	MpmPhysics mpmSimParam{
		{0.0f, -9.8f, 0.0f},// 重力
		1.0 / 1000.0f,		// 時間間隔
		{0.0f, 1.0f, 0.0f},	// 地面の法線
		0.1f,				// 地面の高さ
		0.5f,				// 地面の反発係数
		0.6f,				// 地面の摩擦係数
		kGridSpacing,		// グリッドの間隔
		1.0f / kGridSpacing,// 間隔の逆数

		// 粒子
		0.2f,											// 粒子の反発係数
		pow(kGridSpacing * 0.5f, 3.0f),					// 粒子の体積
		E / (2.0f * (1.0f + Nu)),						// 粒子のラメ係数
		E * Nu / ((1.0f + Nu) * (1.0f - 2.0f * Nu)),	// 粒子のラメ係数
		(kGridSpacing * 0.5f) * (kGridSpacing * 0.5f) * (kGridSpacing * 0.5f) * 400.0f,// 粒子の質量
		0.01f,											// 粒子の半径
		0.0001f,										// 粒子の重なり
		0,												// 調整

		// 障害物
		{0.5f, 0.1f, 0.5f, 0.1f},
		{0.0f, 0.0f, 0.0f, 0.0f}
	};

	// 背景色指定
	glClearColor(0.8f, 0.8f, 0.8f, 1.0f);

	glPointSize(static_cast<GLfloat>(window.getSize().y * 0.01));
	glEnable(GL_POINT_SMOOTH);

	// シミュレーション処理クラスのインスタンス生成
	MpmSimulator mpmSimulator(kParticleCount, kGrid, kWorldScale);
	mpmSimulator.resetParticles(1.0f, false);
	mpmSimulator.setPhysics(mpmSimParam);
	// 画面描画クラスのインスタンス生成
	Renderer renderer(kWorldScale);
	// プレイヤー操作処理のインスタンス生成
	InteractionController controller;
	// 描画空間におけるカメラ生成
	Camera camera;

	// ウィンドウ起動中
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


		// 入力とカメラ更新
		controller.update(window, camera, obstacle, mpmSimParam.timestep);

		// SDF(障害物) の更新
		obstacle.update(mpmSimParam.timestep);

		// 値の共有と物理計算の進行
		mpmSimulator.setPhysics(mpmSimParam);
		mpmSimulator.step(obstacle);		// *** 計算の主体 ***

		// 画面の描画
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);// ウィンドウを消去(カラー/デプスバッファを初期状態に)
		renderer.drawFloor(view,projection,model,mpmSimParam);
		renderer.drawMpm(mpmSimulator.getMpmObject(), view, projection, model, window.getUseDebugColor());
		renderer.drawObstacle(obstacle,view,projection,model);
		renderer.drawBoundary(view,projection,glm::mat4(1.0f));

		// Swing モードのプレビュー描画
		if (controller.isDragging()) {
			// Renderer にデータを渡して描画させる
			renderer.drawPreview(view, projection, model, controller.calcPreviewPoints(obstacle));
		}

		// OpenGL 周りのエラーがないかチェック
		Errorcheck();

#if defined(IMGUI_VERSION)
		ImGui::Begin("Simulation Control");

		// 現在のFPSと1フレームあたりの処理時間を表示
		ImGui::Text("FPS: %.1f (%.3f ms/frame)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);
		ImGui::Separator();

		// シミュレーションパラメータの表示と編集
		ImGui::Text("Physics Parameters:");
		ImGui::SliderFloat("Floor Height", &mpmSimParam.f_height, -5.0f, 5.0f);
		ImGui::SliderFloat3("Floor Normal", &mpmSimParam.f_normal[0], 0.0f, 1.0f);

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
		const char* modes[] = { "Orbit (俯瞰)", "FPS (主観)" };
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



		// 「リスタート」ボタン
		if (ImGui::Button("Restart Simulation")) {
			// パーティクルの初期化を呼ぶ
			mpmSimulator.resetParticles(1.0f,false);

			mpmSimulator.setPhysics(mpmSimParam);
		}

		ImGui::End();
#endif

		// カラーバッファを入れ替えてイベントを取り出す
		window.swapBuffers();
	}
}