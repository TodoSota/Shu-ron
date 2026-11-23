//==============================================================================
// InteractionController.cpp : Shoot / Swing の入力処理と運動学的な軌道の計算
// レイの生成: スクリーン座標 → NDC → 逆射影でビュー空間 → 逆ビューでワールド空間、
//             カメラ位置からその点への方向をレイとする。
//==============================================================================
#include "InteractionController.h"
#include "../core/Window.h"
#include "../core/Camera.h"
#include "../sdf/SDFInstance.h"
#include "../mpm/MpmPhysics.h"
#include <algorithm>

// ImGui の状態取得用（UI操作中は弾くため）
#include "../../ImGui/imgui.h" 

namespace {
void advanceShoot(SdfInstance& obstacle, bool& floorEnabled, bool& boundaryContact,
    float dt, const MpmPhysics& physics, float worldScale) {
    if (dt <= 0.0f || !obstacle.resource || obstacle.resource->vertices.empty()) return;

    // Shoot keeps its launch orientation and scale. Mesh vertices are independent
    // of the padding used for SDF sampling.
    const glm::vec3 normal = physics.f_normal;
    const auto& vertices = obstacle.resource->vertices;
    const glm::vec3 firstOffset = obstacle.rotation * (obstacle.scale * vertices.front().position);
    glm::vec3 low = firstOffset, high = firstOffset;
    float floorSupport = glm::dot(firstOffset, normal);
    for (const auto& vertex : vertices) {
        const glm::vec3 offset = obstacle.rotation * (obstacle.scale * vertex.position);
        low = glm::min(low, offset);
        high = glm::max(high, offset);
        floorSupport = (std::min)(floorSupport, glm::dot(offset, normal));
    }
    const float gap = glm::dot(obstacle.position, normal) + floorSupport - physics.f_height;
    constexpr float contactTolerance = 1e-5f;
    floorEnabled = floorEnabled || gap >= -contactTolerance;

    // Preserve the aiming ray until a static surface constrains the shot.
    if (boundaryContact) obstacle.velocity += physics.gravity * dt;
    glm::vec3 displacement = obstacle.velocity * dt;
    const float normalSpeed = glm::dot(obstacle.velocity, normal);
    if (floorEnabled && normalSpeed < 0.0f && gap + normalSpeed * dt <= contactTolerance) {
        boundaryContact = true;
        const float hitTime = std::clamp(gap / -normalSpeed, 0.0f, dt);
        constexpr float bounce = 0.25f;
        constexpr float bounceThreshold = 0.25f; // m/s
        const float rebound = -normalSpeed >= bounceThreshold ? -bounce * normalSpeed : 0.0f;
        const glm::vec3 tangent = obstacle.velocity - normalSpeed * normal;
        const float tangentSpeed = glm::length(tangent);
        const float friction = physics.f_friction * (rebound - normalSpeed);
        const glm::vec3 sliding = tangentSpeed > friction ? tangent * (1.0f - friction / tangentSpeed) : glm::vec3(0.0f);
        displacement = obstacle.velocity * hitTime + sliding * (dt - hitTime);
        obstacle.velocity = sliding + rebound * normal;
        displacement += rebound * normal * (dt - hitTime);
    }

    // Clip the travel along each outward axis, leaving wall-parallel motion free.
    for (int axis = 0; axis < 3; ++axis) {
        const float lower = (std::min)(0.0f, -low[axis] - obstacle.position[axis]);
        const float upper = (std::max)(0.0f, worldScale - high[axis] - obstacle.position[axis]);
        const float travel = std::clamp(displacement[axis], lower, upper);
        if (travel != displacement[axis]) {
            boundaryContact = true;
            obstacle.velocity[axis] = 0.0f;
        }
        displacement[axis] = travel;
    }
    // A tilted floor can conflict with an axis wall. Resolve residual penetration
    // only along directions that have room inside the box.
    const float remaining = -gap - glm::dot(displacement, normal);
    if (floorEnabled && remaining > 0.0f) {
        glm::vec3 direction = normal;
        const glm::vec3 next = obstacle.position + displacement;
        for (int axis = 0; axis < 3; ++axis) {
            if ((direction[axis] < 0.0f && next[axis] + low[axis] <= contactTolerance) ||
                (direction[axis] > 0.0f && next[axis] + high[axis] >= worldScale - contactTolerance)) {
                direction[axis] = 0.0f;
            }
        }
        const float alignment = glm::dot(direction, normal);
        if (alignment > contactTolerance) {
            const glm::vec3 correction = direction * (remaining / alignment);
            for (int axis = 0; axis < 3; ++axis) {
                displacement[axis] += std::clamp(correction[axis],
                    (std::min)(0.0f, -low[axis] - next[axis]),
                    (std::max)(0.0f, worldScale - high[axis] - next[axis]));
            }
        }
    }
    obstacle.position += displacement;
    floorEnabled = floorEnabled || gap + glm::dot(displacement, normal) >= -contactTolerance;
    obstacle.updateMatrices();
}
}

