#pragma once

// OpenGL関連
#include <GL/glew.h>	// glew使用に際して入れておかないといけない
#include <GLFW/glfw3.h> // ウィンドウ周りの機能を提供(OpenGLだけではそのあたりのサポートはない)

// GLMまわり
#define _USE_MATH_DEFINES	//M_PIなどを使用可能に
#define GLM_FORCE_RADIANS	//GLMの角度を度の単位でなくラジアンの単位に(もともと暗黙的で紛らわしいらしい)
#include <GLM/glm.hpp>		//OpenGL向けのC++数学ライブラリ
#include <GLM/gtc/quaternion.hpp>

// ImGui の組み込み
#define USE_IMGUI true
#if USE_IMGUI
#	include "../ImGui/imgui.h"
#	include "../ImGui/imgui_impl_glfw.h"
#	include "../ImGui/imgui_impl_opengl3.h"
#endif

// 標準ライブラリ
#include <array>

//
// ウィンドウ関連の処理クラス
//
class Window
{
	/// ウィンドウの識別子
	GLFWwindow* const window;	// 一度開いたウィンドウは廃棄するまで保持するのでconst

	/// ウィンドウのサイズ
	glm::dvec2 size;			// dvecなのでdouble型

	/// 操作しているマウスボタン
	int button{ -1 };

	/// ボタンごとのマウスボタンを押した位置
	std::array<glm::dvec2, GLFW_MOUSE_BUTTON_LAST + 1> start{};

	/// ボタンごとの回転
	std::array<glm::dquat, GLFW_MOUSE_BUTTON_LAST + 1> rotation{};

	/// ボタンごとのモデル変換行列
	std::array<glm::mat4, GLFW_MOUSE_BUTTON_LAST + 1> model{};

	/// トラックボール処理の途中経過
	glm::dquat trackball{};

	// マウスホイールの回転量
	glm::dvec2 scroll{ 0.0, 0.0 };

	// 回転前の位置
	glm::dvec2 lastPos{ 0.0, 0.0 };

	// デバックカラー表示フラグ
	bool useDebugColor{ false };

	/// ウィンドウサイズ変更時の処理
	/// @param[in] window サイズ変更の対象のウィンドウの識別子
	/// @param[in] width サイズ変更の対象のウィンドウの幅
	/// @param[in] height サイズ変更の対象のウィンドウの高さ

	// @note glfwSetWindowSizeCallback() で登録するコールバック関数
	static auto resize(GLFWwindow* window, int width, int height) -> void {
		// window が保持するインスタンスの　this ポインタを得る
		const auto instance{ static_cast<Window*>(glfwGetWindowUserPointer(window)) };

		// インスタンスからの呼び出しでなければ終了
		if (instance == nullptr) return;

		// インスタンスのウィンドウサイズを更新
		instance->size = { width, height };

		// フレームバッファの大きさ
		int fbWidth, fbHeight;

		// フレームバッファの大きさを得る
		glfwGetFramebufferSize(window, &fbWidth, &fbHeight);

		// フレームバッファ全体をビューポートにする
		glViewport(0, 0, fbWidth, fbHeight);
	}

	/// マウスボタンの操作時の処理
	/// @param[in] window マウスボタンの操作を受け付けるウィンドウ識別子
	/// @param[in] button 押されたマウスボタンの識別子
	/// @param[in] action マウスボタンの状態
	/// @param[in] mods マウスボタンの状態に影響する修飾キー(Shift, Ctrl, Alt)
	/// 
	/// @note glfwSetMouseButtonCallback() で登録するコールバック関数
	static void mouse(GLFWwindow* window, int button, int action, int mods) {
#if defined(IMGUI_VERSION)
		// ImGui がマウスを使うときは window クラスのマウス位置を更新
		if (ImGui::GetIO().WantCaptureMouse) return;
#endif
		// window が保持するインスタンスの this ポインタを得る
		const auto instance{ static_cast<Window*>(glfwGetWindowUserPointer(window)) };

		// インスタンスからの呼び出しでないなら戻る
		if (instance == nullptr)return;

		// マウスボタンを押していたら
		if (action != GLFW_RELEASE) {
			// 押したマウスボタンを記録
			instance->button = button;

			// ドラッグ開始時のカーソル位置を「保存する
			auto& cursor{ instance->start[button] };
			glfwGetCursorPos(window, &cursor.x, &cursor.y);
		}
		else {
			// マウスボタンを離したことを記録
			instance->button = -1;

			// ドラッグ終了時の回転を保存
			instance->rotation[button] = instance->trackball;
		}
	}

