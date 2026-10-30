#version 430 core

//==============================================================================
// lighting.frag : G-buffer から陰影を計算する全画面パス (deferred shading)
// 目的: 各画素について、
//         太陽光 (拡散反射) × 影  +  空と地面からの環境光 × AO
//       を計算し、トーンマッピングとガンマ補正をして画面に出力する。
//       乾いた砂は粗い面なので、鏡面反射は入れていない。
//       また、G-buffer の深度を画面の深度バッファに書き戻し、この後に描く
//       枠線などが砂や障害物に正しく隠れるようにする。
//==============================================================================

in vec2 v_uv;
out vec4 o_color;

uniform sampler2D gAlbedo;		// rgb: 色, a: 種別
uniform sampler2D gNormal;		// ビュー空間の法線
uniform sampler2D gDepth;		// 深度
uniform sampler2D aoTex;		// SSAO (1 = 遮蔽なし)
uniform sampler2DShadow shadowTex;	// シャドウマップ (比較サンプラ)

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
uniform int debugView;			// 0: 通常, 1: albedo, 2: 法線, 3: AO, 4: 影

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

void main() {
	float depth = texture(gDepth, v_uv).r;
	vec4 albedoKind = texture(gAlbedo, v_uv);

	// 視線の方向 (ワールド座標)。背景の空のグラデーションに使う
	vec4 farView = invProjection * vec4(v_uv * 2.0 - 1.0, 1.0, 1.0);
	vec3 rayWorld = normalize(mat3(invView) * (farView.xyz / farView.w));

	// --- 背景 (何も描かれていない画素) ---
	if (depth >= 1.0) {
		vec3 sky = mix(skyColor * 0.9 + 0.1, skyColor * 0.6, clamp(rayWorld.y * 1.5, 0.0, 1.0));
		o_color = vec4(toDisplay(sky * ambientIntensity * 1.6), 1.0);
		gl_FragDepth = 1.0;
		return;
	}

	// --- 位置と法線の復元 ---
	vec4 vp = invProjection * vec4(v_uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
	vec3 viewPos = vp.xyz / vp.w;
	vec3 worldPos = (invView * vec4(viewPos, 1.0)).xyz;
	vec3 worldNormal = normalize(mat3(invView) * texture(gNormal, v_uv).xyz);

	// --- 太陽光 (ランバート) と影 ---
	float ndl = max(dot(worldNormal, sunDirection), 0.0);
	// 平らにした粒子の面 (Surface / Splat モード) は、シャドウマップに描いた球より低い位置にあるので、
	// 粒子の画素 (種別 0.25) だけ参照位置を余分にずらす。床・障害物はずらさず、接地した影を残す
	bool isParticle = abs(albedoKind.a - 0.25) < 0.1;
	float normalOffset = shadowNormalOffset + (isParticle ? particleShadowOffset : 0.0);
	float shadow = (useShadow && ndl > 0.0) ? shadowFactor(worldPos, worldNormal, normalOffset) : 1.0;

	// --- 環境光: 上を向いた面は空の色、下を向いた面は地面の照り返しの色 ---
	float ao = useAO ? texture(aoTex, v_uv).r : 1.0;
	vec3 ambient = mix(groundColor, skyColor, worldNormal.y * 0.5 + 0.5) * ambientIntensity;

	vec3 albedo = albedoKind.rgb;
	vec3 radiance = albedo * (sunColor * sunIntensity * ndl * shadow + ambient * ao);

	// --- 出力 (デバッグ表示の切り替えを含む) ---
	vec3 outColor;
	if (debugView == 1)      outColor = pow(albedo, vec3(1.0 / 2.2));
	else if (debugView == 2) outColor = worldNormal * 0.5 + 0.5;
	else if (debugView == 3) outColor = vec3(ao);
	else if (debugView == 4) outColor = vec3(shadow * step(0.0, ndl));
	else                     outColor = toDisplay(radiance);

	o_color = vec4(outColor, 1.0);
	gl_FragDepth = depth;	// 画面の深度バッファへ書き戻す (枠線などの前後関係のため)
}