void InteractionController::update(Window& window, const Camera& camera, SdfInstance& obstacle, float dt,
    const MpmPhysics& physics, float worldScale) {
    if (currentMode == 1) {
        handleShootMode(window, camera, obstacle);
    }
    else if (currentMode == 2) {
        handleSwingMode(window, camera, obstacle);
    }

    if (shootActive) {
        advanceShoot(obstacle, shootFloorEnabled, shootBoundaryContact, dt, physics, worldScale);
    } else {
        advanceSwing(obstacle, dt);
    }
    syncMouse(window);
}

void InteractionController::syncMouse(Window& window) {
    lastMouseState = glfwGetMouseButton(window.get(), GLFW_MOUSE_BUTTON_LEFT);
}

/// Shoot モードでの処理
void InteractionController::handleShootMode(Window& window, const Camera& camera, SdfInstance& obstacle) {
    int currentState = glfwGetMouseButton(window.get(), GLFW_MOUSE_BUTTON_LEFT);

    if (!ImGui::GetIO().WantCaptureMouse && currentState == GLFW_PRESS && lastMouseState == GLFW_RELEASE) {
        double xpos, ypos;
        glfwGetCursorPos(window.get(), &xpos, &ypos);

        float x = (2.0f * xpos) / window.getSize().x - 1.0f;
        float y = 1.0f - (2.0f * ypos) / window.getSize().y;
        glm::vec4 ray_clip = glm::vec4(x, y, -1.0f, 1.0f);

        glm::mat4 invProj = glm::inverse(camera.getProjection(window.getAspect()));
        glm::mat4 invView = glm::inverse(camera.getView());

        glm::vec4 ray_eye = invProj * ray_clip;
        ray_eye /= ray_eye.w;
        glm::vec4 ray_world = invView * ray_eye;
        glm::vec3 ray_dir = glm::normalize(glm::vec3(ray_world) - camera.position);

        swingState = SwingState::None;
        obstacle.rotation = glm::quat(1, 0, 0, 0);
        obstacle.position = camera.position + ray_dir * 0.5f;
        obstacle.velocity = ray_dir * 6.0f;
        obstacle.angularVelocity = glm::vec3(0.0f); // 弾丸モードは回転なし
        obstacle.scale = glm::vec3(0.2f);          // 等方スケール

        obstacle.updateMatrices();

        // Shoot操作の記録
        lastAction = LastActionType::Shoot;
        shootActive = true;
        shootFloorEnabled = false;
        shootBoundaryContact = false;
        lastShootPos = obstacle.position;
        lastShootVel = obstacle.velocity;
    }
}

