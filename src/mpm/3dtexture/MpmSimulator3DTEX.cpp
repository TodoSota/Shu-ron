#include <iostream>
#include <stdexcept>
#include <memory>
#include <GLM/gtc/type_ptr.hpp>

#include "MpmSimulator3DTEX.h"
#include "MpmObject3DTEX.h"
#include "../../sdf/SdfInstance.h"
#include "../../core/Shader.h"


/// コンストラクタ
/// @param[in] particleCount 粒子の数
/// @param[in] gridSize グリッドの解像度
/// @param[in] useLinear 補間方式において Linear を用いるのか
MpmSimulator3DTEX::MpmSimulator3DTEX(GLsizei particleCount, int gridSize, float worldScale, bool useLinear)
    : IMpmSimulator(std::make_unique<MpmObject3DTEX>(particleCount, gridSize), worldScale)
{
    // 3Dテクスチャ版のシェーダーのロード
    mpmSetup = loadCompute("src/mpm/shaders/3dtexture/mpm_setup_3dtex.comp");
    mpmGrid = loadCompute("src/mpm/shaders/3dtexture/mpm_grid_3dtex.comp");
    // 指定の補間方法のファイルを選択
    if (!useLinear) {
        // 通常
        mpmP2G = loadCompute("src/mpm/shaders/3dtexture/mpm_p2g_3dtex.comp");
        mpmG2P = loadCompute("src/mpm/shaders/3dtexture/mpm_g2p_3dtex.comp");
        std::cout << "Loaded MPM Shaders: Cubic B-Spline Interpolation" << std::endl;
    }
    else {
        // Linear
        mpmP2G = loadCompute("src/mpm/shaders/3dtexture/mpm_p2g_Linear_3dtex.comp");
        mpmG2P = loadCompute("src/mpm/shaders/3dtexture/mpm_g2p_Linear_3dtex.comp");
        std::cout << "Loaded MPM Shaders: Linear Interpolation" << std::endl;
    }

    if (mpmSetup == 0 || mpmP2G == 0 || mpmGrid == 0 || mpmG2P == 0) {
        throw std::runtime_error("Error: Can not create MPM 3DTEX simulator compute shaders.");
    }

    // 共通部分の Uniform の初期化
    initCommonUniforms();
}

// --- メイン処理 (演算パイプライン) ---

/// コンピュートシェーダーを実行し、シミュレーションを1ステップ進める
void MpmSimulator3DTEX::step(const SdfInstance& obstacle) {
    // ダブルバッファの選択
    GLuint readVbo = mpmObject->getReadVbo();
    GLuint writeVbo = mpmObject->getWriteVbo();
    int gridSize = mpmObject->getGridSize();
    int particleCount = mpmObject->getParticleCount();

    // ダウンキャストして 3Dテクスチャ 固有の Object を呼ぶ
    auto* obj3DTEX = static_cast<MpmObject3DTEX*>(mpmObject.get());


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

    // データバッファのバインド
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, readVbo);  // 計算用の VBO をセット
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, writeVbo); // 結果用の VBO をセット
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, physicsUbo);

    // グリッドの情報転送
    glBindImageTexture(0, obj3DTEX->getGridTexX(), 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
    glBindImageTexture(1, obj3DTEX->getGridTexY(), 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
    glBindImageTexture(2, obj3DTEX->getGridTexZ(), 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
    glBindImageTexture(3, obj3DTEX->getGridTexA(), 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);

    // コンピュートシェーダーの連続ディスパッチ
    int numGroups = (gridSize + 7) / 8; // compファイルでの local_size が 8*8*8 なので 解像度/8 で送信
    int particleGroups = (particleCount + 63) / 64; // こちらも同様。パーティクル数用のグループ数

    // [Setup] グリッドのリセット
    glUseProgram(mpmSetup);
    evaluator.beginTimer("Setup");
    glDispatchCompute(numGroups, numGroups, numGroups);
    evaluator.endTimer("Setup");
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    // [P2G] パーティクルからグリッドへ物理量を転送
    glUseProgram(mpmP2G);
    evaluator.beginTimer("P2G");
    glDispatchCompute(particleGroups, 1, 1);
    evaluator.endTimer("P2G");
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    // [Grid] グリッド上での力学計算と境界(SDF)処理
    glUseProgram(mpmGrid);
    evaluator.beginTimer("Grid");
    glDispatchCompute(numGroups, numGroups, numGroups);
    evaluator.endTimer("Grid");
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    // [G2P] グリッドからパーティクルへ速度と変形勾配を書き戻し
    glUseProgram(mpmG2P);
    evaluator.beginTimer("G2P");
    glDispatchCompute(particleGroups, 1, 1);
    evaluator.endTimer("G2P");
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);

    // ping-pongスワップ(ダブルバッファの切り替え) 
    mpmObject->swapBuffers();

    // シミュレーション評価実行
    stepCount++;    // シミュレーション評価のステップを進める
    evaluator.evaluateStep(writeVbo, particleCount, stepCount);

}