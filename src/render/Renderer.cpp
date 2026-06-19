#include "Renderer.h"

#include "../core/Camera.h"
#include "../core/shader.h"
#include "../mpm/MpmObject.h"
#include "../mpm/MpmPhysics.h"
#include "../sdf/SDFInstance.h"

#include <GLM/gtc/type_ptr.hpp>

/// コンストラクタ（初期化をすべて行う）
Renderer::Renderer(float worldScale) {
    // シェーダーファイルのロード
    pointProgram = loadProgram("src/render/point.vert", "src/render/point.frag");
    meshProgram = loadProgram("src/render/mesh.vert", "src/render/mesh.frag");

    // Uniform ロケーションの事前取得とキャッシュ
    pointMcLoc = glGetUniformLocation(pointProgram, "mc");
    floorNormalLoc = glGetUniformLocation(pointProgram, "floor_normal");
    floorHeightLoc = glGetUniformLocation(pointProgram, "floor_height");
    isFloorLoc = glGetUniformLocation(pointProgram, "is_floor");
    useDebugColorLoc = glGetUniformLocation(pointProgram, "use_debug_color");
    meshMcLoc = glGetUniformLocation(meshProgram, "mc");
    meshModelLoc = glGetUniformLocation(meshProgram, "model");

    // バッファの初期化
    initFloor();                // シミュレーション領域の床
    initBoundary(worldScale);   // シミュレーション領域境界
    initPreview();              // Swingモードでのプレビュー表示
}

/// デストラクタ（GPUリソースの解放）
Renderer::~Renderer() {
    glDeleteVertexArrays(1, &floorVAO); glDeleteBuffers(1, &floorVBO);
    glDeleteVertexArrays(1, &boundaryVAO); glDeleteBuffers(1, &boundaryVBO); glDeleteBuffers(1, &boundaryEBO);
    glDeleteVertexArrays(1, &previewVAO); glDeleteBuffers(1, &previewVBO);
    glDeleteProgram(pointProgram);
    glDeleteProgram(meshProgram);
}


// --- 内部初期化ヘルパー ---
void Renderer::initFloor() {
    const auto GRID_SIZE = 20;
    floorCount = GRID_SIZE * GRID_SIZE;

    // 床の形になるように粒子を配置
    std::vector<glm::vec4> floorPositions(floorCount);
    for (int i = 0; i < GRID_SIZE; i++) {
        for (int j = 0; j < GRID_SIZE; j++) {
            float x = (i - GRID_SIZE / 2) * 0.2f;
            float z = (j - GRID_SIZE / 2) * 0.2f;
            floorPositions[i * GRID_SIZE + j] = glm::vec4(x, 0.0f, z, 1.0f);
        }
    }

    glGenVertexArrays(1, &floorVAO);
    glGenBuffers(1, &floorVBO);
    glBindVertexArray(floorVAO);
    glBindBuffer(GL_ARRAY_BUFFER, floorVBO);
    glBufferData(GL_ARRAY_BUFFER, floorPositions.size() * sizeof(glm::vec4), floorPositions.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0); // position
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(glm::vec4), (void*)0);
    glBindVertexArray(0);
}

