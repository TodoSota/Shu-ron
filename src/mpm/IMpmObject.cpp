#include "IMpmObject.h"
#include "MpmParticle.h"

#include <iostream>

/// 基底クラスにおけるコンストラクタ
/// @brief 共通の VAO/VBO 生成を行う
/// @param [in] count 頂点数
/// @param [in] gridSize グリッドの分割数
IMpmObject::IMpmObject(GLsizei count, int gridSize)
    : particleCount(count), gridSize(gridSize)
{
    glGenVertexArrays(2, vao);
    glGenBuffers(2, vbo);

    // 直前の無関係なエラーをクリア
    while (glGetError() != GL_NO_ERROR) {}

    for (int i = 0; i < 2; i++) {
        glBindVertexArray(vao[i]);
        glBindBuffer(GL_ARRAY_BUFFER, vbo[i]);
        glBufferData(GL_ARRAY_BUFFER, sizeof(MpmParticle) * count, nullptr, GL_DYNAMIC_DRAW);

        glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(MpmParticle), (void*)offsetof(MpmParticle, position));
        glEnableVertexAttribArray(0);
        glVertexAttribIPointer(1, 1, GL_INT, sizeof(MpmParticle), (void*)offsetof(MpmParticle, state));
        glEnableVertexAttribArray(1);
    }
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

/// 基底クラスにおけるデストラクタ
/// @brief 共通の VAO/VBO 破棄を行う
IMpmObject::~IMpmObject() {
    glDeleteVertexArrays(2, vao);
    glDeleteBuffers(2, vbo);
}