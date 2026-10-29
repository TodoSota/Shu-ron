#pragma once

//==============================================================================
// RenderSettings.h : 描画の設定 (UI から変更するパラメータ)
// 目的: 描画モードと、陰影・影・AO の調整値を 1 か所にまとめる。
//       値の単位は、長さが m、角度が度、明るさはトーンマッピング前の相対値。
//       初期値は docs/rendering_guide.md の §8 に対応する。
//==============================================================================

#include <GLM/glm.hpp>
#include <cmath>

/// 粒子の描画方法
enum class RenderMode : int {
	Points = 0,		// 従来の点描画 (陰影なし。state のデバッグ色の確認用)
	Spheres = 1,	// 球インポスタ + 陰影 + 影 + AO (段階 1)
	Surface = 2,	// 粒子の深度を平滑化して連続した面にする (段階 2)。陰影・影・AO は段階 1 と共通
};

struct RenderSettings {
	RenderMode mode{ RenderMode::Spheres };

	// --- 粒子の大きさ ---
	float particleSpacing{ 0.014f };	// 粒子 1 個が占める立方体の一辺 (V_init / N)^(1/3) [m]。main.cpp が設定する
	float radiusScale{ 1.3f };			// 描画半径 = radiusScale × particleSpacing / 2

	// --- 色 (線形色空間) ---
	glm::vec3 sandColor{ 0.40f, 0.27f, 0.15f };
	glm::vec3 stoneColor{ 0.25f, 0.23f, 0.21f };
	glm::vec3 floorColor{ 0.16f, 0.13f, 0.10f };
	glm::vec3 obstacleColor{ 0.55f, 0.62f, 0.70f };
	float colorVariation{ 0.12f };		// 粒子ごとの明るさのばらつき (±)

	// --- 光源 ---
	float sunAzimuth{ 35.0f };			// 太陽の方位角 [度] (+x から +z へ)
	float sunElevation{ 50.0f };		// 太陽の仰角 [度]
	float sunIntensity{ 2.2f };
	glm::vec3 sunColor{ 1.0f, 0.95f, 0.85f };
	float ambientIntensity{ 0.5f };
	glm::vec3 skyColor{ 0.62f, 0.72f, 0.88f };
	glm::vec3 groundColor{ 0.35f, 0.30f, 0.25f };
	float exposure{ 0.85f };

	// --- 影 ---
	bool shadows{ true };
	int shadowResolution{ 2048 };
	float shadowBias{ 0.0015f };		// 深度の比較に加えるバイアス (シャドウマップの深度 [0,1] 単位)
	float shadowNormalOffset{ 0.004f };	// 法線方向へずらして参照する量 [m] (自己影の縞を防ぐ)

	// --- SSAO ---
	bool ssao{ true };
	float ssaoRadiusScale{ 4.0f };		// AO のサンプル半径 = ssaoRadiusScale × 描画半径
	float ssaoBias{ 0.002f };			// [m]
	float ssaoPower{ 2.0f };			// AO の効き具合 (大きいほど暗くなる)

	// --- スクリーン空間で面を張る (Surface モード) ---
	float surfaceRadiusBoost{ 1.4f };	// Surface モードでは球をこの倍率で大きく描き、粒子のすき間を埋める
	float smoothRadiusScale{ 3.0f };	// 平滑化の半径 = smoothRadiusScale × 描画半径 (ワールド単位)
	int smoothIterations{ 2 };			// 平滑化の回数 (横 + 縦で 1 回)
	float smoothDepthScale{ 2.0f };		// これ以上深度が違う画素は混ぜない = smoothDepthScale × 描画半径
	float sphereNormalMix{ 0.15f };		// 平滑化前の球の法線を混ぜる割合 (粒状感を残す)
	float grainStrength{ 0.3f };		// ノイズによる細かい凹凸の強さ
	float grainScale{ 1.0f };			// 凹凸の大きさ = grainScale × 描画半径

	// --- デバッグ表示 ---
	int debugView{ 0 };					// 0: 通常, 1: albedo, 2: 法線, 3: AO, 4: 影

	/// 描画半径 [m]
	float particleRadius() const { return 0.5f * radiusScale * particleSpacing; }

	/// 太陽の方向 (地表から太陽へ向かう単位ベクトル、ワールド座標)
	glm::vec3 sunDirection() const {
		const float az = glm::radians(sunAzimuth), el = glm::radians(sunElevation);
		return glm::normalize(glm::vec3(cos(el) * cos(az), sin(el), cos(el) * sin(az)));
	}
};
