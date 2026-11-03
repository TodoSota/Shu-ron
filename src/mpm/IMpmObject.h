#pragma once
#include <GL/glew.h>

class IMpmObject {
protected: // 派生クラスのみアクセス
    // --- 描画コンテナ ---
    GLuint vao[2]{ 0, 0 };
    GLuint vbo[2]{ 0, 0 };
    int readBuffer{ 0 };    // 対象指定のインデックス

    // --- シミュレーション状況設定 ---
    const GLsizei particleCount;    // 頂点数
    const int gridSize;             // グリッドの分割数

    /// 基底クラスにおけるコンストラクタ
    /// @brief 共通の VAO/VBO 生成を行う
    /// @param [in] count 頂点数
    /// @param [in] gridSize グリッドの分割数
    IMpmObject(GLsizei count, int gridSize);

public:
    /// 基底クラスにおけるデストラクタ
    /// @brief 共通の VAO/VBO 破棄を行う
    virtual ~IMpmObject();

    // コピー・ムーブを禁止
    IMpmObject(const IMpmObject&) = delete;
    IMpmObject& operator=(const IMpmObject&) = delete;

    // --- 共通のゲッター ---
    GLuint getReadVbo() const { return vbo[readBuffer]; }
    GLuint getWriteVbo() const { return vbo[1 - readBuffer]; }
    GLuint getRenderVao() const { return vao[readBuffer]; }
    GLsizei getParticleCount() const { return particleCount; }
    int getGridSize() const { return gridSize; }

    void swapBuffers() { readBuffer = 1 - readBuffer; }
};