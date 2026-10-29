#version 430 core

//==============================================================================
// mesh.frag : 平行光源 1 個のランバート反射で障害物を陰影付けする
// 目的: 粒子 (点) と区別しやすいように、障害物を単色の陰影付き面として表示する。
//==============================================================================
in vec3 v_normal;
layout (location = 0) out vec4 color;

void main() {
    // 簡単な平行光源（左上奥から手前下へ）
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    vec3 n = normalize(v_normal);
    
    // 拡散反射（ランバート反射）
    float diff = max(dot(n, lightDir), 0.2); // 0.2 は環境光（暗くなりすぎないように）
    
    // 少し青みがかったグレーの球体
    vec3 baseColor = vec3(0.6, 0.7, 0.8);
    color = vec4(baseColor * diff, 1.0);
}