#pragma once

//==============================================================================
// SsaoPass.h : スクリーン空間アンビエントオクルージョン (SSAO)
// 目的: G-buffer の深度と法線から、各画素のまわりの半球がどれだけ周囲の面で
//       ふさがれているかを推定し、環境光を弱める係数 (AO) を作る。
//       粒子のすき間や接地面が暗くなり、粒状の質感と立体感が出る。
// 処理: 半解像度で 16 サンプルの AO を計算 → 4×4 のノイズの縞を、深度を考慮したぼかしで消す。
//==============================================================================

#include <GL/glew.h>
#include <GLM/glm.hpp>
#include "RenderTarget.h"

struct RenderSettings;

class SsaoPass {
public:
	SsaoPass();
	~SsaoPass();

	SsaoPass(const SsaoPass&) = delete;
	SsaoPass& operator=(const SsaoPass&) = delete;

	/// AO を計算する
	/// @param[in] depthTex G-buffer の深度テクスチャ
	/// @param[in] normalTex G-buffer の法線テクスチャ (ビュー空間)
	/// @param[in] fullWidth, fullHeight G-buffer の大きさ
	/// @param[in] radius AO のサンプル半径 [m]
	/// @return ぼかした AO のテクスチャ (R8、1 = 遮蔽なし)
	GLuint run(GLuint depthTex, GLuint normalTex, int fullWidth, int fullHeight,
		const glm::mat4& projection, float radius, const RenderSettings& settings, GLuint emptyVao);

private:
	static constexpr int kKernelSize = 16;
	GLuint ssaoProgram{ 0 };
	GLuint blurProgram{ 0 };
	GLuint noiseTex{ 0 };
	glm::vec3 kernel[kKernelSize];
	RenderTarget aoTarget;		// 半解像度の生の AO
	RenderTarget blurTarget;	// ぼかした AO
};
