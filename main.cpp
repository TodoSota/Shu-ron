// Windowsの OpenGL ライブラリをリンクする
#pragma comment(lib, "opengl32.lib")

#include "Window.h"		// ウィンドウの生成から入力などの処理
#include "errorcheck.h"	// OepnGL のエラーチェック
#include "shader.h"		// シェーダー読み込み処理
#include "Object.h"		// 図形処理関連の処理

// 標準ライブラリ
#include <iostream>
#include <random>

// GLM関連
#include <GLM/gtc/type_ptr.hpp>
#include <GLM/gtc/matrix_transform.hpp>

// 粒子数
const auto PARTICLE_COUNT{ 100 }; // ノートPCでやるには10000重いので

/// 点群データ作成
/// @param[in] object 点群データを作成する対象のオブジェクト
/// @param[in] scale 点群データのスケール
/// @param[in] sphere 球状に配置するなら true 、立方体状に配置するなら false
void generateParticles(const Object& object, float scale, bool sphere = true) {
	// 乱数生成器を初期化する
	std::random_device seed_gen;
	std::mt19937 engine(seed_gen());

	// 頂点バッファオブジェクトをバインドし頂点データをマップ
	glBindBuffer(GL_ARRAY_BUFFER, object.vbo);
	const auto position{ static_cast<Particle*>(glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY)) };	// glMapBufferでGPUのものをCPUでいじりますと宣言

	// 球状に配置するか
	if (sphere) {
		// 球状に配置する場合は、 0.0f～1.0f の範囲の一様乱数を生成
		std::uniform_real_distribution<GLfloat> dist(0.0f, 1.0f);

		// 粒子の初期位置を設定する
		for (auto i = 0; i < object.count; i++) {
			const float u{ dist(engine) };				// 経度（角度 t を決めるための乱数）用
			const float v{ dist(engine) * 2.0f - 1.0f };// 緯度に対応。-1.0〜+1.0 の範囲を持たせる（Y軸方向の高さ）
			const float w{ dist(engine) };				// 球の中心からどれだけ離れているか
			const float r{ cbrt(w) * scale };			// 球の半径に比例する距離。cbrt で球全体に均等に粒子が分布させる
			const float s{ sqrt(1.0f - v * v) * r };	// xy 平面上の距離（XとYの合成長さ）。z（高さ）とのバランスを取る
			const float t{ u * 6.2831853f };			// 角度（0〜2π）を表すラジアン。経度方向の回転

			// 粒子を球状に配置する
			position[i].position = { s * cos(t), s * sin(t), r * v, 1.0f };
			position[i].velocity = { 0.0f, 0.0f, 0.0f };
		}
	}
	else {
		// 立方体状に配置する場合 -0.5f * scale ～ 0.5f * scale の範囲の一様乱数を生成
		std::uniform_real_distribution<GLfloat> dist(-0.5f * scale, 0.5f * scale);

		// 粒子の初期位置を決める
		for (auto i = 0; i < object.count; i++) {
			// 粒子を立方体状に配置
			position[i].position = { dist(engine), dist(engine),dist(engine), 1.0f };
			position[i].velocity = { 0.0f, 0.0f, 0.0f };
		}
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

	// プログラムオブジェクトの作成
	const auto program{ loadProgram("point.vert", "point.frag") };

	// プログラムオブジェクトの作成失敗
	if (program == 0) {
		std::cerr << "Can not create program object" << std::endl;
		return EXIT_FAILURE;
	}

	// uniform 変数 mc の場所を取得
	const auto mcLoc{ glGetUniformLocation(program, "mc") };

	// 粒子の処理を初期化するコンピュートシェーダーのプログラムオブジェクトを作成
	const auto setup{ loadCompute("setup.comp") };

	// プログラムオブジェクトの作成失敗なら
	if (setup == 0) {
		std::cerr << "Can not create setup shader." << std::endl;
		return EXIT_FAILURE;
	}

	// 粒子の衝突を処理するコンピュートシェーダーのプログラムオブジェクトを作成
	const auto collide{ loadCompute("collide.comp") };

	// プログラムオブジェクトの作成失敗なら
	if (collide == 0) {
		std::cerr << "Can not create collide shader." << std::endl;
		return EXIT_FAILURE;
	}

	// 粒子の位置を更新するコンピュートシェーダーのプログラムオブジェクトを作成
	const auto update{ loadCompute("update.comp") };

	// プログラムオブジェクトの作成失敗なら
	if (update == 0) {
		std::cerr << "Can not create update shader." << std::endl;
		return EXIT_FAILURE;
	}

	// 図形を作成
	Object object(PARTICLE_COUNT);
	generateParticles(object, 1.0f);

	// 地面用のオブジェクトを用意
	const auto GRID_SIZE = 20;
	Object floorObject(GRID_SIZE * GRID_SIZE);

	// 地面用の点群データを生成し転送
	std::vector<Particle> floorParticles(GRID_SIZE* GRID_SIZE);
	for (int i = 0; i < GRID_SIZE; i++) {
		for (int j = 0; j < GRID_SIZE; j++) {
			float x = (i - GRID_SIZE / 2) * 0.2f;
			float z = (j - GRID_SIZE / 2) * 0.2f;
			// 簡易的に y=0 にて初期化
			floorParticles[i * GRID_SIZE + j].position = glm::vec4(x, 0.0f, z, 1.0f);
		}
	}
	// VBO へデータ転送
	glBindBuffer(GL_ARRAY_BUFFER, floorObject.vbo);
	glBufferSubData(GL_ARRAY_BUFFER, 0, floorParticles.size() * sizeof(Particle), floorParticles.data());

	// 粒子群の物理パラメータ
	struct Physics {
		// 重力
		alignas(16) glm::vec3 gravity;

		// 地面の高さ
		alignas(4) GLfloat floor_height;

		// 地面の法線
		alignas(16) glm::vec3 floor_normal;

		// 地面の反発係数
		alignas(4) GLfloat floor_restitution;

		// 粒子の反発係数
		alignas(4) GLfloat particle_restitution;

		// 粒子の質量
		alignas(4) GLfloat mass;

		// 粒子の半径
		alignas(4) GLfloat radius;

		// 粒子の重なり
		alignas(4) GLfloat overlap;

		// 時間間隔
		alignas(4) GLfloat timestep;
	};

	Physics physics{
		//重力
		{0.0f, -1.0f, 0.0f},

		//地面の高さ
		-1.0f,

		// 地面の法線
		{0.0f, 1.0f, 0.0f},

		// 地面の反発係数
		0.3f,

		// 粒子の反発係数
		0.2f,

		// 粒子の質量
		1.0f,

		// 粒子の半径
		0.1f,

		// 粒子の重なり
		0.0001f,

		// 時間間隔
		1.0f / 60.0f
	};

	// 粒子群の物理パラメータを格納するユニフォームバッファオブジェクト
	GLuint ubo;

	// ユニフォームバッファオブジェクトを作成
	glGenBuffers(1, &ubo);
	glBindBuffer(GL_UNIFORM_BUFFER, ubo);
	glBufferData(GL_UNIFORM_BUFFER, sizeof physics, &physics, GL_DYNAMIC_DRAW);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);

	// 背景色指定
	glClearColor(0.8f, 0.8f, 0.8f, 1.0f);

	glPointSize(static_cast<GLfloat>(window.getSize().y * 0.01));
	glEnable(GL_POINT_SMOOTH);

	// ウィンドウ起動中
	while (window) {
		// 更新処理を行う
		window.update();

		// シェーダーストレージバッファオブジェクトを 0 番の結合ポイントに結合する
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, object.vbo);

		// ユニフォームバッファオブジェクトを 1 番の結合ポイントに結合する
		glBindBufferBase(GL_UNIFORM_BUFFER, 1, ubo);

		// 粒子の処理を初期化するコンピュートシェーダーを指定する
		glUseProgram(setup);

		// 計算を実行
		glDispatchCompute(object.count, 1, 1);

		// シェーダーストレージバッファオブジェクトへ書き込み完了を待つ
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

		// 粒子の衝突を処理するコンピュートシェーダーを指定
		glUseProgram(collide);

		// 計算を実行
		glDispatchCompute(object.count, 1, 1);

		// シェーダーストレージバッファオブジェクトへ書き込み完了を待つ
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

		// 粒子の位置を更新するコンピュートシェーダーを指定する
		glUseProgram(update);

		// 計算を実行
		glDispatchCompute(object.count, 1, 1);

		// ウィンドウを消去(カラー/デプスバッファを初期状態に)
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		// プログラムオブジェクトを指定
		glUseProgram(program);

		// モデル変換行列を設定・ウィンドウ内のでマウスの動きから変換
		const auto& model{ window.getModel(GLFW_MOUSE_BUTTON_LEFT) };

		// ビュー変換行列を設定
		const auto view{ glm::translate(glm::mat4(1.0f), glm::vec3(0.0f,0.0f,-3.0f)) };

		// 投影変換行列を設定
		const auto projection{ glm::perspective(glm::radians(60.0f), window.getAspect(), 1.0f, 10.0f) };

		// uniform 変数 mc に値を設定する
		glUniformMatrix4fv(mcLoc, 1, GL_FALSE, glm::value_ptr(projection * view * model));

		// 床の描画
		glUniform3fv(glGetUniformLocation(program, "floor_normal"), 1, glm::value_ptr(physics.floor_normal));
		glUniform1f(glGetUniformLocation(program, "floor_height"), physics.floor_height);
		GLint isFloorLocation = glGetUniformLocation(program, "is_floor");

		// 粒子の描画
		glUniform1i(isFloorLocation, 0); // 地面フラグ off
		glBindVertexArray(object.vao);
		glDrawArrays(GL_POINTS, 0, object.count);

		// 地面の描画
		glUniform1i(isFloorLocation, 1); // 地面フラグ on
		glBindVertexArray(floorObject.vao);
		glDrawArrays(GL_POINTS, 0, floorObject.count);

		glBindVertexArray(0);

		// OpenGL 周りのエラーがないかチェック
		errorcheck();

