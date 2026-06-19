#pragma once

#include <vector>

#include <GL/glew.h>
#include <GLM/glm.hpp>

// 前方宣言
struct MpmObject;
class SDFInstance;
struct MpmPhysics;

// シミュレート対象・読み込みオブジェクト以外の全て
class Renderer {
private:
    // --- シェーダープログラム ---
    GLuint pointProgram{ 0 };
    GLuint meshProgram{ 0 };

    // --- Uniform ロケーションのキャッシュ ---
    GLint pointMcLoc{ -1 };
    GLint floorNormalLoc{ -1 };
    GLint floorHeightLoc{ -1 };
    GLint isFloorLoc{ -1 };
    GLint useDebugColorLoc{ -1 };

    GLint meshMcLoc{ -1 };
    GLint meshModelLoc{ -1 };

    // --- 床用バッファ ---
    GLuint floorVAO{ 0 }, floorVBO{ 0 };
    GLsizei floorCount{ 0 };

    // --- 境界線(Bounding Box)用バッファ ---
    GLuint boundaryVAO{ 0 }, boundaryVBO{ 0 }, boundaryEBO{ 0 };

    // --- プレビュー軌道用バッファ ---
    GLuint previewVAO{ 0 }, previewVBO{ 0 };

    // --- 内部初期化ヘルパー ---
    void initFloor();
    void initBoundary(float worldScale);
    void initPreview();
public:
    /// コンストラクタ（初期化をすべて行う）
    Renderer(float worldScale);

    /// デストラクタ（GPUリソースの解放）
    ~Renderer();

    // コピー禁止（OpenGLリソースの二重解放を防ぐため）
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // --- 描画メソッド ---

    // 床の描画
    void drawFloor(const glm::mat4& view, const glm::mat4& projection, const glm::mat4& model, const MpmPhysics& physics);

    // MPMパーティクルの描画
    void drawMpm(const MpmObject& MpmObject, const glm::mat4& view, const glm::mat4& projection, const glm::mat4& model, bool useDebugColor);

    // 障害物（SDFインスタンス）の描画
    void drawObstacle(const SDFInstance& obstacle, const glm::mat4& view, const glm::mat4& projection, const glm::mat4& model);

    // シミュレーション境界の描画
    void drawBoundary(const glm::mat4& view, const glm::mat4& projection, const glm::mat4& model);

    // スイング軌道・予測ベクトルの描画
    void drawPreview(const glm::mat4& view, const glm::mat4& projection, const glm::mat4& model, const std::vector<glm::vec3>& previewPoints);
};

