//==============================================================================
// Renderer.cpp : 描画用シェーダーとバッファの準備、および各要素の描画
//==============================================================================
#include "Renderer.h"

#include "../core/Camera.h"
#include "../core/shader.h"
#include "../mpm/MpmObject.h"
#include "../mpm/MpmPhysics.h"
#include "../sdf/SdfInstance.h"

#include <GLM/gtc/type_ptr.hpp>
#include <cmath>

#include "../../ImGui/imgui.h"

/// コンストラクタ（初期化をすべて行う）
Renderer::Renderer(float worldScale) : worldScale(worldScale) {
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
    initDeferred();             // 陰影付き描画 (Spheres モード) のシェーダーとバッファ
}

/// デストラクタ（GPUリソースの解放）
Renderer::~Renderer() {
    glDeleteVertexArrays(1, &floorVAO); glDeleteBuffers(1, &floorVBO);
    glDeleteVertexArrays(1, &boundaryVAO); glDeleteBuffers(1, &boundaryVBO); glDeleteBuffers(1, &boundaryEBO);
    glDeleteVertexArrays(1, &previewVAO); glDeleteBuffers(1, &previewVBO);
    glDeleteProgram(pointProgram);
    glDeleteProgram(meshProgram);
    glDeleteProgram(sphereProgram);
    glDeleteProgram(gbufferMeshProgram);
    glDeleteProgram(depthMeshProgram);
    glDeleteProgram(lightingProgram);
    glDeleteVertexArrays(1, &emptyVAO);
    glDeleteVertexArrays(1, &floorQuadVAO); glDeleteBuffers(1, &floorQuadVBO);
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

    // 描画用バッファを SSBO (binding = 0) としてバインドする
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, MpmObject.getReadVbo());

    glBindVertexArray(MpmObject.getRenderVao());
    glDrawArrays(GL_POINTS, 0, MpmObject.count);
    glBindVertexArray(0);
}

// 障害物（SDFインスタンス）の描画
void Renderer::drawObstacle(const SdfInstance& obstacle, const glm::mat4& view, const glm::mat4& projection, const glm::mat4& model) {
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

//==============================================================================
// 陰影付き描画 (Spheres モード)
//==============================================================================

// uniform を設定する小さなヘルパー (呼び出し回数が少ないので、ロケーションは毎回問い合わせる)
namespace {
    void setMat4(GLuint prog, const char* name, const glm::mat4& m) { glUniformMatrix4fv(glGetUniformLocation(prog, name), 1, GL_FALSE, glm::value_ptr(m)); }
    void setVec3(GLuint prog, const char* name, const glm::vec3& v) { glUniform3fv(glGetUniformLocation(prog, name), 1, glm::value_ptr(v)); }
    void setFloat(GLuint prog, const char* name, float v) { glUniform1f(glGetUniformLocation(prog, name), v); }
    void setInt(GLuint prog, const char* name, int v) { glUniform1i(glGetUniformLocation(prog, name), v); }

    // 互換プロファイルでは、gl_PointCoord を使うために GL_POINT_SPRITE を有効にする必要がある
    // (コアプロファイルでは常に有効で、この列挙子自体がない)
    constexpr GLenum kPointSprite = 0x8861;
    bool isCompatibilityProfile() {
        GLint mask = 0;
        glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &mask);
        return (mask & GL_CONTEXT_CORE_PROFILE_BIT) == 0;
    }
}

void Renderer::initDeferred() {
    sphereProgram = loadProgram("src/render/shaders/sphere.vert", "src/render/shaders/sphere.frag");
    gbufferMeshProgram = loadProgram("src/render/shaders/gbuffer_mesh.vert", "src/render/shaders/gbuffer_mesh.frag");
    depthMeshProgram = loadProgram("src/render/shaders/gbuffer_mesh.vert", "src/render/shaders/depth_only.frag");
    lightingProgram = loadProgram("src/render/shaders/fullscreen.vert", "src/render/shaders/lighting.frag");

    glGenVertexArrays(1, &emptyVAO);

    // 床の四角形 (4 頂点 × (位置 + 法線))。頂点の位置は床の高さ・法線に合わせて毎フレーム更新する
    glGenVertexArrays(1, &floorQuadVAO);
    glGenBuffers(1, &floorQuadVBO);
    glBindVertexArray(floorQuadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, floorQuadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(glm::vec3) * 8, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3) * 2, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3) * 2, (void*)sizeof(glm::vec3));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    ssao = std::make_unique<SsaoPass>();
    surface = std::make_unique<ScreenSpaceSurface>();
    dust = std::make_unique<DustSystem>();
    splat = std::make_unique<SplatSurface>();
}