	/// マウスホイールを操作した時の処理
	/// @param[in] window マウスホイールの操作を受け付けるウィンドウの識別子
	/// @param[in] x マウスホイールの x 方向の回転量
	/// @param[in] y マウスホイールの y 方向の回転量
	/// @note glfwSetScrollCallback() で登録するコールバック関数
	static void wheel(GLFWwindow* window, double x, double y) {
#if defined(IMGUI_VERSION)
		// ImGui がマウスを使うときは Window クラスのホイールの回転量を更新しない
		if (ImGui::GetIO().WantCaptureMouse) return;
#endif

		// window が保持するインスタンスの this ポインタを得る
		const auto instance{ static_cast<Window*>(glfwGetWindowUserPointer(window)) };

		// インスタンスからの呼び出しでなければ戻る
		if (instance == nullptr) return;

		// マウスホイールの回転量の保存
		instance->scroll += glm::dvec2{ x, y };
	}

	/// キーボード操作時の処理
	/// @note glfwSetKeyCallback() で登録するコールバック関数
	static void key(GLFWwindow* window, int key, int scancode, int action, int mods) {
#if defined(IMGUI_VERSION)
		// ImGui がキーボードを使うときは Window クラスのキー入力を無視
		if (ImGui::GetIO().WantCaptureKeyboard) return;
#endif
		const auto instance{ static_cast<Window*>(glfwGetWindowUserPointer(window)) };
		if (instance == nullptr) return;

		// スペースキーが「押された瞬間 (GLFW_PRESS)」のみ反応させる
		if (key == GLFW_KEY_SPACE && action == GLFW_PRESS) {
			instance->useDebugColor = !(instance->useDebugColor); // フラグを反転
		}
	}

public:
	/// コンストラクタ
	/// @param[in] width ウィンドウの幅
	/// @param[in] height ウィンドウの高さ
	/// @param[in] title ウィンドウのタイトル

	Window(int width = 640, int height = 640, const char* title = "GLFW Window") :
		// メンバ変数の初期化

		// ウィンドウを生成し識別子を保存
		window{ glfwCreateWindow(width,height,title, nullptr, nullptr) },

		// 開いたウィンドウのサイズを保存する
		size{ width,height }
		// コンストラクタ内本体の処理
	{
		// 開いたウィンドウがなければ戻る
		if (window == nullptr)return;

		// 現在のウィンドウを処理対象とする
		glfwMakeContextCurrent(window);

		// 表示はディスプレイのリフレッシュレートに同期させる
		glfwSwapInterval(1);

		// このインスタンスの this ポインタを記録しておく
		glfwSetWindowUserPointer(window, this);

		// マウスホイールの操作時に呼び出す処理を登録する
		glfwSetScrollCallback(window, wheel);

		// マウスボタンの操作時に呼び出す処理を登録する
		glfwSetMouseButtonCallback(window, mouse);

		// キーボードの操作時に呼び出す処理を登録する
		glfwSetKeyCallback(window, key);

		// 各種の状態の復帰処理を行う
		reset();

		// ウィンドウのサイズ変更時に呼び出す処理を登録する
		glfwSetWindowSizeCallback(window, resize);

		// 開いたウィンドウに初期設定を適用する
		resize(window, width, height);
	}

	// コピーコンストラクタは使用しない
	Window(const Window& draw) = delete;

	/// デコンストラクタ(オブジェクトの破棄時に自動で呼び出される)
	virtual ~Window() {
		// ウィンドウを破棄する
		glfwDestroyWindow(window);
	}

	// 代入演算子は使用しない
	Window& operator=(const Window& draw) = delete;

	// ムーブ代入演算子はデフォルトのものを使用する
	Window& operator=(Window&& window) = default;

