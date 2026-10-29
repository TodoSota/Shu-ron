#pragma once

//==============================================================================
// DustSystem.h : 粉塵 (段階 3)
// 目的: 砂が速く動いている場所 (着地・崩落・障害物の衝突) から舞い上がる粉塵を、
//       シミュレーションとは独立した粒子系として発生・移動させ、半透明で描く。
//       シミュレーションのコードは変更せず、砂の粒子バッファを読むだけにしている。
// 処理:
//   update() : (1) dust_emit.comp   砂の粒子から確率的に粉塵を発生 (リングバッファへ追加)
//              (2) dust_update.comp 空気抵抗・沈降・乱流で移動し、寿命を減らす
//   draw()   : (H) dust.vert/frag で WBOIT のバッファへ蓄積し、
//              wboit_composite.frag で陰影付きの画像の上に重ねる (ソート不要)
//==============================================================================

#include <GL/glew.h>
#include <GLM/glm.hpp>

struct RenderSettings;
struct MpmObject;
struct MpmPhysics;

class DustSystem {
public:
	static constexpr int kCapacity = 65536;	// 同時に存在できる粉塵の数 (リングバッファの大きさ)

	DustSystem();
	~DustSystem();

	DustSystem(const DustSystem&) = delete;
	DustSystem& operator=(const DustSystem&) = delete;

	/// 粉塵を発生させ、dt 秒だけ進める (シミュレーションを進めたフレームにだけ呼ぶ)
	void update(const MpmObject& mpmObject, const MpmPhysics& physics, float dt, float particleRadius, const RenderSettings& settings);

	/// すべての粉塵を消す (巻き戻し・再起動のとき)
	void clear();

	/// 粉塵を描き、画面 (既定のフレームバッファ) に重ねる
	/// @param[in] sceneDepthTex G-buffer の深度 (前後関係と soft particle に使う)
	/// @param[in] shadowTex シャドウマップ (0 なら影なし)
	void draw(GLuint sceneDepthTex, int width, int height, const glm::mat4& view, const glm::mat4& projection,
		GLuint shadowTex, const glm::mat4& lightViewProjection, float particleRadius, const RenderSettings& settings, GLuint emptyVao);

private:
	GLuint dustBuffer{ 0 };		// 粉塵の SSBO (1 個 32 バイト × kCapacity)
	GLuint counterBuffer{ 0 };	// リングバッファの書き込み位置 (uint 1 個)
	GLuint emitProgram{ 0 };
	GLuint updateProgram{ 0 };
	GLuint drawProgram{ 0 };
	GLuint compositeProgram{ 0 };

	// WBOIT 用のオフスクリーン (accum: RGBA32F, revealage: R16F)。前後判定はシェーダーで G-buffer の深度と比べる
	GLuint fbo{ 0 };
	GLuint accumTex{ 0 };
	GLuint revealTex{ 0 };
	int fboWidth{ 0 }, fboHeight{ 0 };
	GLuint attachedDepth{ 0 };

	unsigned int frameIndex{ 0 };	// 発生の乱数の種
	float time{ 0.0f };				// 乱流の場を時間で動かすため

	void ensureTargets(int width, int height, GLuint sceneDepthTex);
};
