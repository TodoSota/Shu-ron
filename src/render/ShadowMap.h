#pragma once

//==============================================================================
// ShadowMap.h : 太陽 (平行光源) のシャドウマップ
// 目的: 光源から見た深度を描いておき、陰影の計算で「その点が光源から見えるか」を判定する。
//       シミュレーション領域 [0, worldScale]³ は固定なので、領域を包む平行投影を 1 枚使う
//       (カメラに追従させる cascaded shadow map は不要)。
//==============================================================================

#include <GL/glew.h>
#include <GLM/glm.hpp>

class ShadowMap {
public:
	ShadowMap() = default;
	~ShadowMap();

	ShadowMap(const ShadowMap&) = delete;
	ShadowMap& operator=(const ShadowMap&) = delete;

	/// 深度テクスチャを作る (解像度が変わったときは作り直す)
	void ensure(int resolution);

	/// 光源の行列を計算する
	/// @param[in] sunDir 地表から太陽へ向かう単位ベクトル
	/// @param[in] worldScale シミュレーション領域の一辺 [m]
	void update(const glm::vec3& sunDir, float worldScale);

	/// 描画先にする (深度だけ)
	void bind() const;

	GLuint depthTexture() const { return depthTex; }
	int getResolution() const { return resolution; }
	const glm::mat4& getView() const { return lightView; }
	const glm::mat4& getProjection() const { return lightProj; }
	glm::mat4 getViewProjection() const { return lightProj * lightView; }

private:
	GLuint fbo{ 0 };
	GLuint depthTex{ 0 };
	int resolution{ 0 };
	glm::mat4 lightView{ 1.0f };
	glm::mat4 lightProj{ 1.0f };
};