	/// 更新処理
	auto update() -> void {
		// マウスのいずれのボタンも押されていなければ何もしない
		if (button < GLFW_MOUSE_BUTTON_LEFT) return;

#if defined(IMGUI_VERSION)
		// ImGui の状態を取り出す
		const auto& io{ ImGui::GetIO() };

		// ImGui がマウスを使うときは Window クラスのマウス位置を更新しない
		if (io.WantCaptureMouse) return;

		// マウスの現在位置を取り出す
		double x{ io.MousePos.x }, y{ io.MousePos.y };
#else
		// マウスの現在位置を取り出す
		double x, y;
		glfwGetCursorPos(window, &x, &y);
#endif

		// マウスの相対変位
		const auto dx{ (x - start[button].x) / size.x };
		const auto dy{ (start[button].y - y) / size.y };

		// マウスイポイントの位置のドラッグ開始位置の距離
		const auto length{ hypot(dx, dy) };

		// マウスイポイントの位置が移動していなければ
		if (length == 0.0) return;

		// マウスの移動方向と直行するベクトルを回転軸
		const auto axis{ glm::normalize(glm::dvec3(-dy, dx, 0.0)) };

		// マウスの移動量を回転角とした回転を現在の回転と合成
		trackball = glm::angleAxis(length * M_PI, axis) * rotation[button];

		// 合成した回転の四元数から回転の変換行列を求める
		model[button] = glm::mat4_cast(static_cast<glm::quat>(trackball));
	}

	/// 復帰処理
	auto reset() -> void {
		// すべてのボタンの回転を初期化する
		std::fill(rotation.begin(), rotation.end(), glm::quat(1.0f, 0.0f, 0.0f, 0.0f));

		// すべてボタンのモデル変換行列を初期化する
		std::fill(model.begin(), model.end(), glm::mat4(1.0f));
	}

	/// マウスの移動量を取り出す
	auto getMouseDelta() -> glm::dvec2 {
		double x, y;
#if defined(IMGUI_VERSION)
		if (ImGui::GetIO().WantCaptureMouse) return { 0,0 };
		x = ImGui::GetIO().MousePos.x;
		y = ImGui::GetIO().MousePos.y;
#else
		glfwGetCursorPos(window, &x, &y);
#endif
		glm::dvec2 current{ x, y };
		glm::dvec2 delta = current - lastPos;
		lastPos = current;

		return delta;
	}

	/// ウィンドウの識別子を取り出す
	/// @return ウィンドウの識別子
	auto get() const {
		// 識別子を返す
		return window;
	}

	/// ウィンドウサイズを取り出す
	/// @return ウィンドウのサイズ
	const auto& getSize() const {
		// ウィンドウのサイズを返す
		return size;
	}

	/// デバッグカラー表示フラグを取り出す
	bool getUseDebugColor() const {
		return useDebugColor;
	}

	/// スクロール量（Y方向）を取り出す(の後にリセット)
	auto getScrollDelta() -> double {
		double currentScroll = scroll.y;
		scroll.y = 0.0; // 1フレームごとにリセット
		return currentScroll;
	}

	/// ImGui等から強制的にフラグを書き換える用
	void setUseDebugColor(bool flag) {
		useDebugColor = flag;
	}

	/// 描画の継続判定
	/// こいつのおかげで while(window){} ができる
	/// @return 描画を継続する場合 true
	explicit operator bool() {
		// イベントを取り出す
		glfwPollEvents();

		// ウィンドウを閉じるなら false を返す
		if (glfwWindowShouldClose(window))return false;

#if defined(IMGUI_VERSION)
		// ImGui の新規フレームを作成する
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
#endif

		// ウィンドウを閉じない
		return true;
	}

	/// ダブルバッファリング
	auto swapBuffers() const {
#if defined(IMGUI_VERSION)
		ImGui::Render();
		// ImGui の描画データがあればフレームをレンダリングする
		const auto data{ ImGui::GetDrawData() };
		if (data) ImGui_ImplOpenGL3_RenderDrawData(data);
#endif
		// カラーバッファを入れ替える
		glfwSwapBuffers(window);
	}

	/// ウィンドウの縦横比を取り出す
	/// @return ウィンドウの縦横比
	auto getAspect() const {
		// ウィンドウのサイズから縦横比を計算して返す
		return static_cast<GLfloat>(size.x / size.y);
	}

	/// モデル変換行列を取り出す
	/// @param[in] button マウスボタンの識別子
	/// @return モデル変換行列
	const auto& getModel(int button) {
		// マウスホイールの回転量をモデル変換行列の平行移動量に設定
		model[button][3][0] = static_cast<float>(scroll.x * 0.1);
		model[button][3][2] = static_cast<float>(scroll.y * 0.1);

		// 指定したボタンに割り当てたモデル変換行列を返す
		return model[button];
	}
};

