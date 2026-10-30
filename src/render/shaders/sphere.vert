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

// Splat モードの surfel (SplatSurface が作る。surfelDebug > 0 か hideInterior のときだけ読む)
struct Surfel {
	vec4 normalSurface;	// xyz: 法線 (ワールド座標、なければ 0), w: 表面らしさ
	vec4 extra;			// x: 密度 [個/セル]
};
layout(std430, binding = 4) readonly buffer SurfelBuffer { Surfel s[]; };

uniform mat4 view;
uniform mat4 projection;
uniform float pointRadius;		// 描画半径 [m]
uniform float viewportHeight;	// 描画先の高さ [px]

uniform int surfelDebug;		// 0: なし, 1: 表面らしさ, 2: 法線, 3: 密度 (RenderSettings::SurfelDebug)
uniform bool hideInterior;		// 表面らしさが surfaceCutoff 未満の粒子を描かない
uniform float surfaceCutoff;
uniform float rhoRef;			// 粒子が隙間なく詰まったときの密度 [個/セル]

flat out vec3 v_centerView;	// 球の中心 (ビュー座標)
flat out int v_state;		// 塑性の状態 (デバッグ色用)
flat out int v_material;	// 材質 (0 = 砂, 1 = 石)
flat out int v_id;			// 粒子番号 (色のばらつき用)
flat out vec3 v_surfelColor;	// surfel のデバッグ色 (表示する色そのもの。surfelDebug > 0 のとき)

void main() {
	vec4 pv = view * vec4(position.xyz, 1.0);
	v_centerView = pv.xyz;
	v_state = state;
	v_material = p[gl_VertexID].padding[0];
	v_id = gl_VertexID;

	gl_Position = projection * pv;

	// --- Splat モード段階 1: 表面らしさ・法線・密度を色にする ---
	v_surfelColor = vec3(0.0);
	if (surfelDebug > 0 || hideInterior) {
		vec4 ns = s[gl_VertexID].normalSurface;
		if (surfelDebug == 1) {
			v_surfelColor = mix(vec3(0.1, 0.3, 1.0), vec3(1.0, 0.15, 0.05), ns.w);
		} else if (surfelDebug == 2) {
			v_surfelColor = (ns.xyz == vec3(0.0)) ? vec3(0.5) : ns.xyz * 0.5 + 0.5;
		} else if (surfelDebug == 3) {
			v_surfelColor = vec3(clamp(s[gl_VertexID].extra.x / rhoRef, 0.0, 1.0));
		}
		// 内部の粒子はクリップ空間の外へ出して捨てる
		if (hideInterior && ns.w < surfaceCutoff) gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
	}
	// 投影後の直径 [px] = 2r · P[1][1] · (H / 2) / w
	// 透視投影でも平行投影 (w = 1) でも同じ式で使える。1 px 未満にはしない
	gl_PointSize = max(pointRadius * projection[1][1] * viewportHeight / gl_Position.w, 1.0);
}
