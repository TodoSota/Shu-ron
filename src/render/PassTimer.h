#pragma once

//==============================================================================
// PassTimer.h : 描画パスごとの GPU 時間の計測
// 目的: GL_TIME_ELAPSED クエリで各パスの実行時間を測り、UI に表示する。
//       結果は数フレーム遅れて読み出し (GL_QUERY_RESULT_AVAILABLE を確認)、
//       CPU が GPU の完了を待つことによる性能の低下を避ける。
//       シミュレーションのパスは MpmEvaluator が別に計測している。
//==============================================================================

#include <GL/glew.h>
#include <string>
#include <vector>

class PassTimer {
public:
	PassTimer() = default;
	~PassTimer();

	PassTimer(const PassTimer&) = delete;
	PassTimer& operator=(const PassTimer&) = delete;

	/// フレームの最初に呼ぶ (数フレーム前の結果を回収する)
	void beginFrame();
	/// パスの計測を始める (計測は入れ子にできない)
	void begin(const char* name);
	/// パスの計測を終える
	void end();

	/// 直近に回収できた結果 (パス名と時間 [ms]) 。指数移動平均で平滑化している
	const std::vector<std::pair<std::string, double>>& results() const { return smoothed; }

private:
	static constexpr int kFrames = 4;	// クエリの組をいくつ回して使うか
	struct Slot {
		std::vector<GLuint> queries;
		std::vector<std::string> names;
		int used{ 0 };
	};
	Slot slots[kFrames];
	int current{ -1 };
	bool active{ false };
	std::vector<std::pair<std::string, double>> smoothed;

	void collect(Slot& slot);
};
