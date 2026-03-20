#version 430 core

// フラグメントの色
layout (location = 0) out vec4 color;

// 扱っている点が床か粒子か
uniform bool is_floor;

void main(){
	if(is_floor) {
		// 地面を白に設定
		color = vec4(1.0f, 1.0f, 1.0f, 1.0f);
	} else {
		// 粒子の色としてこげ茶色を出力する
		color = vec4(0.32f, 0.18f, 0.1f, 1.0f);
	}
}