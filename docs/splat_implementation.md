# Splat モードの実装解説書

本書は、`todo-fix4` ブランチで実装した「格子による表面粒子の抽出とスプラッティング（Splat モード）」について、実際のコードに沿って、処理の内容・設計の理由・調整の経緯を説明する。

- 手法の検討（スクリーン空間の方法が土砂に合わない理由、法線に密度勾配を採用した理由）は [splat_study.md](splat_study.md)
- 実装前の計画は [splat_guide.md](splat_guide.md)
- Spheres / Surface モードと粉塵の実装は [rendering_implementation.md](rendering_implementation.md)

にある。本書は、計画に対して **実際に何をどう実装したか**（計画から変えた点を含む）をまとめたものである。
シミュレーション（`src/mpm/`）は変更しておらず、描画は粒子バッファを読み出すだけである。

---

## 目次

1. [全体像](#1-全体像)
2. [ファイル構成](#2-ファイル構成)
3. [描画用の密度格子](#3-描画用の密度格子)
4. [表面らしさと法線](#4-表面らしさと法線)
5. [Deferred splatting](#5-deferred-splatting)
6. [陰影の計算](#6-陰影の計算)
7. [粉塵の影](#7-粉塵の影)
8. [デバッグ表示](#8-デバッグ表示)
9. [パラメータ一覧](#9-パラメータ一覧)
10. [開発中に見つかった問題と対処](#10-開発中に見つかった問題と対処)
11. [検証の方法と結果](#11-検証の方法と結果)
12. [既知の制限と今後の課題](#12-既知の制限と今後の課題)
13. [参考文献](#13-参考文献)

---

## 1. 全体像

### 1.1 考え方

土砂の表面は連続した 1 枚の面ではなく、表面付近の粒子群がぼんやりと作る層である。
Splat モードでは、面をはっきり抽出するのではなく、次のように描く。

1. 描画専用の粗い **密度格子** に粒子の個数を集める。
2. 密度格子から、粒子ごとに **表面らしさ**（0〜1）と **法線**（密度勾配の逆向き）を求める。
3. 表面の粒子を、法線の向きに向けた **半透明の円盤（surfel）** として重ねて描き、重み付き平均で色と法線を混ぜる。
4. 視線上の粒子の量から **被覆率** α を求め、輪郭やまばらな所を半透明にする。
5. 拡散反射を柔らかくし（wrapped Lambert）、砂の影を **密度格子のレイマーチ** で求める。

法線と表面の判定を物体空間（格子）で求めるので、視点を動かしても見え方が変わらず、フレーム間でも安定する。
これが、スクリーン空間で深度を平滑化する Surface モードとの最大の違いである。

### 1.2 1 フレームの処理の流れ

Splat モードでは、`Renderer::drawScene()` が次のパスを順に実行する。
(A)(B)(F)(G)(H) は Spheres / Surface モードと共通のパス（[rendering_implementation.md](rendering_implementation.md) §1.2）で、(S) と (P) が新しいパスである。
括弧内は UI の GPU 時間の表示名である。

```
 ───── 描画 (Renderer::drawScene, Splat モード) ───────────────────────────
        │
 (S) 格子の準備 (Grid)          SplatSurface::prepare
       S1 density_splat.comp     粒子 → 密度格子 (uint の SSBO へ固定小数点で atomicAdd)
       S2 density_resolve.comp   uint → float の 3D テクスチャ
          density_blur.comp      x → y → z の 3 回でガウスぼかし
       S3 surfel_prepare.comp    粒子ごとに 表面らしさ・法線・密度 → surfel バッファ
 (A) シャドウマップ (Shadow)    障害物だけ (砂の影は (G) で密度格子から求める)
 (B) G-buffer (G-buffer)        床 + 障害物 (粒子は描かない)
 (P) スプラッティング (Splat)   SplatSurface::draw
       P1 G-buffer の深度をコピーし、蓄積バッファを 0 にする
       Pc 被覆率      視線上のすべての円盤の重みを加算 (床・障害物の深度とだけ比べる)
       P2 可視性      表面の円盤を少し奥へずらした深度で描き、手前の層の厚みを決める
       P3 蓄積        厚みの内側の円盤の 重み × 色・法線・深度 を加算
       P4 正規化      → splat 層 (色 + 被覆率、法線 + あいまいさ、線形深度)
       P5 正規化      → G-buffer (被覆率 0.9 以上の不透明な画素だけ)
 (F) SSAO (SSAO)                G-buffer の不透明な部分が対象
 (G) 陰影の計算 (Lighting)      wrapped Lambert、密度格子の影、splat 層を α で重ねる
 (H) 粉塵 (Dust)                従来どおり (影の効きを弱めた。§7)
```

ポイントは、**半透明の輪郭と不透明な中身を分けて扱う** ことである。
G-buffer は 1 画素に 1 つの面しか持てないので、被覆率 α が 1 に近い画素（中身）だけを G-buffer に書き、SSAO と深度の前後関係はそれで扱う。
α が小さい輪郭やまばらな部分は別の「splat 層」に置き、(G) で背後（床・障害物・空）の色の上に α で重ねる。

### 1.3 バッファの構成

| バッファ | 形式 | 内容 |
|---|---|---|
| 密度格子（SSBO binding 5） | uint × N³ | 固定小数点（× 1024）の粒子数 |
| 密度テクスチャ（2 枚） | R32F の 3D テクスチャ（N³） | 1 セルあたりの粒子数。ぼかしで交互に使う。格子の外は 0（`GL_CLAMP_TO_BORDER`） |
| surfel バッファ（SSBO binding 4） | vec4 × 2 × 粒子数 | `normalSurface`（xyz: 法線、w: 表面らしさ）、`extra`（x: 密度、y: 勾配の強さ） |
| 蓄積 0 | RGBA16F | Σw·色、Σw |
| 蓄積 1 | RGBA16F | Σw·法線（ビュー空間）、Σw·あいまいさ |
| 蓄積 2 | R32F | Σw·線形深度（精度のため 32F） |
| 蓄積 3 | R16F | 被覆率のパスの Σw（視線上のすべての円盤） |
| 蓄積の深度 | D32F | P1 で G-buffer からコピー。P2 で手前の層の深度を書く |
| splat 層 0 | RGBA8 | 色、被覆率 α |
| splat 層 1 | RGBA16F | 法線（ビュー空間）、あいまいさ |
| splat 層 2 | R32F | 線形深度 $-z_{view}$ [m]（0 = なし） |

G-buffer の構成は Spheres / Surface モードと同じである。
ただし、Splat モードの粒子の画素では、法線（location 1）の w に **あいまいさ** fuzz = 1 − ρ / ρ_ref を入れる（ほかのモードでは 0）。

---

## 2. ファイル構成

### 2.1 新しく作ったファイル

| ファイル | 役割 | 段階 |
|---|---|---|
| `src/render/SplatSurface.h/.cpp` | 密度格子・surfel バッファ・蓄積と splat 層のターゲットの管理。`prepare()` で (S)、`draw()` で (P) | 1〜3 |
| `src/render/shaders/density_splat.comp` | S1 粒子 → 密度格子（三線形の重み、固定小数点の atomicAdd） | 1 |
| `src/render/shaders/density_resolve.comp` | S2 uint → float の 3D テクスチャ | 1 |
| `src/render/shaders/density_blur.comp` | S2 1 次元のガウスぼかし（方向を uniform で指定） | 1 |
| `src/render/shaders/surfel_prepare.comp` | S3 表面らしさ・法線・密度 | 1 |
| `src/render/shaders/splat.vert/.frag` | 円盤の描画（被覆率・可視性・蓄積の 3 つのパスで共用） | 2, 3 |
| `src/render/shaders/splat_normalize.frag` | P4/P5 正規化と被覆率 | 2, 3 |

### 2.2 変更したファイル

| ファイル | 変更内容 |
|---|---|
| `src/render/RenderSettings.h` | `RenderMode::Splat`、`SurfelDebug`、Splat モードのパラメータ、`dustShadow`、デバッグ表示の Coverage |
| `src/render/Renderer.h/.cpp` | `SplatSurface` の保持、`drawScene()` の Splat の分岐（(S)・(P)・シャドウマップの粒子の省略）、陰影の計算への splat 層と密度テクスチャの受け渡し、影の参照位置のずらし、UI |
| `src/render/shaders/lighting.frag` | 陰影の計算を `shade()` にまとめ、G-buffer の画素と splat 層の画素の両方に使う。splat 層の重ね合わせ、wrapped Lambert、密度格子の影（`densityTransmittance()`）、粒子の画素だけの影のずらし、Coverage の表示 |
| `src/render/shaders/sphere.vert/.frag` | 段階 1 のデバッグ色（surfel バッファを読む）、内部の粒子を描かない表示 |
| `src/render/DustSystem.cpp`, `shaders/dust.vert` | 粉塵の影の効き具合 `dustShadow` |
| `GPUMPMTex3D.vcxproj(.filters)` | 上のファイルの追加 |

`main.cpp` は変更していない。描画モードの切り替えは `Renderer` の中で完結している。

---

## 3. 描画用の密度格子

### 3.1 格子の大きさ（`SplatSurface::prepare`）

範囲はシミュレーション領域全体 [0, 1.5 m]³ である。

- セルの一辺 h = `densityCellScale`（既定 1.5）× 粒子間隔。粒子間隔は `RenderSettings::particleSpacing`（main.cpp が設定、既定の 10,000 粒子で約 14 mm）。
- 一辺のセル数 N = ceil(1.5 / h)。既定では h ≈ 20.9 mm、N = 72（約 37 万セル）。
- N は 8〜160 に制限する。160 を超えるときは h を 1.5 / 160 に広げる（160³ の uint で 16 MB）。
- N は毎フレーム計算し、変わったときだけ格子を作り直す。粒子数を変えて再起動したときにも追従する。

MPM の格子（128³）は使わない。セルあたり約 0.6 個と粒子に対して細かすぎ、密度が粒子 1 個ごとの斑点になってしまうためである。

粒子が隙間なく詰まった状態の密度を **ρ_ref** = (h / 粒子間隔)³ とし（既定で約 3.4 個/セル）、しきい値や影の濃さの基準にする。

### 3.2 粒子からの密度の加算（S1, `density_splat.comp`）

GLSL 4.3 には float の atomicAdd がないので、**uint の固定小数点**（× 1024）で加算する。
粒子を最寄りの 1 セルではなく、周囲 8 セルへ **三線形の重み** で配る。最寄りの 1 セルだけに入れると密度がセル単位の段になり、勾配が粗くなるためである。

セル (i, j, k) の中心を ((i, j, k) + 0.5) h に置く。3D テクスチャの texel の中心と同じ約束にして、後で `texture()` の三線形補間でそのまま読めるようにするためである。

毎フレームの最初に `glClearBufferData` で 0 にする。1 セルの値は最大でも 粒子数 × 1024 で、10,000 粒子なら約 1,000 万と uint の範囲に十分収まる。

### 3.3 float への変換とぼかし（S2）

- `density_resolve.comp` で 1024 で割って「1 セルあたりの粒子数」に戻し、R32F の 3D テクスチャに `imageStore` する。
- `density_blur.comp` で x → y → z の順に 1 次元のガウスぼかしを 3 回かける。標準偏差は `densityBlurSigma`（既定 1.0 セル）、片側のタップ数は ceil(2σ)（最大 8）。
- 格子の外は 0（空）として扱い、重みの和には含める。境界で密度が下がり、外が空であることが法線に残る。

**ぼかしの強さが、法線の滑らかさを決める唯一の値になる。**
これが、法線に PCA ではなく密度勾配を採用した理由の 1 つ（見かけを 1 つの値で連続に制御できる）である（[splat_study.md](splat_study.md)「(a) を採用する理由」）。

---

## 4. 表面らしさと法線

### 4.1 粒子ごとの計算（S3, `surfel_prepare.comp`）

粒子 1 個 = 1 スレッドで、次を求めて surfel バッファに書く。

- **法線**: n = −∇ρ / |∇ρ|。中心差分の幅は 1 セルで、テクスチャの三線形補間を使うので、粒子の位置で連続に変わる。
- **勾配の強さ**: |∇ρ|·h / ρ_ref（1 セル進む間の密度の変化を ρ_ref に対する割合で表したもの）。これが 0.05 未満なら向きが定まらないので **法線なし**（n = 0）とする。
- **表面らしさ**: 法線方向に `outsideOffsetScale`（既定 1.0）セル外へ出た点の密度 ρ_out で決める。

$$
\text{surface} = 1 - \mathrm{smoothstep}(\text{lo}\cdot\rho_{ref},\ \text{hi}\cdot\rho_{ref},\ \rho_{out})
$$

  lo = 0.25、hi = 0.75 が既定である。外が空なら 1、外も詰まっていれば 0 で、`smoothstep` によって境目で粒子が急に現れたり消えたりしない。
  法線がない粒子は、ρ_out の代わりに粒子の位置の密度 ρ で決める。空中に孤立した粒は ρ が小さいので表面になり、内部の一様な場所は ρ が大きいので内部になる。
- **密度** ρ: 陰影のあいまいさ（§6.2）に使う。

### 4.2 内部の粒子

表面らしさが `surfaceCutoff`（既定 0.02）未満の粒子は内部として扱い、円盤を描かない。

立方体の砂（10,000 粒子）での表面らしさの分布は、0.8 以上が約 2,850 個、0.1〜0.8 が約 2,100 個、0.1 未満が約 5,000 個だった。
立方体の一番外側の層は約 2,600 個（10,000 − 19.5³）なので、表面と判定された粒子はほぼ外側の 1 層に一致する。
その内側の 1〜2 層は中間の値になり、重みが小さくなる。

---

## 5. Deferred splatting

Botsch et al. (2005) の 3 パス（可視性・蓄積・正規化）を、既存の G-buffer に合わせて実装した。
段階 3 で、被覆率のための 1 パスを加えた。

### 5.1 円盤（`splat.vert`）

粒子 1 個を点スプライト 1 個として描く。点の大きさは `sphere.vert` と同じ式で、半径 R の球を覆う大きさにする（円盤はその球の内側に入るので、どの向きでもはみ出さない）。

- 中心 = 粒子の位置、向き = 法線、半径 R = `splatRadiusScale`（既定 1.8）× 粒子間隔（既定で約 25 mm）。
- 法線がない粒子（孤立した粒）は、カメラを向いた円盤にする。
- **裏向きの円盤は描かない**（dot(n, −c) < 0）。山の裏側の表面粒子が、半透明の輪郭から透けて見えるのを防ぐためである。ただし被覆率のパスでは描く（§5.4）。そのため、裏向きの円盤は交点の計算用にカメラ側へ向け直しておく。
- 描かない粒子（内部・裏向き）は、`gl_Position` をクリップ空間の外へ出して捨てる。

### 5.2 視線と円盤の交点と重み（`splat.frag`）

点スプライトの各画素について、視線と円盤の平面の交点 x を求め、円盤の外なら捨てる。

```glsl
vec2 ndc = gl_FragCoord.xy / viewport * 2.0 - 1.0;
vec3 dir = normalize(vec3(ndc.x / projection[0][0], ndc.y / projection[1][1], -1.0));	// 視線
float dn = dot(dir, v_normal);
if (abs(dn) < 0.05) discard;					// ほぼ真横から見ている
vec3 x = dir * (dot(v_center, v_normal) / dn);	// 平面との交点
float u2 = dot(x - v_center, x - v_center) / (splatRadius * splatRadius);
if (u2 > 1.0) discard;							// 円盤の外
float facing = smoothstep(0.05, 0.35, abs(dn));
float w = v_weight * exp(-splatSharpness * u2) * facing;
```

重みは **表面らしさ × ガウスカーネル × 横向きの減衰** である。

- ガウスカーネル exp(−β u²) の β = `splatSharpness`（既定 3）で、縁の重みは約 0.05 になる。
- 横向きの減衰は、視線と円盤の角度が浅いほど重みを下げる。真横に近い円盤は細い棒になり、薄い膜の縁で筋として目立つためである（§10.2）。
- 色は `sphere.frag` と同じく、材質の基本色に粒子番号のハッシュによる明るさのばらつきを加える。粒状感はこの色のばらつきで出し、法線には凹凸を加えない。球の法線のような粒子ごとの濃淡がないので、球が見える問題は起きない。

### 5.3 パスの組み立て（`SplatSurface::draw`）

| パス | 描画先 | 深度テスト | 深度の書き込み | ブレンド | 裏向き |
|---|---|---|---|---|---|
| P1 深度のコピー | 蓄積の深度 | — | — | — | — |
| Pc 被覆率 | 蓄積 3 | LEQUAL（床・障害物） | しない | 加算 | 描く |
| P2 可視性 | 深度のみ（カラー書き込みなし） | LESS | する | なし | 描かない |
| P3 蓄積 | 蓄積 0〜2 | LEQUAL（P2 の深度） | しない | 加算 | 描かない |
| P4 正規化 | splat 層 | なし | しない | なし | — |
| P5 正規化 | G-buffer | LESS | する | なし | — |

- **P1**: G-buffer の深度（床・障害物）を `glBlitFramebuffer` でコピーする（両方 D32F）。可視性パスが G-buffer の深度を直接書き換えると、半透明の輪郭の画素で床の深度が失われ、床の影や AO がずれるためである。
- **P2**: 交点を視線の方向へ `depthOffset` = `splatDepthOffsetScale`（既定 0.5）× R だけ奥にずらした深度を書く。この厚みの内側にある円盤が P3 で混ざり合い、外側（奥の面）は捨てられる。
  **重み 0.1 未満の部分は深度を書かない**（§10.1）。
- **P3**: P2 の深度と LEQUAL で比べるだけで書き込まず、重み × 色・法線・線形深度を加算する。
- **P4/P5**: 同じ全画面シェーダー `splat_normalize.frag` を uniform `target` で切り替えて 2 回描く。

`glClearBufferfv` の番号は描画バッファの番号なので、毎フレームの最初に 4 枚すべてを描画バッファにしてから消す（§10.5）。

### 5.4 被覆率（段階 3）

被覆率は、その画素がどれだけ粒子に覆われているかを表す。

$$
\alpha = 1 - \exp\left(-k \sum_i w_i\right)
$$

k = `coverageK`（既定 2）は、どれだけ速く不透明になるかを決める。

Σw には、P3 の手前の層の重みではなく、**被覆率のパス（Pc）で求めた視線上のすべての円盤の重み** を使う（`max(Σw_all, Σw)`）。
Pc は P2 の前に、床・障害物の深度とだけ比べ、裏向きの円盤や表面の少し下の円盤も含めて重みを足す。
手前の層の重みだけで決めると、山の中で手前の層の円盤がたまたま少ない所の α が下がり、円盤の形の穴が開いて後ろの床が透けて見えたためである（§10.4）。

### 5.5 正規化（`splat_normalize.frag`）

```glsl
float sw = a0.a;										// Σw (手前の層)
if (sw < 1e-4) discard;
float cover = max(texelFetch(coverageTex, pix, 0).r, sw);
float alpha = 1.0 - exp(-coverageK * cover);
if (target == 1 && alpha < opaqueAlpha) discard;		// P5: 不透明な画素だけ G-buffer へ
vec3 n = normalize(a1.xyz);								// 重み付き平均の法線
float depthLin = texelFetch(accum2, pix, 0).r / sw;		// 重み付き平均の線形深度
```

- 色・法線・線形深度・あいまいさを Σw で割った重み付き平均にする。重なり合う円盤の値が滑らかに混ざるので、粒子の集まりが連続した層として見える。
- 線形深度から深度バッファの値を、透視投影の式 clip.z = P₂₂·z + P₃₂、clip.w = −z で求めて `gl_FragDepth` に書く。
- P4（target 0）は円盤がかかった画素すべてを splat 層に書く。アルファには種別ではなく被覆率 α を入れる。
- P5（target 1）は α ≥ `opaqueAlpha`（既定 0.9）の画素だけを G-buffer に書く（種別 0.25 = 粒子）。深度テストは LESS で、床・障害物より手前の画素だけが残る。

---

## 6. 陰影の計算

`lighting.frag` は、陰影の計算の本体を関数 `shade(albedo, viewPos, viewNormal, isParticle, fuzz, ao)` にまとめ、G-buffer の画素と splat 層の画素の両方に使うように整理した。
Spheres / Surface モードの見た目は変わらないようにしている（wrap などの値は Splat モードのときだけ渡す）。

### 6.1 splat 層を重ねる

```glsl
// G-buffer の画素 (背景・床・障害物・不透明な粒子) の陰影 → baseRadiance
if (useSplatLayer && α > 0 && α < opaqueAlpha && 0 < z_splat < z_base) {
	radiance = mix(baseRadiance, shade(splat 層の画素).radiance, α);
}
```

- 背景（空）の画素にも重ねられるように、背景の空の色もトーンマッピング前の放射輝度として求め、混ぜてからトーンマッピングする。
- splat 層の画素の位置は、その画素の視線と線形深度から復元する。
- splat 層の画素の AO は、同じ画素の G-buffer 側の AO をそのまま使う（輪郭なので遮蔽は小さく、目立たない）。
- 深度は G-buffer の値を書く。半透明の輪郭は、後から描く枠線を隠さない。

### 6.2 柔らかい拡散反射（wrapped Lambert）

粒子の画素では、ランバートの n·l の代わりに次を使う。

$$
\mathrm{ndl} = \mathrm{clamp}\left(\frac{\mathbf{n}\cdot\mathbf{l} + a}{1 + a},\ 0,\ 1\right),\qquad a = \text{splatWrap} + \text{fuzzWrap}\cdot\text{fuzz}
$$

- a = 0 で通常のランバートになる。大きくすると明暗の境目が広がり、影側にも光が回り込む。粒の集まりの中での光の散乱を大まかに表すものである。
- fuzz = 1 − ρ / ρ_ref はまばらさで、PCA の surface variation の代わりに使う。まばらな所ほど陰影をあいまいにする。
- 既定は splatWrap = 0.3、fuzzWrap = 0.4。Oren–Nayar は、wrapped Lambert で足りないと分かってから試す予定で、まだ試していない。

### 6.3 密度格子による砂の影（段階 4）

砂の影は、密度格子を太陽の方向にレイマーチした透過率で求める。

```glsl
float densityTransmittance(vec3 worldPos, vec3 worldNormal) {
	vec3 x = worldPos + worldNormal * cellSize;		// 自分自身の密度を拾わないように 1 セル外から
	float tau = 0.0;
	for (int i = 0; i < 160; ++i) {
		x += sunDirection * cellSize;				// 1 セルずつ太陽へ
		vec3 uvw = x / gridExtent;
		if (any(lessThan(uvw, vec3(0.0))) || any(greaterThan(uvw, vec3(1.0)))) break;
		tau += texture(densityTex, uvw).r * tauPerDensity;
		if (tau > 8.0) break;
	}
	return exp(-tau);
}
```

- 1 セル進むごとの光学的厚さは 密度 × `tauPerDensity` = 密度 / ρ_ref × `densityShadowOpacity` で、詰まった砂を 1 セル通ると τ = 1.5（既定）になる。
- **シャドウマップには障害物だけを描き**、その影と透過率を掛け合わせる。床・障害物・粒子のすべての画素で、砂による影を同じ方法で求める。
- 粒子の球がシャドウマップから消えるので、次の 2 つの問題がなくなった。
  - 平らにした面が近くの球の影に入る斑点（Surface モードと同じ問題。§10.3）
  - 円盤として描いていない縁の粒の球が、床に落とす暗い点（§10.6）
- 薄い所は光が透け、表面のすぐ下は少し暗くなる。床の影の縁も柔らかくなる。
- `densityShadow` を切ると、従来どおりシャドウマップに全粒子を球で描き、粒子の画素だけ参照位置を描画半径の 3 倍ずらす。

### 6.4 影の参照位置のずらし（`particleShadowOffset`）

シャドウマップで砂の影を求めるとき、平らにした粒子の面（Surface モードの平滑化した面、Splat モードの円盤）は、シャドウマップに描いた球より低い位置にある。
そのため、粒子の画素（種別 0.25）だけ、影を参照する位置を法線方向へ余分にずらす。

| モード | 粒子の画素の追加のずらし |
|---|---|
| Surface | 描画半径 |
| Splat（密度格子の影を使わないとき） | 描画半径の 3 倍（2 倍では斑点が残った） |
| Splat（密度格子の影を使うとき） | 0（シャドウマップに粒子がない） |

以前は Surface モードで床を含むすべての画素をずらしていたため、床に落ちる粒の影が薄くなっていた。Splat モードの追加にあわせて、粒子の画素だけに改めた。

---

## 7. 粉塵の影

粉塵は、粉塵 1 個につき中心の 1 点だけでシャドウマップを参照し、明るさを決めている（[rendering_implementation.md](rendering_implementation.md) §6.4）。
砂の層の中で発生した粉塵は影と判定され、空の光だけで照らされるので、青みがかった **灰色の斑点** として砂の上に目立った（§10.7）。

- 影の効き具合 `dustShadow`（既定 0.3、0 で影なし）を加え、`lit = mix(1, lit, dustShadow)` とした。全モードで効く。
- Splat モードで密度格子の影を使うときは、シャドウマップに砂を描かないので、砂による粉塵の影はなくなる（障害物の影は残る）。

---

## 8. デバッグ表示

| UI | 内容 |
|---|---|
| Splatting（チェックを外す） | 円盤を描かず、段階 1 と同じく粒子を球で描く。円盤の描画と見比べるため |
| Hide interior | 球で描くとき、表面らしさが `surfaceCutoff` 未満の粒子を描かない |
| Surfel view = Surface | 表面らしさを色で表示する（青 = 内部 → 赤 = 表面） |
| Surfel view = Normal | 法線をワールド座標で表示する（n × 0.5 + 0.5。法線なしは灰色） |
| Surfel view = Density | 密度 / ρ_ref を灰色の濃さで表示する |
| Debug view = Coverage | 被覆率 α を表示する（白 = 不透明、黒 = なし） |

Surfel view の色は、球でも円盤でも表示できる。陰影を付けると色が読めないので、Debug view が Final のときは自動的に Albedo 表示（陰影なし）に切り替える。
シェーダーではガンマ補正の逆（2.2 乗）をかけた色を書き、Albedo 表示でその色がそのまま出るようにしている。

---

## 9. パラメータ一覧

UI の「Rendering」で Splat を選ぶと表示される。既定値は `RenderSettings.h` にある。

| 分類 | パラメータ | UI | 既定値 | 説明 |
|---|---|---|---|---|
| 格子 | `densityCellScale` | Cell scale | 1.5 | セルの一辺 = この値 × 粒子間隔 |
| | `densityBlurSigma` | Density blur | 1.0 | 密度のぼかしの標準偏差 [セル]。法線の滑らかさ |
| 表面 | `outsideOffsetScale` | Outside offset | 1.0 | 表面判定で法線方向に見る距離 [セル] |
| | `surfaceLo` / `surfaceHi` | Surface lo / hi | 0.25 / 0.75 | 表面判定のしきい値（ρ_ref に対する割合） |
| | `surfaceCutoff` | — | 0.02 | これ未満の粒子は内部として描かない |
| 円盤 | `splatRadiusScale` | Splat radius | 1.8 | 半径 = この値 × 粒子間隔 |
| | `splatSharpness` | Splat sharpness | 3.0 | カーネル exp(−β u²) の β |
| | `splatDepthOffsetScale` | Splat depth offset | 0.5 | 可視性パスの奥へのずらし量 = この値 × 半径 |
| 被覆率 | `coverageK` | Coverage k | 2.0 | α = 1 − exp(−k Σw) の k |
| | `opaqueAlpha` | Opaque alpha | 0.9 | これ以上を不透明として G-buffer に書く |
| 陰影 | `splatWrap` | Wrap | 0.3 | wrapped Lambert の基本の a |
| | `fuzzWrap` | Fuzz wrap | 0.4 | まばらさに応じて a に足す量 |
| 影 | `densityShadow` | Density shadow | true | 砂の影を密度格子のレイマーチで求める |
| | `densityShadowOpacity` | Shadow opacity | 1.5 | 詰まった砂 1 セル分の光学的厚さ |
| 粉塵 | `dustShadow` | Dust shadow | 0.3 | 粉塵の影の効き具合（全モード共通） |
| 表示 | `splatting` | Splatting | true | false で球の表示（比較用） |
| | `surfelDebug` | Surfel view | Off | 表面らしさ・法線・密度の表示 |
| | `hideInterior` | Hide interior | false | 球の表示で内部の粒子を描かない |

既定値は、開発用の環境（ソフトウェアの OpenGL）で、画像と数値を見ながら決めたものである。

---

## 10. 開発中に見つかった問題と対処

| # | 段階 | 症状 | 原因 | 対処 |
|---|---|---|---|---|
| 10.1 | 2 | 円盤の縁の形がうろこ状に見える | 表面らしさの低い粒子や円盤の縁（ほとんど寄与しない部分）が可視性パスで手前の深度を決め、後ろの円盤を締め出していた。締め出された画素では、その円盤の色だけが縁まで残る | 可視性パスでは、重み 0.1 未満の部分は深度を書かない |
| 10.2 | 2 | 薄い膜の縁で、円盤が細い棒として筋になる | ほぼ真横から見た円盤 | 視線との角度が浅いほど重みを下げる（`facing`）。効果は小さかったが、筋が出にくくなるので残した |
| 10.3 | 2 | 円盤の面に、影の黒い斑点が出る | 円盤は粒子の中心を通り、さらに重み付き平均で少し奥に下がるので、シャドウマップに描いた近くの球の影に入る | 粒子の画素だけ、影の参照位置を描画半径の 3 倍ずらす（2 倍では残った）。可視性パスのずらし量を大きくすると、平均の深度が下がって斑点が増えたので、0.5 × 半径のままにした |
| 10.4 | 3 | 山の中に円盤の形の穴が開き、後ろの床が透ける | 被覆率を手前の層の重みだけで決めていたため、手前の層の円盤がたまたま少ない所の α が下がった | 視線上のすべての円盤（裏向き・表面の少し下も含む）の重みを足す被覆率のパスを加えた（§5.4） |
| 10.5 | 3 | （実行前のコードの見直しで発見） | `glClearBufferfv` の番号は描画バッファの番号で、前のフレームの最後に被覆率の 3 番を外した設定が残るため、被覆率のテクスチャが消えない | 消す前に 4 枚すべてを描画バッファにする |
| 10.6 | 2, 3 | 縁の外の床に暗い点が並ぶ | 円盤として描いていない（裏向き・横向きで薄くした）縁の粒が、シャドウマップでは球として床に影を落としていた | 段階 4 の密度格子の影で解消（§6.3） |
| 10.7 | — | 砂の上に灰色のまだらが出る（ユーザーの実機。開発用の環境では再現しなかった） | 砂の層の中で発生した粉塵が、中心の 1 点の判定で砂の影に入り、空の光だけで照らされていた。ユーザーの観察（粉塵の影が黒すぎる）で、粉塵が原因と分かった | 粉塵の影の効き具合 `dustShadow` を加え、密度格子の影ではシャドウマップに砂を描かない（§7） |
| 10.8 | 3 | 立方体の左の面が暗くなった（不具合を疑った） | 陰影の計算を書き直した影響ではないかを、段階 2 のシェーダーに戻して比べた。結果は同じで、太陽の反対側の面なので暗いのが正しい | 対処なし |

---

## 11. 検証の方法と結果

検証の環境と方法は [rendering_implementation.md](rendering_implementation.md) §10 と同じである（クラウド上の Linux で、Xvfb と Mesa の llvmpipe によりアプリ全体を実行し、環境変数で指定した条件で画面を保存する）。
Splat モードでは、次を加えた。

- **表面らしさの分布**: ビルド用のコピーにだけ、surfel バッファを読み出して表面らしさのヒストグラムを出力する処理を差し込み、§4.2 の数値を確かめた。
- **ぼかしの効き**: Density blur を 0 と 2.5 にして法線の表示を比べ、0 でばらつき、2.5 で山全体が滑らかに変わることを確かめた。
- **ちらつきの指標**: Surface モードの開発（[rendering_implementation.md](rendering_implementation.md) §9.5）と同じく、低い角度のカメラから連続 2 フレームを保存し、砂の画素について次を比べた。

| 70 フレーム目、低い角度 | Surface | 段階 2 | 段階 3 | 段階 4 |
|---|---|---|---|---|
| 暗い画素の割合 | 1.97% | 4.75% | 2.60% | 1.87% |
| 隣り合うフレームの明るさの差（平均） | 2.13 | 3.52 | 2.91 | 1.86 |
| 明るさが 40 以上変わる画素の割合 | 0.60% | 1.23% | 0.67% | 0.12% |

段階 2 では、薄い膜の縁が影の側で暗く、縁の粒の球が床に影を落としていたため、Surface モードより悪化した。
段階 3 の柔らかい拡散反射で縁の暗さが和らぎ、段階 4 の密度格子の影で、すべての指標が Surface モードより良くなった。

なお、70 フレーム目の砂は動いているので、隣り合うフレームの差には実際の動きも含まれる。

---

## 12. 既知の制限と今後の課題

- **薄く広がった砂:** 粒子 1〜2 個分の厚さまで薄く広がった砂では、ほぼすべての粒子が表面と判定され、表と裏の区別も付きにくい。これはシミュレーション側の問題（[code_review.md](code_review.md) の A-2・A-3）によるもので、修正すると砂山が保たれ、描画の見栄えも良くなる。
- **粘土のような滑らかさ:** 密度のぼかしと円盤の重ね合わせで、表面が滑らかになりすぎ、立方体の角も丸くなる。粒状感は色のばらつきだけで出している。Density blur を小さく、Splat sharpness を大きくすると粒状に近づく。
- **輪郭の大きさ:** 円盤の半径が粒子間隔の 1.8 倍なので、砂のかたまりが球の表示より一回り大きく見える。
- **石の粒:** 石（材質フラグ 1）の粒子も同じ半径の円盤になるので、灰色の円として目立つことがある。
- **影のレイマーチの負荷:** 陰影の計算で、画素ごとに最大 160 回（splat 層の画素はさらにもう 1 回）3D テクスチャを読む。開発用の環境では GPU 時間を正しく測れないので、実機の Lighting の時間で確かめる必要がある。重い場合は、歩幅を広げる、半解像度で求める、などの対策がある。
- **影の範囲:** 密度格子の影は領域の中だけで求める。領域の外にはみ出した砂の影は落ちない。
- **粉塵と密度格子の影:** 粉塵は密度格子の影を受けない（シャドウマップの障害物の影だけを受ける）。砂の影の中の粉塵も明るく見える。
- **Hide interior の表示:** 表面と内部の境目を連続に変えているので、表面のすぐ下の層は中間の値になり、Hide interior でも隙間から見える。
- **粉塵の量:** Linear 補間で砂が細かく揺れ続けると、粉塵が出続けて砂が見えなくなることがある。Speed threshold を上げると抑えられる。

---

## 13. 参考文献

1. M. Zwicker, H. Pfister, J. van Baar, M. Gross. Surface splatting. *Proc. SIGGRAPH 2001*, pp. 371–378, 2001.
2. M. Botsch, A. Hornung, M. Zwicker, L. Kobbelt. High-quality surface splatting on today's GPUs. *Proc. Eurographics Symposium on Point-Based Graphics*, pp. 17–24, 2005.
3. M. Müller, D. Charypar, M. Gross. Particle-based fluid simulation for interactive applications. *Proc. ACM SIGGRAPH/Eurographics Symposium on Computer Animation*, pp. 154–159, 2003.（color field の勾配による表面の法線）
4. H. Hoppe, T. DeRose, T. Duchamp, J. McDonald, W. Stuetzle. Surface reconstruction from unorganized points. *Proc. SIGGRAPH '92*, pp. 71–78, 1992.（PCA による法線。比較の対象）
5. T. Lokovic, E. Veach. Deep shadow maps. *Proc. SIGGRAPH 2000*, pp. 385–392, 2000.（半透明の物体を通る光の透過率による影）
6. J. Meng, M. Papas, R. Habel, C. Dachsbacher, S. Marschner, M. Gross, W. Jarosz. Multi-scale modeling and rendering of granular materials. *ACM Transactions on Graphics*, 34(4), Article 49, 2015. https://doi.org/10.1145/2766949
7. M. McGuire, L. Bavoil. Weighted blended order-independent transparency. *Journal of Computer Graphics Techniques*, 2(2):122–141, 2013.（粉塵の合成）

※ 6 は原典（論文 PDF）で確認済み。それ以外の書誌情報は既知のものであり、論文に引用する前に出版社のページで確認すること。