// 床の平面 n·x = h 上に、領域を十分に覆う四角形を置く
void Renderer::updateFloorQuad(const MpmPhysics& physics) {
    glm::vec3 n = physics.f_normal;
    const float len2 = glm::dot(n, n);
    if (len2 < 1e-8f) n = glm::vec3(0, 1, 0);
    const glm::vec3 nn = glm::normalize(n);
    // 平面上で領域の中心の真下にある点
    const glm::vec3 c(0.5f * worldScale, 0.0f, 0.5f * worldScale);
    const glm::vec3 origin = c - nn * ((glm::dot(n, c) - physics.f_height) / glm::dot(n, nn));
    // 平面内の直交する 2 方向
    const glm::vec3 helper = (std::abs(nn.x) < 0.9f) ? glm::vec3(1, 0, 0) : glm::vec3(0, 0, 1);
    const glm::vec3 t = glm::normalize(glm::cross(helper, nn));
    const glm::vec3 b = glm::cross(nn, t);
    const float e = 6.0f * worldScale;	// 四角形の半分の大きさ
    // GL_TRIANGLE_STRIP の順 (床は両面とも描くので、向きは問わない)
    const glm::vec3 v[8] = {
        origin - t * e - b * e, nn,  origin - t * e + b * e, nn,
        origin + t * e - b * e, nn,  origin + t * e + b * e, nn,
    };
    glBindBuffer(GL_ARRAY_BUFFER, floorQuadVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(v), v);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// 現在のモードでの球の描画半径 (Surface モードでは、すき間を埋めるため大きくする)
float Renderer::drawRadius() const {
    const float r = settings.particleRadius();
    return (settings.mode == RenderMode::Surface) ? r * settings.surfaceRadiusBoost : r;
}

// 粒子を球インポスタで描く (G-buffer とシャドウマップの両方で使う)
void Renderer::drawSpheres(const MpmObject& mpmObject, const glm::mat4& view, const glm::mat4& projection, float viewportHeight, bool useDebugColor, bool surfelView) {
    glUseProgram(sphereProgram);
    setMat4(sphereProgram, "view", view);
    setMat4(sphereProgram, "projection", projection);
    setFloat(sphereProgram, "pointRadius", drawRadius());
    setFloat(sphereProgram, "viewportHeight", viewportHeight);
    setVec3(sphereProgram, "sandColor", settings.sandColor);
    setVec3(sphereProgram, "stoneColor", settings.stoneColor);
    setFloat(sphereProgram, "colorVariation", settings.colorVariation);
    setInt(sphereProgram, "useDebugColor", useDebugColor ? 1 : 0);
    // Splat モード段階 1: surfel バッファ (SplatSurface::prepare の結果) を読んで色を決める
    const bool useSurfel = surfelView && splat && splat->surfelBuffer() != 0;
    setInt(sphereProgram, "surfelDebug", useSurfel ? static_cast<int>(settings.surfelDebug) : 0);
    setInt(sphereProgram, "hideInterior", (useSurfel && settings.hideInterior) ? 1 : 0);
    setFloat(sphereProgram, "surfaceCutoff", settings.surfaceCutoff);
    setFloat(sphereProgram, "rhoRef", splat ? splat->referenceDensity() : 1.0f);
    if (useSurfel) glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SplatSurface::kSurfelBinding, splat->surfelBuffer());

    // 点の大きさをシェーダーで決め、点スプライトを有効にする
    const bool compat = isCompatibilityProfile();
    const GLboolean smoothWasOn = glIsEnabled(GL_POINT_SMOOTH);
    glEnable(GL_PROGRAM_POINT_SIZE);
    if (compat) { glEnable(kPointSprite); glDisable(GL_POINT_SMOOTH); }

    // 材質フラグを読むため、粒子バッファを SSBO としても束縛する
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, mpmObject.getReadVbo());
    glBindVertexArray(mpmObject.getRenderVao());
    glDrawArrays(GL_POINTS, 0, mpmObject.count);
    glBindVertexArray(0);

    glDisable(GL_PROGRAM_POINT_SIZE);
    if (compat) { glDisable(kPointSprite); if (smoothWasOn) glEnable(GL_POINT_SMOOTH); }
}

