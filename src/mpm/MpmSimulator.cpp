#include "MpmSimulator.h"
#include "MpmPhysics.h"
#include "../sdf/SdfInstance.h"
#include "../core/Shader.h"
#include <iostream>
#include <random>
#include <GLM/gtc/type_ptr.hpp>

/// コンストラクタ
/// @param[in] particleCount 粒子の数
/// @param[in] gridSize グリッドの解像度
/// @param[in] useLinear 補間方式において Linear を用いるのか
MpmSimulator::MpmSimulator(int particleCount, int gridSize, float worldScale, bool useLinear)
    : mpmObject(particleCount, gridSize), worldScale(worldScale), evaluator()
{
    // コンピュートシェーダーのロード
    mpmSetup = loadCompute("src/mpm/shaders/mpm_setup.comp");
    mpmGrid = loadCompute("src/mpm/shaders/mpm_grid.comp");
    // 指定の補間方法のファイルを選択
    if (!useLinear) {
        // 通常
        mpmP2G = loadCompute("src/mpm/shaders/mpm_p2g.comp");
        mpmG2P = loadCompute("src/mpm/shaders/mpm_g2p.comp");
        std::cout << "Loaded MPM Shaders: Cubic B-Spline Interpolation" << std::endl;
    }
    else {
        // Linear
        mpmP2G = loadCompute("src/mpm/shaders/mpm_p2g_Linear.comp");
        mpmG2P = loadCompute("src/mpm/shaders/mpm_g2p_Linear.comp");
        std::cout << "Loaded MPM Shaders: Linear Interpolation" << std::endl;
    }

    if (mpmSetup == 0 || mpmP2G == 0 || mpmGrid == 0 || mpmG2P == 0) {
        std::cerr << "Error: Can not create MPM simulator compute shaders." << std::endl;
    }

    // Uniform ロケーションの取得
    sdfModelLoc = glGetUniformLocation(mpmGrid, "sdf_model");
    sdfInvModelLoc = glGetUniformLocation(mpmGrid, "sdf_inv_model");
    sdfVelocityLoc = glGetUniformLocation(mpmGrid, "sdf_velocity");
    sdfAabbMinLoc = glGetUniformLocation(mpmGrid, "sdf_aabb_min");
    sdfAabbMaxLoc = glGetUniformLocation(mpmGrid, "sdf_aabb_max");
    sdfAngularVelocityLoc = glGetUniformLocation(mpmGrid, "sdf_angular_velocity");
    sdfCenterLoc = glGetUniformLocation(mpmGrid, "sdf_center");

    // 物理パラメータ転送用の UBO を生成
    glGenBuffers(1, &physicsUbo);
    glBindBuffer(GL_UNIFORM_BUFFER, physicsUbo);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(MpmPhysics), nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    // 評価クラスでの評価機能初期設定 : 全てOFF
    EvaluatorOptions initOptions;
    initOptions.enableGpuTimer = false;
    initOptions.enableGpuTimerReadback = false;
    initOptions.enableStateEvaluation = false;
    initOptions.enableStateReadback = false;
    initOptions.enableRepParticleReadback = false;
    initOptions.enableFpsLogging = false;
    initOptions.stateEvaluationInterval = 10;
    evaluator.setOptions(initOptions);

    // --- 評価機能における代表粒子選択 ---
    if (particleCount > 0) evaluator.addRepresentativeParticle(0);     // 中心付近の粒子など
    if (particleCount > 500)evaluator.addRepresentativeParticle(500);  // 別の場所の粒子など
}
MpmSimulator::~MpmSimulator() {
    glDeleteProgram(mpmSetup);
    glDeleteProgram(mpmP2G);
    glDeleteProgram(mpmGrid);
    glDeleteProgram(mpmG2P);
    glDeleteBuffers(1, &physicsUbo);
}

// --- 初期化・設定 ---

