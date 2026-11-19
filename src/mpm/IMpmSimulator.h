#pragma once

#include <GL/glew.h>
#include <GLM/glm.hpp>
#include <memory>
#include <string>
#include <utility>
#include <stdexcept>

#include "IMpmObject.h"
#include "MpmPhysics.h"
#include "MpmEvaluator.h"

// 前方宣言
class SdfInstance;

class IMpmSimulator {
    GLuint faultBuffer{ 0 };

protected:
    // C-9 diagnostics for all transfer pipelines.
    void initDeformationValidation();
    void prepareDeformationValidation();
    bool checkDeformationFault(const SdfInstance& obstacle);

    // --- 共通データ ---
    std::unique_ptr<IMpmObject> mpmObject; // MpmObject は方式で異なるのでポインタで多態的に保持
    MpmEvaluator evaluator;
    float worldScale;
    int stepCount{ 0 };
    // Keep the caller's material density when switching particle geometry.
    MpmPhysics requestedPhysics{};
    bool hasPhysics{ false };
    bool sphereParticles{ false };
    float sphereParticleVolume{ 0.0f };
    std::string simulationError;

    // --- 共通のOpenGLリソース ---
    GLuint physicsUbo{ 0 };
    GLuint mpmSetup{ 0 };
    GLuint mpmP2G{ 0 };
    GLuint mpmGrid{ 0 };
    GLuint mpmG2P{ 0 };

    // --- 共通のSDF用Uniformロケーション ---
    GLint sdfModelLoc{ -1 };
    GLint sdfInvModelLoc{ -1 };
    GLint sdfVelocityLoc{ -1 };
    GLint sdfAabbMinLoc{ -1 };
    GLint sdfAabbMaxLoc{ -1 };
    GLint sdfAngularVelocityLoc{ -1 };
    GLint sdfCenterLoc{ -1 };

    /// 基底コンストラクタ
    /// @param [in] obj 派生先の形式に対応した MpmObject
    /// @param [in] worldScale シミュレーション領域の縮尺
    IMpmSimulator(std::unique_ptr<IMpmObject> obj, float worldScale);

    // 共通のUniformロケーション初期化
    void initCommonUniforms();

public:
    /// デストラクタ
    virtual ~IMpmSimulator();

    // コピー・ムーブ禁止
    IMpmSimulator(const IMpmSimulator&) = delete;
    IMpmSimulator& operator=(const IMpmSimulator&) = delete;

    // --- 共通実装関数 ---

    /// パーティクルの初期化・MPM 用の点群データ作成
    /// @param[in] scale 点群データのスケール
    /// @param[in] sphere 球状に配置するなら true 、立方体状に配置するなら false
    /// @note 球半径はworldScale*0.1*scale。球の粒子体積は球体積/粒子数、密度はsetPhysicsの指定を維持する。
    void resetParticles(float scale, bool sphere = true);
    /// 物理パラメータ(UBO)をGPUに転送・更新する
    /// @note 球配置ではp_vol/p_massを配置体積と指定密度から再計算する。立方体は指定値を使う。
    void setPhysics(const MpmPhysics& physics);

    /// コンピュートシェーダーを実行し、シミュレーションを1ステップ進める
    /// @param[in] obstacle 衝突判定を行うSDF障害物
    virtual void step(const SdfInstance& obstacle) = 0;

    // --- 共通ゲッター ---
    const IMpmObject& getMpmObject() const { return *mpmObject; }
    MpmEvaluator& getEvaluator() { return evaluator; }
    int getStepCount() const { return stepCount; }
    const std::string& getSimulationError() const { return simulationError; }
};
