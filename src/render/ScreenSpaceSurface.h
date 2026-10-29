#pragma once

//==============================================================================
// ScreenSpaceSurface.h : スクリーン空間で粒子の面を張る (段階 2)
// 目的: 球インポスタの集まりは粒の凹凸がそのまま見えるので、粒子の深度を画面上で
//       平滑化し、そこから法線を求め直して、連続した砂の面として陰影を付ける。
//       (van der Laan et al. 2009 の screen space fluid rendering を砂に当てはめたもの)
// 処理:
//   (D) Smooth : G-buffer の粒子の線形深度を、横・縦に分けたバイラテラルフィルタで平滑化
//   (E) Normal : 平滑化した深度から法線を再構成し、球の法線とノイズの凹凸を少し混ぜる
//   (E') 書き戻し: 粒子の画素だけ、G-buffer の法線と深度を置き換える
//   この後の SSAO と陰影の計算は、段階 1 と同じ処理をそのまま使う。
//==============================================================================

#include <GL/glew.h>
#include <GLM/glm.hpp>
#include "RenderTarget.h"

struct RenderSettings;

class ScreenSpaceSurface {
public:
	ScreenSpaceSurface();
	~ScreenSpaceSurface();

	ScreenSpaceSurface(const ScreenSpaceSurface&) = delete;
	ScreenSpaceSurface& operator=(const ScreenSpaceSurface&) = delete;

	/// 面を張る処理を行い、結果を G-buffer に書き戻す
	/// @param[in] gbuffer location 1 = 法線、location 2 = 粒子の線形深度 を持つ G-buffer
	/// @param[in] particleRadius 描画半径 [m]
	void run(const RenderTarget& gbuffer, const glm::mat4& projection, const glm::mat4& view,
		float particleRadius, const RenderSettings& settings, GLuint emptyVao);

	/// 平滑化した線形深度 (デバッグ表示用)
	GLuint smoothedDepthTexture() const { return pingTarget.colorTexture(0); }

private:
	GLuint smoothProgram{ 0 };
	GLuint normalProgram{ 0 };
	GLuint compositeProgram{ 0 };
	RenderTarget pingTarget;	// 平滑化した線形深度 (R32F)
	RenderTarget pongTarget;	// 平滑化の作業用 (R32F)
	RenderTarget surfaceTarget;	// 法線 (xyz) + 平滑化した線形深度 (w) (RGBA16F は精度が足りないので RGBA32F)
};
