#pragma once

#include "../IMpmSimulator.h"

class MpmSimulatorSSBO : public IMpmSimulator {
private:
    // グリッドサイズのUniformロケーション
    // SSBOでは imageSize() が使えない
    GLint setupGridSizeLoc{ -1 };
    GLint p2gGridSizeLoc{ -1 };
    GLint gridGridSizeLoc{ -1 };
    GLint g2pGridSizeLoc{ -1 };

public:
    /// コンストラクタ
    /// @param[in] particleCount 粒子数
    /// @param[in] gridSize グリッドの解像度
    /// @param[in] useLinear 補間方式において Linear を用いるのか
    MpmSimulatorSSBO(GLsizei particleCount, int gridSize, float worldScale, bool useLinear);

    /// デストラクタ
    ~MpmSimulatorSSBO() override = default;

    /// コンピュートシェーダーを実行し、シミュレーションを1ステップ進める
    /// @param[in] obstacle 衝突判定を行うSDF障害物
    void step(const SdfInstance& obstacle) override;
};