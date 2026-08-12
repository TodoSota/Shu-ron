#pragma once

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#define _USE_MATH_DEFINES	//M_PIなどを使用可能に
#define GLM_FORCE_RADIANS	//GLMの角度を度の単位でなくラジアンの単位に(もともと暗黙的で紛らわしいらしい)
#include <GLM/glm.hpp>
#include <GLM/gtc/quaternion.hpp>

#include <vector>

// 前方宣言
class Window;
struct Camera;
class SdfInstance;

// スイングの状態
enum class SwingState {
    None,
    Dragging,
    Swinging
};

// 記録用のアクションタイプ
enum class LastActionType {
    None,
    Shoot,
    Swing
};

class InteractionController {
private:
    // --- モード管理 ---
    int currentMode{ 0 }; // 0: Camera, 1: Shoot, 2: Swing

    // --- 入力状態 ---
    int lastMouseState{ GLFW_RELEASE };

    // --- スイングモード用パラメータ ---
    SwingState swingState{ SwingState::None };
    glm::dvec2 dragStartPos{ 0.0, 0.0 };
    glm::vec3 swingPivot{ 0.0f };
    glm::vec3 swingAxis{ 1.0f, 0.0f, 0.0f };

    float swingMaxAngle{ 0.0f };
    float swingCurrentTime{ 0.0f };

    // スイングの固定パラメータ（UIで調整可能にするならpublicにするかセッターを設ける）
    float swingRadius{ 0.4f };
    float swingSpeedMult{ 6.0f };
    float maxPixelDrag{ 300.0f }; // 最大威力に必要なドラッグ量
    float pivotDepth{ 1.0f };     // 支点の奥行き(カメラから)

    // 記録用パラメータ
    LastActionType lastAction{ LastActionType::None };

    // Shootの記録データ
    glm::vec3 lastShootPos{ 0.0f };
    glm::vec3 lastShootVel{ 0.0f };

    // Swingの記録データ
    glm::vec3 lastSwingPivot{ 0.0f };
    glm::vec3 lastSwingAxis{ 1.0f, 0.0f, 0.0f };
    float lastSwingMaxAngle{ 0.0f };

    // --- 内部処理メソッド ---

    /// Shoot モードでの処理
    /// @param[in] window 画面の主体。マウスの状態、クリック位置などを保持
    /// @param[in] camera view や projection などの行列を保持
    /// @param[in] obstacle 動きの主体となるオブジェクトのデータ
    void handleShootMode(Window& window, const Camera& camera, SdfInstance& obstacle);
    /// Swing モードでの処理
    /// @param[in] window 画面の主体。マウスの状態、クリック位置などを保持
    /// @param[in] camera view や projection などの行列を保持
    /// @param[in] obstacle 動きの主体となるオブジェクトのデータ
    /// @param[in] dt シミュレーション内の 1 ステップの長さ
    void handleSwingMode(Window& window, const Camera& camera, SdfInstance& obstacle, float dt);

public:
    InteractionController() = default;
    ~InteractionController() = default;

    // 毎フレーム呼ばれる更新処理
    void update(Window& window, const Camera& camera, SdfInstance& obstacle, float dt);

    // ImGuiと連携するためのモードGetter/Setter
    int getMode() const { return currentMode; }
    void setMode(int mode) { currentMode = mode; }

    // スイング中（ドラッグ中）かどうか
    bool isDragging() const { return swingState == SwingState::Dragging; }

    /// Renderer に渡すプレビュー用の頂点データを生成(計算)して返す
    /// @param[in] obstacle スイングの主体となる物体のデータ
    std::vector<glm::vec3> calcPreviewPoints(const SdfInstance& obstacle) const;

    /// 最後に実行したアクションを再発火
    /// @param[in] obstacle パラメータを適用するSDFオブジェクト
    void fireLastAction(SdfInstance& obstacle);

    /// 再生可能なログがあるか確認
    bool hasLastAction() const { return lastAction != LastActionType::None; }
};