#version 430 core

//==============================================================================
// lighting.frag : G-buffer から陰影を計算する全画面パス (deferred shading)
// 目的: 各画素について、
//         太陽光 (拡散反射) × 影  +  空と地面からの環境光 × AO
//       を計算し、トーンマッピングとガンマ補正をして画面に出力する。
//       乾いた砂は粗い面なので、鏡面反射は入れていない。
//       また、G-buffer の深度を画面の深度バッファに書き戻し、この後に描く
//       枠線などが砂や障害物に正しく隠れるようにする。
// Splat モード (段階 3):
//       - 被覆率 α が小さい輪郭やまばらな部分は、G-buffer ではなく splat 層に入っている。
//         G-buffer の画素 (背景・床・障害物) の色の上に、splat 層の色を α で重ねる
//       - 粒子の拡散反射を wrapped Lambert にして、明暗の境目を柔らかくする。
//         まばらな所 (あいまいさ fuzz が大きい所) ほど柔らかくする
//       - 砂の影を、密度格子を太陽の方向にレイマーチした透過率 T = exp(-Σ τ) で求める (段階 4)。
//         シャドウマップには障害物だけを描き、両方を掛け合わせる。粒子の球による影の斑点がなくなり、
//         薄い所は光が透け、表面のすぐ下は少し暗くなる
//==============================================================================

in vec2 v_uv;
out vec4 o_color;

uniform sampler2D gAlbedo;		// rgb: 色, a: 種別
uniform sampler2D gNormal;		// xyz: ビュー空間の法線, w: あいまいさ (Splat モードの粒子)
uniform sampler2D gDepth;		// 深度
uniform sampler2D aoTex;		// SSAO (1 = 遮蔽なし)
uniform sampler2DShadow shadowTex;	// シャドウマップ (比較サンプラ)
uniform sampler2D splatAlbedo;	// splat 層 rgb: 色, a: 被覆率 α
uniform sampler2D splatNormal;	// splat 層 xyz: ビュー空間の法線, w: あいまいさ
uniform sampler2D splatDepth;	// splat 層 線形深度 -z_view [m] (0 = なし)

uniform mat4 invProjection;
uniform mat4 invView;
uniform mat4 lightViewProjection;

uniform vec3 sunDirection;		// 地表から太陽へ (ワールド座標)
uniform vec3 sunColor;
uniform float sunIntensity;
uniform vec3 skyColor;
uniform vec3 groundColor;
uniform float ambientIntensity;
uniform float exposure;

uniform bool useShadow;
uniform bool useAO;
uniform float shadowBias;
uniform float shadowNormalOffset;
uniform float particleShadowOffset;	// 粒子の画素だけに追加するずらし量 [m] (Surface / Splat モード)
uniform bool useSplatLayer;		// splat 層を重ねる (Splat モード)
uniform float opaqueAlpha;		// これ以上の被覆率の画素は G-buffer に入っている (splat 層は重ねない)
uniform float particleWrap;		// 粒子の wrapped Lambert の基本の a (Splat モード以外は 0)
uniform float fuzzWrap;			// あいまいさに応じて a に足す量
uniform bool useDensityShadow;	// 密度格子による砂の影 (Splat モード)
uniform sampler3D densityTex;	// 1 セルあたりの粒子数 (ぼかし済み、格子の外は 0)
uniform float gridExtent;		// 格子の一辺の長さ [m]
uniform float cellSize;			// 格子のセルの一辺 h [m]
uniform float tauPerDensity;	// 1 セル進むごとの光学的厚さ = 密度 × tauPerDensity
uniform int debugView;			// 0: 通常, 1: albedo, 2: 法線, 3: AO, 4: 影, 5: 被覆率

// ACES のトーンマッピングの近似 (Narkowicz 2015)
vec3 toneMap(vec3 x) {
	return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}
vec3 toDisplay(vec3 linear) {
	return pow(toneMap(linear * exposure), vec3(1.0 / 2.2));
}

// 影の係数 (1 = 日なた、0 = 影)。3×3 の PCF で縁をなめらかにする
float shadowFactor(vec3 worldPos, vec3 worldNormal, float normalOffset) {
	// 法線方向に少しずらした点で参照し、自己影の縞 (シャドウアクネ) を防ぐ
	vec4 ls = lightViewProjection * vec4(worldPos + worldNormal * normalOffset, 1.0);
	vec3 sc = ls.xyz / ls.w * 0.5 + 0.5;
	if (sc.z > 1.0) return 1.0;
	vec2 texel = 1.0 / vec2(textureSize(shadowTex, 0));
	float lit = 0.0;
	for (int y = -1; y <= 1; ++y)
		for (int x = -1; x <= 1; ++x)
			lit += texture(shadowTex, vec3(sc.xy + vec2(x, y) * texel, sc.z - shadowBias));
	return lit / 9.0;
}

// 密度格子を太陽の方向にレイマーチして、砂を通り抜ける光の透過率を求める (1 = 遮るものなし)
//   出発点は、自分自身の密度を拾わないように法線方向へ 1 セル外へ出した点。
//   1 セルずつ進み、格子の外に出るか、ほぼ真っ暗になったら打ち切る
float densityTransmittance(vec3 worldPos, vec3 worldNormal) {
	vec3 x = worldPos + worldNormal * cellSize;
	float tau = 0.0;
	for (int i = 0; i < 160; ++i) {
		x += sunDirection * cellSize;
		vec3 uvw = x / gridExtent;
		if (any(lessThan(uvw, vec3(0.0))) || any(greaterThan(uvw, vec3(1.0)))) break;
		tau += texture(densityTex, uvw).r * tauPerDensity;
		if (tau > 8.0) break;
	}
	return exp(-tau);
}

