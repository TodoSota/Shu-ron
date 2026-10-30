#version 430 core

//==============================================================================
// splat.vert : 表面粒子を円盤 (surfel) として描く頂点シェーダー (Splat モード P2/P3)
// 目的: 粒子 1 個を点スプライト 1 個として描く。円盤の中心 = 粒子の位置、向き = 法線
//       (surfel_prepare.comp の密度勾配)、半径 = splatRadius。円盤の形はフラグメントシェーダーで作る。
//       点の大きさは sphere.vert と同じ式で、半径 splatRadius の球を覆う大きさにする
//       (円盤はその球の内側に入るので、どの向きでもはみ出さない)。
//       描かない粒子 (内部、または裏向きの円盤) は、クリップ空間の外へ出して捨てる。
//==============================================================================

layout(location = 0) in vec4 position;
layout(location = 1) in int state;

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

struct Surfel {
	vec4 normalSurface;	// xyz: 法線 (ワールド座標、なければ 0), w: 表面らしさ
	vec4 extra;			// x: 密度 [個/セル]
};
layout(std430, binding = 4) readonly buffer SurfelBuffer { Surfel s[]; };

uniform mat4 view;
uniform mat4 projection;
uniform float splatRadius;		// 円盤の半径 [m]
uniform float viewportHeight;	// 描画先の高さ [px]
uniform float surfaceCutoff;	// 表面らしさがこれ未満の粒子は描かない
uniform float rhoRef;			// 粒子が隙間なく詰まったときの密度 [個/セル]
uniform int surfelDebug;		// 0: なし, 1: 表面らしさ, 2: 法線, 3: 密度 (RenderSettings::SurfelDebug)

flat out vec3 v_center;		// 円盤の中心 (ビュー座標)
flat out vec3 v_normal;		// 円盤の法線 (ビュー座標、カメラ側を向く)
flat out float v_weight;	// 表面らしさ (カーネルの重みに掛ける)
flat out float v_fuzz;		// あいまいさ = 1 - 密度 / ρ_ref (段階 3 で陰影に使う)
flat out int v_state;		// 塑性の状態 (デバッグ色用)
flat out int v_material;	// 材質 (0 = 砂, 1 = 石)
flat out int v_id;			// 粒子番号 (色のばらつき用)
flat out vec3 v_surfelColor;	// surfel のデバッグ色 (surfelDebug > 0 のとき)

void main() {
	Surfel sf = s[gl_VertexID];
	vec4 pv = view * vec4(position.xyz, 1.0);

	// 法線がない粒子 (孤立した粒) は、カメラを向いた円盤にする
	bool hasNormal = sf.normalSurface.xyz != vec3(0.0);
	vec3 n = hasNormal ? normalize(mat3(view) * sf.normalSurface.xyz) : normalize(-pv.xyz);

	v_center = pv.xyz;
	v_normal = n;
	v_weight = sf.normalSurface.w;
	v_fuzz = 1.0 - clamp(sf.extra.x / rhoRef, 0.0, 1.0);
	v_state = state;
	v_material = p[gl_VertexID].padding[0];
	v_id = gl_VertexID;

	v_surfelColor = vec3(0.0);
	if (surfelDebug == 1)      v_surfelColor = mix(vec3(0.1, 0.3, 1.0), vec3(1.0, 0.15, 0.05), sf.normalSurface.w);
	else if (surfelDebug == 2) v_surfelColor = hasNormal ? sf.normalSurface.xyz * 0.5 + 0.5 : vec3(0.5);
	else if (surfelDebug == 3) v_surfelColor = vec3(1.0 - v_fuzz);

	// 内部の粒子と、裏向きの円盤 (山の裏側の表面) は描かない
	bool skip = sf.normalSurface.w < surfaceCutoff || dot(n, -pv.xyz) < 0.0;
	gl_Position = skip ? vec4(2.0, 2.0, 2.0, 1.0) : projection * pv;
	// 投影後の直径 [px] = 2R · P[1][1] · (H / 2) / w (sphere.vert と同じ)
	gl_PointSize = skip ? 1.0 : max(splatRadius * projection[1][1] * viewportHeight / gl_Position.w, 1.0);
}
