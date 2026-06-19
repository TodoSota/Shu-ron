// Windowsの OpenGL ライブラリをリンクする
#pragma comment(lib, "opengl32.lib")

#include "core/Window.h"		// ウィンドウの生成から入力などの処理
#include "core/Camera.h"		// 3D空間におけるカメラ位置
#include "core/errorcheck.h"	// OepnGL のエラーチェック
#include "core/shader.h"		// シェーダー読み込み処理
#include "core/Object.h"		// 描画のためのデータパッケージ
#include "core/MeshResource.h"	// SDFに存在するオブジェクトのデータ
#include "sdf/SDFInstance.h"	// SDFに登録するオブジェクト
#include "mpm/MpmObject.h"		// MPM 用の描画データパッケージ
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
const auto PARTICLE_COUNT{ 10000 }; // ノートPCでやるには10000重いので
const float worldScale = 0.67f;

/// MPM 用の点群データ作成
/// @param[in] object 点群データを作成する対象のオブジェクト
/// @param[in] scale 点群データのスケール
/// @param[in] sphere 球状に配置するなら true 、立方体状に配置するなら false
void generateMPMParticles(const MpmObject& object, float scale, bool sphere = true) {
	// 乱数生成器を初期化する
	std::random_device seed_gen;
	std::mt19937 engine(seed_gen());

	// vboをバインドし頂点データをマップ
	glBindBuffer(GL_ARRAY_BUFFER, object.vbo);
	const auto position{ static_cast<MpmParticle*>(glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY)) };	// glMapBufferでGPUのものをCPUでいじりますと宣言

	// 球状に配置する場合は、 0.0f～1.0f の範囲の一様乱数を生成
	std::uniform_real_distribution<GLfloat> dist(-1.0f, 1.0f);
	// 立方体状に配置する場合 0.4f * scale ～ 0.6f * scale の範囲の一様乱数を生成
	std::uniform_real_distribution<GLfloat> distCube(0.4f * scale, 0.6f * scale);

	for (auto i = 0; i < object.count; i++) {
		if (sphere) {
			const float u{ dist(engine) };				// 経度（角度 t を決めるための乱数）用
			const float v{ dist(engine) * 2.0f - 1.0f };// 緯度に対応。-1.0〜+1.0 の範囲を持たせる（Y軸方向の高さ）
			const float w{ dist(engine) };				// 球の中心からどれだけ離れているか
			const float r{ cbrt(w) * scale };			// 球の半径に比例する距離。cbrt で球全体に均等に粒子が分布させる
			const float s{ sqrt(1.0f - v * v) * r };	// xy 平面上の距離（XとYの合成長さ）。z（高さ）とのバランスを取る
			const float t{ u * 6.2831853f };			// 角度（0〜2π）を表すラジアン。経度方向の回転

			// 粒子を球状に配置する
			position[i].position = { s * cos(t) + 0.5f, s * sin(t) + 0.5f, r * v + 0.5f, 1.0f };
		}
		else {
			// 粒子を立方体状に配置
			position[i].position = { distCube(engine), distCube(engine),distCube(engine), 1.0f };
		}

		// MPM 粒子の初期化 : mat3 は vec4 の3つ分
		position[i].velocity = glm::vec4(0.0f);		// 速度
		position[i].affineC = glm::mat4(0.0f);		// アフィン速度行列
		position[i].deformation = glm::mat4(1.0f);	// 変形勾配:単位行列で初期化
		position[i].alpha = 0.267765f;				// 降伏面の大きさ(硬化に影響)
		position[i].q = 0.0f;						// 降伏面の更新に使用
		position[i].vc = 0.0f;						// 体積の変化量
		position[i].state = 1;						// 状態1:弾性変形
		position[i].scale = worldScale;				// スケール(グリッド書き込み時に値を拡大)
		position[i].padding[0] = position[i].padding[1] = position[i].padding[2] = 0;//調整
		// 石の色を少し混ぜる
		std::uniform_real_distribution<GLfloat> matDist(0.0f, 1.0f);
		position[i].padding[0] = (matDist(engine) < 0.05f) ? 1 : 0;
	}

	// バッファオブジェクトの結合を解除。GPUへの諸々操作も終了したしターゲティングも終わりと宣言
	glUnmapBuffer(GL_ARRAY_BUFFER);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

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

	// MPM シミュレーション
	const auto mpmSetup{ loadCompute("src/mpm/shaders/mpm_setup.comp") };
	const auto mpmP2G{ loadCompute("src/mpm/shaders/mpm_p2g.comp") };
	const auto mpmGrid{ loadCompute("src/mpm/shaders/mpm_grid.comp") };
	const auto mpmG2P{ loadCompute("src/mpm/shaders/mpm_g2p.comp") };
	const auto mpmMove{ loadCompute("src/mpm/shaders/mpm_move.comp") };
	// プログラムオブジェクトの作成失敗なら
	if (mpmSetup == 0 || mpmP2G == 0 || mpmGrid == 0 || mpmG2P == 0 || mpmMove == 0) {
		std::cerr << "Can not create mpm simulator shader." << std::endl;
		return EXIT_FAILURE;
	}

	// MPM シミュレーション領域を生成
	const int N_GRID = 128; // グリッドの解像度
	MpmObject MpmParticleSystem(PARTICLE_COUNT, N_GRID);
	generateMPMParticles(MpmParticleSystem, 1.0f, false);	// false なので立方体

	// シミュレーション空間内に存在するオブジェクトリソースのロード
	auto sdfResource = std::make_shared<MeshResource>("src/assets/object.obj");
	sdfResource->generateSDF(64);// 64^3の解像度でSDFを生成

	// インスタンスを作成
	SDFInstance obstacle(sdfResource);
	obstacle.position = glm::vec3(0.5f, 0.3f, 0.5f);
	obstacle.scale = glm::vec3(-0.2f);
	obstacle.updateMatrices();

	// 各種材料の特性値とシミュレーションの設定
	//const float E_s = 3.537e5f;	// ヤング率
	const float E_s = 5e4f;	// ヤング率
	const float nu_s = 0.3f;	// ポアソン比
	const float g_interval = worldScale / (float)N_GRID;	// グリッドの間隔

	// 空間における設定
	MpmPhysics MpmSimParam{
		// シミュレーション空間
		{0.0f, -9.8f, 0.0f},// 重力
		1.0 / 1000.0f,		// 時間間隔
		{0.0f, 1.0f, 0.0f},	// 地面の法線
		0.1f,				// 地面の高さ
		0.5f,				// 地面の反発係数
		0.6f,				// 地面の摩擦係数
		g_interval,			// グリッドの間隔
		1.0f / g_interval,	// 間隔の逆数

		// 粒子
		0.2f,												// 粒子の反発係数
		pow(g_interval * 0.5f, 3.0f),						// 粒子の体積
		E_s / (2.0f * (1.0f + nu_s)),						// 粒子のラメ係数
		E_s * nu_s / ((1.0f + nu_s) * (1.0f - 2.0f * nu_s)),// 粒子のラメ係数
		(g_interval * 0.5f) * (g_interval * 0.5f) * (g_interval * 0.5f) * 400.0f,// 粒子の質量
		0.01f,												// 粒子の半径
		0.0001f,											// 粒子の重なり
		0,													// 調整

		// 障害物
		{0.5f, 0.1f, 0.5f, 0.1f},
		{0.0f, 0.0f, 0.0f, 0.0f}
	};

	// 粒子群の物理パラメータを格納するユニフォームバッファオブジェクト
	GLuint ubo;

	// ユニフォームバッファオブジェクトを作成
	glGenBuffers(1, &ubo);
	glBindBuffer(GL_UNIFORM_BUFFER, ubo);
	glBufferData(GL_UNIFORM_BUFFER, sizeof MpmSimParam, &MpmSimParam, GL_DYNAMIC_DRAW);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);

	// 背景色指定
	glClearColor(0.8f, 0.8f, 0.8f, 1.0f);

	glPointSize(static_cast<GLfloat>(window.getSize().y * 0.01));
	glEnable(GL_POINT_SMOOTH);

	// 画面描画クラスのインスタンス生成
	Renderer renderer(worldScale);

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

		controller.update(window, camera, obstacle, MpmSimParam.timestep);

		// SDF の更新
		obstacle.update(MpmSimParam.timestep);	// 物理挙動を更新

		// 以降で3枚のテクスチャを使うので 4 番でテクスチャをバインド
		glActiveTexture(GL_TEXTURE4);
		glBindTexture(GL_TEXTURE_3D, sdfResource->sdfTexture3D);

		// ユニフォーム変数に値を送信 | モデル逆行列 & 速度
		const glm::mat4& modelMat = obstacle.getModelMatrix();
		const glm::mat4& modelInv = obstacle.getInverseModelMatrix();
		glUseProgram(mpmGrid);
		glUniformMatrix4fv(glGetUniformLocation(mpmGrid, "sdf_model"), 1, GL_FALSE, glm::value_ptr(modelMat));
		glUniformMatrix4fv(glGetUniformLocation(mpmGrid, "sdf_inv_model"), 1, GL_FALSE, glm::value_ptr(modelInv));
		// 2. 速度
		glUniform3fv(glGetUniformLocation(mpmGrid, "sdf_velocity"), 1, glm::value_ptr(obstacle.velocity));
		// 3. テクスチャ空間の範囲（UVWマッピング用）
		glUniform3fv(glGetUniformLocation(mpmGrid, "sdf_aabb_min"), 1, glm::value_ptr(sdfResource->aabbMin));
		glUniform3fv(glGetUniformLocation(mpmGrid, "sdf_aabb_max"), 1, glm::value_ptr(sdfResource->aabbMax));
		// 4. 重心座標と角速度を送信
		glUniform3fv(glGetUniformLocation(mpmGrid, "sdf_angular_velocity"), 1, glm::value_ptr(obstacle.angularVelocity));
		glUniform3fv(glGetUniformLocation(mpmGrid, "sdf_center"), 1, glm::value_ptr(obstacle.position));

		// シェーダーストレージバッファオブジェクトを 0 番の結合ポイントに結合する
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, MpmParticleSystem.vbo);

		// ユニフォームバッファオブジェクトを 1 番に結合
		glBindBufferBase(GL_UNIFORM_BUFFER, 1, ubo);

		// 3Dテクスチャを 0-3 番に結合
		glBindImageTexture(0, MpmParticleSystem.gridTexX, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
		glBindImageTexture(1, MpmParticleSystem.gridTexY, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
		glBindImageTexture(2, MpmParticleSystem.gridTexZ, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
		glBindImageTexture(3, MpmParticleSystem.gridTexA, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);

		// [mpm_setup] グリッドのリセット
		glUseProgram(mpmSetup);
		int numGroups = (MpmParticleSystem.gridSize + 7) / 8;// compファイルでの local_size が 8*8*8 なので 解像度/8 で送信
		glDispatchCompute(numGroups, numGroups, numGroups);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		// [mpm_p2g] P2G
		glUseProgram(mpmP2G);
		glDispatchCompute((MpmParticleSystem.count + 63) / 64, 1, 1);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		// [mpm_grid] グリッドでの計算
		glUseProgram(mpmGrid);
		glDispatchCompute(numGroups, numGroups, numGroups);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		// [mpm_g2p] G2P
		glUseProgram(mpmG2P);
		glDispatchCompute((MpmParticleSystem.count + 63) / 64, 1, 1);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		// [mpm_move] 粒子の移動
		glUseProgram(mpmMove);
		glDispatchCompute((MpmParticleSystem.count + 63) / 64, 1, 1);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		// ウィンドウを消去(カラー/デプスバッファを初期状態に)
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		// 画面の描画
		renderer.drawFloor(view,projection,model,MpmSimParam);
		renderer.drawMpm(MpmParticleSystem,view,projection,model,window.getUseDebugColor());
		renderer.drawObstacle(obstacle,view,projection,model);
		renderer.drawBoundary(view,projection,glm::mat4(1.0f));

		// Swing モードのプレビュー計算と描画
		if (controller.isDragging()) {
			// Renderer にデータを渡して描画させる
			renderer.drawPreview(view, projection, model, controller.calcPreviewPoints(obstacle));
		}

		// OpenGL 周りのエラーがないかチェック
		errorcheck();

#if defined(IMGUI_VERSION)
		ImGui::Begin("Simulation Control");

		// 現在のFPSと1フレームあたりの処理時間を表示
		ImGui::Text("FPS: %.1f (%.3f ms/frame)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);
		ImGui::Separator();

		// シミュレーションパラメータの表示と編集
		ImGui::Text("Physics Parameters:");
		ImGui::SliderFloat("Floor Height", &MpmSimParam.f_height, -5.0f, 5.0f);
		ImGui::SliderFloat3("Floor Normal", &MpmSimParam.f_normal[0], 0.0f, 1.0f);

		ImGui::Separator(); // デバック範囲のため区切り線
		bool debugFlag = window.getUseDebugColor();
		if (ImGui::Checkbox("Debug Color Mode (Space key)", &debugFlag)) {
			window.setUseDebugColor(debugFlag);
		}

		// ▼ 追加：インタラクションモードの切り替え
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
			generateMPMParticles(MpmParticleSystem, 1.0f, false);

			// physicsのUBOに新しい値を反映させる
			glBindBuffer(GL_UNIFORM_BUFFER, ubo);
			glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(MpmSimParam), &MpmSimParam);
			glBindBuffer(GL_UNIFORM_BUFFER, 0);
		}

		ImGui::End();
#endif

		// カラーバッファを入れ替えてイベントを取り出す
		window.swapBuffers();
	}
}