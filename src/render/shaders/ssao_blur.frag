#version 430 core

//==============================================================================
// ssao_blur.frag : SSAO のぼかし
// 目的: 4×4 のノイズによる縞を、4×4 の平均で消す。深度が大きく違う画素
//       (手前の物体と奥の物体) は混ぜず、物体の輪郭で AO がにじまないようにする。
//==============================================================================

in vec2 v_uv;
out float o_ao;

uniform sampler2D aoTex;
uniform sampler2D gDepth;
uniform mat4 invProjection;
uniform float depthThreshold;	// これ以上深度が違う画素は混ぜない [m]

float viewZ(vec2 uv) {
	float d = texture(gDepth, uv).r;
	vec4 v = invProjection * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
	return v.z / v.w;
}

void main() {
	vec2 texel = 1.0 / vec2(textureSize(aoTex, 0));
	float z0 = viewZ(v_uv);
	float sum = 0.0, wsum = 0.0;
	for (int y = -2; y < 2; ++y) {
		for (int x = -2; x < 2; ++x) {
			vec2 uv = v_uv + (vec2(x, y) + 0.5) * texel;
			float w = (abs(viewZ(uv) - z0) < depthThreshold) ? 1.0 : 0.0;
			sum += texture(aoTex, uv).r * w;
			wsum += w;
		}
	}
	o_ao = (wsum > 0.0) ? sum / wsum : texture(aoTex, v_uv).r;
}
