//==============================================================================
// MpmEvaluator.cpp : 評価・計測処理の実装
//==============================================================================
#include "MpmEvaluator.h"
#include "MpmParticle.h" 
#include "../core/Shader.h"
#include "../core/Errorcheck.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <locale>

// コンストラクタ
MpmEvaluator::MpmEvaluator() {
    // SSBOの生成
    glGenBuffers(1, &evalSsbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, evalSsbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(MpmEvalResult), nullptr, GL_DYNAMIC_READ);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    // 評価用コンピュートシェーダーのロード
    evalProgram = loadCompute("src/mpm/shaders/mpm_evaluator.comp");
    if (evalProgram == 0) {
        std::cerr << "Error: Can not create MPM evaluator compute shaders." << std::endl;
    }
}

// デストラクタ
MpmEvaluator::~MpmEvaluator() {
    glDeleteProgram(evalProgram);
    glDeleteBuffers(1, &evalSsbo);
    for (auto& pair : timerQueries) {
        glDeleteQueries(1, &pair.second);
    }
}

// 代表粒子の登録
void MpmEvaluator::addRepresentativeParticle(int index) {
    repParticleIndices.push_back(index);
}

// FPS 記録 : main.cpp より毎フレーム呼び出し想定
void MpmEvaluator::recordAppFps(int renderFrame, float fps, float frameTimeMs) {
    // オプションからオフなら停止
    if (!options.enableFpsLogging) return;

    FpsLog log;
    log.renderFrame = renderFrame;
    log.fps = fps;
    log.frameTimeMs = frameTimeMs;

    fpsLogs.push_back(log);  // 記録を追加
};

// 全記録ログのリセット
void MpmEvaluator::clearLogs() {
    perfLogs.clear();
    detailLogs.clear();
    fpsLogs.clear();
}

// --- 計測用タイマー ---
// タイマーの開始をセット
void MpmEvaluator::discardStepsAfter(int stepCount) {
    const auto later = [stepCount](const auto& row) { return row.stepFrame > stepCount; };
    perfLogs.erase(std::remove_if(perfLogs.begin(), perfLogs.end(), later), perfLogs.end());
    detailLogs.erase(std::remove_if(detailLogs.begin(), detailLogs.end(), later), detailLogs.end());
}

void MpmEvaluator::beginTimer(const std::string& passName) {
    if (!options.enableGpuTimer) return;

    if (timerQueries.find(passName) == timerQueries.end()) {
        GLuint queryId;
        glGenQueries(1, &queryId);
        timerQueries[passName] = queryId;
    }
    glBeginQuery(GL_TIME_ELAPSED, timerQueries[passName]);
}

// 開始されたタイマーの停止
void MpmEvaluator::endTimer(const std::string& passName) {
    if (!options.enableGpuTimer) return;
    glEndQuery(GL_TIME_ELAPSED);
}