// 障害物のメッシュを、指定したプログラム (G-buffer 用 / 深度のみ) で描く
void Renderer::drawMeshTo(GLuint program, const SdfInstance& obstacle, const glm::mat4& view, const glm::mat4& projection) {
    glUseProgram(program);
    setMat4(program, "model", obstacle.getModelMatrix());
    setMat4(program, "view", view);
    setMat4(program, "projection", projection);
    glBindVertexArray(obstacle.resource->vao);
    glDrawElements(GL_TRIANGLES, obstacle.resource->indexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

void Renderer::drawScene(const MpmObject& mpmObject, const SdfInstance& obstacle, const MpmPhysics& physics,
    const glm::mat4& view, const glm::mat4& projection, int fbWidth, int fbHeight, bool useDebugColor) {
    if (fbWidth <= 0 || fbHeight <= 0) return;	// 最小化中
    timer.beginFrame();

    // G-buffer を画面の大きさに合わせる (初回は作成)
    if (gbuffer.framebuffer() == 0) {
        gbuffer.create(fbWidth, fbHeight, {
            { GL_RGBA8,   GL_RGBA, GL_UNSIGNED_BYTE, GL_NEAREST },	// albedo + 種別
            { GL_RGBA16F, GL_RGBA, GL_FLOAT,         GL_NEAREST },	// ビュー空間の法線
            { GL_R32F,    GL_RED,  GL_FLOAT,         GL_NEAREST },	// 粒子の線形深度 (0 = 粒子なし)
        }, true);
    }
    else {
        gbuffer.resize(fbWidth, fbHeight);
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    // --- (S) Splat モード: 密度格子で表面粒子と法線を求める ---
    const bool splatMode = (settings.mode == RenderMode::Splat);
    if (splatMode) {
        timer.begin("Grid");
        splat->prepare(mpmObject, worldScale, settings);
        timer.end();
    }

    // --- (A) シャドウマップ: 光源から見た深度 ---
    const glm::vec3 sunDir = settings.sunDirection();
    if (settings.shadows) {
        timer.begin("Shadow");
        shadowMap.ensure(settings.shadowResolution);
        shadowMap.update(sunDir, worldScale);
        shadowMap.bind();
        glClear(GL_DEPTH_BUFFER_BIT);
        glDisable(GL_CULL_FACE);	// 障害物の裏面も影を落とすようにする
        drawSpheres(mpmObject, shadowMap.getView(), shadowMap.getProjection(), static_cast<float>(shadowMap.getResolution()), false);
        drawMeshTo(depthMeshProgram, obstacle, shadowMap.getView(), shadowMap.getProjection());
        glEnable(GL_CULL_FACE);
        timer.end();
    }

    // --- (B) G-buffer: カメラから見た色・法線・深度 ---
    timer.begin("G-buffer");
    gbuffer.bind();
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);	// 種別 0 = 背景、線形深度 0 = 粒子なし
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 床
    updateFloorQuad(physics);
    glUseProgram(gbufferMeshProgram);
    setMat4(gbufferMeshProgram, "model", glm::mat4(1.0f));
    setMat4(gbufferMeshProgram, "view", view);
    setMat4(gbufferMeshProgram, "projection", projection);
    setVec3(gbufferMeshProgram, "albedo", settings.floorColor);
    setFloat(gbufferMeshProgram, "kind", 0.5f);
    setInt(gbufferMeshProgram, "twoSided", 1);
    glDisable(GL_CULL_FACE);
    glBindVertexArray(floorQuadVAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glEnable(GL_CULL_FACE);

    // 障害物
    glUseProgram(gbufferMeshProgram);
    setVec3(gbufferMeshProgram, "albedo", settings.obstacleColor);
    setFloat(gbufferMeshProgram, "kind", 0.75f);
    setInt(gbufferMeshProgram, "twoSided", 0);
    drawMeshTo(gbufferMeshProgram, obstacle, view, projection);

    // 粒子
    // Splat モードでは、粒子は (P) で円盤として描く (Splatting を切ると球で描く: 段階 1 の表示)
    const bool splatting = splatMode && settings.splatting;
    if (!splatting) drawSpheres(mpmObject, view, projection, static_cast<float>(fbHeight), useDebugColor, splatMode);
    timer.end();

    // --- (P) Splat モード: 表面粒子の円盤を重ねて描き、G-buffer に書き込む ---
    if (splatting) {
        timer.begin("Splat");
        splat->draw(mpmObject, gbuffer, view, projection, useDebugColor, settings, emptyVAO);
        timer.end();
    }

    // --- (D)(E) Surface モード: 粒子の深度を平滑化して面を張り、G-buffer の法線と深度を置き換える ---
    if (settings.mode == RenderMode::Surface) {
        timer.begin("Surface");
        surface->run(gbuffer, projection, view, drawRadius(), settings, emptyVAO);
        timer.end();
    }

    // --- (F) SSAO ---
    GLuint aoTex = 0;
    if (settings.ssao) {
        timer.begin("SSAO");
        aoTex = ssao->run(gbuffer.depthTexture(), gbuffer.colorTexture(1), fbWidth, fbHeight,
            projection, settings.ssaoRadiusScale * drawRadius(), settings, emptyVAO);
        timer.end();
    }

    // --- (G) 陰影の計算: 画面へ色と深度を出力する ---
    timer.begin("Lighting");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, fbWidth, fbHeight);
    glDepthFunc(GL_ALWAYS);	// G-buffer の深度をそのまま画面の深度バッファへ書く
    glUseProgram(lightingProgram);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, gbuffer.colorTexture(0));
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, gbuffer.colorTexture(1));
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, gbuffer.depthTexture());
    glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, aoTex);
    glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, shadowMap.depthTexture());
    setInt(lightingProgram, "gAlbedo", 0);
    setInt(lightingProgram, "gNormal", 1);
    setInt(lightingProgram, "gDepth", 2);
    setInt(lightingProgram, "aoTex", 3);
    setInt(lightingProgram, "shadowTex", 4);
    setMat4(lightingProgram, "invProjection", glm::inverse(projection));
    setMat4(lightingProgram, "invView", glm::inverse(view));
    setMat4(lightingProgram, "lightViewProjection", shadowMap.getViewProjection());
    setVec3(lightingProgram, "sunDirection", sunDir);
    setVec3(lightingProgram, "sunColor", settings.sunColor);
    setFloat(lightingProgram, "sunIntensity", settings.sunIntensity);
    setVec3(lightingProgram, "skyColor", settings.skyColor);
    setVec3(lightingProgram, "groundColor", settings.groundColor);
    setFloat(lightingProgram, "ambientIntensity", settings.ambientIntensity);
    setFloat(lightingProgram, "exposure", settings.exposure);
    setInt(lightingProgram, "useShadow", (settings.shadows && shadowMap.depthTexture() != 0) ? 1 : 0);
    setInt(lightingProgram, "useAO", (settings.ssao && aoTex != 0) ? 1 : 0);
    setFloat(lightingProgram, "shadowBias", settings.shadowBias);
    // Surface モードでは、平滑化した面が球の頂点より低くなり、シャドウマップ上の球に自分で影を
    // 落としてしまう (谷が黒い斑点になる)。そのため、粒子の画素だけ参照位置を法線方向へずらす。
    // Splat モードの円盤は粒子の中心を通り、さらに重み付き平均で少し奥に下がるので、
    // 描画半径の 3 倍ずらす (2 倍では斑点が残った)
    const float particleOffset = (settings.mode == RenderMode::Surface) ? drawRadius() : splatting ? 3.0f * drawRadius() : 0.0f;
    setFloat(lightingProgram, "shadowNormalOffset", settings.shadowNormalOffset);
    setFloat(lightingProgram, "particleShadowOffset", particleOffset);
    // Splat モードの surfel のデバッグ色は、陰影を付けずに表示する (Final のときは Albedo 表示に切り替える)
    const bool surfelColors = splatMode && settings.surfelDebug != SurfelDebug::Off;
    setInt(lightingProgram, "debugView", (surfelColors && settings.debugView == 0) ? 1 : settings.debugView);
    glBindVertexArray(emptyVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    timer.end();

    // --- (H) 粉塵: 陰影を付けた画像の上に、半透明で重ねる ---
    if (settings.dust) {
        timer.begin("Dust");
        dust->draw(gbuffer.depthTexture(), fbWidth, fbHeight, view, projection,
            settings.shadows ? shadowMap.depthTexture() : 0, shadowMap.getViewProjection(),
            settings.particleRadius(), settings, emptyVAO);
        timer.end();
    }

    // 後始末 (この後に描く枠線・プレビュー・ImGui が従来どおり描けるように戻す)
    for (int unit = 4; unit >= 0; --unit) { glActiveTexture(GL_TEXTURE0 + unit); glBindTexture(GL_TEXTURE_2D, 0); }
    glDepthFunc(GL_LESS);
    glUseProgram(0);
    glClearColor(0.8f, 0.8f, 0.8f, 1.0f);	// main.cpp の背景色に戻す (Points モード用)
}

