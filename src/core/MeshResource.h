#pragma once
#include <GL/glew.h>
#include <GLM/glm.hpp>
#include <vector>
#include <string>
#include <iostream>

#include "../tiny_obj_loader/tiny_obj_loader.h"

// SDF の頂点構造体
struct SDFVertex {
    glm::vec3 position; // ローカル空間での頂点位置
    glm::vec3 normal;   // 法線
};

// メッシュ + GPUリソース + SDF をまとめた共有リソース
class MeshResource {
public:
    GLuint vao{ 0 }, vbo{ 0 }, ebo{ 0 };    // 描画バッファ
    GLsizei indexCount{ 0 };                // インデックス数

    // CPU におけるメッシュデータ
    std::vector<SDFVertex> vertices;
    std::vector<GLuint> indices;

    // ローカル空間でのAABB（SDF生成の範囲決定に使う）
    glm::vec3 aabbMin{ 1e10f };
    glm::vec3 aabbMax{ -1e10f };

    // 生成される SDFテクスチャ 1 枚
    GLuint sdfTexture3D{ 0 };

    /// コンストラクタ
    /// @param[in] objFilePath 読み込むオブジェクトファイルへのパス(.vcxprojのある場所から相対パスで)
    MeshResource(const std::string& objFilePath) {
        if (loadOBJ(objFilePath)) {
            setupMesh();
            calculateAABB();
        }
    }

    /// SDF の生成 + 各ボクセルからメッシュへの最短距離の計算 | ※呼び出しは一回だけ！
    /// @param[in] resolution 作成する3D(SDF)テクスチャの解像度 | 現在は固定値で
    void generateSDF(int resolution = 64) {
        std::cout << "SDF Generation Started... (Resolution: " << resolution << ")" << std::endl;

        // 1. 境界誤差防止のためSDFの計算範囲を AABB より少し広めに
        glm::vec3 extent = aabbMax - aabbMin;
        float padding = glm::max(glm::max(extent.x, extent.y), extent.z) * 0.1f;
        glm::vec3 sdfMin = aabbMin - glm::vec3(padding);
        glm::vec3 sdfMax = aabbMax + glm::vec3(padding);

        // 1ボクセルあたりのサイズ
        glm::vec3 cellSize = (sdfMax - sdfMin) / static_cast<float>(resolution - 1);

        // 3D配列（1次元配列として確保）
        int voxelCount = resolution * resolution * resolution;
        std::vector<float> sdfData(voxelCount, 0.0f);

        // 2. 各ボクセルについて総当たりで最短距離を計算
        for (int z = 0; z < resolution; ++z) {
            for (int y = 0; y < resolution; ++y) {
                for (int x = 0; x < resolution; ++x) {
                    // 現在のボクセルのワールド座標
                    glm::vec3 p = sdfMin + glm::vec3(x, y, z) * cellSize;

                    float minDist = 1e10f;
                    float sign = 1.0f; // 1.0 = 外側, -1.0 = 内側

                    // 全ての三角形に対して最短距離を計算
                    for (size_t i = 0; i < indices.size(); i += 3) {
                        glm::vec3 v0 = vertices[indices[i]].position;
                        glm::vec3 v1 = vertices[indices[i + 1]].position;
                        glm::vec3 v2 = vertices[indices[i + 2]].position;

                        // 三角形上の最近接点
                        glm::vec3 closestPt = closestPointOnTriangle(p, v0, v1, v2);
                        float dist = glm::length(p - closestPt);

                        if (dist < minDist) {
                            minDist = dist;
                            // 三角形の法線を使って内側か外側かを判定
                            glm::vec3 normal = glm::normalize(glm::cross(v1 - v0, v2 - v0));
                            // 最も近い点からボクセルへのベクトルと法線の内積で判定
                            sign = glm::dot(p - closestPt, normal) >= 0.0f ? 1.0f : -1.0f;
                        }
                    }

                    // インデックスの計算 (Z * res*res + Y * res + X)
                    int index = z * resolution * resolution + y * resolution + x;
                    sdfData[index] = minDist * sign;
                }
            }
            // 進行度の表示（計算が重いためデバッグのため）
            if (z % 10 == 0) std::cout << "Progress: " << (z * 100 / resolution) << "%" << std::endl;
        }

        // 3. 計算したデータをOpenGLの3Dテクスチャとして転送
        glGenTextures(1, &sdfTexture3D);
        glBindTexture(GL_TEXTURE_3D, sdfTexture3D);

        // R32F（32ビット浮動小数点の1チャンネル）として転送
        glTexImage3D(GL_TEXTURE_3D, 0, GL_R32F, resolution, resolution, resolution, 0, GL_RED, GL_FLOAT, sdfData.data());

        // サンプリングの設定（滑らかに補間する）
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        // はみ出した場合は最も外側の値を返すようにする(端の値も保持)
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

        glBindTexture(GL_TEXTURE_3D, 0);

        std::cout << "SDF Generation Completed!" << std::endl;

        // SDF用のAABBを更新（パディングを含めたものにするため）
        aabbMin = sdfMin;
        aabbMax = sdfMax;
    }

