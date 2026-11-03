#pragma once

#include "../IMpmSimulator.h"

// Mpm の処理部分
class MpmSimulator3DTEX : public IMpmSimulator {
public:
    /// コンストラクタ
    /// @param[in] particleCount 粒子数
    /// @param[in] gridSize グリッドの解像度
    /// @param[in] useLinear 補間方式において Linear を用いるのか
    MpmSimulator3DTEX(GLsizei particleCount, int gridSize, float worldScale, bool useLinear);

    /// デストラクタ
    ~MpmSimulator3DTEX() override = default;

    /// コンピュートシェーダーを実行し、シミュレーションを1ステップ進める
    /// @param[in] obstacle 衝突判定を行うSDF障害物
    void step(const SdfInstance& obstacle) override;
};