//==============================================================================
// PassTimer.cpp : 描画パスごとの GPU 時間の計測の実装
//==============================================================================
#include "PassTimer.h"

PassTimer::~PassTimer() {
	for (auto& s : slots) {
		if (!s.queries.empty()) glDeleteQueries(static_cast<GLsizei>(s.queries.size()), s.queries.data());
	}
}

void PassTimer::beginFrame() {
	current = (current + 1) % kFrames;
	Slot& slot = slots[current];
	// kFrames 前のフレームで発行したクエリの結果を回収してから再利用する
	if (slot.used > 0) collect(slot);
	slot.used = 0;
	slot.names.clear();
}

void PassTimer::begin(const char* name) {
	if (current < 0 || active) return;
	Slot& slot = slots[current];
	if (slot.used == static_cast<int>(slot.queries.size())) {
		GLuint q;
		glGenQueries(1, &q);
		slot.queries.push_back(q);
	}
	slot.names.emplace_back(name);
	glBeginQuery(GL_TIME_ELAPSED, slot.queries[slot.used]);
	active = true;
}

void PassTimer::end() {
	if (!active) return;
	glEndQuery(GL_TIME_ELAPSED);
	slots[current].used++;
	active = false;
}

void PassTimer::collect(Slot& slot) {
	// まだ結果が出ていないクエリがあれば、このフレームの分は捨てる (待たない)
	for (int i = 0; i < slot.used; ++i) {
		GLint available = 0;
		glGetQueryObjectiv(slot.queries[i], GL_QUERY_RESULT_AVAILABLE, &available);
		if (!available) return;
	}
	for (int i = 0; i < slot.used; ++i) {
		GLuint64 ns = 0;
		glGetQueryObjectui64v(slot.queries[i], GL_QUERY_RESULT, &ns);
		const double ms = ns / 1.0e6;
		bool found = false;
		for (auto& r : smoothed) {
			if (r.first == slot.names[i]) { r.second = 0.9 * r.second + 0.1 * ms; found = true; break; }
		}
		if (!found) smoothed.emplace_back(slot.names[i], ms);
	}
}
