#pragma once
#include <GLM/glm.hpp>
#include <GLM/gtc/matrix_transform.hpp>
#include <memory>
#include "../core/MeshResource.h"

// 実験機能なので使う宣言
#define GLM_ENABLE_EXPERIMENTAL
#include <GLM/gtx/quaternion.hpp>

// SDFの状態を表現した構造体
struct SdfSnapshot {
    glm::vec3 position;
    glm::vec3 velocity;
    glm::quat rotation;
    glm::vec3 angularVelocity;
    glm::vec3 scale;
};

class SdfInstance {
public:
    // リソースデータへのポインタ（複数インスタンスで同じリソースを共有）
    std::shared_ptr<MeshResource> resource;

    // 動的パラメータ（ワールド空間での状態）
    glm::vec3 position{ 0.5f, 0.5f, 0.5f };
    glm::vec3 scale{ 1.0f };
    glm::quat rotation{ 1,0,0,0 }; // クォータニオン回転

    // 物理パラメータ
    glm::vec3 velocity{ 0.0f };         // 並進速度ベクトル
    glm::vec3 angularVelocity{ 0.0f };   // 角速度ベクトル

    // キャッシュ
    glm::mat4 modelMatrix{ 1.0f };  // モデル行列
    glm::mat4 invModelMatrix{ 1.0f };// モデル逆行列 

    /// コンストラクタ
    /// @param[in] res 空間内に存在するオブジェクトインスタンス
    SdfInstance(std::shared_ptr<MeshResource> res) : resource(res) {}

    /// 現在の状態をスナップショットとして抽出
    SdfSnapshot captureSnapshot() const {
        return { position, velocity, rotation, angularVelocity, scale };
    }

    /// スナップショットから状態を復元し、行列を再計算する
    void restoreSnapshot(const SdfSnapshot& snap) {
        position = snap.position;
        velocity = snap.velocity;
        rotation = snap.rotation;
        angularVelocity = snap.angularVelocity;
        scale = snap.scale;

        updateMatrices();
    }

    /// オブジェクトの時間更新
    /// param[in] dt タイムステップ
    void update(float dt) {
        // 並進の更新
        position += velocity * dt;

        // 回転の更新(角速度からクォータニオンをまわす)
        float angularSpeed = glm::length(angularVelocity);
        if (angularSpeed > 1e-6f) {
            glm::vec3 axis = angularVelocity / angularSpeed;
            // dtあたりの回転量をクォータニオンで生成して合成
            glm::quat deltaRot = glm::angleAxis(angularSpeed * dt, axis);
            rotation = glm::normalize(deltaRot * rotation); // 誤差蓄積防止の正規化
        }

        updateMatrices();
    }

    // モデル行列と逆行列の再計算
    void updateMatrices() {
        modelMatrix = glm::translate(glm::mat4(1.0f), position);
        modelMatrix *= glm::mat4_cast(rotation);
        modelMatrix = glm::scale(modelMatrix, scale);

        invModelMatrix = glm::inverse(modelMatrix);
    }

    /// 描画用のモデル行列を返す
    /// @param[out] m このオブジェクトのモデル行列
    glm::mat4 getModelMatrix() const { return modelMatrix; };

    /// 描画用のモデルの逆行列を返す
    /// @param[out] m このオブジェクトのモデル逆行列
    glm::mat4 getInverseModelMatrix() const { return invModelMatrix; }
};