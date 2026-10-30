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
// 処理 (draw, deferred splatting [Botsch et al. 2005]):
//   P1 G-buffer の深度 (床・障害物) を splat 用の深度へコピー
//   被覆率  : 視線上のすべての円盤の重みを足す (splat.vert/frag, pass 2。床・障害物の深度とだけ比べる)
//   P2 可視性: 表面粒子の円盤を、少し奥へずらした深度で描く (pass 0)
//   P3 蓄積  : 厚みの内側の円盤の 重み × 色・法線・深度 を加算 (pass 1)
//   P4 正規化: Σw で割って splat 層に書く (被覆率 α も。半透明の輪郭は lighting.frag で重ねる)
//   P5 正規化: α が十分大きい不透明な画素だけを G-buffer に書く (splat_normalize.frag)
//==============================================================================

#include <GL/glew.h>
#include <GLM/glm.hpp>
#include "RenderTarget.h"

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

	/// 表面粒子を円盤として重ねて描き、G-buffer の色・法線・深度に書き込む (P1〜P5)
	/// prepare() の後、G-buffer に床と障害物を描いた後に呼ぶ
	void draw(const MpmObject& mpmObject, const RenderTarget& gbuffer, const glm::mat4& view, const glm::mat4& projection,
		bool useDebugColor, const RenderSettings& settings, GLuint emptyVao);

	/// surfel バッファ (粒子 1 個あたり vec4 × 2: 法線 + 表面らしさ、密度 + 勾配の強さ)
	GLuint surfelBuffer() const { return surfels; }
	/// splat 層 (0: RGBA8 色 + 被覆率, 1: RGBA16F 法線 + あいまいさ, 2: R32F 線形深度)
	GLuint layerTexture(int index) const { return layerTarget.colorTexture(index); }
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
	GLuint drawProgram{ 0 };		// splat.vert/frag (可視性・蓄積)
	GLuint normalizeProgram{ 0 };	// splat_normalize.frag

	RenderTarget layerTarget;		// splat 層 (P4 の出力)
	RenderTarget accumTarget;		// 0: RGBA16F (Σw·色, Σw), 1: RGBA16F (Σw·法線, Σw·あいまいさ), 2: R32F (Σw·深度),
									// 3: R16F (視線上の Σw、被覆率), 深度 D32F

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
