#version 430 core

//==============================================================================
// wboit_composite.frag : WBOIT の結果を画面に重ねる
// 目的: 蓄積した色の重み付き平均を、不透明度 1 - revealage で陰影付きの画像に重ねる。
//       ブレンドは glBlendFunc(GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA) で、
//         画面 = 平均の色 × (1 - revealage) + 元の画面 × revealage
//       となる。
//==============================================================================

in vec2 v_uv;
out vec4 o_color;

uniform sampler2D accumTex;
uniform sampler2D revealTex;

void main() {
	ivec2 p = ivec2(gl_FragCoord.xy);
	float revealage = texelFetch(revealTex, p, 0).r;
	if (revealage >= 0.999) discard;	// 粉塵がない画素
	vec4 accum = texelFetch(accumTex, p, 0);
	// 万一あふれた画素 (無限大・NaN) は重ねない (黒や白の塊になるのを防ぐ)
	if (any(isinf(accum)) || any(isnan(accum))) discard;
	vec3 average = accum.rgb / max(accum.a, 1e-5);
	o_color = vec4(average, revealage);
}