/// 計測のメインステップ
/// @param [in] paticleVbo      粒子の VBO
/// @param [in] particleCount   定義されている全粒子数
/// @param [in] stepCount       現在のシミュレーションのステップ数
void MpmEvaluator::evaluateStep(GLuint particleVbo, int particleCount, int stepCount, float worldScale, double simulationTime, const MpmPhysics& physics) {
    // パフォーマンスログの回収（タイマー結果の取得）
    // 本来はパイプラインストールを防ぐため次フレームで取得すべきだが、確実性重視で同期取得
    PerformanceLog pLog;
    pLog.stepFrame = stepCount;
    pLog.simulationTime = simulationTime;
    pLog.timestep = physics.timestep;
    pLog.floorHeight = physics.f_height;
    pLog.floorNormal = physics.f_normal;
    pLog.timeSetup = 0.0;
    pLog.timeP2G = 0.0;
    pLog.timeGrid = 0.0;
    pLog.timeG2P = 0.0;
    pLog.timeComputeTotal = 0.0;

    if (options.enableGpuTimer && options.enableGpuTimerReadback) {
        auto getTimerMs = [&](const std::string& name) -> double {
            if (timerQueries.find(name) == timerQueries.end()) return 0.0;
            GLuint64 elapsed = 0;
            glGetQueryObjectui64v(timerQueries.at(name), GL_QUERY_RESULT, &elapsed);
            return elapsed / 1000000.0; // ナノ秒からミリ秒へ変換
        };

        pLog.timeSetup = getTimerMs("Setup");
        pLog.timeP2G = getTimerMs("P2G");
        pLog.timeGrid = getTimerMs("Grid");
        pLog.timeG2P = getTimerMs("G2P");
        pLog.timeComputeTotal = pLog.timeSetup + pLog.timeP2G + pLog.timeGrid + pLog.timeG2P;

        perfLogs.push_back(pLog);
    }

    // 詳細評価（指定インターバルごと）
    if (stepCount % std::clamp(options.stateEvaluationInterval, 1, 100) == 0) {
        const bool evaluateState = options.enableStateEvaluation && evalProgram != 0 && particleCount > 0;
        const bool readState = evaluateState && options.enableStateReadback;
        if (!evaluateState && !options.enableRepParticleReadback) return;

        MpmEvalResult result{};
        if (evaluateState) {
            result.minPosX = result.minPosY = result.minPosZ = (std::numeric_limits<GLuint>::max)();
            glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, evalSsbo);
            glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(result), &result);

            glUseProgram(evalProgram);
            glUniform1f(glGetUniformLocation(evalProgram, "worldScale"), worldScale);
            glUniform1ui(glGetUniformLocation(evalProgram, "particleCount"), particleCount);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, particleVbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, evalSsbo);
            glDispatchCompute((particleCount + 63) / 64, 1, 1);

            if (readState) {
                glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, evalSsbo);
                glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(result), &result);
            }
        }
        if (!readState && !options.enableRepParticleReadback) return;

        // --- 代表粒子の読み出し (VBOから直接) ---
        // ※ 読みだしで同期が発生、それが粒子数分繰り返すので性能に注意
        std::vector<RepParticleLog> reps;
        if (options.enableRepParticleReadback) {
            glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
            glBindBuffer(GL_ARRAY_BUFFER, particleVbo);
            for (int idx : repParticleIndices) {
                if (idx < 0 || idx >= particleCount) {
                    std::cerr << "Invalid representative particle index: " << idx << " / particleCount: " << particleCount << std::endl;
                    continue;
                }

                MpmParticle p;
                glGetBufferSubData(GL_ARRAY_BUFFER, idx * sizeof(MpmParticle), sizeof(MpmParticle), &p);

                RepParticleLog repLog;
                repLog.index = idx;
                repLog.position = glm::vec3(p.position);
                repLog.velocityNorm = glm::length(glm::vec3(p.velocity));
                repLog.q = p.q;
                repLog.state = p.state;
                reps.push_back(repLog);
            }
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }

        // --- 詳細ログの構築 ---
        DetailLog dLog{};
        dLog.hasState = readState;
        dLog.stepFrame = stepCount;
        dLog.simulationTime = simulationTime;
        dLog.timestep = physics.timestep;
        dLog.floorHeight = physics.f_height;
        dLog.floorNormal = physics.f_normal;
        dLog.totalParticleCount = particleCount;
        dLog.validCount = result.validParticleCount;
        dLog.aliveRatio = (particleCount > 0) ? (static_cast<double>(result.validParticleCount) / particleCount) : 0.0;
        dLog.nanPosCount = result.nanPositionCount;
        dLog.nanVelCount = result.nanVelocityCount;
        dLog.oobCount = result.oobCount;
        dLog.maxVelocity = glm::uintBitsToFloat(result.maxVelocityBits_GPU);
        dLog.repParticles = std::move(reps);

        if (readState && result.validParticleCount > 0) {
            dLog.aabbMin = glm::uintBitsToFloat(glm::uvec3(result.minPosX, result.minPosY, result.minPosZ));
            dLog.aabbMax = glm::uintBitsToFloat(glm::uvec3(result.maxPosX, result.maxPosY, result.maxPosZ));
            dLog.aabbExtent = dLog.aabbMax - dLog.aabbMin;
        }
        detailLogs.push_back(std::move(dLog));
    }
}

