#version 430 core

//==============================================================================
// gbuffer_mesh.frag : 床と障害物の G-buffer 出力
// 目的: 一様な色と、ビュー空間の法線を G-buffer に書く。種別 (kind) で、
//       後のパスが粒子・床・障害物を区別できるようにする。
//==============================================================================

in vec3 v_normalView;
in vec3 v_positionView;

uniform vec3 albedo;	// 色 (線形)
uniform float kind;		// 種別 (0.5 = 床, 0.75 = 障害物)
uniform bool twoSided;	// 両面を描く面 (床) なら true

layout(location = 0) out vec4 o_albedo;
layout(location = 1) out vec4 o_normal;
layout(location = 2) out float o_linearDepth;	// 粒子ではないので 0 (粒子の手前にある場合は粒子の深度を消す)

void main() {
	vec3 n = normalize(v_normalView);
	// 両面の面 (床) では、法線を視点の側に向ける (下から見た場合や、頂点の並び順が逆の場合)
	// 障害物は裏面を描かない (カリング) ので反転しない。輪郭付近で補間された法線が
	// 視線とわずかに逆を向いても、反転すると不自然に暗くなるため
	if (twoSided && dot(n, -v_positionView) < 0.0) n = -n;
	o_albedo = vec4(albedo, kind);
	o_normal = vec4(n, 0.0);
	o_linearDepth = 0.0;
}
