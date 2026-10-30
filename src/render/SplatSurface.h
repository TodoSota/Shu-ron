#pragma once

//==============================================================================
// SplatSurface.h : 格子による表面粒子の抽出 (Splat モード)
// 目的: 描画専用の粗い密度格子を作り、粒子ごとに「表面らしさ」と法線 (密度勾配) を求める。
//       これを使って、表面の粒子だけを半透明のカーネルで重ねて描く (段階 2 以降)。
//       シミュレーションのコードは変更せず、粒子バッファを読むだけにしている。
//       手順と設計は docs/splat_guide.md を参照。
// 処理 (prepare, 毎フレーム):
//   S1 density_splat.comp   粒子 → 密度格子 (uint の SSBO へ固定小数点で atomicAdd)
//   S2 density_resolve.comp uint → float の 3D テクスチャ
//      density_blur.comp    x → y → z の 3 回でガウスぼかし
//   S3 surfel_prepare.comp  粒子ごとに 表面らしさ・法線・密度 を surfel バッファへ
//==============================================================================

#include <GL/glew.h>

struct RenderSettings;
struct MpmObject;

class SplatSurface {
public:
	static constexpr int kMaxGrid = 160;			// 格子の一辺のセル数の上限 (160³ × 4 バイト ≈ 16 MB)
	static constexpr GLuint kSurfelBinding = 4;		// surfel バッファの SSBO の束縛位置
	static constexpr GLuint kDensityBinding = 5;	// 密度格子 (uint) の SSBO の束縛位置

	SplatSurface();
	~SplatSurface();

	SplatSurface(const SplatSurface&) = delete;
	SplatSurface& operator=(const SplatSurface&) = delete;

	/// 密度格子と surfel バッファを作り直す (S1〜S3)
	/// @param[in] worldScale シミュレーション領域の一辺 [m]
	void prepare(const MpmObject& mpmObject, float worldScale, const RenderSettings& settings);

	/// surfel バッファ (粒子 1 個あたり vec4 × 2: 法線 + 表面らしさ、密度 + 勾配の強さ)
	GLuint surfelBuffer() const { return surfels; }
	/// ぼかし済みの密度の 3D テクスチャ (1 セルあたりの粒子数)
	GLuint densityTexture() const { return densityTex[finalTex]; }
	/// 格子の一辺のセル数
	int gridSize() const { return gridN; }
	/// セルの一辺 [m]
	float cellSize() const { return cell; }
	/// 粒子が隙間なく詰まったときの密度 [個/セル]
	float referenceDensity() const { return rhoRef; }

private:
	GLuint splatProgram{ 0 };
	GLuint resolveProgram{ 0 };
	GLuint blurProgram{ 0 };
	GLuint surfelProgram{ 0 };

	GLuint densityBuffer{ 0 };		// 固定小数点の密度 (uint × N³)
	GLuint densityTex[2]{ 0, 0 };	// float の密度 (R32F の 3D テクスチャ、ぼかしで交互に使う)
	int finalTex{ 0 };				// ぼかしの結果が入っている方
	int gridN{ 0 };
	float cell{ 0.0f };
	float rhoRef{ 1.0f };

	GLuint surfels{ 0 };
	GLsizei surfelCapacity{ 0 };

	void ensureGrid(int n);
	void ensureSurfels(GLsizei count);
};
