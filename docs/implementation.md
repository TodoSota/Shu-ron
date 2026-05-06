# 実装詳細（Implementation）

## ■ 3Dテクスチャによる計算グリッドの実装
本プロジェクトの核心は、MPMの格子（グリッド）を image3D として実装している点にあります。

### ● **なぜ3Dテクスチャか**
 - **O(1) の直接参照**: 空間座標を`ivec3`のインデックスとしてそのまま利用でき、近傍探索のポインタ計算やハッシュ計算を完全に排除しています。
 - **GPUネイティブ**: `imageLoad`/`imageStore`を用いることで、GPUのテクスチャユニットに最適化された形式で格子データにアクセス可能です。

### ● グリッドアクセスのロジック
P2G（粒子→格子）および G2P（格子→粒子）において、粒子周辺の $3 \times 3 \times 3$ の格子点へのアクセスは、以下のような単純なオフセットループで記述されます。

```OpenGL Shading Language
// イメージ例
for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
        for (int k = 0; k < 3; k++) {
            ivec3 cell_idx = base_idx + ivec3(i, j, k);
            vec4 cell_data = imageLoad(grid_texture, cell_idx);
            // ... 計算処理
        }
    }
}
```

---

## ■ GPUメモリレイアウトとアライメント
C++（CPU）とGLSL（GPU）間でのデータ転送における破綻を防ぐため、メモリ配置を厳密に制御しています。

### ● SSBOの構造設計
 - **粒子データ** (`std430`): 大量（数万〜数十万）の粒子を扱うため、パディングを最小限に抑えた`std430`レイアウトを採用し、メモリ帯域を最適化しています。

 - アライメント調整:`vec3`はGPU側で16バイトとして扱われるため、C++側でも`vec4`もしくは明示的なパディング変数を挿入し、データ構造の完全な一致を保証しています。

```OpenGL Shading Language
// 粒子のデータ構造
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
	 int scale;			//スケール
	 int padding[3];		//調整
};
```

---

## ■ 高度な物理演算のGPU実装
MPMの複雑な数理モデルをコンピュートシェーダーで並列化するための工夫です。

### ● 競合制御：AtomicAdd (CASループ)
P2Gステップにおいて、複数の粒子が同一の格子点に同時に書き込む際の競合を避けるため`imageAtomicCompSwap`を用いた **CAS (Compare-And-Swap)** ループによる安全な加算処理を実装しています。

```OpenGL Shading Language
// AtomicAdd
// CAS ループによるAtomicAdd
void atomicAddFloatX(ivec3 coords, float val) {
    uint oldVal = imageLoad(gridIMX, coords).x;
    uint newVal, expectedVal;
    // 加算前後での値を比較、正しく書き込みが完了できるまで終わらない
    do {
        expectedVal = oldVal;
        newVal = floatBitsToUint(uintBitsToFloat(expectedVal) + val);
        oldVal = imageAtomicCompSwap(gridIMX, coords, expectedVal, newVal);
    } while (oldVal != expectedVal);
}
```

### ● SVD（特異値分解）
土砂の塑性変形を計算するための応力計算には、変形勾配のSVDが必要です。GLSL環境では外部ライブラリを使用できず、シェーダー内で**ヤコビ法を用いた固有値分解**をベースに直接実装しています。

```OpenGL Shading Language
// 特異値分解
void svd(mat3 F, out mat3 U, out mat3 Sigma, out mat3 V) {
    mat3 S = transpose(F) * F;

    jacobiEigen(S, V);  // 固有値分解

    Sigma = mat3(0.0);
  	// ゼロ除算の恐れがあるので、微小な値を足す(エプシロン)
    Sigma[0][0] = sqrt(max(S[0][0], 1e-4));
    Sigma[1][1] = sqrt(max(S[1][1], 1e-4));
    Sigma[2][2] = sqrt(max(S[2][2], 1e-4));

    mat3 invSigma = mat3(0.0);

    invSigma[0][0] = 1.0 / Sigma[0][0];
    invSigma[1][1] = 1.0 / Sigma[1][1];
    invSigma[2][2] = 1.0 / Sigma[2][2];

    U = F * V * invSigma;
}
```

---

## ■ 新規機能の実装ポイント

### ● SDF（符号付き距離関数）当たり判定の準備

従来の単純な「床（平面）」との判定から、複雑なメッシュ形状への対応を見据えた拡張です。

 - **オブジェクト管理**:`Mesh`クラスで読み込んだ障害物の位置情報をシェーダーに渡し、格子点（Grid）計算ステップで衝突判定を行います。

 - **次のステップ**: 現在の幾何形状判定から、距離フィールド（SDF）テクスチャを生成・参照する仕組みへの移行を予定しています。

```
// SDF を追加するための判定ロジック部分
vec3 sphere_center = obstacle_sphere.xyz;
float sphere_radius = obstacle_sphere.w;

// 中心から現在位置へのベクトル
vec3 to_pos = pos - sphere_center;
	
// SDFの距離計算：中心からの距離 - 半径
float sdf_dist = length(to_pos) - sphere_radius;

// 距離が0未満（球の内部にめり込んでいる）なら衝突処理
if (sdf_dist < 0.0) {
  // 球の表面法線（中心から外側へ向かう正規化ベクトル）
  vec3 normal = normalize(to_pos);
  vec3 obs_vel = obstacle_velocity.xyz;

  vec3 v_rel = v - obs_vel;
  float v_normal = dot(v_rel, normal);
  float push_out_v = (-sdf_dist / timestep) * 0.3;	// ペナルティ法(物体に触れているときちょっと斥力を与える)

  // 球の中心に向かって進んでいる（めり込もうとしている）場合のみ
  if (v_normal < 0.0) {
    vec3 vn = normal * v_normal; // 法線方向の速度
    vec3 vt = v_rel - vn;        // 接線方向の速度

    // 摩擦の適用
    float vt_len = length(vt);
    if(vt_len > 1e-7){
      float normal_force = abs(v_normal - push_out_v);	// 押し出しに必要な力
      float friction_factor = max(0.0, 1.0 - f_friction * normal_force / vt_len);
      vt *= friction_factor;
    }
    // 反発と押し出し(ペナルティ法)の強い方を採用する
    float final_vn = max(push_out_v, -v_normal * f_restitution);

    // 速度の更新（接線方向の速度 + 反発係数によって減衰させた法線方向の押し返し）
    v_rel = vt + normal * final_vn;
    v = v_rel + obs_vel;
  }
}
```

### ● 弾発射モード
 - **初速の付与**: 特定の粒子群に対し、カメラ方向を基準としたインパルス（初速）をコンピュートシェーダーで直接適用します。

 - **可視化**: 射出した粒子や石（材料違い）を識別するため`padding`変数を利用してフラグを管理し、`point.frag`で色分け描画（灰色など）を行っています。

---

## ■ まとめ

本実装では、3Dテクスチャの適用により、

 - データアクセスの単純化
 - 近傍探索の削減
 - GPU処理との統合

また、3Dテクスチャ活用のため

- GPUに適したデータ構造設計
- コンピュートシェーダーによる段階分割
- メモリアクセスの最適化

を行いました。