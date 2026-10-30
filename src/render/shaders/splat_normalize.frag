#version 430 core

//==============================================================================
// splat_normalize.frag : 蓄積した円盤を正規化する (Splat モード P4/P5)
// 目的: 画素ごとに Σ w で割って、色・法線・深度の重み付き平均を求める。
//       重なり合う円盤の値が滑らかに混ざるので、粒子の集まりが連続した層として見える。
//   target 0 (P4): splat 層へ。円盤がかかった画素すべてに、色 + 被覆率 α、法線 + あいまいさ、
//                  線形深度を書く。被覆率 α = 1 - exp(-k Σw) は、その画素がどれだけ粒子に覆われているか
//   target 1 (P5): G-buffer へ。α が opaqueAlpha 以上の不透明な画素だけを書き
//                  (location 0: 色 + 種別, 1: 法線 + あいまいさ, 2: 線形深度, 深度)、
//                  SSAO・深度の前後関係は G-buffer の不透明な部分で扱う。
//   α が小さい輪郭やまばらな部分は splat 層だけに入り、陰影の計算 (lighting.frag) で
//   背景の色の上に α で重ねる。
//==============================================================================

in vec2 v_uv;

uniform sampler2D accum0;	// rgb: Σ w·色, a: Σ w
uniform sampler2D accum1;	// xyz: Σ w·法線, w: Σ w·あいまいさ
uniform sampler2D accum2;	// Σ w·線形深度
uniform sampler2D coverageTex;	// 視線上のすべての円盤の Σ w (被覆率のパス)
uniform mat4 projection;
uniform float coverageK;	// 被覆率の k
uniform float opaqueAlpha;	// これ以上の被覆率の画素だけを G-buffer に書く
uniform int target;			// 0: splat 層, 1: G-buffer

layout(location = 0) out vec4 o_albedo;	// rgb: 色 (線形), a: 種別 (0.25 = 粒子) / splat 層では被覆率 α
layout(location = 1) out vec4 o_normal;	// xyz: ビュー空間の法線, w: あいまいさ
layout(location = 2) out float o_linearDepth;	// -z_view [m]

void main() {
	ivec2 pix = ivec2(gl_FragCoord.xy);
	vec4 a0 = texelFetch(accum0, pix, 0);
	float sw = a0.a;
	if (sw < 1e-4) discard;		// 円盤がかかっていない画素

	// 被覆率は、手前の層だけでなく視線上のすべての円盤の重みで決める
	float cover = max(texelFetch(coverageTex, pix, 0).r, sw);
	float alpha = 1.0 - exp(-coverageK * cover);
	if (target == 1 && alpha < opaqueAlpha) discard;

	vec4 a1 = texelFetch(accum1, pix, 0);
	float depthLin = texelFetch(accum2, pix, 0).r / sw;
	vec3 n = a1.xyz;
	n = (dot(n, n) > 1e-12) ? normalize(n) : vec3(0.0, 0.0, 1.0);

	// 線形深度から深度バッファの値を求める (透視投影: clip.z = P22·z + P32, clip.w = -z)
	float z = -depthLin;
	float ndcZ = (projection[2][2] * z + projection[3][2]) / -z;
	gl_FragDepth = ndcZ * 0.5 + 0.5;

	o_albedo = vec4(a0.rgb / sw, (target == 1) ? 0.25 : alpha);
	o_normal = vec4(n, a1.w / sw);
	o_linearDepth = depthLin;
}
