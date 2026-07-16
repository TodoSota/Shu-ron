#include "TimelineManager.h"
#include "mpm/MpmObject.h"

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
}

/// 1フレーム分のシミュレーションの状態を記録
/// @param [in] sourceVbo シミュレーションで使用している粒子のVBO
/// @param [in] sdfs シミュレーションで使用しているSDFオブジェクト
void TimelineManager::recordFrame(GLuint sourceVbo, const SdfInstance* sdfs) {
    assert(sdfs != nullptr);

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