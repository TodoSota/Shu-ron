//==============================================================================
// TimelineManager.cpp : タイムラインの記録 (GPU→GPU コピー) と復元
//==============================================================================
#include "TimelineManager.h"
#include "../core/Shader.h"
#include "../mpm/MpmParticle.h"
#include <algorithm>
#include <iostream>
#include <limits>

// コンストラクタ
/// @param [in] requestedFrames リングバッファに要求する最大フレーム数
/// @param [in] particleCount 粒子数
/// @param [in] sdfCount 存在するオブジェクト
TimelineManager::TimelineManager(int requestedFrames, int particleCount, int sdfCount)
    : maxFrames(0), particleCount(particleCount), sdfCount(sdfCount),
      frameStride(0),
      currentFrameCount(0), headIndex(0), playbackFrame(0)
{
    if (requestedFrames <= 0 || particleCount <= 0 || sdfCount <= 0) return;

    constexpr GLint64 historyBudget = 128LL * 1024 * 1024;
    GLint64 maxBlockBytes = 0;
    glGetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &maxBlockBytes);
    const auto maxBytes = (std::min)({ historyBudget, maxBlockBytes,
        static_cast<GLint64>((std::numeric_limits<GLsizeiptr>::max)()) });
    const auto bytesPerFrame = static_cast<GLint64>(sizeof(MpmParticle)) * particleCount;
    if (bytesPerFrame > maxBytes) return;
    frameStride = static_cast<size_t>(bytesPerFrame);
    int frames = (std::min)(requestedFrames, static_cast<int>(maxBytes / frameStride));
    if (frames <= 0) return;

    stateHistory.resize(frames);
    sdfHistory.resize(static_cast<size_t>(frames) * sdfCount);
    recordProgram = loadCompute("src/mpm/shaders/timeline_record.comp");
    if (recordProgram == 0) return;
    writeOffsetLoc = glGetUniformLocation(recordProgram, "writeOffset");

    glGenBuffers(1, &historyVbo);
    if (historyVbo == 0) return;
    glBindBuffer(GL_ARRAY_BUFFER, historyVbo);
    while (frames > 0) {
        const auto bytes = static_cast<GLsizeiptr>(frameStride * frames);
        glBufferData(GL_ARRAY_BUFFER, bytes, nullptr, GL_DYNAMIC_COPY);
        GLint64 allocated = 0;
        glGetBufferParameteri64v(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &allocated);
        if (allocated == bytes) {
            maxFrames = frames;
            break;
        }
        std::cerr << "Timeline allocation failed (" << bytes << " bytes, GL error "
                  << glGetError() << "); trying fewer frames.\n";
        frames /= 2;
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    sdfHistory.resize(static_cast<size_t>(maxFrames) * sdfCount);
}

/// デストラクタ
TimelineManager::~TimelineManager() {
    glDeleteBuffers(1, &historyVbo);
    glDeleteProgram(recordProgram);
}

/// 1フレーム分のシミュレーションの状態を記録
/// @param [in] sourceVbo シミュレーションで使用している粒子のVBO
/// @param [in] sdfs シミュレーションで使用しているSDFオブジェクト
void TimelineManager::recordFrame(GLuint sourceVbo, const SdfInstance* sdfs, const TimelineState& state) {
    if (maxFrames == 0) return;
    assert(sdfs != nullptr);

    // データバッファのバインド
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, sourceVbo);  // 計算用の VBO をセット
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, historyVbo); // 結果用の VBO をセット
    
    int particleGroups = (particleCount + 63) / 64; // パーティクル数用のグループ数
    // [record_Frame] VBO の記録
    glUseProgram(recordProgram);
    glUniform1ui(writeOffsetLoc, static_cast<GLuint>(headIndex * particleCount)); // Uniform 変数の送信
    glDispatchCompute(particleGroups, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);

    // バインドの解除
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);  // 計算用の VBO
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, 0); // 結果用の VBO

    stateHistory[headIndex] = state;

    // SDF状態の記録
    for (int i = 0; i < sdfCount; ++i) {
        int index = headIndex * sdfCount + i;
        sdfHistory[index] = sdfs[i].captureSnapshot();
    }

    // リングバッファの書き込み位置を進める (満杯になったら最古のフレームを上書きする)
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
void TimelineManager::restoreToFrame(int logicalFrame, GLuint targetVbo, SdfInstance* sdfs, TimelineState* state) {
    if (maxFrames == 0) return;
    assert(sdfs != nullptr);
    assert(logicalFrame >= 0 && logicalFrame < currentFrameCount);

    int physicalIndex = logicalToPhysicalFrame(logicalFrame);

    if (state) *state = stateHistory[physicalIndex];

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
void TimelineManager::discardFuture() {
    if (currentFrameCount == 0 || playbackFrame == currentFrameCount - 1) return;
    headIndex = (logicalToPhysicalFrame(playbackFrame) + 1) % maxFrames;
    currentFrameCount = playbackFrame + 1;
}
