#version 430 core

//==============================================================================
// gbuffer_mesh.vert : 床と障害物 (三角形メッシュ) を G-buffer に描く頂点シェーダー
// 目的: 頂点を MVP で変換し、法線をビュー空間へ変換してフラグメントシェーダーへ渡す。
//       シャドウマップのパスでも、view と projection を光源のものにして使う。
//==============================================================================

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out vec3 v_normalView;
out vec3 v_positionView;	// ビュー座標の位置 (法線を視点側に向けるのに使う)

void main() {
	mat4 modelView = view * model;
	// 法線は「モデルビュー行列の逆行列の転置」で変換する (非等方スケールでも面に垂直なまま)
	v_normalView = transpose(inverse(mat3(modelView))) * normal;
	vec4 pv = modelView * vec4(position, 1.0);
	v_positionView = pv.xyz;
	gl_Position = projection * pv;
}
