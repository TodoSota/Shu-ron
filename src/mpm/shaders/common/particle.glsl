struct mpmParticle {
	vec4 position;		// 位置
	vec4 velocity;		// 速度 + padding
	mat4 affineC;		//アフィン速度行列 (mat3 分のみ値あり)
	mat4 deformation;	// 変形勾配 (mat3 分のみ値あり)

	// 砂の塑性変形パラメーター
	 float alpha;		//降伏面の大きさ
	 float q;			//硬化状態
	 float vc;			//変化の際の体積変化
	 int state;			//状態(変化)
	 int scale; // Reserved in the 192-byte particle layout.
	 int padding[3];		//調整
};
