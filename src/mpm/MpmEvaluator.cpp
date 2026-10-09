#include "MpmEvaluator.h"
#include "../core/Shader.h"
#include "../core/Errorcheck.h"
#include <iostream>
#include <fstream>
#include <cmath>

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
void MpmEvaluator::evaluateStep(GLuint particleVbo, int particleCount, int stepCount) {
    // パフォーマンスログの回収（タイマー結果の取得）
    // 本来はパイプラインストールを防ぐため次フレームで取得すべきだが、確実性重視で同期取得
    PerformanceLog pLog;
    pLog.stepFrame = stepCount;
    pLog.timeSetup = 0.0;
    pLog.timeP2G = 0.0;
    pLog.timeGrid = 0.0;
    pLog.timeG2P = 0.0;
    pLog.timeComputeTotal = 0.0;

    if (options.enableGpuTimerReadback) {
        auto getTimerMs = [&](const std::string& name) -> double {
            if (timerQueries.find(name) == timerQueries.end()) return 0.0;
            GLuint64 elapsed = 0;
            glGetQueryObjectui64v(timerQueries[name], GL_QUERY_RESULT, &elapsed);
            return elapsed / 1000000.0; // ナノ秒からミリ秒へ変換
        };

        pLog.timeSetup = getTimerMs("Setup");
        pLog.timeP2G = getTimerMs("P2G");
        pLog.timeGrid = getTimerMs("Grid");
        pLog.timeG2P = getTimerMs("G2P");
        pLog.timeComputeTotal = pLog.timeSetup + pLog.timeP2G + pLog.timeGrid + pLog.timeG2P;

        // タイマー計測自体が ON なら記録 / OFF なら 0 で記録される
        if (options.enableGpuTimer) {
            perfLogs.push_back(pLog);
        }
    }

    // 詳細評価（指定インターバルごと）
    if (stepCount % options.stateEvaluationInterval == 0) {
        // --- GPU側のSSBOを初期化 ---
        MpmEvalResult zeroData = { 0 };
        zeroData.minPosX = zeroData.minPosY = zeroData.minPosZ = 2147483647;  // int の MAX
        zeroData.maxPosX = zeroData.maxPosY = zeroData.maxPosZ = -2147483647; // int の MIN

        // Comp Shader による状態評価実行
        if (options.enableStateEvaluation) {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, evalSsbo);
            glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(MpmEvalResult), &zeroData);

            // --- 評価シェーダーのディスパッチ ---
            glUseProgram(evalProgram);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, particleVbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, evalSsbo);

            int numGroups = (particleCount + 63) / 64;
            glDispatchCompute(numGroups, 1, 1);

            // GPUの書き込み完了を待機
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        }

        // --- 結果の読み出し (SSBO) ---
        MpmEvalResult result = zeroData;
        if (options.enableStateReadback) {
            glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(MpmEvalResult), &result);
        }
        
        // --- 代表粒子の読み出し (VBOから直接) ---
        // ※ 読みだしで同期が発生、それが粒子数分繰り返すので性能に注意
        std::vector<RepParticleLog> reps;
        if (options.enableRepParticleReadback) {
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
                repLog.deltaQ = p.q;
                repLog.state = p.state;
                reps.push_back(repLog);
            }
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }

        // --- 詳細ログの構築 ---
        DetailLog dLog;
        dLog.stepFrame = stepCount;
        dLog.totalParticleCount = particleCount;
        dLog.validCount = result.validParticleCount;
        dLog.aliveRatio = (particleCount > 0) ? (static_cast<double>(result.validParticleCount) / particleCount) : 0.0;
        dLog.nanPosCount = result.nanPositionCount;
        dLog.nanVelCount = result.nanVelocityCount;
        dLog.oobCount = result.oobCount;
        dLog.maxVelocity = glm::uintBitsToFloat(result.maxVelocityBits_GPU);
        dLog.repParticles = std::move(reps);

        // 固定小数点からの復元 (SCALE = 100.0)
        double scale = 100.0;
        if (result.validParticleCount > 0) {
            dLog.aabbMin = glm::vec3(result.minPosX / scale, result.minPosY / scale, result.minPosZ / scale);
            dLog.aabbMax = glm::vec3(result.maxPosX / scale, result.maxPosY / scale, result.maxPosZ / scale);
            dLog.aabbExtent = dLog.aabbMax - dLog.aabbMin;
        }
        else {
            dLog.aabbMin = glm::vec3(0.0f);
            dLog.aabbMax = glm::vec3(0.0f);
            dLog.aabbExtent = glm::vec3(0.0f);
        }

        // いずれかの評価ログフラグが立っている場合のみ保存
        if (options.enableStateEvaluation || options.enableRepParticleReadback) {
            detailLogs.push_back(dLog);
        }
    }
}

