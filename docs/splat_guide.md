# 格子による表面粒子の抽出とスプラッティング（Splat モード）の実装手順書

[splat_study.md](splat_study.md) で検討した描画方法（描画用の密度格子で表面粒子と法線を求め、半透明のカーネルで重ねて描く）について、設計と手順を具体的に説明する。
既存の Spheres / Surface モードと並ぶ 4 番目の描画モード **Splat** として追加する。

**実装の状況（2026-10-30）**: 段階 1〜4 をすべて実装した（worktree todo-fix4）。実装中に計画から変えた点は、各節の「段階 N の実装で…」の段落と §1 の流れの図に反映してある。

**方針**

- 変更するのは描画だけである。シミュレーション（`src/mpm/` 以下の計算とシェーダー）には手を付けない。粒子バッファは読み出すだけにする。
- 法線は **密度勾配** n = −∇ρ / |∇ρ| で求める。精密さよりも、見かけとその制御のしやすさ、および速度を重視するためである（PCA は使わない）。
- 既存のパイプライン（シャドウマップ、G-buffer、SSAO、陰影の計算、粉塵）はそのまま使い、Splat モードで変わるのは粒子を G-buffer に書く部分と、陰影の計算の一部だけにする。
- 各段階は単独で動く状態にしてからコミットする。Points / Spheres / Surface モードは残し、UI で切り替えて見比べられるようにする。

---

## 目次