/// Swing モードでの処理
void InteractionController::handleSwingMode(Window& window, const Camera& camera, SdfInstance& obstacle) {
    int currentState = glfwGetMouseButton(window.get(), GLFW_MOUSE_BUTTON_LEFT);
    glm::mat4 invView = glm::inverse(camera.getView());
    glm::mat4 invProj = glm::inverse(camera.getProjection(window.getAspect()));

    if (!ImGui::GetIO().WantCaptureMouse) {
        if (currentState == GLFW_PRESS && lastMouseState == GLFW_RELEASE && swingState != SwingState::Swinging) {
            // ドラッグ開始
            swingState = SwingState::Dragging;
            glfwGetCursorPos(window.get(), &dragStartPos.x, &dragStartPos.y);

            // レイの生成
            float x = (2.0f * dragStartPos.x) / window.getSize().x - 1.0f;
            float y = 1.0f - (2.0f * dragStartPos.y) / window.getSize().y;
            glm::vec4 ray_clip = glm::vec4(x, y, -1.0f, 1.0f);
            glm::vec4 ray_eye = invProj * ray_clip;
            ray_eye /= ray_eye.w;
            glm::vec4 ray_world = invView * ray_eye;
            glm::vec3 ray_dir = glm::normalize(glm::vec3(ray_world) - camera.position);

            // 支点(Pivot)の計算
            glm::vec3 cam_front = -glm::normalize(glm::vec3(invView[2]));
            float t = pivotDepth / glm::dot(cam_front, ray_dir);
            swingPivot = camera.position + ray_dir * t;

            // デフォルト回転軸
            swingAxis = glm::normalize(glm::vec3(invView[0]));

            swingMaxAngle = 0.0f;

        }
        else if (currentState == GLFW_RELEASE && swingState == SwingState::Dragging) {
            // ドラッグ終了 -> 発動
            shootActive = false;
            swingState = SwingState::Swinging;
            swingCurrentTime = 0.0f;
            obstacle.scale = glm::vec3(0.06f, 0.4f, 0.06f);

            // Swing操作の記録
            lastAction = LastActionType::Swing;
            lastSwingPivot = swingPivot;
            lastSwingAxis = swingAxis;
            lastSwingMaxAngle = swingMaxAngle;
        }

        // ドラッグ中のリアルタイム計算
        if (swingState == SwingState::Dragging) {
            double currentX, currentY;
            glfwGetCursorPos(window.get(), &currentX, &currentY);

            float dx = static_cast<float>(currentX - dragStartPos.x);
            float dy = static_cast<float>(currentY - dragStartPos.y);

            glm::vec3 cam_right = glm::normalize(glm::vec3(invView[0]));
            glm::vec3 cam_back = glm::normalize(glm::vec3(invView[2]));

            glm::vec3 drag3D = dx * cam_right + dy * cam_back;
            float dragLen = glm::length(drag3D);

            float normalizedDrag = glm::clamp(dragLen / maxPixelDrag, 0.0f, 1.0f);
            swingMaxAngle = normalizedDrag * glm::radians(90.0f);

            if (dragLen > 1.0f) {
                glm::vec3 downVector = glm::vec3(0.0f, -1.0f, 0.0f);
                const glm::vec3 axis = glm::cross(drag3D, downVector);
                if (glm::length(axis) > 1e-6f) swingAxis = glm::normalize(axis);
            }
        }
    }

    if (currentState == GLFW_RELEASE && ImGui::GetIO().WantCaptureMouse) cancelDrag();
}

