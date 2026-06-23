#include "InteractionController.h"
#include "../core/Window.h"
#include "../core/Camera.h"
#include "../sdf/SdfInstance.h"

// ImGui の状態取得用（UI操作中は弾くため）
#include "../../ImGui/imgui.h" 

// 毎フレームの処理
void InteractionController::update(Window& window, const Camera& camera, SdfInstance& obstacle, float dt) {
    if (currentMode == 1) {
        handleShootMode(window, camera, obstacle);
    }
    else if (currentMode == 2) {
        handleSwingMode(window, camera, obstacle, dt);
    }
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

        obstacle.position = camera.position + ray_dir * 0.5f;
        obstacle.velocity = ray_dir * 6.0f;
        obstacle.angularVelocity = glm::vec3(0.0f); // 弾丸モードは回転なし
        obstacle.scale = glm::vec3(-0.2f);          // 等方スケール
    }
}

/// Swing モードでの処理
void InteractionController::handleSwingMode(Window& window, const Camera& camera, SdfInstance& obstacle, float dt) {
    int currentState = glfwGetMouseButton(window.get(), GLFW_MOUSE_BUTTON_LEFT);
    glm::mat4 invView = glm::inverse(camera.getView());
    glm::mat4 invProj = glm::inverse(camera.getProjection(window.getAspect()));

    if (!ImGui::GetIO().WantCaptureMouse) {
        if (currentState == GLFW_PRESS && lastMouseState == GLFW_RELEASE) {
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

            // スイングの初期姿勢
            obstacle.scale = glm::vec3(-0.06f, -0.4f, -0.06f);

        }
        else if (currentState == GLFW_RELEASE && swingState == SwingState::Dragging) {
            // ドラッグ終了 -> 発動
            swingState = SwingState::Swinging;
            swingCurrentTime = 0.0f;
            obstacle.scale = glm::vec3(-0.06f, -0.4f, -0.06f);
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
                swingAxis = glm::normalize(glm::cross(drag3D, downVector)); // 修正済みの方向
            }
        }
    }

    // スイング運動のキネマティック更新
    if (swingState == SwingState::Swinging) {
        swingCurrentTime += dt * swingSpeedMult;
        float phase = swingCurrentTime;

        if (phase > glm::pi<float>()) {
            swingState = SwingState::None;
            obstacle.velocity = glm::vec3(0.0f);
            obstacle.angularVelocity = glm::vec3(0.0f);
        }
        else {
            float currentAngle = -swingMaxAngle * cos(phase);
            float angularSpeed = swingMaxAngle * sin(phase) * swingSpeedMult;

            glm::vec3 currentAngularVelocity = swingAxis * angularSpeed;
            glm::vec3 downVector = glm::vec3(0.0f, -1.0f, 0.0f);
            glm::mat4 rotMat = glm::rotate(glm::mat4(1.0f), currentAngle, swingAxis);
            glm::vec3 offset = glm::vec3(rotMat * glm::vec4(downVector * swingRadius, 0.0f));

            obstacle.position = swingPivot + offset;
            obstacle.velocity = glm::cross(currentAngularVelocity, offset);
            obstacle.angularVelocity = currentAngularVelocity;
            obstacle.rotation = glm::quat_cast(rotMat);
        }
    }
}

// Renderer に渡すプレビュー用の頂点データを生成(計算)して返す
std::vector<glm::vec3> InteractionController::calcPreviewPoints(const SdfInstance& obstacle) const {
    std::vector<glm::vec3> previewPoints;
    if (swingState != SwingState::Dragging) return previewPoints;

    glm::vec3 downVector = glm::vec3(0.0f, -1.0f, 0.0f);
    int segments = 30;

    float tipOffset = abs(obstacle.scale.y) * 0.5f;
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