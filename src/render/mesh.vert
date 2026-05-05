#version 430 core
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