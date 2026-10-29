#version 430 core

//==============================================================================
// sphere.vert : 粒子を球インポスタとして描く頂点シェーダー
// 目的: 粒子 1 個を点スプライト 1 個として描き、その大きさを「半径 r の球を
//       投影したときの直径」に合わせる。球の形と深度はフラグメントシェーダーで作る。
//       カメラ (透視投影) からの G-buffer と、光源 (平行投影) からのシャドウマップの
//       両方で使う。
//==============================================================================

// 粒子の位置と状態 (MpmObject の VAO から)
layout(location = 0) in vec4 position;
layout(location = 1) in int state;

// 粒子のデータ構造 (シミュレーションのシェーダーと同じもの)。材質フラグ padding[0] を読むために使う
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
layout(std430, binding = 0) readonly buffer mpmBuffer { mpmParticle p[]; };

uniform mat4 view;
uniform mat4 projection;
uniform float pointRadius;		// 描画半径 [m]
uniform float viewportHeight;	// 描画先の高さ [px]

flat out vec3 v_centerView;	// 球の中心 (ビュー座標)
flat out int v_state;		// 塑性の状態 (デバッグ色用)
flat out int v_material;	// 材質 (0 = 砂, 1 = 石)
flat out int v_id;			// 粒子番号 (色のばらつき用)

void main() {
	vec4 pv = view * vec4(position.xyz, 1.0);
	v_centerView = pv.xyz;
	v_state = state;
	v_material = p[gl_VertexID].padding[0];
	v_id = gl_VertexID;

	gl_Position = projection * pv;
	// 投影後の直径 [px] = 2r · P[1][1] · (H / 2) / w
	// 透視投影でも平行投影 (w = 1) でも同じ式で使える。1 px 未満にはしない
	gl_PointSize = max(pointRadius * projection[1][1] * viewportHeight / gl_Position.w, 1.0);
}
