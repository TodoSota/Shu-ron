#include "IMpmSimulator.h"
#include "MpmParticle.h"
#include <iostream>
#include <random>

/// 基底コンストラクタ
/// @param [in] obj 派生先の形式に対応した MpmObject
/// @param [in] worldScale シミュレーション領域の縮尺
IMpmSimulator::IMpmSimulator(std::unique_ptr<IMpmObject> obj, float worldScale)
    : mpmObject(std::move(obj)),  evaluator(), worldScale(worldScale)
{
    if (!mpmObject) throw std::invalid_argument("IMpmObject must not be null.");

    // --- 物理パラメータ転送用の UBO を生成 ---
    glGenBuffers(1, &physicsUbo);
    glBindBuffer(GL_UNIFORM_BUFFER, physicsUbo);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(MpmPhysics), nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    // --- 評価クラスでの評価機能初期設定 ---
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
    const int count = mpmObject->getParticleCount();
    if (count > 0) evaluator.addRepresentativeParticle(0);
    if (count > 500) evaluator.addRepresentativeParticle(500);
}

/// デストラクタ
IMpmSimulator::~IMpmSimulator() {
    glDeleteProgram(mpmSetup);
    glDeleteProgram(mpmP2G);
    glDeleteProgram(mpmGrid);
    glDeleteProgram(mpmG2P);
    glDeleteBuffers(1, &physicsUbo);
}

// 共通のUniformロケーション初期化
void IMpmSimulator::initCommonUniforms() {
    sdfModelLoc = glGetUniformLocation(mpmGrid, "sdf_model");
    sdfInvModelLoc = glGetUniformLocation(mpmGrid, "sdf_inv_model");
    sdfVelocityLoc = glGetUniformLocation(mpmGrid, "sdf_velocity");
    sdfAabbMinLoc = glGetUniformLocation(mpmGrid, "sdf_aabb_min");
    sdfAabbMaxLoc = glGetUniformLocation(mpmGrid, "sdf_aabb_max");
    sdfAngularVelocityLoc = glGetUniformLocation(mpmGrid, "sdf_angular_velocity");
    sdfCenterLoc = glGetUniformLocation(mpmGrid, "sdf_center");
}

/// パーティクルの初期化・MPM 用の点群データ作成
/// @param[in] scale 点群データのスケール
/// @param[in] sphere 球状に配置するなら true 、立方体状に配置するなら false
void IMpmSimulator::resetParticles(float scale, bool sphere) {
    //std::random_device seed_gen;
    //std::mt19937 engine(seed_gen());
    std::mt19937 engine(0);

    GLuint readVbo = mpmObject->getReadVbo();
    int count = static_cast<int>(mpmObject->getParticleCount());

    glBindBuffer(GL_ARRAY_BUFFER, readVbo);
    while (glGetError() != GL_NO_ERROR) {}
    const auto position = static_cast<MpmParticle*>(glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY));

    if (!position) {
        GLenum err = glGetError();
        std::cerr << "Error: Failed to map buffer for particles. GL Error Code: " << err << std::endl;
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        return;
    }

    // 粒子のランダム生成位置
    const float center = static_cast<float>(worldScale * 0.5);
    const float halfExtent = static_cast<float>(worldScale * 0.1 * scale);

    std::uniform_real_distribution<GLfloat> dist(-1.0f, 1.0f);
    // 立方体生成の際には worldScale を加味した、世界の中心に粒子を生成
    std::uniform_real_distribution<GLfloat> distCube(center - halfExtent, center + halfExtent);
    std::uniform_real_distribution<GLfloat> matDist(0.0f, 1.0f);// 粒子のランダム色変更

    for (int i = 0; i < count; i++) {
        if (sphere) {
            const float u = dist(engine);
            const float v = dist(engine) * 2.0f - 1.0f;
            const float w = dist(engine);
            const float r = cbrt(w) * scale;
            const float s = sqrt(1.0f - v * v) * r;
            const float t = u * 6.2831853f;

            position[i].position = { s * cos(t) + center, s * sin(t) + center, r * v + center, 1.0f };
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
    const GLboolean valid = glUnmapBuffer(GL_ARRAY_BUFFER);
    if (!valid) std::cerr << "Error: Corrupted buffer data during unmap." << std::endl;

    // 両バッファへのコピー処理
    glBindBuffer(GL_COPY_READ_BUFFER, mpmObject->getReadVbo());
    glBindBuffer(GL_COPY_WRITE_BUFFER, mpmObject->getWriteVbo());
    glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, sizeof(MpmParticle) * count);

    glBindBuffer(GL_COPY_READ_BUFFER, 0);
    glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // 評価クラスの内部ログのクリア
    stepCount = 0;
    evaluator.clearLogs();
}

/// 物理パラメータ(UBO)をGPUに転送・更新する
void IMpmSimulator::setPhysics(const MpmPhysics& physics) {
    glBindBuffer(GL_UNIFORM_BUFFER, physicsUbo);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(MpmPhysics), &physics);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}