void MpmSimulator::resetParticles(float scale, bool sphere) {
    //std::random_device seed_gen;
    //std::mt19937 engine(seed_gen());
    std::mt19937 engine(0); // 評価のため固定シードを使用

    // 0番の方のvboをバインドし頂点データをマップ
    glBindBuffer(GL_ARRAY_BUFFER, mpmObject.vbo[0]);

    const auto position = static_cast<MpmParticle*>(glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY));

    // 粒子のランダム生成位置
    const float center = static_cast<float>(worldScale * 0.5);
    const float halfExtent = static_cast<float>(worldScale * 0.1 * scale);

    std::uniform_real_distribution<GLfloat> dist(-1.0f, 1.0f);
    // 立方体生成の際には worldScale を加味した、世界の中心に粒子を生成
    std::uniform_real_distribution<GLfloat> distCube(center - halfExtent, center + halfExtent);
    // 粒子のランダム色変更
    std::uniform_real_distribution<GLfloat> matDist(0.0f, 1.0f);

    for (auto i = 0; i < mpmObject.count; i++) {
        if (sphere) {
            const float u = dist(engine);
            const float v = dist(engine) * 2.0f - 1.0f;
            const float w = dist(engine);
            const float r = cbrt(w) * scale;
            const float s = sqrt(1.0f - v * v) * r;
            const float t = u * 6.2831853f;

            position[i].position = { s * cos(t) + 0.5f, s * sin(t) + 0.5f, r * v + 0.5f, 1.0f };
        }
        else {
            position[i].position = { distCube(engine), distCube(engine), distCube(engine), 1.0f };
        }

        // 粒子の初期化
        position[i].velocity = glm::vec4(0.0f);
        position[i].affineC = glm::mat4(0.0f);
        position[i].deformation = glm::mat4(1.0f);
        position[i].alpha = 0.267765f;
        position[i].q = 0.0f;
        position[i].vc = 0.0f;
        position[i].state = 1.0f;
        position[i].scale = worldScale;


        position[i].padding[0] = position[i].padding[1] = position[i].padding[2] = 0;

        // 石の色を少し混ぜる・確率は 0,05f のマジックナンバー
        position[i].padding[0] = (matDist(engine) < 0.05f) ? 1 : 0;
    }
    glUnmapBuffer(GL_ARRAY_BUFFER); // 情報入力

    // もう片方(1番)にコピーする
    glBindBuffer(GL_COPY_READ_BUFFER, mpmObject.vbo[0]);
    glBindBuffer(GL_COPY_WRITE_BUFFER, mpmObject.vbo[1]);
    glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, sizeof(MpmParticle) * mpmObject.count);

    // 書き込みバッファ設定、コピー対象設定解除
    glBindBuffer(GL_COPY_READ_BUFFER, 0);
    glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // シミュレーションステップ数/ログをリセット
    stepCount = 0;
    evaluator.clearLogs();
}

/// 物理パラメータ(UBO)をGPUに転送・更新する
void MpmSimulator::setPhysics(const MpmPhysics& physics) {
    glBindBuffer(GL_UNIFORM_BUFFER, physicsUbo);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(MpmPhysics), &physics);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

// --- メイン処理 (演算パイプライン) ---

/// コンピュートシェーダーを実行し、シミュレーションを1ステップ進める
void MpmSimulator::step(const SdfInstance& obstacle) {
    // ダブルバッファの選択
    GLuint readVbo = MpmSimulator::getReadVbo();
    GLuint writeVbo = MpmSimulator::getWriteVbo();

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
    glBindImageTexture(0, mpmObject.gridTexX, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
    glBindImageTexture(1, mpmObject.gridTexY, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
    glBindImageTexture(2, mpmObject.gridTexZ, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
    glBindImageTexture(3, mpmObject.gridTexA, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);

    // コンピュートシェーダーの連続ディスパッチ
    int numGroups = (mpmObject.gridSize + 7) / 8; // compファイルでの local_size が 8*8*8 なので 解像度/8 で送信
    int particleGroups = (mpmObject.count + 63) / 64; // こちらも同様。パーティクル数用のグループ数

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
    mpmObject.swapBuffers();
    
    // シミュレーション評価実行
    stepCount++;    // シミュレーション評価のステップを進める
    evaluator.evaluateStep(writeVbo, mpmObject.count, stepCount);
    
}