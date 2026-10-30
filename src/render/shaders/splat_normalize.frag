#version 430 core

//==============================================================================
// splat_normalize.frag : 蓄積した円盤を正規化して G-buffer に書く (Splat モード P5)
// 目的: 画素ごとに Σ w で割って、色・法線・深度の重み付き平均を求める。
//       重なり合う円盤の値が滑らかに混ざるので、粒子の集まりが連続した層として見える。
//       結果は G-buffer (location 0: 色 + 種別, 1: 法線 + あいまいさ, 2: 線形深度, 深度) に書き、
//       この後の SSAO・陰影の計算・粉塵は、ほかのモードと同じ処理をそのまま使う。
//       被覆率 α = 1 - exp(-k Σw) が opaqueAlpha 未満の画素は書かない
//       (段階 2 では opaqueAlpha = 0 で、円盤がかかった画素をすべて書く)。
//==============================================================================

in vec2 v_uv;

uniform sampler2D accum0;	// rgb: Σ w·色, a: Σ w
uniform sampler2D accum1;	// xyz: Σ w·法線, w: Σ w·あいまいさ
uniform sampler2D accum2;	// Σ w·線形深度
uniform mat4 projection;
uniform float coverageK;	// 被覆率の k
uniform float opaqueAlpha;	// これ以上の被覆率の画素だけを書く

layout(location = 0) out vec4 o_albedo;	// rgb: 色 (線形), a: 種別 (0.25 = 粒子)
layout(location = 1) out vec4 o_normal;	// xyz: ビュー空間の法線, w: あいまいさ
layout(location = 2) out float o_linearDepth;	// -z_view [m]

void main() {
	ivec2 pix = ivec2(gl_FragCoord.xy);
	vec4 a0 = texelFetch(accum0, pix, 0);
	float sw = a0.a;
	if (sw < 1e-4) discard;		// 円盤がかかっていない画素

	float alpha = 1.0 - exp(-coverageK * sw);
	if (alpha < opaqueAlpha) discard;

	vec4 a1 = texelFetch(accum1, pix, 0);
	float depthLin = texelFetch(accum2, pix, 0).r / sw;
	vec3 n = a1.xyz;
	n = (dot(n, n) > 1e-12) ? normalize(n) : vec3(0.0, 0.0, 1.0);

	// 線形深度から深度バッファの値を求める (透視投影: clip.z = P22·z + P32, clip.w = -z)
	float z = -depthLin;
	float ndcZ = (projection[2][2] * z + projection[3][2]) / -z;
	gl_FragDepth = ndcZ * 0.5 + 0.5;

	o_albedo = vec4(a0.rgb / sw, 0.25);
	o_normal = vec4(n, a1.w / sw);
	o_linearDepth = depthLin;
}