    /// デストラクタ
    ~MeshResource() {
        glDeleteVertexArrays(1, &vao);
        glDeleteBuffers(1, &vbo);
        glDeleteBuffers(1, &ebo);
        if (sdfTexture3D != 0) glDeleteTextures(1, &sdfTexture3D);
    }

private:
    /// OBJ ファイル読み込み
    /// @param[in] filepath ファイルパス
    bool loadOBJ(const std::string& filepath) {
        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;

        // 読み込みエラー処理
        std::string warn, err;
        bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filepath.c_str());
        if (!warn.empty()) std::cout << "OBJ Warn: " << warn << std::endl;
        if (!err.empty()) std::cerr << "OBJ Error: " << err << std::endl;
        if (!ret) return false;

        // 頂点展開 : インデックスをフラットに
        for (const auto& shape : shapes) {
            for (const auto& index : shape.mesh.indices) {
                SDFVertex vertex{};

                // 位置
                vertex.position = {
                    attrib.vertices[3 * index.vertex_index + 0],
                    attrib.vertices[3 * index.vertex_index + 1],
                    attrib.vertices[3 * index.vertex_index + 2]
                };

                // 法線
                if (index.normal_index >= 0) {
                    vertex.normal = {
                        attrib.normals[3 * index.normal_index + 0],
                        attrib.normals[3 * index.normal_index + 1],
                        attrib.normals[3 * index.normal_index + 2]
                    };
                }
                vertices.push_back(vertex);
                // 順番にインデックス付与 (よって非共有頂点)
                indices.push_back(static_cast<GLuint>(indices.size()));
            }
        }
        return true;
    }

    /// 読み込んだメッシュに対して GPU バッファ生成
    void setupMesh() {
        indexCount = static_cast<GLsizei>(indices.size());
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ebo);

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(SDFVertex), vertices.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLuint), indices.data(), GL_STATIC_DRAW);

        // position を 0 番に紐づけ 
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SDFVertex), (void*)offsetof(SDFVertex, position));
        // normal を 1 番に紐づけ
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(SDFVertex), (void*)offsetof(SDFVertex, normal));

        glBindVertexArray(0);
    }

    /// AABB を計算
    void calculateAABB() {
        for (const auto& v : vertices) {
            aabbMin = glm::min(aabbMin, v.position);
            aabbMax = glm::max(aabbMax, v.position);
        }
    }

    /// 点と三角形の最近接点を求める
    /// @param[in] p ボクセルのワールド座標
    /// @param[in] a,b,c 各種三角形
    glm::vec3 closestPointOnTriangle(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
        glm::vec3 ab = b - a;
        glm::vec3 ac = c - a;
        glm::vec3 ap = p - a;

        float d1 = glm::dot(ab, ap);
        float d2 = glm::dot(ac, ap);
        if (d1 <= 0.0f && d2 <= 0.0f) return a;

        glm::vec3 bp = p - b;
        float d3 = glm::dot(ab, bp);
        float d4 = glm::dot(ac, bp);
        if (d3 >= 0.0f && d4 <= d3) return b;

        float vc = d1 * d4 - d3 * d2;
        if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
            float v = d1 / (d1 - d3);
            return a + v * ab;
        }

        glm::vec3 cp = p - c;
        float d5 = glm::dot(ab, cp);
        float d6 = glm::dot(ac, cp);
        if (d6 >= 0.0f && d5 <= d6) return c;

        float vb = d5 * d2 - d1 * d6;
        if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
            float w = d2 / (d2 - d6);
            return a + w * ac;
        }

        float va = d3 * d6 - d5 * d4;
        if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
            float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
            return b + w * (c - b);
        }

        float denom = 1.0f / (va + vb + vc);
        float v = vb * denom;
        float w = vc * denom;
        return a + ab * v + ac * w;
    }
};