#if defined(IMGUI_VERSION)
		ImGui::Begin("Simulation Control");

		// シミュレーションパラメータの表示と編集
		ImGui::Text("Physics Parameters:");
		ImGui::SliderFloat3("Gravity", &physics.gravity[0], -10.0f, 10.0f);
		ImGui::SliderFloat("Floor Height", &physics.floor_height, -5.0f, 5.0f);
		ImGui::SliderFloat3("Floor Normal", &physics.floor_normal[0], 0.0f, 1.0f);
		ImGui::SliderFloat("Floor Restitution", &physics.floor_restitution, 0.0f, 1.0f);
		ImGui::SliderFloat("Particle Restitution", &physics.particle_restitution, 0.0f, 1.0f);
		ImGui::SliderFloat("Mass", &physics.mass, 0.1f, 10.0f);
		ImGui::SliderFloat("Radius", &physics.radius, 0.01f, 1.0f);
		ImGui::SliderFloat("Overlap", &physics.overlap, 0.0f, 0.01f);
		ImGui::SliderFloat("Timestep", &physics.timestep, 0.001f, 0.1f);

		// 「リスタート」ボタン
		if (ImGui::Button("Restart Simulation")) {
			// パーティクルの初期化を呼ぶ
			generateParticles(object, 1.0f);

			// physicsのUBOに新しい値を反映させる
			glBindBuffer(GL_UNIFORM_BUFFER, ubo);
			glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(physics), &physics);
			glBindBuffer(GL_UNIFORM_BUFFER, 0);
		}

		ImGui::End();
#endif

		// カラーバッファを入れ替えてイベントを取り出す
		window.swapBuffers();
	}
}