void Renderer::initBoundary(float worldScale) {
    glGenVertexArrays(1, &boundaryVAO);
    glGenBuffers(1, &boundaryVBO);
    glGenBuffers(1, &boundaryEBO);

    // 1辺が worldScale の立方体状に粒子を配置・辺を繋ぐ(12本の辺 x 2頂点)
    glm::vec3 vertices[] = {
        {0, 0, 0}, {worldScale, 0, 0}, {worldScale, 0, worldScale}, {0, 0, worldScale},
        {0, worldScale, 0}, {worldScale, worldScale, 0}, {worldScale, worldScale, worldScale}, {0, worldScale, worldScale}
    };
    GLuint indices[] = {
        0, 1, 1, 2, 2, 3, 3, 0,
        4, 5, 5, 6, 6, 7, 7, 4,
        0, 4, 1, 5, 2, 6, 3, 7
    };

    glBindVertexArray(boundaryVAO);
    glBindBuffer(GL_ARRAY_BUFFER, boundaryVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, boundaryEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    glBindVertexArray(0);
}

void Renderer::initPreview() {
    glGenVertexArrays(1, &previewVAO);
    glGenBuffers(1, &previewVBO);

    glBindVertexArray(previewVAO);
    glBindBuffer(GL_ARRAY_BUFFER, previewVBO);
     //最大100頂点分のvec3データを格納・動的に書き換えるため GL_DYNAMIC_DRAW
    glBufferData(GL_ARRAY_BUFFER, sizeof(glm::vec3) * 100, nullptr, GL_DYNAMIC_DRAW);  
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
    glBindVertexArray(0);
}


// --- パブリック描画メソッド ---

// 床の描画
void Renderer::drawFloor(const glm::mat4& view, const glm::mat4& projection, const glm::mat4& model, const MpmPhysics& physics) {
    glUseProgram(pointProgram);

    glm::mat4 mvp = projection * view * model; // アスペクト比は呼び出し元で補正推奨
    glUniformMatrix4fv(pointMcLoc, 1, GL_FALSE, glm::value_ptr(mvp));
    glUniform3fv(floorNormalLoc, 1, glm::value_ptr(physics.f_normal));
    glUniform1f(floorHeightLoc, physics.f_height);

    glUniform1i(glGetUniformLocation(pointProgram, "is_floor"), 1); // 地面フラグON

    glBindVertexArray(floorVAO);
    glDrawArrays(GL_POINTS, 0, floorCount);
    glBindVertexArray(0);
}

// MPMパーティクルの描画
void Renderer::drawMpm(const MpmObject& MpmObject, const glm::mat4& view, const glm::mat4& projection, const glm::mat4& model, bool useDebugColor) {
    glUseProgram(pointProgram);

    glm::mat4 mvp = projection * view * model; // アスペクト比は呼び出し元で補正推奨
    glUniformMatrix4fv(pointMcLoc, 1, GL_FALSE, glm::value_ptr(mvp));

    glUniform1i(isFloorLoc, 0); // 地面フラグOFF
    glUniform1i(useDebugColorLoc, useDebugColor ? 1 : 0);

    glBindVertexArray(MpmObject.vao);
    glDrawArrays(GL_POINTS, 0, MpmObject.count);
    glBindVertexArray(0);
}

// 障害物（SDFインスタンス）の描画
void Renderer::drawObstacle(const SDFInstance& obstacle, const glm::mat4& view, const glm::mat4& projection, const glm::mat4& model) {
    glUseProgram(meshProgram);

    glm::mat4 finalModel = model * obstacle.getModelMatrix();
    glm::mat4 mvp = projection * view * finalModel; // アスペクト比は呼び出し元で補正推奨

    glUniformMatrix4fv(meshMcLoc, 1, GL_FALSE, glm::value_ptr(mvp));
    glUniformMatrix4fv(meshModelLoc, 1, GL_FALSE, glm::value_ptr(finalModel));

    glBindVertexArray(obstacle.resource->vao);
    glDrawElements(GL_TRIANGLES, obstacle.resource->indexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

// シミュレーション境界の描画
void Renderer::drawBoundary(const glm::mat4& view, const glm::mat4& projection, const glm::mat4& model) {
    glUseProgram(meshProgram);

    glm::mat4 mvp = projection * view * model; // アスペクト比は呼び出し元で補正推奨
    glUniformMatrix4fv(meshMcLoc, 1, GL_FALSE, glm::value_ptr(mvp));
    glUniformMatrix4fv(meshModelLoc, 1, GL_FALSE, glm::value_ptr(model));

    glBindVertexArray(boundaryVAO);
    glDrawElements(GL_LINES, 24, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

// スイング軌道・予測ベクトルの描画
void Renderer::drawPreview(const glm::mat4& view, const glm::mat4& projection, const glm::mat4& model, const std::vector<glm::vec3>& previewPoints) {
    if (previewPoints.empty()) return;

    glBindBuffer(GL_ARRAY_BUFFER, previewVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, previewPoints.size() * sizeof(glm::vec3), previewPoints.data());

    glUseProgram(meshProgram);
    glm::mat4 mvp = projection * view * model; // アスペクト比は呼び出し元で補正推奨
    glUniformMatrix4fv(meshMcLoc, 1, GL_FALSE, glm::value_ptr(mvp));
    glUniformMatrix4fv(meshModelLoc, 1, GL_FALSE, glm::value_ptr(model));

    int segments = 30; // 軌道分割数（mainと合わせる）
    glBindVertexArray(previewVAO);
    glDrawArrays(GL_LINE_STRIP, 0, segments + 1);             // 軌道
    glDrawArrays(GL_LINES, segments + 1, 2);                  // 速度ベクトル
    glDrawArrays(GL_LINES, segments + 3, 6);                  // 支点マーカー
    glBindVertexArray(0);
}