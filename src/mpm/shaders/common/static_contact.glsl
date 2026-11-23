const int staticBoundaryNodes = 3;

vec3 applyStaticContact(vec3 v, float dist, ivec3 coords, ivec3 size) {
	// 床より下なら
	if (dist < 0.0) {
		float v_normal = dot(v, f_normal); // 法線方向の成分を取得

		// 下向きに落下してるなら (床から離れる向きの速度は拘束しない = separating 境界)
		if (v_normal < 0.0) {
			vec3 vn = f_normal * v_normal;	// 法線方向の速度
			vec3 vt = v - vn;				// 接線方向の速度

			// 摩擦 (Coulomb): 法線方向に止めた速度 |v_n| に比例して接線速度を減らし、
			// 減らしきれる場合は 0 にする (静止摩擦)
			float vt_len = length(vt);
			if(vt_len > 1e-7){
				float friction_factor = max(0.0, 1.0 - f_friction * abs(v_normal) / vt_len);
				vt *= friction_factor;
			}

			// 速度の更新
			// 侵入法線速度を0にする非弾性境界。f_restitutionは使用しない。
			v = vt;
		}
	}

	// 両端の各3節点を拘束する。上端は size-3 を含める。
	// 高速移動の全条件で粒子支持域を保証するものではない（B-5、対応台帳参照）。
	if (coords.x < staticBoundaryNodes && v.x < 0.0) v.x = 0.0;
	if (coords.x >= size.x - staticBoundaryNodes && v.x > 0.0) v.x = 0.0;
	if (coords.y < staticBoundaryNodes && v.y < 0.0) v.y = 0.0;
	if (coords.y >= size.y - staticBoundaryNodes && v.y > 0.0) v.y = 0.0;
	if (coords.z < staticBoundaryNodes && v.z < 0.0) v.z = 0.0;
	if (coords.z >= size.z - staticBoundaryNodes && v.z > 0.0) v.z = 0.0;

	return v;
}

// Resolve any floor-normal velocity introduced by clipping at a grid edge.
// Only use correction directions that cannot point out of the grid.
vec3 keepStaticContact(vec3 v, float dist, ivec3 coords, ivec3 size) {
	v = applyStaticContact(v, dist, coords, size);
	float floorSpeed = dot(v, f_normal);
	if (dist >= 0.0 || floorSpeed >= 0.0) return v;

	vec3 correction = f_normal;
	for (int axis = 0; axis < 3; ++axis) {
		if ((coords[axis] < staticBoundaryNodes && correction[axis] < 0.0) ||
			(coords[axis] >= size[axis] - staticBoundaryNodes && correction[axis] > 0.0)) {
			correction[axis] = 0.0;
		}
	}
	float response = dot(correction, f_normal);
	if (response <= 1e-7) return vec3(0.0);
	return v - (floorSpeed / response) * correction;
}
