#pragma once

//==============================================================================
// MeshResource.h : 障害物メッシュの読み込みと SDF の生成 (共有リソース)
// 目的: OBJ メッシュを読み込み、
//         ・描画用の VAO / VBO
//         ・衝突判定用の符号付き距離場 (SDF) を 3D テクスチャ (R32F) として
//       を用意する。同じ形状の障害物を複数置く場合は、このリソースを SdfInstance から共有する。
// SDF生成格子点から全三角形への最短距離と立体角総和による巻き数を求める。
// B-6d: 閉曲面・面方向整合・非退化・多様体を検査し、非対応入力は拒否する。
// 全体の面方向反転は許容。自己交差は検査しておらず、対応範囲外（対応台帳参照）。
//==============================================================================
#include <GL/glew.h>
#include <GLM/glm.hpp>
#include <vector>
#include <string>
#include <iostream>
#include <cmath>
#include <array>
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

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
    /// @note ファイルが読めない場合は、代わりに半径 0.5 の球を作る (障害物の機能をそのまま試せるようにするため)
    MeshResource(const std::string& objFilePath) {
        if (!loadOBJ(objFilePath)) {
            std::cerr << "OBJ file not found: " << objFilePath << " -> using a built-in sphere instead." << std::endl;
            vertices.clear();
            indices.clear();
            createSphere(0.5f, 24, 32);
        }
        setupMesh();
        calculateAABB();
    }

    /// SDF の生成 + 各ボクセルからメッシュへの最短距離の計算 | ※呼び出しは一回だけ！
    /// @param[in] resolution 作成する3D(SDF)テクスチャの解像度 | 現在は固定値で
    void generateSDF(int resolution = 64) {
        if (resolution < 2 || resolution > 512) throw std::invalid_argument("SDF resolution must be in [2,512]");
        if (sdfTexture3D) throw std::runtime_error("SDF has already been generated");
        validateClosedMesh();
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
                    // 現在のSDF格子点のローカル座標
                    glm::vec3 p = sdfMin + glm::vec3(x, y, z) * cellSize;

                    float minDist = 1e10f;
                    double solidAngle = 0.0;

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
                        }
                        // B-6d: generalized winding via signed triangle solid angles.
                        // Absolute total permits a globally reversed closed surface.
                        const glm::dvec3 a = glm::dvec3(v0) - glm::dvec3(p);
                        const glm::dvec3 b = glm::dvec3(v1) - glm::dvec3(p);
                        const glm::dvec3 c = glm::dvec3(v2) - glm::dvec3(p);
                        const double la = glm::length(a), lb = glm::length(b), lc = glm::length(c);
                        const double numerator = glm::dot(a, glm::cross(b, c));
                        const double denominator = la*lb*lc + glm::dot(a,b)*lc + glm::dot(b,c)*la + glm::dot(c,a)*lb;
                        solidAngle += 2.0 * std::atan2(numerator, denominator);
                    }

                    // インデックスの計算 (Z * res*res + Y * res + X)
                //   glTexImage3D は x が最も速く変わる順に並んだデータを受け取るので、この並びに合わせる
                    int index = z * resolution * resolution + y * resolution + x;
                    const float sign = std::abs(solidAngle) > 6.283185307179586 ? -1.0f : 1.0f;
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
    // Support closed, consistently oriented, nondegenerate two-manifold triangle
    // meshes. Self intersections are outside the supported input contract.
    // Quantization welds OBJ-expanded vertices and the built-in sphere seam.
    void validateClosedMesh() const {
        if (indices.empty() || indices.size()%3) throw std::runtime_error("SDF requires triangles");
        const glm::vec3 extent = aabbMax-aabbMin;
        const double size = glm::max(glm::max(extent.x,extent.y),extent.z);
        if (!(size > 0.0) || !std::isfinite(size)) throw std::runtime_error("Invalid SDF mesh bounds");
        const double weld = size*1e-6;
        std::map<std::array<long long,3>, size_t> vertexIds;
        std::map<std::pair<size_t,size_t>, std::pair<int,int>> edges;
        std::map<size_t, std::map<size_t,std::set<size_t>>> links;
        for (size_t i=0; i<indices.size(); i+=3) {
            size_t ids[3]; glm::dvec3 p[3];
            for (int j=0; j<3; ++j) {
                if (indices[i+j]>=vertices.size()) throw std::runtime_error("Invalid SDF vertex index");
                p[j] = glm::dvec3(vertices[indices[i+j]].position);
                std::array<long long,3> key{};
                for (int axis=0; axis<3; ++axis) {
                    if (!std::isfinite(p[j][axis]) || std::abs(p[j][axis]/weld)>9e18)
                        throw std::runtime_error("Nonfinite or excessive SDF coordinate");
                    key[axis] = std::llround(p[j][axis]/weld);
                }
                auto found=vertexIds.find(key);
                if (found==vertexIds.end()) found=vertexIds.emplace(key,vertexIds.size()).first;
                ids[j]=found->second;
            }
            if (ids[0]==ids[1] || ids[1]==ids[2] || ids[2]==ids[0]
                || glm::length(glm::cross(p[1]-p[0],p[2]-p[0])) <= size*size*1e-12)
                throw std::runtime_error("SDF rejects degenerate triangles");
            for (int j=0; j<3; ++j) {
                const size_t a=ids[j], b=ids[(j+1)%3], cc=ids[(j+2)%3];
                auto& edge=edges[std::minmax(a,b)];
                ++edge.first; edge.second += a<b ? 1 : -1;
                links[a][b].insert(cc); links[a][cc].insert(b);
            }
        }
        for (const auto& entry:edges) if (entry.second.first!=2 || entry.second.second!=0)
            throw std::runtime_error("SDF requires closed, consistently oriented manifold edges");
        for (const auto& entry:links) {
            const auto& link=entry.second;
            std::set<size_t> visited; std::vector<size_t> stack{link.begin()->first};
            while (!stack.empty()) {
                const auto id=stack.back(); stack.pop_back();
                if (!visited.insert(id).second) continue;
                const auto& neighbors=link.at(id);
                if (neighbors.size()!=2) throw std::runtime_error("Nonmanifold SDF vertex");
                stack.insert(stack.end(),neighbors.begin(),neighbors.end());
            }
            if (visited.size()!=link.size()) throw std::runtime_error("Disconnected SDF vertex fan");
        }
    }

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
                if (index.vertex_index < 0 || static_cast<size_t>(index.vertex_index)*3+2 >= attrib.vertices.size())
                    throw std::runtime_error("Invalid OBJ vertex index");

                // 位置
                vertex.position = {
                    attrib.vertices[3 * index.vertex_index + 0],
                    attrib.vertices[3 * index.vertex_index + 1],
                    attrib.vertices[3 * index.vertex_index + 2]
                };

                // 法線
                if (index.normal_index >= 0) {
                    if (static_cast<size_t>(index.normal_index)*3+2 >= attrib.normals.size())
                        throw std::runtime_error("Invalid OBJ normal index");
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

    /// 代わりの形状として、原点中心の UV 球を作る (loadOBJ と同じく、頂点を共有しない三角形の並びにする)
    /// @param[in] radius 半径
    /// @param[in] stacks 緯度方向の分割数
    /// @param[in] slices 経度方向の分割数
    void createSphere(float radius, int stacks, int slices) {
        const float pi = 3.14159265358979f;
        auto point = [&](int i, int j) {
            const float theta = pi * static_cast<float>(i) / stacks;            // 北極からの角度
            const float phi = 2.0f * pi * static_cast<float>(j) / slices;      // 経度
            const glm::vec3 n(std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi));
            return SDFVertex{ n * radius, n };
        };
        for (int i = 0; i < stacks; ++i) {
            for (int j = 0; j < slices; ++j) {
                const SDFVertex a = point(i, j), b = point(i, j + 1), c = point(i + 1, j), d = point(i + 1, j + 1);
                // 外側から見て反時計回り (表面) になる順。極では面積 0 の三角形になるので出力しない
                // (SDF の内外判定で法線が NaN になるのを防ぐ)
                auto emit = [&](const SDFVertex& p0, const SDFVertex& p1, const SDFVertex& p2) {
                    for (const SDFVertex& v : { p0, p1, p2 }) {
                        vertices.push_back(v);
                        indices.push_back(static_cast<GLuint>(indices.size()));
                    }
                };
                if (i > 0) emit(a, b, c);              // 北極の行では a と b が同じ点
                if (i < stacks - 1) emit(c, b, d);     // 南極の行では c と d が同じ点
            }
        }
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
