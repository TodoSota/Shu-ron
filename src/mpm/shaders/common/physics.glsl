layout(std140, binding = 1) uniform MPMPhysics {
	// シミュレーション空間
	vec3 gravity;		// 重力
	float timestep;		// 時間間隔
	vec3 f_normal;		// 地面の法線
	float f_height;		// 地面の高さ

	float f_restitution; // Reserved in the 112-byte UBO layout.
	float f_friction;	// 地面の摩擦係数
	float dx;			// グリッドの間隔
	float inv_dx;		// 間隔の逆数

	// 粒子
	float p_restitution; // Reserved in the 112-byte UBO layout.
	float p_vol;		// 粒子の体積
	float p_mu;			// 粒子のラメ係数
	float p_lambda;		// 粒子のラメ係数

	float p_mass;		// 粒子の質量
	float p_radius;		// 粒子の半径
	float p_overlap; // Reserved in the 112-byte UBO layout.
	float p_padding; // Reserved in the 112-byte UBO layout.

	// 障害物
	vec4 obstacle_sphere; // Reserved in the 112-byte UBO layout.
	vec4 obstacle_velocity; // Reserved in the 112-byte UBO layout.
};
