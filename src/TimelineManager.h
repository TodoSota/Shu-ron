#pragma once
#define _USE_MATH_DEFINES	//M_PIなどを使用可能に
#define GLM_FORCE_RADIANS	//GLMの角度を度の単位でなくラジアンの単位に(もともと暗黙的で紛らわしいらしい)
#include <GL/glew.h>
#include <GLM/glm.hpp>
#include <GLM/gtc/quaternion.hpp>
#include <vector>
#include <cassert>
#include "sdf/SdfInstance.h"

/// @brief MPMシミュレーションの状態を時系列で記録・復元するタイムライン管理クラス
/// @note 物理フレーム : リングバッファにおける最新/最古とされる具体的な場所
/// @note 論理フレーム : 純粋な時間軸における情報(変換したものとして捉えて構わない)
class TimelineManager {
private:
    int maxFrames;      // リングバッファの要領
    int particleCount;  // シミュレーション内で扱う粒子数
    int sdfCount;       // シミュレーション内で存在するオブジェクト

    size_t particleBytes;   // 粒子1個のバイト数(GPUアクセス上)
    size_t frameStride;     // 1フレームでのデータサイズ(= particleBytes * particleCount)

    int currentFrameCount; // 現在記録されている有効フレーム数
    int headIndex;         // 次に recordFrame() が書き込む物理インデックス(最新は headIndex - 1)
    int playbackFrame;     // UI等で扱う論理フレーム位置 (0 〜 currentFrameCount - 1)

    GLuint recordProgram{ 0 };
    GLint writeOffsetLoc{ -1 };
    GLuint historyVbo;                      // GPU 上での記録媒体
    std::vector<SdfSnapshot> sdfHistory;    // CPU 上での記録媒体

    /// 論理フレーム番号を物理インデックスに変換
    int logicalToPhysicalFrame(int logicalFrame) const {
        assert(logicalFrame >= 0);
        assert(logicalFrame < currentFrameCount);

        return (headIndex - currentFrameCount + logicalFrame + maxFrames) % maxFrames;
    }

public:
    // コンストラクタ / デストラクタ
    /// @param [in] maxFrames リングバッファの容量
    /// @param [in] particleCount 粒子数
    /// @param [in] sdfCount 存在するオブジェクト
    TimelineManager(int maxFrames, int particleCount, int sdfCount = 1);
    ~TimelineManager();


    void recordFrame(GLuint sourceVbo, const SdfInstance* sdfs);
    void restoreToFrame(int logicalFrame, GLuint targetVbo, SdfInstance* sdfs);

    // ゲッター / セッター
    int getCurrentFrameCount() const { return currentFrameCount; }
    int getPlaybackFrame() const { return playbackFrame; }
    GLuint getHistoryVbo() const { return historyVbo; }
    void setPlaybackFrame(int frame) {
        assert(frame >= 0);
        assert(frame < currentFrameCount || currentFrameCount == 0);
        playbackFrame = frame;
    }

    // Play状態に戻す際など、再生ヘッドを最新フレームに合わせる
    void resetPlaybackToHead() {
        if (currentFrameCount == 0) {
            playbackFrame = 0;
        }
        else {
            playbackFrame = currentFrameCount - 1;
        }
    }
};