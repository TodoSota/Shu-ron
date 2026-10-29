#version 430 core

//==============================================================================
// sphere.frag : 点スプライトの中に球を描くフラグメントシェーダー (球インポスタ)
// 目的: 正方形の点スプライトのうち円の内側だけを残し、球面の法線と正しい深度を書く。
//       深度を球面に合わせることで、粒子どうしや障害物との前後関係が正しくなる。
//       G-buffer への出力: location 0 = albedo (rgb) と種別 (a)、location 1 = ビュー空間の法線。
//       シャドウマップのパス (カラー出力なし) では、深度だけが使われる。
//==============================================================================

flat in vec3 v_centerView;
flat in int v_state;
flat in int v_material;
flat in int v_id;

uniform mat4 projection;
uniform float pointRadius;
uniform vec3 sandColor;
uniform vec3 stoneColor;
uniform float colorVariation;
uniform bool useDebugColor;

layout(location = 0) out vec4 o_albedo;	// rgb: 色 (線形), a: 種別 (0.25 = 粒子)
layout(location = 1) out vec4 o_normal;	// xyz: ビュー空間の法線

// 整数のハッシュ (粒子番号から再現性のある乱数を作る)
float hash(uint x) {
	x ^= x >> 16; x *= 0x7feb352du;
	x ^= x >> 15; x *= 0x846ca68bu;
	x ^= x >> 16;
	return float(x) / 4294967295.0;
}

void main() {
	// 点スプライト内の座標を [-1, 1]² にする (gl_PointCoord は左上が原点なので y を反転)
	vec2 c = gl_PointCoord * 2.0 - 1.0;
	c.y = -c.y;
	float r2 = dot(c, c);
	if (r2 > 1.0) discard;	// 円の外は捨てる

	// 球面上の点の法線と位置 (ビュー座標)。点スプライトは画面に平行なので、
	// 画面の端では輪郭がわずかに楕円になるが、視野角 60° では目立たない
	vec3 n = vec3(c, sqrt(1.0 - r2));
	vec3 pos = v_centerView + n * pointRadius;

	// 球面の正しい深度を書く
	vec4 clip = projection * vec4(pos, 1.0);
	gl_FragDepth = clip.z / clip.w * 0.5 + 0.5;

	// 色: 材質の基本色に、粒子ごとの明るさと色合いのばらつきを加える
	vec3 albedo;
	if (useDebugColor) {
		// 塑性の状態で色分け (従来の point.frag と同じ配色)
		if (v_state == 0)      albedo = vec3(1.0, 0.2, 0.2);	// 引張
		else if (v_state == 1) albedo = vec3(0.6, 0.9, 0.1);	// 弾性
		else if (v_state == 2) albedo = vec3(0.2, 0.4, 1.0);	// 塑性
		else                   albedo = vec3(1.0, 0.0, 1.0);
	} else {
		albedo = (v_material == 1) ? stoneColor : sandColor;
		float v1 = hash(uint(v_id) * 2u + 1u) * 2.0 - 1.0;
		float v2 = hash(uint(v_id) * 2u + 2u) * 2.0 - 1.0;
		albedo *= 1.0 + colorVariation * v1;					// 明るさ
		albedo *= vec3(1.0 + 0.3 * colorVariation * v2, 1.0, 1.0 - 0.3 * colorVariation * v2);	// わずかな色合い
	}

	o_albedo = vec4(clamp(albedo, 0.0, 1.0), 0.25);
	o_normal = vec4(n, 0.0);
}
