#version 430 core

//==============================================================================
// surface_composite.frag : 面を張った結果を G-buffer に書き戻す
// 目的: 粒子の画素だけ、G-buffer の法線 (location 1) と深度を、平滑化した面のものに置き換える。
//       色 (location 0) と線形深度 (location 2) は書き換えない (C++ 側で描画先から外している)。
//==============================================================================

layout(location = 1) out vec4 o_normal;

uniform sampler2D surfaceTex;	// xyz = 法線、w = 平滑化した線形深度 (0 = 粒子なし)
uniform mat4 projection;

void main() {
	vec4 s = texelFetch(surfaceTex, ivec2(gl_FragCoord.xy), 0);
	if (s.w <= 0.0) discard;	// 粒子のない画素 (床・障害物・背景) はそのまま

	o_normal = vec4(s.xyz, 0.0);

	// 線形深度 z (ビュー座標の z = -z) を、深度バッファの値 [0, 1] に戻す
	float z = s.w;
	float ndcZ = (projection[2][2] * (-z) + projection[3][2]) / z;
	gl_FragDepth = ndcZ * 0.5 + 0.5;
}
