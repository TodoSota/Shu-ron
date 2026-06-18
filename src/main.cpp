// Windowsの OpenGL ライブラリをリンクする
#pragma comment(lib, "opengl32.lib")

#include "core/Window.h"		// ウィンドウの生成から入力などの処理
#include "core/Camera.h"		// 3D空間におけるカメラ位置
#include "core/errorcheck.h"	// OepnGL のエラーチェック
#include "core/shader.h"		// シェーダー読み込み処理
#include "core/Object.h"		// 描画のためのデータパッケージ
#include "core/Mesh.h"			// UV球のメッシュデータパッケージ
#include "core/MeshResource.h"	// UV球のメッシュデータパッケージ
#include "sdf/SDFInstance.h"
#include "mpm/mpmObject.h"	// MPM 用の描画データパッケージ

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
		std::uniform_real_distribution<GLfloat> dist(-1.0f * scale, 1.0f * scale);

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

	// 1. プログラムオブジェクトのロード
	// プログラムオブジェクトの作成
	const auto program{ loadProgram("src/render/point.vert", "src/render/point.frag") };
	// プログラムオブジェクトの作成失敗
	if (program == 0) {
		std::cerr << "Can not create program object" << std::endl;
		return EXIT_FAILURE;
	}

	// uniform 変数の設定
	const auto mcLoc{ glGetUniformLocation(program, "mc") };	// mc の場所を取得

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

	// 2. オブジェクトの作成
	// UV球を準備
	const auto meshProgram{ loadProgram("src/render/mesh.vert", "src/render/mesh.frag") };
	MeshObject obstacleMesh(32, 16);	// 障害物(UV球)のポリゴン情報
	glEnable(GL_DEPTH_TEST);

	// 図形を作成
	Object object(PARTICLE_COUNT);
	generateParticles(object, 1.0f);

	// MPM シミュレーション領域を生成
	const int N_GRID = 128; // グリッドの解像度
	mpmObject mpmObj(PARTICLE_COUNT, N_GRID);
	generateMPMParticles(mpmObj, 1.0f, false);	// false なので立方体

	// シミュレーション空間内に存在するオブジェクトリソースのロード
	auto sdfResource = std::make_shared<MeshResource>("src/assets/object.obj");


	sdfResource->generateSDF(64);// 64^3の解像度でSDFを生成

	// インスタンスを作成
	SDFInstance obstacle(sdfResource);
	obstacle.position = glm::vec3(0.5f, 0.3f, 0.5f);
	obstacle.scale = glm::vec3(-0.2f);
	obstacle.updateMatrices();

	// 地面用のオブジェクトを用意
	const auto GRID_SIZE = 20;
	Object floorObject(GRID_SIZE * GRID_SIZE);
	// 地面用の点群データを生成し転送
	std::vector<Particle> floorParticles(GRID_SIZE * GRID_SIZE);
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

	// 各種材料の特性値とシミュレーションの設定
	//const float E_s = 3.537e5f;	// ヤング率
	const float E_s = 5e4f;	// ヤング率
	const float nu_s = 0.3f;	// ポアソン比
	const float g_interval = worldScale / (float)N_GRID;	// グリッドの間隔

	// MPMObject で定義している構造体 : 空間における設定
	MPMPhysics mpmphysics{
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
	glBufferData(GL_UNIFORM_BUFFER, sizeof mpmphysics, &mpmphysics, GL_DYNAMIC_DRAW);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);

	// 背景色指定
	glClearColor(0.8f, 0.8f, 0.8f, 1.0f);

	glPointSize(static_cast<GLfloat>(window.getSize().y * 0.01));
	glEnable(GL_POINT_SMOOTH);

	// デバッグに用いる変数
	bool useDebugColor = false;         // デバッグカラーのON/OFFフラグ
	bool spacePressedLastFrame = false; // 1フレーム前のキー状態（押しっぱなし判定用）

	// インタラクティブ操作モード
	int isFireMode = 0;                // 0: カメラ操作モード, 1: 球の発射モード
	int lastMouseState = GLFW_RELEASE; // クリックされた瞬間を判定するため

	// スイングモード用の状態管理
	enum class SwingState { None, Dragging, Swinging };	// 遷移状態
	SwingState swingState = SwingState::None;			// 初期化

	glm::dvec2 dragStartPos{ 0.0, 0.0 };
	glm::vec3 swingPivot{ 0.0f };       // 支点(画面手前Z平面の懸念を考慮しオフセットした座標)
	glm::vec3 swingAxis{ 1, 0, 0 };     // 回転軸(カメラの左右方向)

	float swingMaxAngle = 0.0f;         // 溜めた角度(振幅)
	float swingCurrentTime = 0.0f;      // スイングの進行時間
	float swingRadius = 0.4f;           // 軌道半径
	float swingSpeedMult = 6.0f;        // スイングの角速度倍率

	// --- プレビュー描画用のVAO/VBOの用意 ---
	GLuint debugVAO, debugVBO;
	glGenVertexArrays(1, &debugVAO);
	glGenBuffers(1, &debugVBO);
	glBindVertexArray(debugVAO);
	glBindBuffer(GL_ARRAY_BUFFER, debugVBO);
	// 最大100頂点分のvec3データを格納できるサイズを確保 (動的に書き換えるため GL_DYNAMIC_DRAW)
	glBufferData(GL_ARRAY_BUFFER, sizeof(glm::vec3) * 100, nullptr, GL_DYNAMIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
	glBindVertexArray(0);

	// --- シミュレーション境界(Bounding Box)描画用のVAO/VBO/EBO ---
	GLuint boundaryVAO, boundaryVBO, boundaryEBO;
	glGenVertexArrays(1, &boundaryVAO);
	glGenBuffers(1, &boundaryVBO);
	glGenBuffers(1, &boundaryEBO);
	float W = worldScale; // シミュレーション空間の最大サイズ
	// 立方体の8つの頂点
	glm::vec3 boundaryVertices[] = {
		{0, 0, 0}, {W, 0, 0}, {W, 0, W}, {0, 0, W},
		{0, W, 0}, {W, W, 0}, {W, W, W}, {0, W, W}
	};
	// 線分(GL_LINES)として描画するためのインデックスデータ(12本の辺 x 2頂点)
	GLuint boundaryIndices[] = {
		0, 1, 1, 2, 2, 3, 3, 0, // 底面
		4, 5, 5, 6, 6, 7, 7, 4, // 上面
		0, 4, 1, 5, 2, 6, 3, 7  // 側面
	};
	glBindVertexArray(boundaryVAO);
	glBindBuffer(GL_ARRAY_BUFFER, boundaryVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(boundaryVertices), boundaryVertices, GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, boundaryEBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(boundaryIndices), boundaryIndices, GL_STATIC_DRAW);
	glBindVertexArray(0);

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

		// MVP行列の設定
		glm::mat4 model = glm::mat4(1.0);// モデル変換行列を設定・モデルは回転させずに固定
		glm::mat4 view = camera.getView();// ビュー変換行列を設定・カメラの現在位置を取得
		glm::mat4 projection = camera.getProjection(window.getAspect());// 投影変換行列を設定
		// 計算用
		glm::mat4 invView = glm::inverse(view);
		glm::mat4 invProj = glm::inverse(projection);

		int currentState = glfwGetMouseButton(window.get(), GLFW_MOUSE_BUTTON_LEFT);

		// クリックされた場所に球を飛ばすモード
		if (isFireMode == 1) {
			// ImGuiウィンドウ上ではなく、新しくクリックされた瞬間のみ反応
			if (!ImGui::GetIO().WantCaptureMouse && currentState == GLFW_PRESS && lastMouseState == GLFW_RELEASE) {
				// ウィンドウ(2D)上でのクリック位置(目的地)を取得
				double xpos, ypos;
				glfwGetCursorPos(window.get(), &xpos, &ypos);

				// Ray を生成
				float x = (2.0f * xpos) / window.getSize().x - 1.0f;	// -1～1 へ正規化
				float y = 1.0f - (2.0f * ypos) / window.getSize().y;	// -1～1 へ正規化・y座標の扱いのため反転
				glm::vec4 ray_clip = glm::vec4(x, y, -1.0f, 1.0f);		// 3D でのクリック位置座標に変換(OpenGLではウィンドウはサイズに関わらず正方形)

				// 3D シミュレート空間上での座標に変換・方向ベクトルを生成
				glm::vec4 ray_eye = invProj * ray_clip;// projectionの逆変換でカメラ空間へ戻す
				ray_eye /= ray_eye.w;									// 変換により w が 1 でなくなるので補正(透視除算 : Perspective Division というらしい)
				glm::vec4 ray_world = invView * ray_eye;		// view の逆変換で 3D 空間の座標に戻す
				glm::vec3 ray_origin = glm::vec3(ray_world);			// 飛んでいく目的地
				glm::vec3 ray_dir = glm::normalize(ray_origin - camera.position);// 方向ベクトルの生成( 目的地 - 出発位置 )

				// 3D空間上でのカメラ位置から光線方向へ発射
				obstacle.position = camera.position + ray_dir * 0.5f;
				obstacle.velocity = ray_dir * 6.0f;
				obstacle.scale = glm::vec3(-0.2f);
			}
		}

		// スイングモード (ドラッグで溜めて離して発動)
		if (isFireMode == 2 && !ImGui::GetIO().WantCaptureMouse) {
			// スイングモード発動時の初期化部分
			obstacle.scale = glm::vec3(-0.06f, -0.4f, -0.06f); // Y軸方向に長くして「棍棒」状にする
			if (currentState == GLFW_PRESS && lastMouseState == GLFW_RELEASE) {
				// ドラッグ開始
				swingState = SwingState::Dragging;
				glfwGetCursorPos(window.get(), &dragStartPos.x, &dragStartPos.y);

				// Z深度対策: カメラから一定距離(swingRadius等)奥を支点にする
				float x = (2.0f * dragStartPos.x) / window.getSize().x - 1.0f;
				float y = 1.0f - (2.0f * dragStartPos.y) / window.getSize().y;
				glm::vec4 ray_clip = glm::vec4(x, y, -1.0f, 1.0f);
				glm::vec4 ray_eye = invProj * ray_clip;
				ray_eye /= ray_eye.w;
				glm::vec4 ray_world = invView * ray_eye;
				glm::vec3 ray_dir = glm::normalize(glm::vec3(ray_world) - camera.position);

				float distanceD = 1.0f; // カメラからどれくらい奥をスイング平面にするか

				// Viewの逆行列の3列目（インデックス2）がカメラの後ろ方向(+Z)なので、反転させて前方向(-Z)を取得
				glm::vec3 cam_front = -glm::normalize(glm::vec3(invView[2]));

				// レイと平面の交差距離 t を計算
				float t = distanceD / glm::dot(cam_front, ray_dir);

				// 支点座標をセット
				swingPivot = camera.position + ray_dir * t;

				// 画面水平方向(カメラのRightベクトル)を回転軸に設定
				swingAxis = glm::normalize(glm::vec3(invView[0]));
			}
			else if (currentState == GLFW_RELEASE && swingState == SwingState::Dragging) {
				// ドラッグ終了 -> スイング発動
				swingState = SwingState::Swinging;
				swingCurrentTime = 0.0f;

				// 初期位置へセット
				obstacle.scale = glm::vec3(-0.06f, -0.4f, -0.06f); // SDF用の反転スケール
			}

			if (swingState == SwingState::Dragging) {
				double currentX, currentY;
				glfwGetCursorPos(window.get(), &currentX, &currentY);

				float dx = static_cast<float>(currentX - dragStartPos.x);
				float dy = static_cast<float>(currentY - dragStartPos.y);

				// カメラの右方向と「後ろ」方向（手前に引くため）を取得
				glm::vec3 cam_right = glm::normalize(glm::vec3(invView[0]));
				glm::vec3 cam_back = glm::normalize(glm::vec3(invView[2]));

				// ドラッグ量を3Dワールドベクトルに変換
				glm::vec3 drag3D = dx * cam_right + dy * cam_back;
				float dragLen = glm::length(drag3D);

				float maxPixelDrag = 300.0f; // 最大威力に必要なドラッグ量
				float normalizedDrag = glm::clamp(dragLen / maxPixelDrag, 0.0f, 1.0f);
				swingMaxAngle = normalizedDrag * glm::radians(90.0f);

				// ドラッグ距離がわずかでもあれば回転軸を更新
				if (dragLen > 1.0f) {
					// 振り子の真下ベクトルと引っ張った方向の外積が回転軸になる
					glm::vec3 downVector = glm::vec3(0.0f, -1.0f, 0.0f);
					swingAxis = glm::normalize(glm::cross(drag3D, downVector));
				}
			}
		}

		// スイング中のキネマティック軌道計算
		if (swingState == SwingState::Swinging) {
			swingCurrentTime += mpmphysics.timestep * swingSpeedMult;

			// cos波を利用して -swingMaxAngle から 逆側の +swingMaxAngle へ振り抜く
			float phase = swingCurrentTime;

			// π(半周期)を超えたらスイング終了
			if (phase > glm::pi<float>()) {
				swingState = SwingState::None;
				obstacle.velocity = glm::vec3(0.0f);
				obstacle.angularVelocity = glm::vec3(0.0f);
			}
			else {
				// 現在の角度と角速度(微積分関係)
				float currentAngle = -swingMaxAngle * cos(phase);
				float angularSpeed = swingMaxAngle * sin(phase) * swingSpeedMult;

				// 角速度ベクトル
				glm::vec3 currentAngularVelocity = swingAxis * angularSpeed;

				// 位置の算出 (支点から真下ベクトルの回転)
				glm::vec3 downVector = glm::vec3(0.0f, -1.0f, 0.0f);
				glm::mat4 rotMat = glm::rotate(glm::mat4(1.0f), currentAngle, swingAxis);
				glm::vec3 offset = glm::vec3(rotMat * glm::vec4(downVector * swingRadius, 0.0f));

				// インスタンスの強制更新
				obstacle.position = swingPivot + offset;
				// 剛体の速度法則 v = ω × r (並進速度の正確な生成)
				obstacle.velocity = glm::cross(currentAngularVelocity, offset);
				obstacle.angularVelocity = currentAngularVelocity;

				// オブジェクトの見た目も軌道に沿って回転させる
				obstacle.rotation = glm::quat_cast(rotMat);
			}
		}

		glBindBuffer(GL_UNIFORM_BUFFER, ubo);
		glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(MPMPhysics), &mpmphysics);
		glBindBuffer(GL_UNIFORM_BUFFER, 0);

		lastMouseState = currentState; // マウス状態の更新

		// SDF の更新
		obstacle.update(mpmphysics.timestep);	// 物理挙動を更新

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
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, mpmObj.vbo);

		// ユニフォームバッファオブジェクトを 1 番に結合
		glBindBufferBase(GL_UNIFORM_BUFFER, 1, ubo);

		// 3Dテクスチャを 0-3 番に結合
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

		// ウィンドウを消去(カラー/デプスバッファを初期状態に)
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		// プログラムオブジェクトを指定
		glUseProgram(program);

		// uniform 変数 mc に値を設定
		glUniformMatrix4fv(mcLoc, 1, GL_FALSE, glm::value_ptr(projection * view * model));

		// 床の描画
		glUniform3fv(glGetUniformLocation(program, "floor_normal"), 1, glm::value_ptr(mpmphysics.f_normal));
		glUniform1f(glGetUniformLocation(program, "floor_height"), mpmphysics.f_height);
		GLint isFloorLocation = glGetUniformLocation(program, "is_floor");
		glUniform1i(isFloorLocation, 1); // 地面フラグ on
		glBindVertexArray(floorObject.vao);
		glDrawArrays(GL_POINTS, 0, floorObject.count);


		// MPM 結果の描画
		glUniform1i(isFloorLocation, 0); // 地面フラグ off
		glUniform1i(glGetUniformLocation(program, "use_debug_color"), window.getUseDebugColor() ? 1 : 0); // デバッグモード起動中かどうか
		glBindVertexArray(mpmObj.vao);
		glDrawArrays(GL_POINTS, 0, mpmObj.count);

		// 障害物の描画
		glUseProgram(meshProgram);

		glm::mat4 finalModel = model * modelMat;
		glm::mat4 mvp = projection * view * finalModel;		// MVP行列を計算してシェーダーに送信
		glUniformMatrix4fv(glGetUniformLocation(meshProgram, "mc"), 1, GL_FALSE, glm::value_ptr(mvp));
		glUniformMatrix4fv(glGetUniformLocation(meshProgram, "model"), 1, GL_FALSE, glm::value_ptr(finalModel));

		glBindVertexArray(obstacle.resource->vao);
		glDrawElements(GL_TRIANGLES, obstacle.resource->indexCount, GL_UNSIGNED_INT, 0);
		glBindVertexArray(0);

		// --- シミュレーション境界の描画 ---
		glUseProgram(meshProgram);
		glm::mat4 mvp_boundary = projection * view; // 空間の原点にそのまま配置
		glUniformMatrix4fv(glGetUniformLocation(meshProgram, "mc"), 1, GL_FALSE, glm::value_ptr(mvp_boundary));
		glUniformMatrix4fv(glGetUniformLocation(meshProgram, "model"), 1, GL_FALSE, glm::value_ptr(glm::mat4(1.0f)));

		glBindVertexArray(boundaryVAO);
		// 24個のインデックスを GL_LINES (線分) として描画
		glDrawElements(GL_LINES, 24, GL_UNSIGNED_INT, 0);
		glBindVertexArray(0);

		/*
		glm::vec3 spherePos = glm::vec3(mpmphysics.obstacle_sphere);// 球の位置とサイズをシミュレーションデータから取得
		float sphereRadius = mpmphysics.obstacle_sphere.w;
		glm::mat4 objModel =
			glm::translate(glm::mat4(1.0f), spherePos) * glm::scale(glm::mat4(1.0f), glm::vec3(sphereRadius));// モデル行列の作成（平行移動 × 拡大縮小）
		glm::mat4 sphereModel = model * objModel;	// マウスの回転も含めた model を作成
		glm::mat4 mvp = projection * view * sphereModel;		// MVP行列を計算してシェーダーに送信
		glUniformMatrix4fv(glGetUniformLocation(meshProgram, "mc"), 1, GL_FALSE, glm::value_ptr(mvp));
		glUniformMatrix4fv(glGetUniformLocation(meshProgram, "model"), 1, GL_FALSE, glm::value_ptr(sphereModel));

		// メッシュを描画
		glBindVertexArray(obstacleMesh.vao);
		glDrawElements(GL_TRIANGLES, obstacleMesh.indexCount, GL_UNSIGNED_INT, 0);
		glBindVertexArray(0);
		*/

		glBindVertexArray(0);	// 念のため

		// Swing モードのプレビュー
		if (swingState == SwingState::Dragging) {
			std::vector<glm::vec3> previewPoints;
			glm::vec3 downVector = glm::vec3(0.0f, -1.0f, 0.0f);
			int segments = 30; // 円弧の分割数

			float tipOffset = abs(obstacle.scale.y) * 0.5f;
			float tipRadius = swingRadius + tipOffset;

			// 1. スイング軌道（円弧）の計算
			for (int i = 0; i <= segments; i++) {
				float t = (float)i / segments;
				float angle = -swingMaxAngle + (swingMaxAngle * 2.0f) * t;
				glm::mat4 rotMat = glm::rotate(glm::mat4(1.0f), angle, swingAxis);

				// ▼ 変更: swingRadius ではなく tipRadius を使って先端の軌道を描画
				glm::vec3 offset = glm::vec3(rotMat * glm::vec4(downVector * tipRadius, 0.0f));
				previewPoints.push_back(swingPivot + offset);
			}

			// 2. 最下点（最大速度が発生する場所）の予想速度ベクトルの計算
			glm::vec3 bottomPos = swingPivot + downVector * tipRadius; // ▼ ここも tipRadius に

			// 速度公式: ω(最大角速度) × r (※ここでの r は先端までの距離)
			glm::vec3 maxAngularVelocity = swingAxis * (swingMaxAngle * swingSpeedMult);
			glm::vec3 maxVelocity = glm::cross(maxAngularVelocity, downVector * tipRadius); // ▼ ここも

			previewPoints.push_back(bottomPos);
			previewPoints.push_back(bottomPos + maxVelocity * 0.1f);

			// 3. 支点（Pivot）の十字マーカー計算
			float d = 0.05f;
			previewPoints.push_back(swingPivot + glm::vec3(-d, 0, 0));
			previewPoints.push_back(swingPivot + glm::vec3(d, 0, 0));
			previewPoints.push_back(swingPivot + glm::vec3(0, -d, 0));
			previewPoints.push_back(swingPivot + glm::vec3(0, d, 0));
			previewPoints.push_back(swingPivot + glm::vec3(0, 0, -d));
			previewPoints.push_back(swingPivot + glm::vec3(0, 0, d));

			// データをVBOへ転送
			glBindBuffer(GL_ARRAY_BUFFER, debugVBO);
			glBufferSubData(GL_ARRAY_BUFFER, 0, previewPoints.size() * sizeof(glm::vec3), previewPoints.data());

			// meshProgram を使って単色(Shader次第)で描画
			glUseProgram(meshProgram);
			glm::mat4 mvp = projection * view; // モデル行列は単位行列（そのままワールド座標として扱う）
			glUniformMatrix4fv(glGetUniformLocation(meshProgram, "mc"), 1, GL_FALSE, glm::value_ptr(mvp));
			glUniformMatrix4fv(glGetUniformLocation(meshProgram, "model"), 1, GL_FALSE, glm::value_ptr(glm::mat4(1.0f)));

			glBindVertexArray(debugVAO);
			glDrawArrays(GL_LINE_STRIP, 0, segments + 1);             // 軌道
			glDrawArrays(GL_LINES, segments + 1, 2);                  // 速度ベクトル
			glDrawArrays(GL_LINES, segments + 3, 6);                  // 支点マーカー
			glBindVertexArray(0);
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
		ImGui::SliderFloat3("Gravity", &mpmphysics.gravity[0], -10.0f, 10.0f);
		ImGui::SliderFloat("Timestep", &mpmphysics.timestep, 0.001f, 0.1f);
		ImGui::SliderFloat("Floor Height", &mpmphysics.f_height, -5.0f, 5.0f);
		ImGui::SliderFloat3("Floor Normal", &mpmphysics.f_normal[0], 0.0f, 1.0f);
		ImGui::SliderFloat("Floor Restitution", &mpmphysics.f_restitution, 0.0f, 1.0f);
		ImGui::SliderFloat("Floor Friction", &mpmphysics.f_friction, 0.0f, 1.0f);
		ImGui::SliderFloat("dx", &mpmphysics.dx, 0.0f, 1.0f);
		ImGui::SliderFloat("inv_dx", &mpmphysics.inv_dx, 0.0f, N_GRID * 2);
		ImGui::SliderFloat("Particle Restitution", &mpmphysics.p_restitution, 0.0f, 1.0f);
		ImGui::SliderFloat("Particle vol", &mpmphysics.p_vol, 0.0f, 1.0f);
		//ImGui::SliderFloat("Particle mu", &mpmphysics.p_mu, 0.0f, 1.0f);
		//ImGui::SliderFloat("Particle lambda", &mpmphysics.p_lambda, 0.0f, 1.0f);
		ImGui::SliderFloat("Prticle Mass", &mpmphysics.p_mass, 0.1f, 10.0f);
		ImGui::SliderFloat("Particle Radius", &mpmphysics.p_radius, 0.01f, 1.0f);
		ImGui::SliderFloat("Particle Overlap", &mpmphysics.p_overlap, 0.0f, 0.01f);

		ImGui::Separator(); // デバック範囲のため区切り線
		bool debugFlag = window.getUseDebugColor();
		if (ImGui::Checkbox("Debug Color Mode (Space key)", &debugFlag)) {
			window.setUseDebugColor(debugFlag);
		}

		// ▼ 追加：インタラクションモードの切り替え
		ImGui::Separator();
		ImGui::Text("Interaction Mode:");
		ImGui::RadioButton("Camera Control", &isFireMode, 0); ImGui::SameLine();
		ImGui::RadioButton("Shoot Sphere", &isFireMode, 1); ImGui::SameLine();
		ImGui::RadioButton("Swing", &isFireMode, 2);
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