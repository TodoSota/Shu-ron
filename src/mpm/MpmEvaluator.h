#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <map>
#include "MpmObject.h" 

// --- Evaluator の機能制御オプション ---
struct EvaluatorOptions {
    bool enableGpuTimer = false;            // GPU Timer Query の実行
    bool enableGpuTimerReadback = false;    // GPU Timer Query 結果の CPU 同期取得
    bool enableStateEvaluation = false;     // GPU 状態評価 Comp Shader の実行
    bool enableStateReadback = false;       // GPU 状態評価結果の SSBO 読み戻し
    bool enableRepParticleReadback = false; // 代表粒子の VBO 読み戻し
    bool enableFpsLogging = false;          // アプリケーションの FPS 記録
    int stateEvaluationInterval = 1;        // 状態評価と代表粒子を実行する間隔
};

// --- GPU側から受け取る集計データ ---
struct MpmEvalResult {
    GLuint validParticleCount;      // 正常に生存している粒子数
    GLuint nanPositionCount;
    GLuint nanVelocityCount;
    GLuint oobCount;                // oob : Out of Bounds
    GLuint maxVelocityBits_GPU;     // GPU 内での速度表現

    // AABB用（スケール済み）
    int minPosX, minPosY, minPosZ;
    int maxPosX, maxPosY, maxPosZ;

    GLuint padding[3];
};

// --- 代表粒子のステータス ---
struct RepParticleLog {
    int index;
    glm::vec3 position;
    float velocityNorm;
    float deltaQ;
    int state;
};

// --- 毎フレームの性能ログ ---
struct PerformanceLog {
    int stepFrame;              // シミュレーションのステップ数
    double timeSetup;
    double timeP2G;
    double timeGrid;
    double timeG2P;
    double timeComputeTotal;    // 上記4つ合計
};

// --- アプリケーション全体の FPS ログ ---
struct FpsLog {
    int renderFrame;  // 描画フレーム数
    float fps;
    float frameTimeMs;
};

// --- 詳細評価ログ（インターバルごと） ---
struct DetailLog {
    int stepFrame;          // シミュレーションのステップ数
    unsigned int totalParticleCount;// 評価対象の全粒子数
    unsigned int validCount;// 正常な粒子数
    double aliveRatio;      // validCount / totalParticleCount

    unsigned int nanPosCount;
    unsigned int nanVelCount;
    unsigned int oobCount;  // oob : Out of Bounds
    float maxVelocity;      // GPU から取得した bits を float へ復元したもの

    // glm::vec3 centerOfMass; // 重心位置・除外中
    glm::vec3 aabbMin;
    glm::vec3 aabbMax;
    glm::vec3 aabbExtent;

    std::vector<RepParticleLog> repParticles;
};

class MpmEvaluator {
private:
    EvaluatorOptions options;

    std::map<std::string, GLuint> timerQueries;

    GLuint evalProgram; // 評価用 Comp Shader
    GLuint evalSsbo;    // 評価用 Comp Shader の SSBO

    std::vector<int> repParticleIndices;// 代表粒子のインデックス

    // 各種結果ログ
    std::vector<PerformanceLog> perfLogs;
    std::vector<DetailLog> detailLogs;
    std::vector<FpsLog> fpsLogs;

public:
    MpmEvaluator();
    ~MpmEvaluator();

    // --- オプション制御 ---
    void setOptions(const EvaluatorOptions& newOptions) { options = newOptions; }
    EvaluatorOptions& getOptions() { return options; }
    const EvaluatorOptions& getOptions() const { return options; }

    // 代表粒子の登録
    void addRepresentativeParticle(int index);

    // FPS 記録 : main.cpp より毎フレーム呼び出し想定
    void recordAppFps(int renderFrame, float fps, float frameTimeMs);

    // 全記録ログのリセット
    void clearLogs();

    // --- 計算時間計測タイマー ---
    void beginTimer(const std::string& passName);
    void endTimer(const std::string& passName);

    /// 計測のメインステップ
    /// @param [in] particleVbo     粒子の VBO
    /// @param [in] particleCount   定義されている全粒子数
    /// @param [in] stepCount       現在のシミュレーションのステップ数
    void evaluateStep(GLuint particleVbo, int particleCount, int stepCount);

    /// CSVへの書き出し
    /// @param [in] perfFilepath   性能の書き出し先 
    /// @param [in] detailFilepath 詳細の書き出し先
    /// @param [in] fpsFilepath    速度の書き出し先
    void saveLogToCSV(const std::string& perfFilepath, const std::string& detailFilepath, const std::string& fpsFilepath) const;
};