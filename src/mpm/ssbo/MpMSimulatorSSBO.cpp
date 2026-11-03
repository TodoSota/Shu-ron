#include <iostream>
#include <stdexcept>
#include <memory>
#include <GLM/gtc/type_ptr.hpp>

#include "MpmSimulatorSSBO.h"
#include "MpmObjectSSBO.h"
#include "../../sdf/SdfInstance.h"
#include "../../core/Shader.h"

/// コンストラクタ
/// @brief 共通の VAO/VBO 生成を行う
/// @param [in] obj 派生先の形式に対応した MpmObject
/// @param [in] particleCount 粒子数
/// @param [in] worldScale シミュレーション領域の縮尺
/// @param [in] useLinear 線形補間の方式を使用するか
MpmSimulatorSSBO::MpmSimulatorSSBO(GLsizei particleCount, int gridSize, float worldScale, bool useLinear)
    : IMpmSimulator(std::make_unique<MpmObjectSSBO>(particleCount, gridSize), worldScale)
{
    // SSBO版のシェーダをロード
    mpmSetup = loadCompute("src/mpm/shaders/ssbo/mpm_setup_ssbo.comp");
    mpmGrid = loadCompute("src/mpm/shaders/ssbo/mpm_grid_ssbo.comp");

    if (!useLinear) {
        // 通常
        mpmP2G = loadCompute("src/mpm/shaders/ssbo/mpm_p2g_ssbo.comp");
        mpmG2P = loadCompute("src/mpm/shaders/ssbo/mpm_g2p_ssbo.comp");
        std::cout << "Loaded MPM Shaders (SSBO): Cubic B-Spline Interpolation" << std::endl;
    }
    else {
        // Linear
        mpmP2G = loadCompute("src/mpm/shaders/ssbo/mpm_p2g_Linear_ssbo.comp");
        mpmG2P = loadCompute("src/mpm/shaders/ssbo/mpm_g2p_Linear_ssbo.comp");
        std::cout << "Loaded MPM Shaders (SSBO): Linear Interpolation" << std::endl;
    }

    if (mpmSetup == 0 || mpmP2G == 0 || mpmGrid == 0 || mpmG2P == 0) {
        throw std::runtime_error("Error: Can not create MPM SSBO simulator compute shaders.");
    }

    // 共通部分の Uniform の初期化
    initCommonUniforms();

    // SSBO の Uniform の初期化
    setupGridSizeLoc = glGetUniformLocation(mpmSetup, "u_gridSize");
    p2gGridSizeLoc = glGetUniformLocation(mpmP2G, "u_gridSize");
    gridGridSizeLoc = glGetUniformLocation(mpmGrid, "u_gridSize");
    g2pGridSizeLoc = glGetUniformLocation(mpmG2P, "u_gridSize");
}

/// コンピュートシェーダーを実行し、シミュレーションを1ステップ進める
/// @param[in] obstacle 衝突判定を行うSDF障害物
void MpmSimulatorSSBO::step(const SdfInstance& obstacle) {
    GLuint readVbo = mpmObject->getReadVbo();
    GLuint writeVbo = mpmObject->getWriteVbo();
    int gridSize = mpmObject->getGridSize();
    int particleCount = mpmObject->getParticleCount();

    // ダウンキャストして SSBO 固有の Object を呼ぶ
    auto* objSSBO = static_cast<MpmObjectSSBO*>(mpmObject.get());

    // SDFテクスチャのバインド
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_3D, obstacle.resource->sdfTexture3D);

    // MpmGrid への Uniform 変数(SDF障害物情報)の送信
    const glm::mat4& modelMat = obstacle.getModelMatrix();
    const glm::mat4& modelInv = obstacle.getInverseModelMatrix();

    glUseProgram(mpmGrid);
    glUniformMatrix4fv(sdfModelLoc, 1, GL_FALSE, glm::value_ptr(modelMat));
    glUniformMatrix4fv(sdfInvModelLoc, 1, GL_FALSE, glm::value_ptr(modelInv));
    glUniform3fv(sdfVelocityLoc, 1, glm::value_ptr(obstacle.velocity));
    glUniform3fv(sdfAabbMinLoc, 1, glm::value_ptr(obstacle.resource->aabbMin));
    glUniform3fv(sdfAabbMaxLoc, 1, glm::value_ptr(obstacle.resource->aabbMax));
    glUniform3fv(sdfAngularVelocityLoc, 1, glm::value_ptr(obstacle.angularVelocity));
    glUniform3fv(sdfCenterLoc, 1, glm::value_ptr(obstacle.position));

    // --- バッファのバインド設定 ---
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, readVbo);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, writeVbo);
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, physicsUbo);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, objSSBO->getGridSsbo()); // SSBO は 1つ

    int numGroups = (gridSize + 7) / 8;
    int particleGroups = (particleCount + 63) / 64;

    // [Setup]
    glUseProgram(mpmSetup);
    glUniform1i(setupGridSizeLoc, gridSize); // グリッドサイズ送信
    evaluator.beginTimer("Setup");
    glDispatchCompute(numGroups, numGroups, numGroups);
    evaluator.endTimer("Setup");
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    // [P2G]
    glUseProgram(mpmP2G);
    glUniform1i(p2gGridSizeLoc, gridSize); // グリッドサイズ送信
    evaluator.beginTimer("P2G");
    glDispatchCompute(particleGroups, 1, 1);
    evaluator.endTimer("P2G");
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    // [Grid]
    glUseProgram(mpmGrid);
    glUniform1i(gridGridSizeLoc, gridSize); // グリッドサイズ送信
    evaluator.beginTimer("Grid");
    glDispatchCompute(numGroups, numGroups, numGroups);
    evaluator.endTimer("Grid");
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    // [G2P]
    glUseProgram(mpmG2P);
    glUniform1i(g2pGridSizeLoc, gridSize); // グリッドサイズ送信
    evaluator.beginTimer("G2P");
    glDispatchCompute(particleGroups, 1, 1);
    evaluator.endTimer("G2P");
    // パーティクル出力(VBO)用のバリアとSSBO用のバリアの論理和
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);

    // ダブルバッファの切り替え
    mpmObject->swapBuffers();

    stepCount++;
    evaluator.evaluateStep(writeVbo, particleCount, stepCount);
}