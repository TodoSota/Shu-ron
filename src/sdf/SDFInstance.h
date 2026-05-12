#pragma once
#include <GLM/glm.hpp>
#include <GLM/gtc/matrix_transform.hpp>
#include <memory>
#include "../core/MeshResource.h"

class SDFInstance {
public:
    // リソースデータへのポインタ（複数インスタンスで同じリソースを共有）
    std::shared_ptr<MeshResource> resource;

    // 動的パラメータ（ワールド空間での状態）
    glm::vec3 position{ 0.5f, 0.5f, 0.5f };
    glm::vec3 scale{ 1.0f };
    glm::quat rotation{ 1,0,0,0 }; // クォータニオン回転

    // 物理パラメータ
    glm::vec3 velocity{ 0.0f };

    // キャッシュ
    glm::mat4 modelMatrix{ 1.0f };  // モデル行列
    glm::mat4 invModelMatrix{ 1.0f };// モデル逆行列 

    /// コンストラクタ
    /// @param[in] res 空間内に存在するオブジェクトインスタンス
    SDFInstance(std::shared_ptr<MeshResource> res) : resource(res) {
        
    }

    /// オブジェクトの時間更新
    /// param[in] dt タイムステップ
    void update(float dt) {
        position += velocity * dt;
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