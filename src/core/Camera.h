#pragma once
#include <GLM/glm.hpp>
#include <GLM/gtc/matrix_transform.hpp>

enum class CameraMode { ORBIT, FPS };

struct Camera {
    CameraMode mode = CameraMode::ORBIT;

    // パラメータ
    glm::vec3 position{ 0.5f, 0.5f, 2.5f }; // カメラの実座標
    float fov = 60.0f;      // 視野角
    glm::vec3 front{ 0.0f, 0.0f, -1.0f };// カメラの向いている方向
    glm::vec3 up{ 0.0f, 1.0f, 0.0f };   // カメラの頭が向いている方向

    glm::vec3 target{ 0.5f, 0.5f, 0.5f };   // <ORBIT>回転中心
    float radius = 2.5f;    //  <ORBIT>ターゲットからの距離

    float yaw = -90.0f;     // <FPS>横回転
    float pitch = 0.0f;     // <FPS>縦回転

    // コンストラクタ
    Camera() { updateVectors(); };

    // マウスの移動量（delta）を受け取って yaw/pitch を更新
    void rotate(float dx, float dy) {
        float sensitivity = 0.1f;
        yaw += dx * sensitivity;
        pitch -= dy * sensitivity; // マウスの移動方向と合わせる

        // 回転角制限・ジンバルロック防止
        if (pitch > 89.0f)  pitch = 89.0f;
        if (pitch < -89.0f) pitch = -89.0f;

        updateVectors();
    }

    // <ORBIT> マウスホール量でズームする
    void zoom(float delta) {
        if (mode == CameraMode::ORBIT) {
            float sensitivity = 0.2f;
            radius -= delta * sensitivity;

            // 近づきすぎ・遠ざかりすぎの制限
            if (radius < 0.1f) radius = 0.1f;
            if (radius > 10.0f) radius = 10.0f;

            updateVectors();
        }
    }

    // 角度変化からベクトルを更新する
    void updateVectors() {
        // どちらが前方(カメラの向き)なのか計算・球座標系から直交座標系(XYZ)への変換
        glm::vec3 f;
        f.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        f.y = sin(glm::radians(pitch));
        f.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
        front = glm::normalize(f);

        // モード別に座標を決定
        if (mode == CameraMode::ORBIT) {
            // 中心から front の逆方向へ radius 分だけ離れた位置
            position = target - front * radius; 
        }
        // FPS は WASD などない限り固定

        // 上方ベクトル再計算
        glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0, 1, 0)));
        up = glm::normalize(glm::cross(right, front));
    }

    // view を計算して渡す
    glm::mat4 getView() const {
        // どこにいる ・  どこを見ている ・ どこが上
        return glm::lookAt(position, position + front, up);
    }

    // projction を計算して渡す
    glm::mat4 getProjection(float aspect) const {
        // レンズの歪みのこと
        return glm::perspective(glm::radians(fov), aspect, 0.1f, 100.0f);
    }
};