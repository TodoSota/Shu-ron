// Windowsの OpenGL ライブラリをリンクする
#pragma comment(lib, "opengl32.lib")

#include "Window.h"		// ウィンドウの生成から入力などの処理
#include "errorcheck.h"	// OepnGL のエラーチェック
#include "shader.h"		// シェーダー読み込み処理
#include "Object.h"		// 描画のためのデータパッケージ
#include "mpmObject.h"	// MPM 用の描画データパッケージ

// 標準ライブラリ
#include <iostream>
#include <random>

// GLM関連
#include <GLM/gtc/type_ptr.hpp>
#include <GLM/gtc/matrix_transform.hpp>

// 粒子数
const auto PARTICLE_COUNT{ 1000 }; // ノートPCでやるには10000重いので
const float worldScale = 1.0f;

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

/// MPM 用の点群データ作成
/// @param[in] object 点群データを作成する対象のオブジェクト
/// @param[in] scale 点群データのスケール
/// @param[in] sphere 球状に配置するなら true 、立方体状に配置するなら false
void generateMPMParticles(const mpmObject& object, float scale, bool sphere = true) {
	// 乱数生成器を初期化する
	std::random_device seed_gen;
	std::mt19937 engine(seed_gen());

	// vboをバインドし頂点データをマップ
	glBindBuffer(GL_ARRAY_BUFFER, object.vbo);
	const auto position{ static_cast<mpmParticle*>(glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY)) };	// glMapBufferでGPUのものをCPUでいじりますと宣言

	// 球状に配置する場合は、 0.0f～1.0f の範囲の一様乱数を生成
	std::uniform_real_distribution<GLfloat> dist(0.0f, 1.0f);
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
		} else {
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

	// 1. プログラムオブジェクトのロード
	// プログラムオブジェクトの作成
	const auto program{ loadProgram("point.vert", "point.frag") };
	// プログラムオブジェクトの作成失敗
	if (program == 0) {
		std::cerr << "Can not create program object" << std::endl;
		return EXIT_FAILURE;
	}

	// uniform 変数の設定
	const auto mcLoc{ glGetUniformLocation(program, "mc") };	// mc の場所を取得

	/* 既存の簡易シミュレーション
	// 粒子の処理を初期化するコンピュートシェーダーのプログラムオブジェクトを作成
	const auto setup{ loadCompute("setup.comp") };
	// 粒子の衝突を処理するコンピュートシェーダーのプログラムオブジェクトを作成
	const auto collide{ loadCompute("collide.comp") };
	// 粒子の位置を更新するコンピュートシェーダーのプログラムオブジェクトを作成
	const auto update{ loadCompute("update.comp") };
	// プログラムオブジェクトの作成失敗なら
	if (setup == 0 || collide == 0 || update == 0) {
		std::cerr << "Can not create primitive simulator shader." << std::endl;
		return EXIT_FAILURE;
	}
	*/
	
	// MPM シミュレーション
	const auto mpmSetup{ loadCompute("mpm_setup.comp") };
	const auto mpmP2G{ loadCompute("mpm_p2g.comp") };
	const auto mpmGrid{ loadCompute("mpm_grid.comp") };
	const auto mpmG2P{ loadCompute("mpm_g2p.comp") };
	const auto mpmMove{ loadCompute("mpm_move.comp") };
	// プログラムオブジェクトの作成失敗なら
	if (mpmSetup == 0 || mpmP2G == 0 || mpmGrid == 0 || mpmG2P == 0 || mpmMove == 0) {
		std::cerr << "Can not create mpm simulator shader." << std::endl;
		return EXIT_FAILURE;
	}

	// 2. オブジェクトの作成
	// 図形を作成
	Object object(PARTICLE_COUNT);
	generateParticles(object, 1.0f);

	// MPM シミュレーション領域を生成
	const int N_GRID = 128; // グリッドの解像度
	mpmObject mpmObj(PARTICLE_COUNT, N_GRID);
	generateMPMParticles(mpmObj, 1.0f, false);	// false なので立方体

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
		alignas(16) glm::vec3 gravity;				// 重力
		alignas(4) GLfloat floor_height;			// 地面の高さ
		alignas(16) glm::vec3 floor_normal;			// 地面の法線
		alignas(4) GLfloat floor_restitution;		// 地面の反発係数
		alignas(4) GLfloat particle_restitution;	// 粒子の反発係数
		alignas(4) GLfloat mass;					// 粒子の質量
		alignas(4) GLfloat radius;					// 粒子の半径
		alignas(4) GLfloat overlap;					// 粒子の重なり
		alignas(4) GLfloat timestep;				// 時間間隔
	};

	// MPMの粒子群の物理パラメータ
	struct MPMPhysics {
		// シミュレーション空間
		alignas(16) glm::vec3 gravity;		// 重力
		alignas(4) GLfloat timestep;		// 時間間隔
		alignas(16) glm::vec3 f_normal;		// 地面の法線
		alignas(4) GLfloat f_height;		// 地面の高さ
		
		alignas(4) GLfloat f_restitution;	// 地面の反発係数
		alignas(4) GLfloat f_friction;		// 地面の摩擦係数
		alignas(4) GLfloat dx;				// グリッドの間隔
		alignas(4) GLfloat inv_dx;			// 間隔の逆数

		// 粒子
		alignas(4) GLfloat p_restitution;	// 粒子の反発係数
		alignas(4) GLfloat p_vol;			// 粒子の体積
		alignas(4) GLfloat p_mu;			// 粒子のラメ係数
		alignas(4) GLfloat p_lambda;		// 粒子のラメ係数

		alignas(4) GLfloat p_mass;			// 粒子の質量
		alignas(4) GLfloat p_radius;		// 粒子の半径
		alignas(4) GLfloat p_overlap;		// 粒子の重なり
		alignas(4) GLfloat p_padding;		// 調整
	};

	// 各種材料の特性値とシミュレーションの設定
	const float E_s = 3.537e5f;	// ヤング率
	const float nu_s = 0.3f;	// ポアソン比
	const float g_interval = worldScale / (float)N_GRID;	// グリッドの間隔

	Physics physics{
		{0.0f, -9.8f, 0.0f},//重力
		-1.0f,				//地面の高さ
		{0.0f, 1.0f, 0.0f},	// 地面の法線
		0.5f,				// 地面の反発係数
		0.2f,				// 粒子の反発係数
		1.0f,				// 粒子の質量
		0.1f,				// 粒子の半径
		0.0001f,			// 粒子の重なり
		1.0f / 60.0f,		// 時間間隔
	};

	MPMPhysics mpmphysics{
		// シミュレーション空間
		{0.0f, -9.8f, 0.0f},// 重力
		1.0 / 1000.0f,		// 時間間隔
		{0.0f, 1.0f, 0.0f},	// 地面の法線
		0.0f,				// 地面の高さ
		0.5f,				// 地面の反発係数
		0.6f,				// 地面の摩擦係数
		g_interval,			// グリッドの間隔
		1.0f / g_interval,			// 間隔の逆数

		// 粒子
		0.2f,												// 粒子の反発係数
		pow(g_interval * 0.5f, 3.0f),								// 粒子の体積
		E_s / (2.0f * (1.0f + nu_s)),						// 粒子のラメ係数
		E_s * nu_s / ((1.0f + nu_s) * (1.0f - 2.0f * nu_s)),// 粒子のラメ係数
		(g_interval * 0.5f)* (g_interval * 0.5f)* (g_interval * 0.5f) * 400.0f,		// 粒子の質量
		0.01f,												// 粒子の半径
		0.0001f,											// 粒子の重なり
		0													// 調整
	};

	// 粒子群の物理パラメータを格納するユニフォームバッファオブジェクト
	GLuint ubo;

	// ユニフォームバッファオブジェクトを作成
	glGenBuffers(1, &ubo);
	glBindBuffer(GL_UNIFORM_BUFFER, ubo);
	glBufferData(GL_UNIFORM_BUFFER, sizeof mpmphysics, &mpmphysics, GL_DYNAMIC_DRAW);
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
		//glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, object.vbo); // 通常
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, mpmObj.vbo); // <MPM用>

		// ユニフォームバッファオブジェクトを 1 番に結合
		glBindBufferBase(GL_UNIFORM_BUFFER, 1, ubo);
		
		// 以下、MPMの計算部分 置換で一気にアクティブにして
		
		// <MPM用>3Dテクスチャを 0-3 番に結合
		glBindImageTexture(0, mpmObj.gridTexX, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
		glBindImageTexture(1, mpmObj.gridTexY, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
		glBindImageTexture(2, mpmObj.gridTexZ, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
		glBindImageTexture(3, mpmObj.gridTexA, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
		

		// [mpm_setup] グリッドのリセット
		glUseProgram(mpmSetup);
		int numGroups = (mpmObj.gridSize + 7) / 8;// compファイルでの local_size が 8*8*8 なので 解像度/8 で送信
		glDispatchCompute(numGroups, numGroups, numGroups);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		// [mpm_p2g] P2G
		glUseProgram(mpmP2G);
		glDispatchCompute((mpmObj.count + 63) / 64, 1, 1);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		// [mpm_grid] グリッドでの計算
		glUseProgram(mpmGrid);
		glDispatchCompute(numGroups, numGroups, numGroups);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		// [mpm_g2p] G2P
		glUseProgram(mpmG2P);
		glDispatchCompute((mpmObj.count + 63) / 64, 1, 1);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		// [mpm_move] 粒子の移動
		glUseProgram(mpmMove);
		glDispatchCompute((mpmObj.count + 63) / 64, 1, 1);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
		

		/*
		// 以下、粒子シミュレート分
		// 
		// 初期化のコンピュートシェーダー
		glUseProgram(setup);
		glDispatchCompute(object.count, 1, 1);// 計算を実行
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);// 書き込み完了待ち

		// 粒子の衝突のコンピュートシェーダー
		glUseProgram(collide);
		glDispatchCompute(object.count, 1, 1);// 計算を実行
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);// 書き込み完了待ち

		// 位置更新のコンピュートシェーダー
		glUseProgram(update);
		glDispatchCompute(object.count, 1, 1);// 計算を実行
		*/

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
		// uniform 変数 mc に値を設定
		glUniformMatrix4fv(mcLoc, 1, GL_FALSE, glm::value_ptr(projection * view * model));

		// 床の描画
		glUniform3fv(glGetUniformLocation(program, "floor_normal"), 1, glm::value_ptr(mpmphysics.f_normal));
		glUniform1f(glGetUniformLocation(program, "floor_height"), mpmphysics.f_height);
		GLint isFloorLocation = glGetUniformLocation(program, "is_floor");

		/*
		// 粒子の描画
		glUniform1i(isFloorLocation, 0); // 地面フラグ off
		glBindVertexArray(object.vao);
		glDrawArrays(GL_POINTS, 0, object.count);
		*/
		
		// MPM 結果の描画
		glUniform1i(isFloorLocation, 0); // 地面フラグ off
		glBindVertexArray(mpmObj.vao);
		glDrawArrays(GL_POINTS, 0, mpmObj.count);
		

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
		ImGui::SliderFloat3("Gravity", &mpmphysics.gravity[0], -10.0f, 10.0f);
		ImGui::SliderFloat("Timestep", &mpmphysics.timestep, 0.001f, 0.1f);
		ImGui::SliderFloat("Floor Height", &mpmphysics.f_height, -5.0f, 5.0f);
		ImGui::SliderFloat3("Floor Normal", &mpmphysics.f_normal[0], 0.0f, 1.0f);
		ImGui::SliderFloat("Floor Restitution", &mpmphysics.f_restitution, 0.0f, 1.0f);
		ImGui::SliderFloat("Floor Friction", &mpmphysics.f_friction, 0.0f, 1.0f);
		ImGui::SliderFloat("dx", &mpmphysics.dx, 0.0f, 1.0f);
		ImGui::SliderFloat("inv_dx", &mpmphysics.inv_dx, 0.0f, N_GRID*2);
		ImGui::SliderFloat("Particle Restitution", &mpmphysics.p_restitution, 0.0f, 1.0f);
		ImGui::SliderFloat("Particle vol", &mpmphysics.p_vol, 0.0f, 1.0f);
		//ImGui::SliderFloat("Particle mu", &mpmphysics.p_mu, 0.0f, 1.0f);
		//ImGui::SliderFloat("Particle lambda", &mpmphysics.p_lambda, 0.0f, 1.0f);
		ImGui::SliderFloat("Prticle Mass", &mpmphysics.p_mass, 0.1f, 10.0f);
		ImGui::SliderFloat("Particle Radius", &mpmphysics.p_radius, 0.01f, 1.0f);
		ImGui::SliderFloat("Particle Overlap", &mpmphysics.p_overlap, 0.0f, 0.01f);
		

		// 「リスタート」ボタン
		if (ImGui::Button("Restart Simulation")) {
			// パーティクルの初期化を呼ぶ
			generateMPMParticles(mpmObj, 1.0f, false);

			// physicsのUBOに新しい値を反映させる
			glBindBuffer(GL_UNIFORM_BUFFER, ubo);
			glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(mpmphysics), &mpmphysics);
			glBindBuffer(GL_UNIFORM_BUFFER, 0);
		}

		ImGui::End();
#endif

		// カラーバッファを入れ替えてイベントを取り出す
		window.swapBuffers();
	}
}