/// CSVへの書き出し
/// @param [in] perfFilepath   性能の書き出し先 
/// @param [in] detailFilepath 詳細の書き出し先
/// @param [in] fpsFilepath    速度の書き出し先
void MpmEvaluator::saveLogToCSV(const std::string& perfFilepath, const std::string& detailFilepath, const std::string& fpsFilepath) const {
    std::cout << "Evaluation logs saved to ";

    // パフォーマンスログ出力
    if (options.enableGpuTimerReadback) {
        std::ofstream perfFile(perfFilepath);
        if (perfFile.is_open()) {
            perfFile << "Frame,Setup(ms),P2G(ms),Grid(ms),G2P(ms),ComputeTotal(ms)\n";
            for (const auto& p : perfLogs) {
                perfFile << p.stepFrame << "," << p.timeSetup << "," << p.timeP2G << ","
                    << p.timeGrid << "," << p.timeG2P << "," << p.timeComputeTotal << "\n";
            }
        }
        std::cout << perfFilepath << " , ";
    }

    // 詳細ログ出力
    if (options.enableStateReadback) {
        std::ofstream detailFile(detailFilepath);
        if (detailFile.is_open()) {
            // ヘッダー作成
            detailFile << "StepFrame,TotalCount,ValidCount,AliveRatio,NaN_Pos,NaN_Vel,OOB,MaxVel,"
                << "ExtentX,ExtentY,ExtentZ,"
                << "AABB_MinX,AABB_MinY,AABB_MinZ,AABB_MaxX,AABB_MaxY,AABB_MaxZ";

            for (size_t i = 0; i < repParticleIndices.size(); i++) {
                std::string prefix = ",Rep" + std::to_string(repParticleIndices[i]) + "_";
                detailFile << prefix << "PosX" << prefix << "PosY" << prefix << "PosZ"
                    << prefix << "Vel" << prefix << "State" << prefix << "Q";
            }
            detailFile << "\n";

            // データ書き込み
            for (const auto& d : detailLogs) {
                detailFile << d.stepFrame << "," << d.totalParticleCount << "," << d.validCount << "," << d.aliveRatio << ","
                    << d.nanPosCount << "," << d.nanVelCount << "," << d.oobCount << "," << d.maxVelocity << ","
                    << d.aabbExtent.x << "," << d.aabbExtent.y << "," << d.aabbExtent.z << ","
                    << d.aabbMin.x << "," << d.aabbMin.y << "," << d.aabbMin.z << ","
                    << d.aabbMax.x << "," << d.aabbMax.y << "," << d.aabbMax.z;

                for (const auto& rep : d.repParticles) {
                    detailFile << "," << rep.position.x << "," << rep.position.y << "," << rep.position.z
                        << "," << rep.velocityNorm << "," << rep.state << "," << rep.deltaQ;
                }
                detailFile << "\n";
            }
        }
        std::cout << detailFilepath << " , ";
    }

    // FPS ログ出力
    if (options.enableFpsLogging) {
        if (!fpsLogs.empty()) {
            std::ofstream fpsFile(fpsFilepath);
            if (fpsFile.is_open()) {
                fpsFile << "RenderFrame,Fps,FrameTime(ms)\n";
                for (const auto& f : fpsLogs) {
                    fpsFile << f.renderFrame << "," << f.fps << "," << f.frameTimeMs << "\n";
                }
            }
        }
        std::cout << fpsFilepath << std::endl;
    }
}