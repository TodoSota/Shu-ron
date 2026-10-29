#version 430 core

//==============================================================================
// dust.frag : 粉塵を WBOIT (Weighted Blended OIT, McGuire & Bavoil 2013) のバッファへ蓄積する
// 目的: 半透明の粉塵を、奥から手前への並べ替えなしに合成するため、
//         accum     += w · (α·c, α)
//         revealage *= (1 - α)
//       を書き出す (加算と乗算は C++ 側のブレンド設定で行う)。
//       重み w は手前ほど大きくして、前後関係をおおまかに反映させる。
//   ・形: 中心が濃く縁が薄いガウス分布状の円
//   ・前後判定: G-buffer の深度と比べ、砂や障害物の後ろにある部分は描かない
//   ・soft particle: 砂や床に近い部分ほど薄くし、板が刺さったような境目を消す
//==============================================================================

flat in float v_alpha;
flat in float v_viewDepth;
flat in vec3 v_light;

uniform sampler2D sceneDepth;	// G-buffer の深度
uniform mat4 projection;
uniform vec3 dustColor;
uniform float exposure;
uniform float softRange;		// この距離 [m] 以内に近づくと薄くなる

layout(location = 0) out vec4 o_accum;
layout(location = 1) out float o_reveal;

vec3 toneMap(vec3 x) {
	return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
	vec2 c = gl_PointCoord * 2.0 - 1.0;
	float r2 = dot(c, c);
	if (r2 > 1.0) discard;

	// 画面に描かれている面の線形深度 (背景なら非常に遠い)
	float d = texelFetch(sceneDepth, ivec2(gl_FragCoord.xy), 0).r;
	float sceneZ = (d >= 1.0) ? 1e6 : projection[3][2] / ((d * 2.0 - 1.0) + projection[2][2]);
	if (v_viewDepth >= sceneZ) discard;							// 面の後ろ
	float soft = clamp((sceneZ - v_viewDepth) / softRange, 0.0, 1.0);

	float alpha = v_alpha * exp(-3.0 * r2) * soft;
	if (alpha < 1e-4) discard;

	// 色 (画面の表示値。陰影の計算パスと同じトーンマップとガンマ)
	vec3 color = pow(toneMap(dustColor * v_light * exposure), vec3(1.0 / 2.2));

	// 重み (McGuire & Bavoil 2013 の式 (10) を基にした深度と α の関数)
	float w = clamp(pow(min(1.0, alpha * 10.0) + 0.01, 3.0) * 1e8 * pow(1.0 - gl_FragCoord.z * 0.9, 3.0), 1e-2, 3e3);
	o_accum = vec4(color * alpha, alpha) * w;
	o_reveal = alpha;
}
