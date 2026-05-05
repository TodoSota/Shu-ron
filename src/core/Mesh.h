#pragma once
#include <GL/glew.h>
#include <GLM/glm.hpp>
#include <vector>

// 頂点データ構造
struct Vertex {
    glm::vec3 position; // 位置
    glm::vec3 normal;   // 法線
};

// メッシュ管理用オブジェクト
struct MeshObject {
    GLuint vao, vbo, ebo;
    GLsizei indexCount;

    /// UV球（緯度・経度ベースの球体）を3角メッシュにて生成するコンストラクタ
    /// @param[in] sectors 緯度
    /// @param[in] stacks  経度
    MeshObject(int sectors = 32, int stacks = 16) {
        std::vector<Vertex> vertices;
        std::vector<GLuint> indices;

        float sectorStep = 2.0f * 3.14159265359f / sectors;
        float stackStep = 3.14159265359f / stacks;

        for (int i = 0; i <= stacks; ++i) {
            float stackAngle = 3.14159265359f / 2.0f - i * stackStep;
            float xy = cos(stackAngle);
            float z = sin(stackAngle);

            for (int j = 0; j <= sectors; ++j) {
                float sectorAngle = j * sectorStep;
                float x = xy * cos(sectorAngle);
                float y = xy * sin(sectorAngle);
                // 半径1の球なので、位置がそのまま法線になる
                vertices.push_back({ glm::vec3(x, y, z), glm::vec3(x, y, z) });
            }
        }

        // 3角形リストを作成
        for (int i = 0; i < stacks; ++i) {
            int k1 = i * (sectors + 1);
            int k2 = k1 + sectors + 1;
            for (int j = 0; j < sectors; ++j, ++k1, ++k2) {
                // 頂点と底はまた別の処理 : EBO 用のデータを生成
                if (i != 0) {
                    indices.push_back(k1); indices.push_back(k2); indices.push_back(k1 + 1);
                }
                if (i != (stacks - 1)) {
                    indices.push_back(k1 + 1); indices.push_back(k2); indices.push_back(k2 + 1);
                }
            }
        }
        indexCount = static_cast<GLsizei>(indices.size());

        // VAO にアクセス。もろもろ紐づけてGPUに送信
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ebo);

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLuint), indices.data(), GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));

        glBindVertexArray(0);
    }

    ~MeshObject() {
        glDeleteVertexArrays(1, &vao);
        glDeleteBuffers(1, &vbo);
        glDeleteBuffers(1, &ebo);
    }
};