void InteractionController::advanceSwing(SdfInstance& obstacle, float dt) {
    if (swingState != SwingState::Swinging) {
        obstacle.update(dt);
        return;
    }

    // 半周期の解析解で姿勢と速度を同じ時刻に設定する。
    const float endPhase = glm::pi<float>();
    swingCurrentTime = (std::min)(swingCurrentTime + dt * swingSpeedMult, endPhase);
    const bool finished = swingCurrentTime >= endPhase;
    const float angle = -swingMaxAngle * cos(swingCurrentTime);
    const float angularSpeed = finished ? 0.0f : swingMaxAngle * sin(swingCurrentTime) * swingSpeedMult;
    const glm::quat rotation = glm::angleAxis(angle, swingAxis);
    const glm::vec3 offset = rotation * glm::vec3(0.0f, -swingRadius, 0.0f);
    obstacle.position = swingPivot + offset;
    obstacle.rotation = rotation;
    obstacle.angularVelocity = swingAxis * angularSpeed;
    obstacle.velocity = glm::cross(obstacle.angularVelocity, offset);
    obstacle.updateMatrices();
    if (finished) swingState = SwingState::None;
}

// Renderer に渡すプレビュー用の頂点データを生成(計算)して返す
std::vector<glm::vec3> InteractionController::calcPreviewPoints() const {
    std::vector<glm::vec3> previewPoints;
    if (swingState != SwingState::Dragging) return previewPoints;

    glm::vec3 downVector = glm::vec3(0.0f, -1.0f, 0.0f);
    int segments = 30;

    float tipOffset = 0.4f * 0.5f;
    float tipRadius = swingRadius + tipOffset;

    for (int i = 0; i <= segments; i++) {
        float t = (float)i / segments;
        float angle = -swingMaxAngle + (swingMaxAngle * 2.0f) * t;
        glm::mat4 rotMat = glm::rotate(glm::mat4(1.0f), angle, swingAxis);
        glm::vec3 offset = glm::vec3(rotMat * glm::vec4(downVector * tipRadius, 0.0f));
        previewPoints.push_back(swingPivot + offset);
    }

    glm::vec3 bottomPos = swingPivot + downVector * tipRadius;
    glm::vec3 maxAngularVelocity = swingAxis * (swingMaxAngle * swingSpeedMult);
    glm::vec3 maxVelocity = glm::cross(maxAngularVelocity, downVector * tipRadius);

    previewPoints.push_back(bottomPos);
    previewPoints.push_back(bottomPos + maxVelocity * 0.1f);

    float d = 0.05f;
    previewPoints.push_back(swingPivot + glm::vec3(-d, 0, 0));
    previewPoints.push_back(swingPivot + glm::vec3(d, 0, 0));
    previewPoints.push_back(swingPivot + glm::vec3(0, -d, 0));
    previewPoints.push_back(swingPivot + glm::vec3(0, d, 0));
    previewPoints.push_back(swingPivot + glm::vec3(0, 0, -d));
    previewPoints.push_back(swingPivot + glm::vec3(0, 0, d));

    return previewPoints;
}

// 自動発火メソッドの実装
void InteractionController::fireLastAction(SdfInstance& obstacle) {
    // 最後のアクションに応じてobstacleのステータスを変更
    if (lastAction == LastActionType::Shoot) {
        // 記録されたパラメータでShootを再現
        obstacle.rotation = glm::quat(1, 0, 0, 0);
        obstacle.position = lastShootPos;
        obstacle.velocity = lastShootVel;
        obstacle.angularVelocity = glm::vec3(0.0f);
        obstacle.scale = glm::vec3(0.2f);

        obstacle.updateMatrices();

        swingState = SwingState::None;
        shootActive = true;
        shootFloorEnabled = false;
        shootBoundaryContact = false;
    }
    else if (lastAction == LastActionType::Swing) {
        // 記録されたパラメータをコントローラーの現在の制御変数に復元
        swingPivot = lastSwingPivot;
        swingAxis = lastSwingAxis;
        swingMaxAngle = lastSwingMaxAngle;

        // Swingのキネマティクス再生状態へ強制遷移
        shootActive = false;
        swingState = SwingState::Swinging;
        swingCurrentTime = 0.0f;
        obstacle.scale = glm::vec3(0.06f, 0.4f, 0.06f);
        advanceSwing(obstacle, 0.0f);
    }
}