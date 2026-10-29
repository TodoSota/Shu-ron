#version 430 core

//==============================================================================
// smooth_depth.frag : 粒子の線形深度の平滑化 (1 方向分のバイラテラルフィルタ)
// 目的: 球インポスタの凹凸を画面上でならし、連続した面の深度にする。
//       横方向と縦方向に分けて 2 回描くことで、計算量を抑える。
//   ・平滑化の半径はワールド単位で決め、深度に応じてピクセル数に換算する
//     (遠くの面ほど画面上の半径が小さくなるので、どの距離でも同じ粗さの面になる)
//   ・深度の差が閾値を超える画素は混ぜない (手前の砂山と奥の砂山を混ぜないため)。
//     斜めから見た面では、画面上で離れるほど深度が大きく変わるので、閾値を
//     「depthThreshold + 離れた画素数 × 画素の大きさ × slopeFactor」として、
//     傾いた面でも平滑化が効くようにする (固定の閾値だと縦方向だけ平滑化されず、筋が出る)
//   ・線形深度 0 は「粒子がない画素」を表し、平均に含めない
//==============================================================================

out float o_depth;

uniform sampler2D depthTex;		// 線形深度 (-z_view [m]、0 = 粒子なし)
uniform ivec2 direction;		// (1, 0) = 横、(0, 1) = 縦
uniform float worldRadius;		// 平滑化の半径 [m]
uniform float pixelsPerMeter;	// 深度 1 m で 1 m が何ピクセルか
uniform float depthThreshold;	// [m]

const int kMaxRadius = 24;		// 画面上の半径の上限 [px] (近づいたときの計算量を抑える)
const float slopeFactor = 5.0;	// 許容する面の傾き (視線に対して約 80° まで)

void main() {
	ivec2 size = textureSize(depthTex, 0);
	ivec2 p = ivec2(gl_FragCoord.xy);
	float z = texelFetch(depthTex, p, 0).r;
	if (z <= 0.0) { o_depth = 0.0; return; }

	float radiusPx = clamp(worldRadius * pixelsPerMeter / z, 1.0, float(kMaxRadius));
	int r = int(ceil(radiusPx));
	float sigma = 0.5 * radiusPx;
	float pixelSize = z / pixelsPerMeter;	// この深度で 1 画素が覆う大きさ [m]

	float sum = 0.0, wsum = 0.0;
	for (int i = -r; i <= r; ++i) {
		ivec2 q = clamp(p + direction * i, ivec2(0), size - 1);
		float zi = texelFetch(depthTex, q, 0).r;
		if (zi <= 0.0) continue;					// 粒子がない画素
		float dz = zi - z;
		float threshold = depthThreshold + float(abs(i)) * pixelSize * slopeFactor;
		if (abs(dz) > threshold) continue;		// 別の面
		float sigmaZ = 0.5 * threshold;
		float w = exp(-0.5 * float(i * i) / (sigma * sigma)) * exp(-0.5 * dz * dz / (sigmaZ * sigmaZ));
		sum += zi * w;
		wsum += w;
	}
	o_depth = sum / wsum;	// 中心の画素は必ず含まれるので wsum > 0
}
