#include "TimelineManager.h"
#include "core/Shader.h"
#include "mpm/MpmObject.h"
#include <GLFW/glfw3.h>

// コンストラクタ
/// @param [in] maxFrames リングバッファの容量
/// @param [in] particleCount 粒子数
/// @param [in] sdfCount 存在するオブジェクト
TimelineManager::TimelineManager(int maxFrames, int particleCount, int sdfCount)
    : maxFrames(maxFrames), particleCount(particleCount), sdfCount(sdfCount),
    currentFrameCount(0), headIndex(0), playbackFrame(0)
{
    // メンバ変数取得
    particleBytes = sizeof(MpmParticle);
    frameStride = particleBytes * static_cast<size_t>(particleCount);

    // コンピュートシェーダーのロード
    recordProgram = loadCompute("src/mpm/shaders/timeline_record.comp");

    // Uniform ロケーションの取得
    writeOffsetLoc = glGetUniformLocation(recordProgram, "writeOffset");

    // バッファ取得
    glGenBuffers(1, &historyVbo);
    assert(historyVbo != 0); // バッファ生成の成功チェック

    glBindBuffer(GL_ARRAY_BUFFER, historyVbo);
    glBufferData(GL_ARRAY_BUFFER, frameStride * static_cast<size_t>(maxFrames), nullptr, GL_DYNAMIC_COPY);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    sdfHistory.resize(static_cast<size_t>(maxFrames) * static_cast<size_t>(sdfCount));
}

/// デストラクタ
TimelineManager::~TimelineManager() {
    glDeleteBuffers(1, &historyVbo);
    glDeleteProgram(recordProgram);
}

/// 1フレーム分のシミュレーションの状態を記録
/// @param [in] sourceVbo シミュレーションで使用している粒子のVBO
/// @param [in] sdfs シミュレーションで使用しているSDFオブジェクト
void TimelineManager::recordFrame(GLuint sourceVbo, const SdfInstance* sdfs) {
    assert(sdfs != nullptr);

    // データバッファのバインド
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, sourceVbo);  // 計算用の VBO をセット
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, historyVbo); // 結果用の VBO をセット
    
    int particleGroups = (particleCount + 63) / 64; // パーティクル数用のグループ数
    // [record_Frame] VBO の記録
    glUseProgram(recordProgram);
    glUniform1ui(writeOffsetLoc, static_cast<GLuint>(headIndex * particleCount)); // Uniform 変数の送信
    glDispatchCompute(particleGroups, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    // バインドの解除
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);  // 計算用の VBO
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, 0); // 結果用の VBO

    /*
    // オフセット計算を size_t で(64bit環境でも稼働するため)
    size_t writeOffset = static_cast<size_t>(headIndex) * frameStride;

    glBindBuffer(GL_COPY_READ_BUFFER, sourceVbo);
    glBindBuffer(GL_COPY_WRITE_BUFFER, historyVbo);
    
    // size_t を GLintptr と GLsizeiptr へキャストして、バッファ内容をコピー
    glCopyBufferSubData(
        GL_COPY_READ_BUFFER,
        GL_COPY_WRITE_BUFFER,
        0,
        static_cast<GLintptr>(writeOffset),
        static_cast<GLsizeiptr>(frameStride)
    );

    // 書き込み対象をバインド
    glBindBuffer(GL_COPY_READ_BUFFER, 0);
    glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
    */
    
    // SDF状態の記録
    for (int i = 0; i < sdfCount; ++i) {
        int index = headIndex * sdfCount + i;
        sdfHistory[index] = sdfs[i].captureSnapshot();
    }

    headIndex = (headIndex + 1) % maxFrames;
    if (currentFrameCount < maxFrames) {
        currentFrameCount++;
    }

    // 録画中は常に再生ヘッドを論理的な最新フレームにしておく
    resetPlaybackToHead();
}

/// 記録済みの任意のフレームへ状態を復元
/// @param [in] logicalFrame 論理フレーム
/// @param [in] targetVbo 書き込み先となるシミュレーションで使用している粒子のVBO
/// @param [in] sdfs シミュレーションで使用しているSDFオブジェクト
void TimelineManager::restoreToFrame(int logicalFrame, GLuint targetVbo, SdfInstance* sdfs) {
    assert(sdfs != nullptr);
    assert(logicalFrame >= 0 && logicalFrame < currentFrameCount);

    int physicalIndex = logicalToPhysicalFrame(logicalFrame);

    // オフセット計算を size_t で
    size_t readOffset = static_cast<size_t>(physicalIndex) * frameStride;

    // 書き込み対象をバインド
    glBindBuffer(GL_COPY_READ_BUFFER, historyVbo);
    glBindBuffer(GL_COPY_WRITE_BUFFER, targetVbo);

    // size_t を GLintptr と GLsizeiptr へキャストして、バッファ内容をコピー
    glCopyBufferSubData(
        GL_COPY_READ_BUFFER,
        GL_COPY_WRITE_BUFFER,
        static_cast<GLintptr>(readOffset),
        0,
        static_cast<GLsizeiptr>(frameStride)
    );

    glBindBuffer(GL_COPY_READ_BUFFER, 0);
    glBindBuffer(GL_COPY_WRITE_BUFFER, 0);

    // SDFをシミュレーションでの状態へ復元
    for (int i = 0; i < sdfCount; ++i) {
        int index = physicalIndex * sdfCount + i;
        sdfs[i].restoreSnapshot(sdfHistory[index]);
    }
}