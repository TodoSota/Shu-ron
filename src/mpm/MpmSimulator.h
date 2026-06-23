#pragma once

#include <GL/glew.h>
#include <GLM/glm.hpp>

#include "MpmObject.h"  // グリッドや粒子の情報
#include "MpmPhysics.h" // MPMの粒子群の物理パラメータ

// 前方宣言
class SdfInstance;

// Mpm の処理部分における Model(処理)部分
class MpmSimulator {
private:
    // --- コンピュートシェーダー群 ---
    GLuint mpmSetup{ 0 };
    GLuint mpmP2G{ 0 };
    GLuint mpmGrid{ 0 };
    GLuint mpmG2P{ 0 };
    GLuint mpmMove{ 0 };

    // --- シミュレーションのコアデータ ---
    MpmObject mpmObject;

    // --- UBO (Uniform Buffer Object) ---
    GLuint physicsUbo{ 0 };

    // --- Uniform ロケーションのキャッシュ ---
    GLint sdfModelLoc{ -1 };
    GLint sdfInvModelLoc{ -1 };
    GLint sdfVelocityLoc{ -1 };
    GLint sdfAabbMinLoc{ -1 };
    GLint sdfAabbMaxLoc{ -1 };
    GLint sdfAngularVelocityLoc{ -1 };
    GLint sdfCenterLoc{ -1 };

    // --- コンストラクタで受け取ったシミュレーションステータス ---
    float worldScale{ 1.0f };

public:
    /// コンストラクタ
    /// @param[in] particleCount 粒子の数
    /// @param[in] gridSize グリッドの解像度
    MpmSimulator(int particleCount, int gridSize, float worldScale);

    /// デストラクタ
    ~MpmSimulator();

    /// コピー禁止
    MpmSimulator(const MpmSimulator&) = delete;
    MpmSimulator& operator=(const MpmSimulator&) = delete;

    // --- ゲッター ---
    /// 描画(Renderer/h/.cpp)に渡すためのデータ取得
    const MpmObject& getMpmObject() const { return mpmObject; }


    // --- 初期化・設定 

    /// パーティクルの初期化・MPM 用の点群データ作成
    /// @param[in] scale 点群データのスケール
    /// @param[in] sphere 球状に配置するなら true 、立方体状に配置するなら false
    void resetParticles(float scale, bool sphere = true);

    /// 物理パラメータ(UBO)をGPUに転送・更新する
    void setPhysics(const MpmPhysics& physics);


    // --- メイン処理 ---
    /// コンピュートシェーダーを実行し、シミュレーションを1ステップ進める
    /// @param[in] obstacle 衝突判定を行うSDF障害物
    void step(const SdfInstance& obstacle);
};