void Renderer::updateDust(const MpmObject& mpmObject, const MpmPhysics& physics, float dt) {
    dust->update(mpmObject, physics, dt, settings.particleRadius(), settings);
}

void Renderer::clearDust() {
    dust->clear();
}

void Renderer::drawSettingsUi() {
    if (!ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen)) return;

    int mode = static_cast<int>(settings.mode);
    ImGui::RadioButton("Points", &mode, 0); ImGui::SameLine();
    ImGui::RadioButton("Spheres", &mode, 1); ImGui::SameLine();
    ImGui::RadioButton("Surface", &mode, 2); ImGui::SameLine();
    ImGui::RadioButton("Splat", &mode, 3);
    settings.mode = static_cast<RenderMode>(mode);
    if (settings.mode == RenderMode::Points) return;

    if (settings.mode == RenderMode::Surface) {
        // 面を張る処理の調整 (平滑化を強くすると滑らかに、弱くすると粒状になる)
        ImGui::SliderFloat("Surface radius boost", &settings.surfaceRadiusBoost, 1.0f, 2.5f);
        ImGui::SliderFloat("Smooth radius", &settings.smoothRadiusScale, 0.5f, 8.0f);
        ImGui::SliderInt("Smooth iterations", &settings.smoothIterations, 0, 4);
        ImGui::SliderFloat("Grain", &settings.grainStrength, 0.0f, 1.0f);
        ImGui::SliderFloat("Sphere normal mix", &settings.sphereNormalMix, 0.0f, 1.0f);
    }

    if (settings.mode == RenderMode::Splat) {
        // 表面粒子の円盤 (段階 2) と、密度格子による表面粒子の抽出・法線 (段階 1)
        ImGui::Checkbox("Splatting", &settings.splatting);
        if (settings.splatting) {
            ImGui::SliderFloat("Splat radius", &settings.splatRadiusScale, 0.8f, 3.0f);
            ImGui::SliderFloat("Splat sharpness", &settings.splatSharpness, 0.5f, 6.0f);
            ImGui::SliderFloat("Splat depth offset", &settings.splatDepthOffsetScale, 0.1f, 1.5f);
        } else {
            ImGui::SameLine();
            ImGui::Checkbox("Hide interior", &settings.hideInterior);
        }
        int debug = static_cast<int>(settings.surfelDebug);
        ImGui::Combo("Surfel view", &debug, "Off\0" "Surface\0" "Normal\0" "Density\0");
        settings.surfelDebug = static_cast<SurfelDebug>(debug);
        ImGui::SliderFloat("Cell scale", &settings.densityCellScale, 1.0f, 3.0f);
        ImGui::SliderFloat("Density blur", &settings.densityBlurSigma, 0.0f, 3.0f);
        ImGui::SliderFloat("Outside offset", &settings.outsideOffsetScale, 0.5f, 2.0f);
        ImGui::SliderFloat("Surface lo", &settings.surfaceLo, 0.0f, 1.0f);
        ImGui::SliderFloat("Surface hi", &settings.surfaceHi, 0.0f, 1.5f);
        if (settings.surfaceHi < settings.surfaceLo + 0.01f) settings.surfaceHi = settings.surfaceLo + 0.01f;
        ImGui::Text("Grid: %d^3, cell %.1f mm", splat->gridSize(), splat->cellSize() * 1000.0f);
    }

    ImGui::SliderFloat("Radius scale", &settings.radiusScale, 0.5f, 3.0f);
    ImGui::Text("Particle radius: %.2f mm", settings.particleRadius() * 1000.0f);
    ImGui::Checkbox("Shadows", &settings.shadows); ImGui::SameLine();
    ImGui::Checkbox("SSAO", &settings.ssao);
    ImGui::SliderFloat("Sun azimuth", &settings.sunAzimuth, -180.0f, 180.0f);
    ImGui::SliderFloat("Sun elevation", &settings.sunElevation, 5.0f, 90.0f);
    ImGui::SliderFloat("Sun intensity", &settings.sunIntensity, 0.0f, 8.0f);
    ImGui::SliderFloat("Ambient", &settings.ambientIntensity, 0.0f, 2.0f);
    ImGui::SliderFloat("Exposure", &settings.exposure, 0.2f, 3.0f);
    ImGui::ColorEdit3("Sand color", &settings.sandColor[0]);
    ImGui::SliderFloat("Color variation", &settings.colorVariation, 0.0f, 0.4f);
    ImGui::Checkbox("Dust", &settings.dust);
    if (settings.dust && ImGui::TreeNode("Dust settings")) {
        ImGui::SliderFloat("Emit rate", &settings.dustEmitRate, 0.0f, 0.2f, "%.3f");
        ImGui::SliderFloat("Speed threshold [m/s]", &settings.dustSpeedThreshold, 0.1f, 3.0f);
        ImGui::SliderFloat("Near floor [m]", &settings.dustNearFloor, 0.0f, 0.5f);
        ImGui::SliderFloat("Lifetime [s]", &settings.dustLifetime, 0.5f, 8.0f);
        ImGui::SliderFloat("Size", &settings.dustSize, 0.5f, 8.0f);
        ImGui::SliderFloat("Growth", &settings.dustGrowth, 0.0f, 5.0f);
        ImGui::SliderFloat("Opacity", &settings.dustOpacity, 0.0f, 0.5f);
        ImGui::SliderFloat("Drag", &settings.dustDrag, 0.1f, 10.0f);
        ImGui::SliderFloat("Turbulence", &settings.dustTurbulence, 0.0f, 1.0f);
        ImGui::ColorEdit3("Dust color", &settings.dustColor[0]);
        if (ImGui::Button("Clear dust")) clearDust();
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Advanced")) {
        ImGui::SliderFloat("AO radius scale", &settings.ssaoRadiusScale, 1.0f, 10.0f);
        ImGui::SliderFloat("AO power", &settings.ssaoPower, 0.5f, 4.0f);
        ImGui::SliderFloat("Shadow bias", &settings.shadowBias, 0.0f, 0.01f, "%.4f");
        ImGui::SliderFloat("Shadow normal offset", &settings.shadowNormalOffset, 0.0f, 0.02f, "%.4f");
        int resIndex = (settings.shadowResolution >= 2048) ? 1 : 0;
        if (ImGui::Combo("Shadow map", &resIndex, "1024\0" "2048\0")) settings.shadowResolution = resIndex ? 2048 : 1024;
        ImGui::SliderFloat("Smooth depth threshold", &settings.smoothDepthScale, 0.5f, 6.0f);
        ImGui::SliderFloat("Grain size", &settings.grainScale, 0.25f, 4.0f);
        ImGui::Combo("Debug view", &settings.debugView, "Final\0" "Albedo\0" "Normal\0" "AO\0" "Shadow\0");
        ImGui::TreePop();
    }
    // 描画パスごとの GPU 時間
    for (const auto& r : timer.results()) ImGui::Text("  %-9s %6.2f ms", r.first.c_str(), r.second);
}
