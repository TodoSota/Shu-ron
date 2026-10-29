#version 430 core

//==============================================================================
// mesh.vert : 障害物メッシュ・領域境界・Swing のプレビュー線を描画する頂点シェーダー
// 目的: 頂点を MVP 行列でクリップ座標へ変換し、陰影付けのための法線を
//       ワールド空間へ回転させてフラグメントシェーダーへ渡す。
//==============================================================================
layout (location = 0) in vec3 position;
layout (location = 1) in vec3 normal;

uniform mat4 mc;    // モデル・ビュー・プロジェクション行列
uniform mat4 model; // モデル行列（法線の回転用）

out vec3 v_normal;

void main() {
    // 法線ベクトルの向きをモデルの回転に合わせる（スケーリングが均等な場合）
    v_normal = mat3(model) * normal;
    gl_Position = mc * vec4(position, 1.0);
}