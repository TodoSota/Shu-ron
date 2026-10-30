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
	Splat = 3,		// 密度格子で表面粒子と法線を求め、半透明のカーネルで描く (docs/splat_guide.md)。
					// 現在は段階 1: 表面らしさ・法線・密度を球の色で表示する
};

/// Splat モードで球の色に使う値 (段階 1 のデバッグ表示)
enum class SurfelDebug : int {
	Off = 0,		// 通常の色
	Surface = 1,	// 表面らしさ (青 = 内部 → 赤 = 表面)
	Normal = 2,		// 法線 (ワールド座標、n × 0.5 + 0.5。法線なしは灰色)
	Density = 3,	// 密度 / ρ_ref (黒 = 0 → 白 = 1 以上)
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

	// --- 格子による表面粒子の抽出 (Splat モード。docs/splat_guide.md §8) ---
	float densityCellScale{ 1.5f };		// 密度格子のセルの一辺 = densityCellScale × particleSpacing
	float densityBlurSigma{ 1.0f };		// 密度のぼかしの標準偏差 [セル]。法線の滑らかさを決める (0 でぼかさない)
	float outsideOffsetScale{ 1.0f };	// 表面判定で法線方向に見る距離 = outsideOffsetScale × セルの一辺
	float surfaceLo{ 0.25f };			// 外の密度がこれ以下 (ρ_ref に対する割合) なら表面
	float surfaceHi{ 0.75f };			// 外の密度がこれ以上なら内部
	float surfaceCutoff{ 0.02f };		// 表面らしさがこれ未満の粒子は内部として扱う (描かない)
	SurfelDebug surfelDebug{ SurfelDebug::Surface };	// 段階 1 のデバッグ表示
	bool hideInterior{ false };			// 内部の粒子を描かない (表面粒子だけを確認する)

	// --- 粉塵 (段階 3。Spheres / Surface / Splat モードで描く) ---
	bool dust{ true };
	float dustEmitRate{ 0.06f };		// 条件を満たす粒子 1 個が、1 フレームに粉塵を出す確率
	float dustSpeedThreshold{ 1.0f };	// これより速い砂の粒子から粉塵が出る [m/s] (2 倍の速さで最大)
	float dustNearFloor{ 0.12f };		// 床からこの高さ以内の粒子だけが粉塵を出す [m] (着地・崩落の場所)
	float dustLifetime{ 3.0f };			// 寿命の平均 [s] (±50% でばらつかせる)
	float dustSize{ 6.0f };				// 発生時の大きさ = dustSize × 描画半径
	float dustGrowth{ 2.0f };			// 1 秒あたりに大きさが何倍分広がるか
	float dustOpacity{ 0.15f };			// 1 個あたりの濃さ
	float dustDrag{ 1.5f };				// 空気抵抗 [1/s] (速度が e^(-drag·t) で減る)
	float dustSinkSpeed{ 0.05f };		// 沈む速さ [m/s]
	float dustTurbulence{ 0.25f };		// 乱流のような揺らぎの強さ [m/s]
	glm::vec3 dustColor{ 0.62f, 0.55f, 0.45f };

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
