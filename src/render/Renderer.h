#pragma once

//==============================================================================
// Renderer.h : 描画処理 (MVC の View)
// 目的: 粒子、床、障害物メッシュ、領域の境界 (立方体の枠線)、Swing の予測軌道を描く。
//       物理計算の状態は変更せず、粒子バッファを読むだけにしている。
//
// 描画モード (RenderSettings::mode):
//   Points  : 従来の点描画 (drawFloor / drawObstacle / drawMpm)
//   Spheres : drawScene() による deferred shading
//             (A) シャドウマップ → (B) G-buffer → (F) SSAO → (G) 陰影の計算
//   Surface : Spheres の (B) と (F) の間に、粒子の面を張る処理 (D)(E) を入れる
//             (パスの名前は docs/rendering_guide.md の図に対応する)
//==============================================================================

#include <vector>
#include <memory>

#include <GL/glew.h>
#include <GLM/glm.hpp>

#include "RenderSettings.h"
#include "RenderTarget.h"
#include "ShadowMap.h"
#include "SsaoPass.h"
#include "ScreenSpaceSurface.h"
#include "PassTimer.h"

// 前方宣言
struct MpmObject;
class SdfInstance;
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

    // --- 陰影付き描画 (Spheres モード) 用 ---
    GLuint sphereProgram{ 0 };      // 球インポスタ (G-buffer とシャドウマップで共用)
    GLuint gbufferMeshProgram{ 0 }; // 床・障害物を G-buffer へ
    GLuint depthMeshProgram{ 0 };   // 障害物をシャドウマップへ (深度のみ)
    GLuint lightingProgram{ 0 };    // 陰影の計算 (全画面)
    GLuint emptyVAO{ 0 };           // 全画面の三角形用 (頂点属性なし)
    GLuint floorQuadVAO{ 0 }, floorQuadVBO{ 0 };    // 床の四角形 (位置 + 法線)

    RenderTarget gbuffer;           // location 0: albedo + 種別 (RGBA8), 1: 法線 (RGBA16F), 2: 粒子の線形深度 (R32F), 深度 (D32F)
    ShadowMap shadowMap;
    std::unique_ptr<SsaoPass> ssao;
    std::unique_ptr<ScreenSpaceSurface> surface;
    PassTimer timer;
    float worldScale{ 1.0f };

    // --- 内部初期化ヘルパー ---
    void initFloor();
    void initBoundary(float worldScale);
    void initPreview();
    void initDeferred();

    // --- Spheres モードの描画ヘルパー ---
    void drawSpheres(const MpmObject& mpmObject, const glm::mat4& view, const glm::mat4& projection, float viewportHeight, bool useDebugColor);
    float drawRadius() const;       // 現在のモードでの球の描画半径 [m]
    void updateFloorQuad(const MpmPhysics& physics);
    void drawMeshTo(GLuint program, const SdfInstance& obstacle, const glm::mat4& view, const glm::mat4& projection);
public:
    /// 描画の設定 (UI から変更する)
    RenderSettings settings;

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
    void drawObstacle(const SdfInstance& obstacle, const glm::mat4& view, const glm::mat4& projection, const glm::mat4& model);

    // シミュレーション境界の描画
    void drawBoundary(const glm::mat4& view, const glm::mat4& projection, const glm::mat4& model);

    // スイング軌道・予測ベクトルの描画
    void drawPreview(const glm::mat4& view, const glm::mat4& projection, const glm::mat4& model, const std::vector<glm::vec3>& previewPoints);

    /// 陰影付きでシーン (床・障害物・粒子) を描く (Spheres / Surface モード)
    /// 画面 (既定のフレームバッファ) に色と深度を書くので、この後に枠線などを重ねて描ける
    /// @param[in] framebufferWidth, framebufferHeight 画面のフレームバッファの大きさ [px]
    void drawScene(const MpmObject& mpmObject, const SdfInstance& obstacle, const MpmPhysics& physics,
        const glm::mat4& view, const glm::mat4& projection, int framebufferWidth, int framebufferHeight, bool useDebugColor);

    /// 描画設定の UI (ImGui) を表示する
    void drawSettingsUi();

    /// 従来の点描画モードかどうか
    bool usesPointMode() const { return settings.mode == RenderMode::Points; }
};