// 1 つの面の陰影 (トーンマッピング前の放射輝度) と、デバッグ表示用の値
struct Shaded {
	vec3 radiance;
	vec3 debugColor;	// debugView 1〜4 の色 (表示用の値)
};

Shaded shade(vec3 albedo, vec3 viewPos, vec3 viewNormal, bool isParticle, float fuzz, float ao) {
	vec3 worldPos = (invView * vec4(viewPos, 1.0)).xyz;
	vec3 worldNormal = normalize(mat3(invView) * viewNormal);

	// --- 太陽光と影 ---
	//   粒子は wrapped Lambert: (n·l + a) / (1 + a)。a を大きくすると明暗の境目が広がり、
	//   陰側にも光が回り込む (粒の集まりの中での散乱を大まかに表す)
	float nl = dot(worldNormal, sunDirection);
	float wrap = isParticle ? particleWrap + fuzzWrap * fuzz : 0.0;
	float ndl = clamp((nl + wrap) / (1.0 + wrap), 0.0, 1.0);
	// 平らにした粒子の面 (Surface / Splat モード) は、シャドウマップに描いた球より低い位置にあるので、
	// 粒子の画素だけ参照位置を余分にずらす。床・障害物はずらさず、接地した影を残す
	float normalOffset = shadowNormalOffset + (isParticle ? particleShadowOffset : 0.0);
	float shadow = (useShadow && ndl > 0.0) ? shadowFactor(worldPos, worldNormal, normalOffset) : 1.0;
	if (useShadow && useDensityShadow && ndl > 0.0) shadow *= densityTransmittance(worldPos, worldNormal);

	// --- 環境光: 上を向いた面は空の色、下を向いた面は地面の照り返しの色 ---
	vec3 ambient = mix(groundColor, skyColor, worldNormal.y * 0.5 + 0.5) * ambientIntensity;

	Shaded s;
	s.radiance = albedo * (sunColor * sunIntensity * ndl * shadow + ambient * ao);
	if (debugView == 1)      s.debugColor = pow(albedo, vec3(1.0 / 2.2));
	else if (debugView == 2) s.debugColor = worldNormal * 0.5 + 0.5;
	else if (debugView == 3) s.debugColor = vec3(ao);
	else                     s.debugColor = vec3(shadow * step(0.0, ndl));
	return s;
}

// 画面上の位置と線形深度 (-z_view) から、ビュー座標を求める
vec3 viewPosFromLinear(vec2 uv, float linearDepth) {
	vec4 farView = invProjection * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
	vec3 ray = farView.xyz / farView.w;
	return ray * (linearDepth / -ray.z);
}

void main() {
	float depth = texture(gDepth, v_uv).r;
	vec4 albedoKind = texture(gAlbedo, v_uv);
	float ao = useAO ? texture(aoTex, v_uv).r : 1.0;

	// --- G-buffer の画素 (背景、または床・障害物・不透明な粒子) ---
	vec3 baseRadiance;
	vec3 baseDebug;
	float baseLinearDepth;
	if (depth >= 1.0) {
		// 背景: 視線の方向による空のグラデーション
		vec4 farView = invProjection * vec4(v_uv * 2.0 - 1.0, 1.0, 1.0);
		vec3 rayWorld = normalize(mat3(invView) * (farView.xyz / farView.w));
		vec3 sky = mix(skyColor * 0.9 + 0.1, skyColor * 0.6, clamp(rayWorld.y * 1.5, 0.0, 1.0));
		baseRadiance = sky * ambientIntensity * 1.6;
		baseDebug = toDisplay(baseRadiance);
		baseLinearDepth = 1e30;
	} else {
		vec4 vp = invProjection * vec4(v_uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
		vec3 viewPos = vp.xyz / vp.w;
		vec4 nf = texture(gNormal, v_uv);
		bool isParticle = abs(albedoKind.a - 0.25) < 0.1;
		Shaded s = shade(albedoKind.rgb, viewPos, nf.xyz, isParticle, isParticle ? nf.w : 0.0, ao);
		baseRadiance = s.radiance;
		baseDebug = s.debugColor;
		baseLinearDepth = -viewPos.z;
	}
	float coverage = (abs(albedoKind.a - 0.25) < 0.1) ? 1.0 : 0.0;	// 被覆率の表示用

	// --- splat 層 (半透明の輪郭・まばらな部分) を α で重ねる ---
	vec3 radiance = baseRadiance;
	vec3 debugColor = baseDebug;
	if (useSplatLayer) {
		vec4 sa = texture(splatAlbedo, v_uv);
		float zs = texture(splatDepth, v_uv).r;
		if (sa.a > 0.0 && sa.a < opaqueAlpha && zs > 0.0 && zs < baseLinearDepth) {
			vec4 sn = texture(splatNormal, v_uv);
			Shaded s = shade(sa.rgb, viewPosFromLinear(v_uv, zs), sn.xyz, true, sn.w, ao);
			radiance = mix(baseRadiance, s.radiance, sa.a);
			debugColor = mix(baseDebug, s.debugColor, sa.a);
			coverage = sa.a;
		}
	}

	// --- 出力 (デバッグ表示の切り替えを含む) ---
	vec3 outColor;
	if (debugView >= 1 && debugView <= 4) outColor = debugColor;
	else if (debugView == 5)              outColor = vec3(coverage);
	else                                  outColor = toDisplay(radiance);

	o_color = vec4(outColor, 1.0);
	gl_FragDepth = depth;	// 画面の深度バッファへ書き戻す (枠線などの前後関係のため)
}
