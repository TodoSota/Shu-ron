#version 430 core

// 頂点の位置 (C++ の VAO から)
layout (location = 0) in vec4 position;

// Uniformを追加
uniform vec3 floor_normal;
uniform float floor_height;
uniform bool is_floor; // 地面描画の際には true

// モデルビュー投影変換行列
uniform mat4 mc;

// MPMの粒子群のデータ構造（コンピュートシェーダーと同じもの）
struct mpmParticle {
	vec4 position;
	vec4 velocity;
	mat4 affineC;
	mat4 deformation;
	float alpha;
	float q;
	float vc;
	int state;
	int scale;
	int padding[3];
};

// SSBOバインド (メインループでバインド済みのVBO兼SSBOを直接読み込む)
layout(std430, binding = 0) buffer mpmBuffer {
	mpmParticle p[];
};

// フラグメントシェーダーへ送る状態変数 (補間しないようにflatを指定)
out flat int v_state;

void main(){
	vec4 pos = position;
	if(is_floor) {
		float h = floor_height - (floor_normal.x * pos.x + floor_normal.z * pos.z) / floor_normal.y;
		pos.y = h;
		v_state = -1;	// 地面用のダミー値
	} else {
		v_state = p[gl_VertexID].state;
	}

	// 頂点の位置をクリッピング座標系に変換
	gl_Position = mc * pos;
}