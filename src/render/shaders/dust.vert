#version 430 core

//==============================================================================
// dust.vert : 粉塵を点スプライトとして描く頂点シェーダー
// 目的: 粉塵のバッファを gl_VertexID で直接読み、
//         ・大きさ: 時間とともに広がる (拡散のように見せる)
//         ・濃さ  : 出始めにふわっと現れ、寿命の終わりに向かって薄くなる
//         ・明るさ: 太陽光 (シャドウマップで影を判定) + 空の環境光
//       を計算する。明るさは粉塵 1 個につき 1 回、頂点シェーダーで求めている。
//       空き (寿命 0) の粉塵は画面外へ出して描かない。
//==============================================================================

struct Dust { vec4 posAge; vec4 velLife; };
layout(std430, binding = 2) readonly buffer DustBuffer { Dust dust[]; };

uniform mat4 view;
uniform mat4 projection;
uniform mat4 lightViewProjection;
uniform float viewportHeight;
uniform float baseSize;		// 発生時の半径 [m]
uniform float growth;		// 1 秒あたりの広がり (倍)
uniform float opacity;
uniform vec3 sunDirection;
uniform vec3 sunColor;
uniform float sunIntensity;
uniform vec3 skyColor;
uniform float ambientIntensity;
uniform bool useShadow;
uniform float shadowStrength;	// 影の効き具合 (0 = 影なし, 1 = 影の中は空の光だけ)
uniform sampler2DShadow shadowTex;

flat out float v_alpha;		// 濃さ
flat out float v_viewDepth;	// 粉塵の中心の線形深度 [m]
flat out vec3 v_light;		// 当たっている光の量

void main() {
	Dust d = dust[gl_VertexID];
	float age = d.posAge.w, life = d.velLife.w;
	if (life <= 0.0) {
		gl_Position = vec4(2.0, 2.0, 2.0, 1.0);	// 画面外 (クリップされる)
		gl_PointSize = 1.0;
		v_alpha = 0.0; v_viewDepth = 0.0; v_light = vec3(0.0);
		return;
	}

	float radius = baseSize * (1.0 + growth * age);
	float u = age / life;
	v_alpha = opacity * smoothstep(0.0, 0.15, u) * pow(1.0 - u, 1.5);

	vec4 pv = view * vec4(d.posAge.xyz, 1.0);
	v_viewDepth = -pv.z;
	gl_Position = projection * pv;
	gl_PointSize = clamp(radius * projection[1][1] * viewportHeight / gl_Position.w, 1.0, 128.0);

	// 影: 粉塵の中心が太陽から見えるか
	float lit = 1.0;
	if (useShadow) {
		vec4 ls = lightViewProjection * vec4(d.posAge.xyz, 1.0);
		vec3 sc = ls.xyz / ls.w * 0.5 + 0.5;
		if (sc.z <= 1.0) lit = texture(shadowTex, vec3(sc.xy, sc.z - 0.002));
	}
	// 細かい粒は光をあらゆる方向へ散乱するので、向きによらない明るさとする
	// 粉塵は半透明で、中心の 1 点だけで影を判定すると影の内外で明るさが急に変わり、
	// 影の中の粉塵が灰色の斑点として目立つ。影の効きを shadowStrength で弱める
	lit = mix(1.0, lit, shadowStrength);
	v_light = sunColor * sunIntensity * 0.6 * lit + skyColor * ambientIntensity;
}
