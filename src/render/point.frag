#version 430 core

// フラグメントの色
layout (location = 0) out vec4 color;

uniform bool is_floor;			// 扱っている点が床か粒子か
uniform bool use_debug_color;	// デバッグモードか

// バーテックスシェーダーから受け取る状態変数
in flat int v_state;
in flat int v_material;

void main(){
	if(is_floor) {
		// 地面を白に設定
		color = vec4(1.0f, 1.0f, 1.0f, 1.0f);
	} else {
		if (use_debug_color){
			// デバッグ・変形状態表示モード
			// state によって色を変える
			if (v_state == 0) {
				// 状態0 (変形なし/引張): 赤
				color = vec4(1.0f, 0.2f, 0.2f, 1.0f);
			} else if (v_state == 1) {
				// 状態1 (粘着力で変形が抑えられている/弾性域): 黄緑
				color = vec4(0.6f, 0.9f, 0.1f, 1.0f);
			} else if (v_state == 2) {
				// 状態2 (塑性変形が起きている): 青
				color = vec4(0.2f, 0.4f, 1.0f, 1.0f);
			} else {
				// 予期せぬ値の場合はマゼンタ (デバッグ用)
				color = vec4(1.0f, 0.0f, 1.0f, 1.0f);
			}
		} else {
			// 通常色モード
			if (v_material == 1) {
				color = vec4(0.5f, 0.5f, 0.5f, 1.0f); // 石のグレー
			} else {
				color = vec4(0.32f, 0.18f, 0.1f, 1.0f); // 砂のこげ茶色
			}
		}
	}
}