#version 430 core

//==============================================================================
// normal_from_depth.frag : 平滑化した深度から面の法線を再構成する
// 目的: 平滑化した深度から各画素のビュー座標を復元し、隣の画素との差分の外積で
//       面の法線を求める。輪郭で法線が乱れないように、左右 (上下) の差分のうち
//       深度の差が小さいほうを使う。さらに、深度の差が「この画素の大きさで面として
//       ありうる範囲」を超える場合は、その差分を使わない。両側とも使えない画素
//       (平滑化後も深度が階段状に残った段差など) では、平滑化前の球の法線を使う。
//       (段差の画素で差分から法線を作ると、下向きの法線になって黒い線やちらつきになる)
//       さらに、平滑化で消えた粒状感を戻すため、
//         ・平滑化前の球の法線を少し混ぜる
//         ・ワールド座標の 3D ノイズの勾配で法線を少し揺らす (細かい凹凸)
//       を行う。出力: xyz = ビュー空間の法線、w = 平滑化した線形深度 (0 = 粒子なし)
//==============================================================================

out vec4 o_surface;

uniform sampler2D smoothDepth;	// 平滑化した線形深度
uniform sampler2D gNormal;		// 平滑化前の球の法線 (ビュー空間)
uniform mat4 projection;
uniform mat4 invView;
uniform float sphereNormalMix;
uniform float grainStrength;
uniform float grainSize;		// 凹凸の大きさ [m]
uniform float depthThreshold;	// 平滑化と同じ深度の閾値 [m]
uniform float pixelsPerMeter;	// 深度 1 m で 1 m が何ピクセルか

const float slopeFactor = 5.0;	// smooth_depth.frag と同じ、面の傾きの許容量

ivec2 size;

// 線形深度 z の画素 p のビュー座標 (対称な透視投影を仮定)
vec3 viewPos(ivec2 p, float z) {
	vec2 ndc = (vec2(p) + 0.5) / vec2(size) * 2.0 - 1.0;
	return vec3(ndc.x * z / projection[0][0], ndc.y * z / projection[1][1], -z);
}

// 3D の値ノイズ (格子点の乱数を三重線形補間)
float hash3(vec3 c) {
	return fract(sin(dot(c, vec3(127.1, 311.7, 74.7))) * 43758.5453);
}
float valueNoise(vec3 x) {
	vec3 i = floor(x), f = fract(x);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(mix(hash3(i + vec3(0,0,0)), hash3(i + vec3(1,0,0)), f.x),
	               mix(hash3(i + vec3(0,1,0)), hash3(i + vec3(1,1,0)), f.x), f.y),
	           mix(mix(hash3(i + vec3(0,0,1)), hash3(i + vec3(1,0,1)), f.x),
	               mix(hash3(i + vec3(0,1,1)), hash3(i + vec3(1,1,1)), f.x), f.y), f.z);
}

void main() {
	size = textureSize(smoothDepth, 0);
	ivec2 p = ivec2(gl_FragCoord.xy);
	float z = texelFetch(smoothDepth, p, 0).r;
	if (z <= 0.0) { o_surface = vec4(0.0); return; }

	vec3 P = viewPos(p, z);
	vec3 sphereN = texelFetch(gNormal, p, 0).xyz;

	// 隣の画素のビュー座標。粒子がない画素は使わない
	float zr = texelFetch(smoothDepth, min(p + ivec2(1, 0), size - 1), 0).r;
	float zl = texelFetch(smoothDepth, max(p - ivec2(1, 0), ivec2(0)), 0).r;
	float zu = texelFetch(smoothDepth, min(p + ivec2(0, 1), size - 1), 0).r;
	float zd = texelFetch(smoothDepth, max(p - ivec2(0, 1), ivec2(0)), 0).r;

	// 隣の画素との深度差として許す上限。1 画素離れた面でありうる差 (傾きを含む) に、
	// 平滑化で残る誤差の分として閾値の半分を加える
	float limit = 0.5 * depthThreshold + (z / pixelsPerMeter) * slopeFactor;
	// 各側の深度差 (粒子がない、または上限を超える側は使わない = 大きな値にする)
	const float BIG = 1e9;
	float dr = (zr > 0.0 && abs(zr - z) <= limit) ? abs(zr - z) : BIG;
	float dl = (zl > 0.0 && abs(z - zl) <= limit) ? abs(z - zl) : BIG;
	float du = (zu > 0.0 && abs(zu - z) <= limit) ? abs(zu - z) : BIG;
	float dd = (zd > 0.0 && abs(z - zd) <= limit) ? abs(z - zd) : BIG;

	vec3 n;
	if (min(dr, dl) < BIG && min(du, dd) < BIG) {
		// 深度の差が小さいほうの差分を使う (輪郭の向こう側の面を拾わないため)
		vec3 dx = (dr <= dl) ? viewPos(p + ivec2(1, 0), zr) - P : P - viewPos(p - ivec2(1, 0), zl);
		vec3 dy = (du <= dd) ? viewPos(p + ivec2(0, 1), zu) - P : P - viewPos(p - ivec2(0, 1), zd);
		n = normalize(cross(dx, dy));
		if (n.z < 0.0) n = -n;	// 視点の側 (+z) を向ける
	} else {
		n = sphereN;			// 段差や孤立した粒子では、平滑化前の球の法線を使う
	}

	// 平滑化前の球の法線を混ぜて、粒の輪郭を少し残す
	n = normalize(mix(n, sphereN, sphereNormalMix));

	// ワールド座標のノイズの勾配で法線を揺らす (粒子と一緒には動かない模様であることに注意)
	if (grainStrength > 0.0) {
		vec3 Pw = (invView * vec4(P, 1.0)).xyz / grainSize;
		const float e = 0.3;
		vec3 g = vec3(valueNoise(Pw + vec3(e, 0, 0)) - valueNoise(Pw - vec3(e, 0, 0)),
		              valueNoise(Pw + vec3(0, e, 0)) - valueNoise(Pw - vec3(0, e, 0)),
		              valueNoise(Pw + vec3(0, 0, e)) - valueNoise(Pw - vec3(0, 0, e))) / (2.0 * e);
		vec3 gView = transpose(mat3(invView)) * g;		// ワールド → ビュー (回転のみ)
		n = normalize(n - grainStrength * (gView - n * dot(gView, n)));
	}

	o_surface = vec4(n, z);
}