std::string MpmEvaluator::saveLogToCSV(const std::string& perfFilepath, const std::string& detailFilepath, const std::string& fpsFilepath) const {
    if (perfLogs.empty() && detailLogs.empty() && fpsLogs.empty()) return "No log data to save.";

    for (const auto* path : { perfLogs.empty() ? nullptr : &perfFilepath,
                             detailLogs.empty() ? nullptr : &detailFilepath,
                             fpsLogs.empty() ? nullptr : &fpsFilepath }) {
        if (!path) continue;
        std::error_code error;
        if (std::filesystem::exists(*path, error)) return "Log already exists: " + *path;
        if (error) return "Cannot check log path: " + *path;
    }

    auto openCsv = [](const std::string& path) {
        std::ofstream file(path);
        file.imbue(std::locale::classic());
        file << std::setprecision(std::numeric_limits<double>::max_digits10);
        return file;
    };

    if (!perfLogs.empty()) {
        auto file = openCsv(perfFilepath);
        file << "Step,SimulationTime(s),Dt(s),FloorHeight(m),FloorNormalX,FloorNormalY,FloorNormalZ,Setup(ms),P2G(ms),Grid(ms),G2P(ms),ComputeTotal(ms)\n";
        for (const auto& p : perfLogs) {
            file << p.stepFrame << "," << p.simulationTime << "," << p.timestep << "," << p.floorHeight
                << "," << p.floorNormal.x << "," << p.floorNormal.y << "," << p.floorNormal.z
                << "," << p.timeSetup << "," << p.timeP2G << ","
                << p.timeGrid << "," << p.timeG2P << "," << p.timeComputeTotal << "\n";
        }
        file.close();
        if (!file) return "Cannot write log: " + perfFilepath;
    }

    if (!detailLogs.empty()) {
        auto file = openCsv(detailFilepath);
        file << "Step,SimulationTime(s),Dt(s),FloorHeight(m),FloorNormalX,FloorNormalY,FloorNormalZ,TotalCount,ValidCount,AliveRatio,NonFinitePos,NonFiniteVel,OOB,MaxVel(m/s),"
            << "ExtentX(m),ExtentY(m),ExtentZ(m),"
            << "AABB_MinX(m),AABB_MinY(m),AABB_MinZ(m),AABB_MaxX(m),AABB_MaxY(m),AABB_MaxZ(m)";
        for (int index : repParticleIndices) {
            const std::string prefix = ",Rep" + std::to_string(index) + "_";
            file << prefix << "PosX(m)" << prefix << "PosY(m)" << prefix << "PosZ(m)"
                << prefix << "Vel(m/s)" << prefix << "State" << prefix << "Q";
        }
        file << "\n";

        for (const auto& d : detailLogs) {
            file << d.stepFrame << "," << d.simulationTime << "," << d.timestep << "," << d.floorHeight
                << "," << d.floorNormal.x << "," << d.floorNormal.y << "," << d.floorNormal.z
                << "," << d.totalParticleCount;
            if (d.hasState) {
                file << "," << d.validCount << "," << d.aliveRatio << ","
                    << d.nanPosCount << "," << d.nanVelCount << "," << d.oobCount;
            } else {
                file << ",,,,,";
            }
            if (d.hasState && d.validCount > 0) {
                file << "," << d.maxVelocity << "," << d.aabbExtent.x << "," << d.aabbExtent.y << ","
                    << d.aabbExtent.z << "," << d.aabbMin.x << "," << d.aabbMin.y << "," << d.aabbMin.z
                    << "," << d.aabbMax.x << "," << d.aabbMax.y << "," << d.aabbMax.z;
            } else {
                file << ",,,,,,,,,,";
            }

            for (int index : repParticleIndices) {
                auto rep = std::find_if(d.repParticles.begin(), d.repParticles.end(),
                    [index](const RepParticleLog& value) { return value.index == index; });
                if (rep != d.repParticles.end()) {
                    file << "," << rep->position.x << "," << rep->position.y << "," << rep->position.z
                        << "," << rep->velocityNorm << "," << rep->state << "," << rep->q;
                } else {
                    file << ",,,,,,";
                }
            }
            file << "\n";
        }
        file.close();
        if (!file) return "Cannot write log: " + detailFilepath;
    }

    if (!fpsLogs.empty()) {
        auto file = openCsv(fpsFilepath);
        file << "RenderFrame,Fps,FrameTime(ms)\n";
        for (const auto& f : fpsLogs) {
            file << f.renderFrame << "," << f.fps << "," << f.frameTimeMs << "\n";
        }
        file.close();
        if (!file) return "Cannot write log: " + fpsFilepath;
    }

    return {};
}