1. [全体の流れ](#1-全体の流れ)
2. [描画用の密度格子](#2-描画用の密度格子)
3. [表面らしさと法線](#3-表面らしさと法線)
4. [Deferred splatting](#4-deferred-splatting)
5. [陰影の計算の変更](#5-陰影の計算の変更)
6. [段階ごとの手順と完了条件](#6-段階ごとの手順と完了条件)
7. [ファイル構成と変更点](#7-ファイル構成と変更点)
8. [パラメータの初期値](#8-パラメータの初期値)
9. [性能の見積もりと計測](#9-性能の見積もりと計測)
10. [確認項目](#10-確認項目)
11. [注意点](#11-注意点)

---

## 1. 全体の流れ

Splat モードの 1 フレームの処理を、既存のパス名（[rendering_guide.md](rendering_guide.md) の A〜H）に合わせて示す。
新しく加えるのは (S) と (P) で、(A) のシャドウマップと (G) の陰影の計算に手を入れる（実装後の流れ）。

```
(S) 格子の準備 (コンピュート、毎フレーム)
    S1 density_splat.comp   粒子 → 密度格子 (uint の SSBO へ atomicAdd)
    S2 density_resolve.comp uint → float の 3D テクスチャ
       density_blur.comp    x → y → z の 3 回でガウスぼかし
    S3 surfel_prepare.comp  粒子ごとに 表面らしさ・法線・密度 を求めて surfel バッファへ
(A) シャドウマップ         障害物だけ (砂の影は (G) で密度格子から求める。Density shadow を切ると全粒子を球で描く)
(B) G-buffer               床・障害物だけ (粒子は描かない)
(P) スプラッティング
    P1 G-buffer の深度を splat 用の深度へコピー (glBlitFramebuffer)
    被覆率のパス            視線上のすべての円盤の重みを加算 (床・障害物の深度とだけ比べる)
    P2 可視性パス           表面粒子の円盤を、少し奥へずらした深度で描く
    P3 蓄積パス             深度の厚みの内側の円盤の 重み × 色・法線・深度 を加算
    P4 正規化 → splat 層    Σw で割り、被覆率 α を求める (半透明の輪郭も含む全画素)
    P5 正規化 → G-buffer    α が十分大きい画素だけ G-buffer に書く (SSAO・深度のため)
(F) SSAO                   既存のまま (G-buffer に入った不透明な部分が対象)
(G) 陰影の計算             wrapped Lambert、密度格子のレイマーチによる砂の影、splat 層を α で重ねる
(H) 粉塵                   既存のまま
```

ポイントは、**半透明の輪郭と不透明な中身を分けて扱う** ことである。
G-buffer は 1 画素に 1 つの面しか持てないので、被覆率 α が 1 に近い画素（中身）だけを G-buffer に書き、SSAO と深度の前後関係はそれで扱う。
α が小さい輪郭やまばらな部分は別の「splat 層」に置き、陰影の計算で、背後（床・障害物・空）の色の上に α で重ねる。

---

## 2. 描画用の密度格子

### 2.1 格子の大きさ

- 範囲はシミュレーション領域全体 [0, kWorldScale]³（kWorldScale = 1.5 m）とする。
- セルの一辺 h = `densityCellScale` × 粒子間隔。粒子間隔は `RenderSettings::particleSpacing`（main.cpp が設定、既定の 10,000 粒子で約 0.014 m）である。
- 一辺のセル数 N = ceil(kWorldScale / h)。既定値 1.5 では h ≈ 0.021 m、N ≈ 72、セル数は約 37 万になる。
- N の上限は 160 とする（160³ ≈ 410 万セル、uint で 16 MB）。粒子数を増やして粒子間隔が小さくなったときに、メモリが増えすぎないようにするためである。上限に当たったら h を kWorldScale / 160 に広げる。
- 粒子数を変えて再起動すると粒子間隔が変わるので、毎フレーム N を計算し、変わったときだけ作り直す。

MPM の格子（gridIMA、128³）は使わない。セルあたり約 0.6 個と粒子に対して細かすぎ、密度が粒子 1 個ごとの斑点になってしまうためである。

### 2.2 粒子からの密度の加算（S1）

GLSL 4.3 には float の atomicAdd がないので、**uint の固定小数点** で加算する（倍率 kFixed = 1024）。
粒子を最寄りの 1 セルだけに入れると密度がセル単位で段になり、勾配が粗くなる。そこで周囲 8 セルへ三線形の重みで配る。

```glsl
// density_splat.comp (抜粋)
layout(local_size_x = 256) in;
layout(std430, binding = 0) readonly buffer mpmBuffer { mpmParticle p[]; };
layout(std430, binding = 5) buffer DensityBuffer { uint cell[]; };
uniform int particleCount;
uniform int gridN;
uniform float cellSize;			// h [m]
const float kFixed = 1024.0;	// 固定小数点の倍率

void main() {
	uint i = gl_GlobalInvocationID.x;
	if (i >= uint(particleCount)) return;
	vec3 g = p[i].position.xyz / cellSize - 0.5;	// セル中心が整数になる座標
	ivec3 b = ivec3(floor(g));
	vec3 f = g - vec3(b);
	for (int k = 0; k < 8; ++k) {
		ivec3 o = ivec3(k & 1, (k >> 1) & 1, k >> 2);
		ivec3 c = b + o;
		if (any(lessThan(c, ivec3(0))) || any(greaterThanEqual(c, ivec3(gridN)))) continue;
		vec3 w3 = mix(1.0 - f, f, vec3(o));
		atomicAdd(cell[(c.z * gridN + c.y) * gridN + c.x], uint(w3.x * w3.y * w3.z * kFixed + 0.5));
	}
}
```

毎フレームの最初に `glClearBufferData` で 0 にする。
セル中心を (i + 0.5)·h に置くのは、3D テクスチャの texel の中心と同じ約束にして、後で `texture()` の三線形補間でそのまま読めるようにするためである。

### 2.3 float への変換とぼかし（S2）

- `density_resolve.comp` で uint を kFixed で割り、R32F の 3D テクスチャ（`layout(r32f) image3D`）に `imageStore` する。
- `density_blur.comp` で x → y → z の順に 1 次元のガウスぼかしを 3 回かける（2 枚の 3D テクスチャを交互に使う）。標準偏差は `densityBlurSigma` セル、タップ数は片側 ceil(2σ)。
- **ぼかしの強さが、法線の滑らかさを決める唯一の値になる。** これが密度勾配を採用した理由の 1 つ（見かけを 1 つの値で連続に制御できる）である。σ = 0 ならぼかさない。
- サンプラは `GL_LINEAR`、ラップは `GL_CLAMP_TO_BORDER`（境界色 0）にする。領域の外を「空」として扱うためである。

単位は「1 セルあたりの粒子数」である。粒子が隙間なく詰まった状態の値 ρ_ref = (h / 粒子間隔)³（既定で 1.5³ ≈ 3.4）を、しきい値の基準に使う。

---

## 3. 表面らしさと法線

### 3.1 粒子ごとの計算（S3）

`surfel_prepare.comp` で、粒子 1 個 = 1 スレッドとして次を求め、surfel バッファ（binding 4、粒子番号で引く）に書く。

```glsl
// surfel_prepare.comp (抜粋)
struct Surfel { vec4 normalSurface; vec4 extra; };	// xyz: 法線 (ワールド), w: 表面らしさ / x: 密度
layout(std430, binding = 4) writeonly buffer SurfelBuffer { Surfel s[]; };
uniform sampler3D density;
uniform float worldScale, cellSize;
uniform float outsideOffset;			// 法線方向に何 m 先の密度を見るか
uniform float surfaceLo, surfaceHi;		// [個/セル] これより疎なら表面 / 密なら内部 (§8 の割合 × ρ_ref を渡す)

float rhoAt(vec3 x) { return texture(density, x / worldScale).r; }

void main() {
	...
	vec3 x = p[i].position.xyz;
	float e = cellSize;
	vec3 grad = vec3(rhoAt(x + vec3(e, 0, 0)) - rhoAt(x - vec3(e, 0, 0)),
	                 rhoAt(x + vec3(0, e, 0)) - rhoAt(x - vec3(0, e, 0)),
	                 rhoAt(x + vec3(0, 0, e)) - rhoAt(x - vec3(0, 0, e))) / (2.0 * e);
	float g = length(grad);
	float rho = rhoAt(x);
	float strength = g * cellSize / rhoRef;	// 1 セル進む間の密度の変化 (ρ_ref に対する割合)
	bool hasNormal = strength > 0.05;		// 勾配が弱すぎれば向きが定まらないので法線なし
	vec3 n = hasNormal ? -grad / g : vec3(0.0);
	// 法線がない粒子は、周囲が疎なら孤立粒子 (表面)、密なら内部の一様な場所 (内部) とする
	float rhoOut = hasNormal ? rhoAt(x + n * outsideOffset) : rho;
	float surface = 1.0 - smoothstep(surfaceLo, surfaceHi, rhoOut);
	s[i].normalSurface = vec4(n, surface);
	s[i].extra = vec4(rho, strength, 0.0, 0.0);
}
```

- **法線**: 中心差分による密度勾配の逆向き。差分の幅は 1 セル。テクスチャの三線形補間を使うので、粒子の位置で連続に変わる。
- **表面らしさ** surface ∈ [0, 1]: 法線方向に `outsideOffset` だけ外へ出た点の密度 ρ_out で決める。外が空（ρ_out ≤ surfaceLo）なら 1、外も詰まっている（ρ_out ≥ surfaceHi）なら 0。`smoothstep` で連続に変えることで、表面と内部の境目で粒子が急に現れたり消えたりしない。splat_study.md の「隣接セルが空なら表面」を、法線方向の 1 点だけで調べる形にしたものである。
- **法線なし**: 勾配がほとんどない粒子（空中に孤立した粒、または内部の一様な場所）は n = 0 とし、描画では「カメラを向いた円盤」として扱う。このとき表面らしさは、外側の密度の代わりに粒子の位置の密度 ρ で決める。孤立した粒は ρ が小さいので表面として描かれ、内部の一様な場所は ρ が大きいので描かれない。
- **密度** ρ: 陰影のあいまいさ（§5.2）に使う。

PCA を使わないので surface variation σ は得られない。代わりに ρ の小ささ（まばらさ）をあいまいさの指標にする。

### 3.2 内部の粒子

surface が `surfaceCutoff`（既定 0.02）未満の粒子は描かない。可視性・蓄積のどちらのパスにも入らないので、粒子数の割に描画は軽い。
シャドウマップ（A）にはすべての粒子を球で描く。内部の粒子も光を遮るからである。

---

## 4. Deferred splatting

Botsch et al. (2005) の 3 パス（可視性・蓄積・正規化）を、既存の G-buffer に合わせて実装する。

### 4.1 円盤の形

- 表面粒子 1 個を、中心 c = 粒子の位置、法線 n、半径 R = `splatRadiusScale` × 粒子間隔 の円盤とする。
- 円盤内の位置 u = |x − c| / R（0〜1）に対して、重み w(u) = surface × exp(−β u²) とする（β = `splatSharpness`、既定 3 で縁の重みは約 0.05）。
- n = 0 の粒子は、n = カメラの方向（ビュー座標で −c の向き）とする。
- **裏向きの円盤は描かない**（dot(n, −c) < 0）。山の裏側の表面粒子が、半透明の輪郭から透けて見えるのを防ぐためである。

### 4.2 頂点シェーダー（splat.vert）

既存の `sphere.vert` と同じく、粒子 1 個を点スプライト 1 個で描く。点の大きさも同じ式 R · P[1][1] · H / w で、半径 R の球を覆う大きさにする。
描かない粒子（内部・裏向き）は `gl_Position = vec4(2.0, 2.0, 2.0, 1.0)` としてクリップで捨てる。

```glsl
// splat.vert (抜粋)
Surfel sf = s[gl_VertexID];
vec4 pv = view * vec4(position.xyz, 1.0);
vec3 n = (sf.normalSurface.xyz == vec3(0.0)) ? normalize(-pv.xyz) : normalize(mat3(view) * sf.normalSurface.xyz);
bool skip = sf.normalSurface.w < surfaceCutoff || dot(n, -pv.xyz) < 0.0;
v_center = pv.xyz; v_normal = n; v_weight = sf.normalSurface.w; v_fuzz = 1.0 - clamp(sf.extra.x / rhoRef, 0.0, 1.0);
gl_Position = skip ? vec4(2.0, 2.0, 2.0, 1.0) : projection * pv;
gl_PointSize = max(splatRadius * projection[1][1] * viewportHeight / gl_Position.w, 1.0);
```

### 4.3 フラグメントシェーダー: 視線と円盤の交点（splat.frag）

点スプライトの各画素について視線と円盤の平面の交点 x を求め、円盤の外なら捨てる。可視性パスと蓄積パスで同じシェーダーを使い、uniform `pass` で出力を切り替える。

```glsl
// splat.frag (抜粋)
vec2 ndc = gl_FragCoord.xy / viewport * 2.0 - 1.0;
vec3 dir = normalize(vec3(ndc.x / projection[0][0], ndc.y / projection[1][1], -1.0));	// 視線 (ビュー座標)
float dn = dot(dir, v_normal);
if (abs(dn) < 0.05) discard;				// 円盤をほぼ真横から見ている
vec3 x = dir * (dot(v_center, v_normal) / dn);	// 平面との交点
float u2 = dot(x - v_center, x - v_center) / (splatRadius * splatRadius);
if (u2 > 1.0) discard;
float facing = smoothstep(0.05, 0.35, abs(dn));		// 真横に近い円盤は薄くする
float w = v_weight * exp(-splatSharpness * u2) * facing;
if (pass == 0 && w < 0.1) discard;				// 重みの小さい部分で手前の深度を決めない

vec3 xd = (pass == 0) ? x + dir * depthOffset : x;	// 可視性パスでは少し奥へずらす
vec4 clip = projection * vec4(xd, 1.0);
gl_FragDepth = clip.z / clip.w * 0.5 + 0.5;
if (pass == 1) {
	o_accum0 = vec4(albedo * w, w);			// RGBA16F: Σw·色, Σw
	o_accum1 = vec4(v_normal * w, v_fuzz * w);	// RGBA16F: Σw·法線, Σw·あいまいさ
	o_accum2 = -x.z * w;					// R32F:    Σw·線形深度
}
```

段階 2 の実装で、次の 2 点を加えた。

- **可視性パスの重みの下限**: 表面らしさの低い粒子や円盤の縁（重み 0.1 未満）は、可視性パスで深度を書かない。書いてしまうと、ほとんど寄与しない円盤が後ろの円盤を締め出し、その円盤の色だけが縁まで残って、円盤の形がうろこ状に見えた。
- **横向きの減衰**: 視線と円盤の角度が浅いほど重みを下げる。真横に近い円盤は細い棒になり、薄い膜の縁で筋として目立つためである。

色（albedo）は `sphere.frag` と同じ、材質の基本色 + 粒子番号のハッシュによる明るさのばらつきとする。粒状感はこの色のばらつきで出し、法線には凹凸を加えない。

### 4.4 パスの組み立て（SplatSurface::draw）

| パス | 描画先 | 深度テスト | 深度の書き込み | ブレンド |
| --- | --- | --- | --- | --- |
| P1 深度のコピー | splat 用の深度 (D32F) | — | — | — |
| 被覆率（段階 3） | coverage (R16F) | LEQUAL（床・障害物の深度） | しない | 加算 (ONE, ONE) |
| P2 可視性 | splat 用の深度のみ（カラーなし） | LESS | する | なし |
| P3 蓄積 | accum0〜2 + splat 用の深度 | LEQUAL | しない | 加算 (ONE, ONE) |
| P4 正規化 → splat 層 | splat 層 (3 枚) | なし | しない | なし |
| P5 正規化 → G-buffer | G-buffer (3 枚 + 深度) | ALWAYS | する | なし |

- **P1** で G-buffer の深度（床と障害物）を splat 用の深度へ `glBlitFramebuffer` でコピーする。可視性パスが G-buffer の深度を直接書き換えると、半透明の輪郭の画素で床の深度が失われ、床の陰影（影・AO）がずれるためである。床や障害物の後ろの円盤は、この深度で正しく隠れる。
- **P2** の奥へのずらし量 `depthOffset` = `splatDepthOffsetScale` × R。この厚みの内側にある円盤が P3 で混ざり合い、外側（奥の面）は捨てられる。
- **P3** は P2 の深度を LEQUAL で比べるだけで、書き込まない。
- **P4/P5** は同じ全画面シェーダー `splat_normalize.frag` を uniform で切り替えて 2 回描く。

```glsl
// splat_normalize.frag (抜粋)
vec4 a0 = texelFetch(accum0, p, 0);
vec4 a1 = texelFetch(accum1, p, 0);
float sw = a0.a;
if (sw < 1e-3) discard;
vec3 albedo = a0.rgb / sw;
vec3 n = normalize(a1.xyz);
float fuzz = a1.w / sw;
float depthLin = texelFetch(accum2, p, 0).r / sw;
float alpha = 1.0 - exp(-coverageK * sw);	// 被覆率
if (target == 1 && alpha < opaqueAlpha) discard;	// P5: 不透明な中身だけ G-buffer へ
// 出力: albedo (+ α または 種別 0.25), 法線 (+ fuzz), 線形深度、gl_FragDepth = 線形深度から求めた深度
```

**被覆率** α = 1 − exp(−k Σw) は、その画素がどれだけ粒子に覆われているかを表す。

段階 3 の実装では、被覆率の Σw を、可視性パスで決めた手前の層の重みではなく、**視線上のすべての円盤の重み** にした。P1 の直後（可視性パスの前）に、床・障害物の深度とだけ比べて、すべての円盤（裏向きのものや、表面の少し下のものも含む）の重みを別のテクスチャ（R16F）に加算する。手前の層の重みだけで決めると、山の中で手前の層の円盤がたまたま少ない所の α が下がり、円盤の形の穴が開いて、後ろの床が透けて見えたためである。粒子が詰まった中身では Σw が大きく α ≈ 1、輪郭やまばらな所では Σw が小さく α が下がる。k = `coverageK` で、どれだけ速く不透明になるかを決める。

### 4.5 splat 層

P4 の出力先として、G-buffer と同じ大きさのターゲットを 1 つ持つ。

| location | 形式 | 内容 |
| --- | --- | --- |
| 0 | RGBA8 | rgb: 色, a: 被覆率 α |
| 1 | RGBA16F | xyz: ビュー空間の法線, w: あいまいさ fuzz |
| 2 | R32F | 線形深度 −z_view [m]（0 = splat なし） |

P5 で G-buffer に書く画素は、法線の w に fuzz を入れる（G-buffer の法線 RGBA16F の w は今は使っていない）。

---

## 5. 陰影の計算の変更

### 5.1 splat 層を重ねる

`lighting.frag` に splat 層の 3 枚（テクスチャユニット 5〜7）と `useSplatLayer` を追加する。

```glsl
// lighting.frag (追加分の考え方)
vec3 base = shade(G-buffer の画素);	// 今の処理 (背景・床・障害物・不透明な splat)
float a = texture(splatAlbedo, v_uv).a;
float zs = texture(splatDepth, v_uv).r;
bool translucent = useSplatLayer && a > 0.0 && a < opaqueAlpha && (G-buffer の画素より手前);
vec3 color = translucent ? mix(base, shade(splat 層の画素), a) : base;
```

- 陰影の計算の本体（太陽光・影・環境光・AO）を関数 `shade(albedo, worldPos, worldNormal, ao, fuzz)` にまとめ、G-buffer の画素と splat 層の画素の両方に使う。
- splat 層の画素の AO は、その画素の G-buffer 側の AO をそのまま使う（輪郭なので遮蔽は小さく、目立たない）。
- 深度は G-buffer の値を書く（半透明の輪郭は、後から描く枠線を隠さない）。

### 5.2 柔らかい拡散反射（wrapped Lambert）

粒子の画素（種別 0.25 と splat 層）では、ランバートの n·l の代わりに次を使う。

```glsl
float wrap = splatWrap + fuzzWrap * fuzz;	// まばらな所ほど陰影をあいまいに
float ndl = clamp((dot(worldNormal, sunDirection) + wrap) / (1.0 + wrap), 0.0, 1.0);
```

- wrap = 0 で通常のランバート、大きくすると明暗の境目が広がり、影側にも光が回り込む。
- fuzz = 1 − ρ / ρ_ref はまばらさで、PCA の surface variation の代わりに使う。
- 影の判定（シャドウマップ）は、ndl > 0 のときに今と同じく行う。Oren–Nayar は wrapped Lambert で足りないと分かってから試す。

### 5.3 影の自己遮蔽

円盤は粒子の中心を通り、さらに重み付き平均で少し奥に下がるので、シャドウマップに描いた球（半径 = 描画半径）より低い。そのままでは近くの球の影に入って斑点になる。段階 2 で試した結果、描画半径の 2 倍のずらしでは斑点が残り、3 倍でほぼ消えた。

ただし、すべての画素をずらすと、床の上の飛んでいる粒の影まで消えてしまう。そこで `lighting.frag` に `particleShadowOffset` を加え、**粒子の画素（種別 0.25）だけ** を余分にずらす（Surface モード: 描画半径、Splat モード: 描画半径の 3 倍）。Surface モードも、以前は床を含むすべての画素をずらしていたが、粒子の画素だけに改めた。

---

## 6. 段階ごとの手順と完了条件

### 6.1 段階 1: 格子と法線

1. `RenderMode::Splat = 3` と §8 のパラメータを `RenderSettings.h` に追加し、UI に「Splat」のラジオボタンを足す。
2. `SplatSurface` クラスを作り、S1〜S3（密度の加算、float への変換とぼかし、surfel の計算）を実装する。
3. この段階の Splat モードでは、粒子を今の `drawSpheres` で描き、色を surfel バッファから決めるデバッグ表示を付ける（`sphere.vert` で binding 4 を読む）。表示は「表面らしさ」「法線」「密度」の 3 種類を UI で切り替える。

**完了条件**

- 砂山の表面の粒子だけが表面らしさ ≈ 1 になり、内部は 0、境目は連続に変わる。
- 法線の色（n × 0.5 + 0.5、ワールド座標）が斜面の向きと合い、カメラを回しても変わらない。
- 「Density blur」を動かすと、法線の色の滑らかさが連続に変わる。
- 格子の準備（S）の GPU 時間が分かる（PassTimer に "Grid" を追加）。

### 6.2 段階 2: Deferred splatting

1. `splat.vert` / `splat.frag` / `splat_normalize.frag` と、accum・splat 層・splat 用深度のターゲットを作る。
2. `drawScene` の Splat モードで、(B) の粒子の球を描かず、P1〜P5 を行う。
3. この段階では P5 の `opaqueAlpha` を 0 にして、すべての splat 画素を G-buffer に書く（半透明はまだ使わない）。

**完了条件**

- 球の陰影の濃淡が見えず、表面が粒子の集まりとして連続して見える。
- 斜めから見たときに、Surface モードで出ていた波打ちやちらつきが出ない（同じ視点・同じフレームで比べる）。
- 床・障害物との前後関係が正しい（円盤が障害物を突き抜けない）。
- デバッグ表示（Albedo / Normal / AO / Shadow）が今までどおり使える。

### 6.3 段階 3: あいまいさ

1. `opaqueAlpha` を既定値に戻し、splat 層と `lighting.frag` の重ね合わせ（§5.1）を入れる。
2. wrapped Lambert と fuzz（§5.2）を入れる。
3. デバッグ表示に「Coverage」（α）を加える。

**完了条件**

- 輪郭と、崩れて広がった薄い部分がぼんやり半透明になり、背後の床が透けて見える。
- 明暗の境目が柔らかくなり、陰側が真っ黒にならない。
- 粉塵（H）が今までどおり重なる。

### 6.4 段階 4（任意）: 密度格子による柔らかい影

S2 で作った 3D テクスチャを、陰影の計算で太陽の方向にレイマーチし、透過率 T = exp(−κ Σ ρ Δs) を求める。
シャドウマップの代わりに使うか、シャドウマップの結果に掛ける。表面のすぐ下が少し暗くなり、薄い所は光が透ける。
段階 3 までの見た目を確認してから、必要かどうかを決める。

**実装（段階 4）**: `lighting.frag` の `densityTransmittance()` で、表面の点から法線方向へ 1 セル外に出た点を出発点にし、太陽の方向へ 1 セルずつ進みながら光学的厚さ τ を足す（格子の外に出るか τ > 8 で打ち切り）。1 セル進むごとの τ は「密度 / ρ_ref × `densityShadowOpacity`」で、詰まった砂を 1 セル通ると τ = 1.5（既定値）になる。シャドウマップには障害物だけを描き、その結果と透過率を掛け合わせる。粒子の球がシャドウマップから消えるので、段階 2 の影の斑点のためのずらし（描画半径の 3 倍）も不要になった。

この結果、縁の外の床に並んでいた暗い点（縁の粒の球が落とす影）が消え、床の影も柔らかくなった。低い角度からの比較（70 フレーム目）では、暗い画素 1.87%・隣り合うフレームの明るさの差 1.86 となり、Surface モード（1.97%・2.13）より良くなった。

**粉塵の影**: 粉塵は中心の 1 点だけで影を判定するので、砂の層の中で発生した粉塵が影と判定され、空の光だけの灰色の斑点として目立っていた。密度格子の影を使うとシャドウマップに砂がないので、この斑点は出なくなる。あわせて、粉塵の影の効き具合 `dustShadow`（既定 0.3、0 で影なし）を加え、ほかのモードでも弱められるようにした。

---

## 7. ファイル構成と変更点

### 7.1 新しく作るファイル

| ファイル | 内容 |
| --- | --- |
| `src/render/SplatSurface.h/.cpp` | 密度格子・surfel バッファ・各ターゲットの管理。`prepare()` で S1〜S3、`draw()` で P1〜P5 |
| `src/render/shaders/density_splat.comp` | S1 粒子 → 密度格子（固定小数点の atomicAdd） |
| `src/render/shaders/density_resolve.comp` | S2 uint → float の 3D テクスチャ |
| `src/render/shaders/density_blur.comp` | S2 1 次元のガウスぼかし（方向を uniform で指定） |
| `src/render/shaders/surfel_prepare.comp` | S3 表面らしさ・法線・密度 |
| `src/render/shaders/splat.vert` / `splat.frag` | P2/P3 円盤の描画（可視性・蓄積） |
| `src/render/shaders/splat_normalize.frag` | P4/P5 正規化と被覆率 |

ファイルは既存と同じく BOM なし UTF-8、改行は CRLF とする。

### 7.2 変更するファイル

| ファイル | 変更内容 |
| --- | --- |
| `src/render/RenderSettings.h` | `RenderMode::Splat`、§8 のパラメータ、`debugView` の項目追加 |
| `src/render/Renderer.h/.cpp` | `SplatSurface` の保持、`drawScene` の Splat の分岐、影の法線オフセット、UI |
| `src/render/shaders/lighting.frag` | `shade()` への整理、splat 層の重ね合わせ、wrapped Lambert、密度格子の影（`densityTransmittance()`）、粒子の画素だけの影のずらし（`particleShadowOffset`）、デバッグ表示（Coverage） |
| `src/render/shaders/sphere.vert` / `sphere.frag` | 段階 1 のデバッグ色（surfel バッファを読む） |
| `src/render/DustSystem.cpp`, `shaders/dust.vert` | 粉塵の影の効き具合 `dustShadow`（段階 4 で追加。§6.4） |
| `GPUMPMTex3D.vcxproj` / `.vcxproj.filters` | 新しいファイルの追加 |

main.cpp は変更しない（描画モードの切り替えは `Renderer` の中で完結している）。

### 7.3 バッファとテクスチャの束縛

| 束縛 | 内容 |
| --- | --- |
| SSBO binding 0 | 粒子バッファ（既存、読み出しのみ） |
| SSBO binding 4 | surfel バッファ（粒子数 × 32 バイト） |
| SSBO binding 5 | 密度格子（uint、N³） |
| 粉塵の binding 2, 3 | 既存のまま（重ならないようにする） |
| `lighting.frag` のテクスチャユニット 5〜7 | splat 層（色 + α、法線 + あいまいさ、線形深度） |
| `lighting.frag` のテクスチャユニット 8 | 密度格子の 3D テクスチャ（影のレイマーチ） |

蓄積用のターゲット（`SplatSurface::accumTarget`）は 4 枚で、0: RGBA16F（Σw·色, Σw）、1: RGBA16F（Σw·法線, Σw·あいまいさ）、2: R32F（Σw·線形深度）、3: R16F（被覆率のパスの Σw）と、深度 D32F からなる。

---

## 8. パラメータの初期値

| 名前 | 初期値 | 意味 |
| --- | --- | --- |
| `densityCellScale` | 1.5 | 格子のセルの一辺 = この値 × 粒子間隔 |
| `densityBlurSigma` | 1.0 | 密度のぼかしの標準偏差 [セル]。法線の滑らかさを決める |
| `outsideOffsetScale` | 1.0 | 表面判定で法線方向に見る距離 = この値 × セルの一辺 |
| `surfaceLo` / `surfaceHi` | 0.25 / 0.75 | 表面判定のしきい値（ρ_ref に対する割合） |
| `surfaceCutoff` | 0.02 | 表面らしさがこれ未満の粒子は描かない |
| `splatRadiusScale` | 1.8 | 円盤の半径 = この値 × 粒子間隔 |
| `splatSharpness` | 3.0 | カーネル exp(−β u²) の β |
| `splatDepthOffsetScale` | 0.5 | 可視性パスの奥へのずらし量 = この値 × 円盤の半径 |
| `coverageK` | 2.0 | 被覆率 α = 1 − exp(−k Σw) の k |
| `opaqueAlpha` | 0.9 | α がこれ以上の画素を不透明として G-buffer に書く |
| `splatWrap` | 0.3 | wrapped Lambert の基本の a |
| `fuzzWrap` | 0.4 | まばらさ fuzz に応じて a に足す量 |
| `densityShadow` | true | 砂の影を密度格子のレイマーチで求める（段階 4） |
| `densityShadowOpacity` | 1.5 | 詰まった砂 1 セル分の光学的厚さ（大きいほど影が濃い） |
| `dustShadow` | 0.3 | 粉塵の影の効き具合（0 = 影なし）。全モード共通 |
| `splatting` | true | 円盤を重ねて描く。false で段階 1 の球の表示（比較用） |
| `surfelDebug` | Off | 表面らしさ・法線・密度を色で表示する（デバッグ） |
| `hideInterior` | false | 球で描くとき、内部の粒子を描かない（デバッグ） |

いずれも見かけを見ながら決める値なので、UI のスライダーから変更できるようにする。

---

## 9. 性能の見積もりと計測

既定の 10,000 粒子、N ≈ 72（約 37 万セル）での見積もりである。

- S1: 粒子 1 個あたり 8 回の atomicAdd。粒子数に比例し、10,000 粒子では無視できる。
- S2: 37 万セル × (変換 1 回 + ぼかし 3 回 × 5 タップ)。数十万回の読み書きで、統合 GPU でも 1 ms 程度以内と見込む。
- S3: 粒子 1 個あたりテクスチャ 8 回の参照。
- P2/P3: 描くのは表面粒子だけで、1 個あたり画面上で直径 2R の点スプライト 2 回分。Spheres モードで全粒子を 1 回描くのと同程度。
- P4/P5、splat 層の陰影: 全画面パスが 2 回と、陰影の計算の一部が 2 倍になる。

粒子数を増やすと N の上限（160）に達し、S2 は最大で約 400 万セルになる。PassTimer に "Grid"（S）と "Splat"（P）を加えて実測し、重ければぼかしを止める（σ = 0）か、格子を粗くする。

---

## 10. 確認項目

**結果（段階 4 まで、ヘッドレス環境での確認）**: 低い角度から見た 70 フレーム目で、§10 の 2 のちらつきの指標を Surface モードと比べた。

| | Surface | 段階 2 | 段階 3 | 段階 4 |
| --- | --- | --- | --- | --- |
| 暗い画素の割合 | 1.97% | 4.75% | 2.60% | 1.87% |
| 隣り合うフレームの明るさの差（平均） | 2.13 | 3.52 | 2.91 | 1.86 |
| 明るさが 40 以上変わる画素の割合 | 0.60% | 1.23% | 0.67% | 0.12% |

段階 2 では薄い膜の縁が影の側で暗く、縁の粒の球が床に影を落としていたため悪化した。段階 3 の柔らかい拡散反射と、段階 4 の密度格子の影で改善し、Surface モードより良くなった。

[rendering_implementation.md](rendering_implementation.md) で使った検証の手順（同じフレーム・同じ視点のスクリーンショットの比較、隣接フレーム間の差によるちらつきの指標）を Splat モードでも使う。

1. 段階 1 のデバッグ表示で、表面らしさと法線が §6.1 の完了条件を満たす。
2. Surface モードで問題になった視点（斜めから見た床付近、グレージング角）で、波打ち・ちらつきが出ない。ちらつきの指標を Surface モードと比べる。
3. カメラだけを動かしたとき（シミュレーションを止めた状態）、砂の表面の明暗の分布が変わらない（視点に依存しない）。
4. 輪郭が半透明になり、背景が透ける。被覆率のデバッグ表示で、中身が α ≈ 1、輪郭で下がる。
5. 床・障害物・粉塵・枠線との前後関係が正しい。
6. 粒子数を変えて再起動しても、格子が作り直されて正しく表示される。
7. 各パスの GPU 時間が実時間の範囲に収まる。

---

## 11. 注意点

- **粒子が疎な場合**: 粒子数が少ない（既定 10,000）と、密度が粒子 1 個ごとの斑点になりやすく、法線が乱れて円盤の間に穴が開く。まず `densityBlurSigma` と `splatRadiusScale` を大きくし、それでも足りなければ `densityCellScale` を大きくする。
- **薄く広がった砂**: 厚さが 1〜2 粒子の膜では、表と裏の区別が付かず法線が定まらない。シミュレーション側の問題 A-2（C × dx）・A-3（粒子体積）が膜の厚さに影響している可能性があるので、見かけの問題がシミュレーションによるものかを、点描画（Points モード）でも確認する。
- **床・障害物に接する粒子**: 床の下は密度 0 なので、床に接する最下層の粒子も「表面」と判定され、法線が下を向く。裏向きの円盤は描かない（§4.1）ので通常は見えないが、輪郭で気になる場合は、床より下を密度 ρ_ref で埋めたものとして S3 で扱う（床の高さと法線は `MpmPhysics` から渡せる）。
- **固定小数点の桁あふれ**: 1 セルの値は最大でも（粒子数 × kFixed）で、10,000 粒子なら 1,024 万と uint の範囲に十分収まる。
- **16F の蓄積**: accum0/1 は RGBA16F でよい。1 画素に重なる円盤は多くても数十枚で、重み w ≤ 1 なので、粉塵のときのような桁あふれは起きない。深度の蓄積 accum2 は精度のため R32F にする。
- **gl_FragDepth**: splat.frag で深度を書くので early-z が効かない。表面粒子だけを描くので負荷は小さいが、計測で確かめる。
- **シャドウマップ**: 段階 3 までは今の球の描画のままにする。円盤で描くと光源から見て真横の円盤が影を落とさず、影に穴が開くためである。
