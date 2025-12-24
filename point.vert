#version 430 core

// 頂点の位置
layout (location = 0) in vec4 position;

// Uniformを追加
uniform vec3 floor_normal;
uniform float floor_height;
uniform bool is_floor; // 地面描画の際には true

// モデルビュー投影変換行列
uniform mat4 mc;

void main(){
	vec4 pos = position;
	if(is_floor) {
		float h = floor_height - (floor_normal.x * pos.x + floor_normal.z * pos.z) / floor_normal.y;
		pos.y = h;
	}

	// 頂点の位置をクリッピング座標系に変換
	gl_Position = mc * pos;
}