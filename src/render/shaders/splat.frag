#version 430 core

//==============================================================================
// splat.frag : 円盤 (surfel) を半透明のカーネルとして描く (Splat モード P2/P3)
// 目的: 点スプライトの各画素について、視線と円盤の平面の交点を求め、円盤の外なら捨てる。
//       円盤の中では、中心からの距離 u (0〜1) に対して w = 表面らしさ × exp(-β u²) の重みを付ける。
//   pass 0 (可視性): 交点を少し奥 (depthOffset) へずらした深度だけを書く。
//                    これで「手前の表面から depthOffset の厚み」が決まる
//   pass 1 (蓄積)  : 交点の深度で、可視性パスの深度と LEQUAL で比べ (厚みの内側だけが通る)、
//                    重み × 色・法線・深度を加算ブレンドで足し合わせる
//   pass 2 (被覆率): 可視性パスの前に、床・障害物の深度とだけ比べて、視線上のすべての円盤
//                    (裏向き・表面の少し下のものも含む) の重みを足し合わせる。
//                    被覆率 α = 1 - exp(-k Σw) を、手前の層の厚みではなく視線上の粒子の量で決め、
//                    山の中で手前の層の円盤がたまたま少ない所が半透明になる (穴が開く) のを防ぐ
// 色は sphere.frag と同じく、材質の基本色 + 粒子番号による明るさのばらつきで、粒状感を出す。
// 法線は円盤の向き (密度勾配) をそのまま使い、球の法線のような粒子ごとの濃淡は付けない。
//==============================================================================

flat in vec3 v_center;
flat in vec3 v_normal;
flat in float v_weight;
flat in float v_fuzz;
flat in int v_state;
flat in int v_material;
flat in int v_id;
flat in vec3 v_surfelColor;

uniform mat4 projection;
uniform vec2 viewport;			// 描画先の大きさ [px]
uniform float splatRadius;		// 円盤の半径 [m]
uniform float splatSharpness;	// カーネル exp(-β u²) の β
uniform float depthOffset;		// 可視性パスで奥へずらす量 [m]
uniform int pass;				// 0: 可視性, 1: 蓄積, 2: 被覆率
uniform vec3 sandColor;
uniform vec3 stoneColor;
uniform float colorVariation;
uniform bool useDebugColor;
uniform int surfelDebug;

layout(location = 0) out vec4 o_accum0;	// rgb: Σ w·色, a: Σ w
layout(location = 1) out vec4 o_accum1;	// xyz: Σ w·法線 (ビュー空間), w: Σ w·あいまいさ
layout(location = 2) out float o_accum2;	// Σ w·線形深度 (-z_view)
layout(location = 3) out float o_coverage;	// 視線上のすべての円盤の Σ w (pass 2)

const float kVisibilityMinWeight = 0.1;	// 可視性パスで深度を決める最小の重み

// 整数のハッシュ (sphere.frag と同じ)
float hash(uint x) {
	x ^= x >> 16; x *= 0x7feb352du;
	x ^= x >> 15; x *= 0x846ca68bu;
	x ^= x >> 16;
	return float(x) / 4294967295.0;
}

void main() {
	// この画素を通る視線 (ビュー座標、カメラから奥へ)
	vec2 ndc = gl_FragCoord.xy / viewport * 2.0 - 1.0;
	vec3 dir = normalize(vec3(ndc.x / projection[0][0], ndc.y / projection[1][1], -1.0));

	// 視線と円盤の平面の交点
	float dn = dot(dir, v_normal);
	if (abs(dn) < 0.05) discard;	// 円盤をほぼ真横から見ている
	vec3 x = dir * (dot(v_center, v_normal) / dn);
	float u2 = dot(x - v_center, x - v_center) / (splatRadius * splatRadius);
	if (u2 > 1.0) discard;			// 円盤の外

	// 重み: 表面らしさ × ガウスカーネル × 横向きの減衰。
	// 円盤を真横に近い角度から見ると細い棒になり、薄い膜の縁や輪郭で黒い筋として目立つ。
	// 視線との角度が浅いほど重みを下げ、棒にならずに消えていくようにする
	float facing = smoothstep(0.05, 0.35, abs(dn));
	float w = v_weight * exp(-splatSharpness * u2) * facing;

	// 可視性パスでは、重みの小さい部分 (表面らしさの低い粒子や、円盤の縁) で手前の深度を決めない。
	// 決めてしまうと、ほとんど寄与しない円盤が後ろの円盤を締め出し、その円盤の色だけが
	// 縁まで残って、円盤の形がうろこ状に見える
	if (pass == 0 && w < kVisibilityMinWeight) discard;

	// 深度: 可視性パスでは視線の方向へ depthOffset だけ奥にずらす
	vec3 xd = (pass == 0) ? x + dir * depthOffset : x;
	vec4 clip = projection * vec4(xd, 1.0);
	gl_FragDepth = clip.z / clip.w * 0.5 + 0.5;
	if (pass == 0) return;
	if (pass == 2) { o_coverage = w; return; }

	// 色 (線形)
	vec3 albedo;
	if (surfelDebug > 0) {
		albedo = pow(v_surfelColor, vec3(2.2));	// Albedo 表示でこの色がそのまま出るように
	} else if (useDebugColor) {
		if (v_state == 0)      albedo = vec3(1.0, 0.2, 0.2);	// 引張
		else if (v_state == 1) albedo = vec3(0.6, 0.9, 0.1);	// 弾性
		else if (v_state == 2) albedo = vec3(0.2, 0.4, 1.0);	// 塑性
		else                   albedo = vec3(1.0, 0.0, 1.0);
	} else {
		albedo = (v_material == 1) ? stoneColor : sandColor;
		float v1 = hash(uint(v_id) * 2u + 1u) * 2.0 - 1.0;
		float v2 = hash(uint(v_id) * 2u + 2u) * 2.0 - 1.0;
		albedo *= 1.0 + colorVariation * v1;
		albedo *= vec3(1.0 + 0.3 * colorVariation * v2, 1.0, 1.0 - 0.3 * colorVariation * v2);
	}

	o_accum0 = vec4(clamp(albedo, 0.0, 1.0) * w, w);
	o_accum1 = vec4(v_normal * w, v_fuzz * w);
	o_accum2 = -x.z * w;
}
