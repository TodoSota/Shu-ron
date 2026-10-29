#version 430 core

//==============================================================================
// ssao.frag : スクリーン空間アンビエントオクルージョン (半解像度)
// 目的: 各画素の表面の上の半球に置いたサンプル点が、画面上の深度で見て
//       他の面の後ろに隠れているかを数え、周囲がふさがれている割合を求める。
//       粒子のすき間・接地面・砂山のくぼみが暗くなる。
// 方法: Crytek 方式の SSAO を、法線方向の半球に限定したもの。
//       サンプルの向きを 4×4 のノイズで回して縞を目立たなくし、後でぼかす。
//==============================================================================

in vec2 v_uv;
out float o_ao;

uniform sampler2D gDepth;	// G-buffer の深度
uniform sampler2D gNormal;	// G-buffer のビュー空間の法線
uniform sampler2D noiseTex;	// 4×4 の回転用ノイズ

uniform vec3 kernel[16];	// 半球内のサンプル (接空間)
uniform mat4 projection;
uniform mat4 invProjection;
uniform vec2 noiseScale;	// 画面の大きさ / 4
uniform float radius;		// サンプル半径 [m]
uniform float bias;			// 自己遮蔽を防ぐバイアス [m]
uniform float power;		// AO の効き具合

// 深度バッファの値からビュー座標を復元する
vec3 viewPosition(vec2 uv) {
	float d = texture(gDepth, uv).r;
	vec4 v = invProjection * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
	return v.xyz / v.w;
}

void main() {
	float d = texture(gDepth, v_uv).r;
	if (d >= 1.0) { o_ao = 1.0; return; }	// 背景

	vec3 P = viewPosition(v_uv);
	vec3 N = normalize(texture(gNormal, v_uv).xyz);

	// 法線を z 軸とする接空間 (ノイズで接平面内の向きをランダムに回す)
	vec3 rv = normalize(vec3(texture(noiseTex, v_uv * noiseScale).xy, 0.0));
	vec3 T = normalize(rv - N * dot(rv, N));
	vec3 B = cross(N, T);
	mat3 TBN = mat3(T, B, N);

	// 画素 1 個がこの深度で覆う大きさ [m]。斜めから見た面では隣の画素との深度差が
	// これに比例して大きくなるので、バイアスに加えて自己遮蔽を防ぐ
	float pixelSize = 2.0 * (-P.z) / (projection[1][1] * float(textureSize(gDepth, 0).y));
	float b = bias + 2.0 * pixelSize;

	float occlusion = 0.0;
	for (int i = 0; i < 16; ++i) {
		// サンプル点 (ビュー座標) を画面へ投影し、その画素に見えている面の深度と比べる
		vec3 s = P + TBN * kernel[i] * radius;
		vec4 o = projection * vec4(s, 1.0);
		vec2 suv = o.xy / o.w * 0.5 + 0.5;
		float sceneZ = viewPosition(suv).z;
		// 遠く離れた面 (別の物体) による遮蔽は弱める
		float rangeCheck = smoothstep(0.0, 1.0, radius / max(abs(P.z - sceneZ), 1e-5));
		occlusion += ((sceneZ >= s.z + b) ? 1.0 : 0.0) * rangeCheck;
	}
	o_ao = pow(clamp(1.0 - occlusion / 16.0, 0.0, 1.0), power);
}
