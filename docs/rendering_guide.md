# 砂の質感向上の実装手順書

[rendering_options.md](rendering_options.md) で提案した 3 段階の実装（① 球インポスタ + 陰影 + 影 + AO、② スクリーン空間で面を張る、③ 粉塵）について、設計と手順を具体的に説明する。
実装を始める前に、全体の構成、各パスの処理内容、変更するファイル、確認の方法をここで決めておく。

**方針**

- 変更するのは描画だけである。シミュレーション（`src/mpm/` 以下の計算とシェーダー）には手を付けない。粒子バッファは読み出すだけにする。
- 各段階は単独で動く状態にしてからコミットする。前の段階の結果は、次の段階の土台になる。
- 今の点描画（デバッグ色による `state` の表示を含む）は残し、UI で切り替えられるようにする。
- 統合 GPU（Intel UHD Graphics）でも実時間で動くことを目標にする（README の計測環境）。

---

## 目次

1. [現在の描画の流れ](#1-現在の描画の流れ)
2. [新しい描画パイプラインの全体像](#2-新しい描画パイプラインの全体像)
3. [共通の基盤](#3-共通の基盤)
4. [段階 1: 球インポスタ・陰影・影・AO](#4-段階-1-球インポスタ陰影影ao)
5. [段階 2: スクリーン空間で面を張る](#5-段階-2-スクリーン空間で面を張る)
6. [段階 3: 粉塵](#6-段階-3-粉塵)
7. [ファイル構成と変更点](#7-ファイル構成と変更点)
8. [パラメータの初期値](#8-パラメータの初期値)
9. [性能の見積もりと計測](#9-性能の見積もりと計測)
10. [確認項目](#10-確認項目)
11. [注意点](#11-注意点)
12. [参考文献](#12-参考文献)

---

## 1. 現在の描画の流れ

`main.cpp` のメインループは、シミュレーションの後に次の順で描画している。

| 順序 | 関数 | 内容 | シェーダー |
|---|---|---|---|
| 1 | `Renderer::drawFloor` | 20×20 個の点を、床の平面上に並べて白く描く | `point.vert` / `point.frag`（`is_floor = 1`） |
| 2 | `Renderer::drawObstacle` | 障害物メッシュをランバート反射で描く | `mesh.vert` / `mesh.frag` |
| 3 | `Renderer::drawMpm` | 粒子を `GL_POINTS` で描く。大きさは画面の高さの 1%（`glPointSize`）で固定。色は材質（砂 / 石）か `state` | `point.vert` / `point.frag` |
| 4 | `Renderer::drawBoundary` | 領域の立方体の枠線 | `mesh.*` |
| 5 | `Renderer::drawPreview` | Swing の予測軌道 | `mesh.*` |
| 6 | ImGui | UI | — |

問題点は次のとおりである。

- 粒子の大きさがピクセル単位で固定されていて、遠近感がない。
- 陰影・影・AO がなく、形がわからない。
- 床が点の集まりで、影を受けられない。

---

## 2. 新しい描画パイプラインの全体像

画面の深度と法線を、AO・面の平滑化・粉塵の 3 つが共通して使う。
そこで、**軽量な deferred shading**（G-buffer に深度・法線・色を書き、最後に全画面のパスでまとめて陰影を付ける方式）にする。

```
                   ┌──────────────────────────────────────────────┐
 段階1             │ (A) Shadow pass   光源から見た深度              │  粒子(インポスタ)・障害物
                   │                   → shadow map (D32F)          │
                   ├──────────────────────────────────────────────┤
 段階1             │ (B) G-buffer pass カメラから                   │  粒子(インポスタ)・床・障害物
                   │   → depth(D32F) / normal(RG16F) / albedo(RGBA8)│
                   │     / linear depth(R32F, 粒子のみ)              │
                   ├──────────────────────────────────────────────┤
 段階2             │ (C) Thickness pass 粒子の厚みを加算 (R16F)     │
 段階2             │ (D) Smooth pass   粒子の深度を平滑化 (R32F ×2)  │
 段階2             │ (E) Normal pass   平滑化した深度から法線         │
                   ├──────────────────────────────────────────────┤
 段階1             │ (F) SSAO pass     半解像度で AO → ぼかし (R8)   │
                   ├──────────────────────────────────────────────┤
 段階1             │ (G) Lighting pass 全画面: 太陽光+影+空の環境光   │
                   │                   ×AO、トーンマップ、ガンマ      │
                   ├──────────────────────────────────────────────┤
 段階3             │ (H) Dust pass     WBOIT で蓄積 → 合成           │
                   ├──────────────────────────────────────────────┤
                   │ (I) Overlay       枠線・プレビュー・ImGui        │  既存の処理のまま
                   └──────────────────────────────────────────────┘
```

段階 1 では (A)(B)(F)(G)(I) を作る。
段階 2 では、(C)(D)(E) を (B) と (F) の間に差し込み、粒子の部分の法線と深度を置き換える。
段階 3 では (H) を足す。
描画モードの切り替え（点 / インポスタ / 面）は、(B) と (C)〜(E) の中身を切り替えるだけで済むようにする。

---

## 3. 共通の基盤

### 3.1 オフスクリーンのレンダーターゲット

`RenderTarget` クラス（FBO と複数のカラーテクスチャ、深度テクスチャをまとめたもの）を作る。

- **大きさ:** ウィンドウの大きさではなく、**フレームバッファの大きさ**（`glfwGetFramebufferSize`）に合わせる。
  `Window::resize` はすでにフレームバッファの大きさで `glViewport` を設定している。高 DPI の画面では、ウィンドウの大きさと値が異なる。
- **作り直し:** リサイズのたびに作り直す。`Window` にフレームバッファの大きさを返す関数を追加し、前のフレームと違えば `RenderTarget::resize()` を呼ぶ。
- **サンプリング:** `GL_NEAREST` と `GL_CLAMP_TO_EDGE` を既定にする。シャドウマップだけは比較サンプラ（`GL_TEXTURE_COMPARE_MODE`）を使う。

### 3.2 全画面パス

全画面の三角形を 1 枚描く（頂点バッファは不要。`gl_VertexID` から座標を作る）。

```glsl
// fullscreen.vert
out vec2 v_uv;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);   // (0,0), (2,0), (0,2)
    v_uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
```

空の VAO を 1 つ作って `glDrawArrays(GL_TRIANGLES, 0, 3)` で描く。
`GL_CULL_FACE` が有効になっているが、この三角形は反時計回りなので消されない。

### 3.3 シェーダーの読み込み

既存の `loadProgram()` をそのまま使う。前回直したので、エラーが起きたファイル名とログが表示される。
新しいシェーダーは `src/render/shaders/` にまとめる。

### 3.4 UI

ImGui に「Rendering」の折りたたみ項目を追加し、次の項目を置く。

- 描画モード（Points / Spheres / Surface）
- 影・AO・粉塵の ON / OFF
- 主なパラメータ（§8）
- 中間バッファの表示（深度・法線・AO・厚み）。デバッグに使う

### 3.5 GPU 時間の計測

描画のパスごとに `GL_TIME_ELAPSED` のクエリを入れ、UI に表示する。
`MpmEvaluator` はシミュレーションのパスを計測しているので、描画用の計測はそれとは別に作る。
クエリの結果は 2〜3 フレーム遅れて読むようにして、同期による待ちを避ける。

---

## 4. 段階 1: 球インポスタ・陰影・影・AO

### 4.1 粒子の描画半径

描画半径は、粒子 1 個が表す体積から決める。
シミュレーションの `p_radius` は接触判定の閾値として使われていて、描画用の値ではない。

$$
r_{draw} = k \cdot \frac{1}{2}\left(\frac{V_{init}}{N}\right)^{1/3}
$$

既定の条件（一辺 0.3 m の立方体に 10,000 粒子）では、$(V_{init}/N)^{1/3} \approx 0.014$ m になる。
係数 $k$ = 1.2〜1.5 程度にして、隣どうしの球を少し重ねると、すき間が見えなくなる。
$k$ は UI から変えられるようにする。

### 4.2 点スプライトの大きさ

`glEnable(GL_PROGRAM_POINT_SIZE)` を有効にし、頂点シェーダーで球の投影後の直径を計算する。
今の `glPointSize` による固定の大きさと、`GL_POINT_SMOOTH` は使わない。

```glsl
// sphere.vert（要点のみ）
uniform mat4  view, projection;
uniform float pointRadius;      // r_draw [m]
uniform float viewportHeight;   // フレームバッファの高さ [px]
flat out vec3 v_centerView;     // 球の中心（ビュー座標）

void main() {
    vec4 pv = view * vec4(position.xyz, 1.0);
    v_centerView = pv.xyz;
    gl_Position  = projection * pv;
    // 投影後の直径 [px] = 2r · P[1][1] · (H/2) / w
    // 透視投影でも平行投影（w = 1、シャドウマップ用）でも同じ式で使える
    gl_PointSize = pointRadius * projection[1][1] * viewportHeight / gl_Position.w;
}
```

粒子の状態（`state`、材質フラグ）は、今の `point.vert` と同じく SSBO から `gl_VertexID` で読む。

### 4.3 フラグメントで球を描く

点スプライトの正方形の中で、円の内側だけを残し、球面の法線と深度を計算する。

```glsl
// sphere.frag（G-buffer 出力の要点）
flat in vec3 v_centerView;
uniform mat4  projection;
uniform float pointRadius;

layout(location = 0) out vec2 o_normal;   // 八面体エンコードしたビュー空間の法線
layout(location = 1) out vec4 o_albedo;   // rgb = 色, a = 種別 (粒子 / 床 / 障害物)
layout(location = 2) out float o_linearDepth; // -z_view（段階 2 で使う）

void main() {
    vec2 c = gl_PointCoord * 2.0 - 1.0;
    c.y = -c.y;                       // gl_PointCoord は左上が原点
    float r2 = dot(c, c);
    if (r2 > 1.0) discard;            // 円の外は捨てる

    vec3 n = vec3(c, sqrt(1.0 - r2)); // ビュー空間の球面の法線
    vec3 p = v_centerView + n * pointRadius;

    vec4 clip = projection * vec4(p, 1.0);
    gl_FragDepth = clip.z / clip.w * 0.5 + 0.5;   // 球面の正しい深度（障害物との交差が正しくなる）

    o_normal = encodeOct(n);          // 八面体エンコード [11]
    o_albedo = vec4(particleColor(), KIND_PARTICLE);
    o_linearDepth = -p.z;
}
```

- 点スプライトは画面に平行な正方形なので、画面の端では球の輪郭が厳密な円にならない（わずかに楕円になる）。視野角 60° では目立たないので、この近似で十分である。
  厳密にしたい場合は、視線と球の交差を計算する方法に替える。
- `gl_FragDepth` を書くと、ふつうは early-Z（シェーダーを実行する前の深度テスト）が無効になる。粒子が重なり合って重い場合は、次のようにする。
  1. 頂点シェーダーで、`gl_Position` の深度を、球の中心ではなく **球のいちばん手前の点**（中心から視点の方向へ r だけずらした点）にする。
  2. フラグメントシェーダーで `layout(depth_greater) out float gl_FragDepth;`（GL 4.2 以降）と宣言する。球面の深度は、いちばん手前の点の深度以上になるので、この宣言は正しい。
  3. こうすると、手前の点の深度ですでに深度テストに失敗する（奥にある）フラグメントを、シェーダーを実行する前に捨てられる。

### 4.4 色

- 砂の基本色はもっと明るくする（線形色空間で (0.55, 0.45, 0.32) 程度）。今の (0.32, 0.18, 0.10) は暗い。
- 粒子番号のハッシュで、明るさを ±10%、色相をわずかにばらつかせる。砂粒の不均一さが出る。
- 石のフラグ（`padding[0] == 1`）は灰色にする。
- デバッグ色（`state` による色分け）も、このシェーダーで選べるようにする。

### 4.5 床と障害物

- **床:** 点の集まりをやめ、床の平面上の大きな四角形（三角形 2 枚）にする。G-buffer に法線と色を書き、影を受けるようにする。
  床の色は砂より少し暗い土の色にする。
- **障害物:** `mesh.frag` を G-buffer 出力版に替える（法線をビュー空間で出力する）。

### 4.6 シャドウマップ

- **方法:** シャドウマップ [4] を使う。
- **光源:** 平行光源（太陽）。方向は UI で変えられるようにする（既定は斜め上から）。
- **投影:** シミュレーション領域 [0, 1.5]³ を包む直方体に合わせた平行投影にする。領域が固定なので、カメラに追従させる必要はない（cascaded shadow map は不要）。
- **解像度:** 2048²（統合 GPU で重い場合は 1024²）。1 テクセル ≈ 1.5 m / 2048 ≈ 0.7 mm になり、粒子の半径（約 7 mm）より十分細かい。
- **描画:** 粒子は §4.2 と同じインポスタで深度だけを書く（平行投影では `w = 1` なので同じ式で大きさが決まる）。障害物も描く。床は影を受けるだけなので描かなくてよい。
- **参照:** 比較サンプラ（`sampler2DShadow`）で 3×3 の PCF（percentage-closer filtering）を行う。影の縁をさらに柔らかくしたい場合は PCSS [5] がある。
  シャドウアクネ（自己影の縞模様）を防ぐため、法線方向へのオフセットと傾きに応じたバイアスを入れる。
- 粒子どうしが落とし合う影によって、砂山の凹凸と、崩落の先端の形がはっきり見えるようになる。質感への効果がいちばん大きい要素である。

### 4.7 SSAO

- 半解像度で、半球内の 16 サンプルの SSAO（Crytek 方式の改良版 [6, 7]）を計算する。
- サンプル半径は粒子半径の 3〜5 倍にする。粒子のすき間と接地面が暗くなり、粒状の質感が強く出る。
- 4×4 のノイズで回転したサンプルの縞を、深度を考慮したぼかし（bilateral blur）で消す。

### 4.8 陰影の計算（Lighting pass）

全画面で、G-buffer から次の量を計算する。

$$
L = \underbrace{\rho\, E_{sun} \max(0, \mathbf{n}\cdot\mathbf{l})\, S}_{\text{太陽光（影 } S\text{）}} + \underbrace{\rho\, \mathrm{mix}(E_{ground}, E_{sky}, \tfrac{1}{2}(n_y + 1))\, A}_{\text{空と地面からの環境光（AO } A\text{）}}
$$

- 乾いた砂は粗い面なので、鏡面反射はほとんどない。拡散反射だけで十分である。
  より砂らしくするなら、Lambert の代わりに Oren–Nayar モデル [8] を使う（光源の反対側から見たときの平坦な明るさが出る）。
- 最後にトーンマッピング（Reinhard か ACES の近似）とガンマ補正（2.2）をシェーダーで行う。
  `GL_FRAMEBUFFER_SRGB` を有効にすると ImGui の色も変わるので、使わない。
- 背景は空の色の縦方向のグラデーションにする（G-buffer の種別が「何もない」画素）。

### 4.9 段階 1 の完了条件

- 粒子が遠近に応じた大きさの、陰影の付いた球として描かれる。
- 粒子と障害物の重なり（深度）が正しい。
- 粒子が床と自分自身に影を落とし、AO で接地面とすき間が暗くなる。
- 点描画とデバッグ色に、UI から戻せる。
- 統合 GPU、10,000 粒子で、描画を含めて今の fps から大きく落ちない（§9）。

---

## 5. 段階 2: スクリーン空間で面を張る

van der Laan らの screen space fluid rendering [1] を砂に当てはめたものである。
深度の平滑化には、境界で面がにじみにくい narrow-range filter [2] を使う。

### 5.1 粒子の深度

段階 1 の G-buffer パスで、粒子の画素だけに線形深度 $-z_{view}$ を書いておく（`o_linearDepth`）。
粒子がない画素は、遠方の値（例: 1e6）で初期化しておく。

### 5.2 厚み（Thickness pass）

- 粒子を、深度テストあり（不透明物の深度と比べる）・深度書き込みなし・加算合成で描き、球の厚み $2r\sqrt{1 - |c|^2}$ を足し合わせる（R16F、半解像度でよい）。
- 厚みが小さい所（崩落の先端、まばらな粒子）を、半透明にしたり縁を柔らかくしたりするのに使う。

### 5.3 深度の平滑化（Smooth pass）

- 線形深度を、2 次元のバイラテラル系フィルタで平滑化する。
  横と縦に分けると速いが、分離できないフィルタなので、斜めの方向に縞が出やすい。narrow-range filter [2] では、分離して使うための工夫が提案されている。
- フィルタ半径は **ワールド単位** で決め（粒子半径の 2〜4 倍）、深度に応じてピクセル数に換算する（遠くの面ほど小さく）。
- 深度の差が閾値（粒子直径程度）を超える画素は平均に含めない。手前の砂山と奥の砂山を混ぜないためである。
- 2〜3 回繰り返す。平滑化を強くすると、面が滑らかになる代わりに、粒状感が消える。

### 5.4 法線の再構成（Normal pass）

平滑化した深度と、逆投影行列から、ビュー座標の位置を復元する。
前方差分と後方差分のうち、深度の差が小さいほうを使うと、輪郭で法線が乱れない。

```glsl
vec3 P  = viewPos(uv);
vec3 dx = viewPos(uv + vec2(texel.x, 0)) - P;
vec3 dx2 = P - viewPos(uv - vec2(texel.x, 0));
if (abs(dx2.z) < abs(dx.z)) dx = dx2;
vec3 dy = viewPos(uv + vec2(0, texel.y)) - P;
vec3 dy2 = P - viewPos(uv - vec2(0, texel.y));
if (abs(dy2.z) < abs(dy.z)) dy = dy2;
vec3 n = normalize(cross(dx, dy));   // 視点に向くように符号を確認する
```

### 5.5 G-buffer への書き戻し

粒子の画素（種別が「粒子」の画素）だけ、G-buffer の法線と深度を平滑化したものに置き換える。
その後の SSAO と陰影の計算は、段階 1 と共通の処理をそのまま使う。

### 5.6 粒状感を戻す

平滑化すると砂粒の凹凸が消えて、泥やプラスチックのように見えやすい。そこで、次の工夫を入れる。

- **細かい凹凸:** ワールド座標のトライプラナーのノイズで法線を少し揺らす（法線マップのような効果）。
  ノイズの座標は粒子と一緒に動かないので、流れている部分では模様が「滑って」見える。気になる場合は、ノイズの強さを速度に応じて弱める。
- **段階 1 との混合:** 平滑化前の球の法線と、平滑化した法線を、UI の係数で混ぜる。

### 5.7 段階 2 の完了条件

- 砂山が連続した面として描かれ、平滑化の強さで粒状から滑らかな面まで変えられる。
- 手前と奥の砂山の境界で、面がにじまない。
- 影と AO が段階 1 と同じように働く。

---

## 6. 段階 3: 粉塵

### 6.1 粉塵粒子の管理

シミュレーションのコードを変えないために、粉塵は **独立した粒子系** [3] にする。

- 粉塵粒子のバッファ（SSBO、容量は 65,536 個程度）を、リングバッファとして使う。1 個あたり 32 バイト（位置 + 残り寿命、速度 + 大きさ）。
- **発生（`dust_emit.comp`）:** シミュレーションの 1 フレームの後に、砂の粒子バッファを **読み出し専用** で走査する。
  次の条件を満たす粒子から、確率的に粉塵を発生させる。
  - 速さ $|\mathbf{v}_p|$ が閾値を超えている（崩落の先端、障害物に当たった所）
  - または、引張状態（`state == 0`）で、粒子がばらけている
  - 確率は「粒子番号とフレーム番号のハッシュ」で決める。乱数の状態を持たずに済む

  書き込み位置は SSBO の `atomicAdd` でリングバッファの先頭を進めて決める。
- **更新（`dust_update.comp`）:**
  - 速度を空気の抵抗で減衰させる。
  - 弱い重力と浮力をかける（ゆっくり沈む）。
  - curl noise [9] で乱流のような揺らぎを足す。
  - 寿命を減らし、大きさを広げ、濃さを薄くしていく。

### 6.2 描画（Dust pass）

- 粉塵は、ガウス分布状に縁がぼける点スプライトとして描く（大きさは §4.2 と同じ式）。
- **soft particle:** G-buffer の深度と比べて、不透明物に近い所ほど薄くする。地面や砂山に刺さった板のような境目が見えなくなる。
- **陰影:** 太陽光と環境光の簡単なものでよい。シャドウマップを参照すると、砂山の影の中の粉塵が暗くなり、立体感が出る。

### 6.3 ソートなしの半透明（WBOIT）

McGuire と Bavoil の Weighted Blended OIT [10] を使う。

1. 2 枚のターゲットを用意する: accum（RGBA16F）、revealage（R16F）。
2. 粉塵を描く。深度テストはあり、深度書き込みはなしにする。
   - accum: `glBlendFunci(0, GL_ONE, GL_ONE)` で $(\mathbf{c}\alpha, \alpha) \cdot w(z, \alpha)$ を足す
   - revealage: `glBlendFunci(1, GL_ZERO, GL_ONE_MINUS_SRC_COLOR)` で $\prod(1 - \alpha)$ を求める
3. 合成パスで、平均の色 $\mathrm{accum.rgb} / \max(\mathrm{accum.a}, \epsilon)$ を、不透明度 $1 - \mathrm{revealage}$ で、陰影を付けた画像の上に重ねる（`glBlendFunc(GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA)`）。

重み $w$ は文献 [10] の式（深度と α の関数）を使う。
粉塵のように色がほぼ同じで、薄く重なる半透明には、ソートした場合とほとんど区別がつかない。

### 6.4 段階 3 の完了条件

- 崩落や障害物の衝突に合わせて粉塵が舞い、ゆっくり消える。
- 粉塵と砂山・障害物の前後関係が正しく、境目が目立たない。
- 粉塵を OFF にすると、段階 2 と同じ画像になる。

---

## 7. ファイル構成と変更点

### 7.1 新しく作るファイル

| ファイル | 役割 | 段階 |
|---|---|---|
| `src/render/RenderTarget.h/.cpp` | FBO と付属テクスチャ、リサイズ | 1 |
| `src/render/ShadowMap.h/.cpp` | 光源の行列、シャドウマップの FBO | 1 |
| `src/render/SsaoPass.h/.cpp` | SSAO とぼかし | 1 |
| `src/render/ScreenSpaceSurface.h/.cpp` | 厚み・平滑化・法線の再構成 | 2 |
| `src/render/DustSystem.h/.cpp` | 粉塵の発生・更新・描画・WBOIT | 3 |
| `src/render/RenderSettings.h` | UI から変える描画パラメータ | 1 |
| `src/render/shaders/fullscreen.vert` | 全画面の三角形 | 1 |
| `src/render/shaders/sphere.vert/.frag` | 球インポスタ（G-buffer / 影の深度） | 1 |
| `src/render/shaders/gbuffer_mesh.vert/.frag` | 床・障害物の G-buffer 出力 | 1 |
| `src/render/shaders/ssao.frag`, `ssao_blur.frag` | SSAO | 1 |
| `src/render/shaders/lighting.frag` | 陰影の計算・トーンマップ | 1 |
| `src/render/shaders/thickness.frag`, `smooth_depth.frag`, `normal_from_depth.frag` | 面を張る処理 | 2 |
| `src/render/shaders/dust_emit.comp`, `dust_update.comp`, `dust.vert/.frag`, `wboit_composite.frag` | 粉塵 | 3 |

### 7.2 変更するファイル

| ファイル | 変更内容 |
|---|---|
| `src/render/Renderer.h/.cpp` | 上のクラスを持ち、`renderFrame(...)` で (A)〜(H) を順に実行する。既存の `drawBoundary` / `drawPreview` は (I) で使う |
| `src/main.cpp` | 描画の呼び出しを `renderer.renderFrame(...)` にまとめる。「Rendering」の UI を追加する。`glPointSize` と `GL_POINT_SMOOTH` を描画モードに応じて設定する |
| `src/core/Window.h` | フレームバッファの大きさを返す関数を追加する |
| `GPUMPMTex3D.vcxproj(.filters)` | 新しいファイルを追加する |

シミュレーション側（`src/mpm/`）は変更しない。
粉塵の発生は、シミュレーションの後に粒子バッファを読むだけで実現する。

---

## 8. パラメータの初期値

| パラメータ | 初期値 | 備考 |
|---|---|---|
| 描画半径の係数 $k$ | 1.3 | §4.1 |
| 太陽の方向 | (0.4, 1.0, 0.3) を正規化 | UI で変更 |
| 太陽の明るさ / 空の明るさ | 3.0 / 0.6 | トーンマップの前の値 |
| 砂の色（線形） | (0.55, 0.45, 0.32) | 粒子ごとに ±10% |
| 床の色（線形） | (0.35, 0.28, 0.20) | |
| シャドウマップの解像度 | 2048² | 重ければ 1024² |
| PCF | 3×3 | |
| SSAO のサンプル数 / 半径 | 16 / 粒子半径の 4 倍 | 半解像度 |
| 平滑化の半径 / 回数 | 粒子半径の 3 倍 / 2 回 | 段階 2 |
| 平滑化の深度の閾値 | 粒子直径 | 段階 2 |
| 粉塵の容量 | 65,536 | 段階 3 |
| 粉塵が発生する速さの閾値 | 1.0 m/s | 段階 3 |
| 粉塵の寿命 | 2〜4 s | 段階 3 |

---

## 9. 性能の見積もりと計測

1920×1080 の画面での目安を示す。統合 GPU では、画面の解像度に比例する処理（全画面のパス）が支配的になる。

| パス | 計算量の目安 | 軽くする方法 |
|---|---|---|
| Shadow | 粒子数 × 点スプライトの面積（シャドウマップ上） | 解像度を 1024² にする。シミュレーションが止まっているときは描き直さない |
| G-buffer | 粒子数 × 点スプライトの面積 | 深度の early-Z を活かす（§4.3） |
| SSAO | 半解像度 × 16 サンプル | サンプル数を 8 にする |
| 平滑化 | 全解像度 × フィルタの大きさ × 回数 | 半解像度で行い、拡大して使う |
| Lighting | 全解像度 × 1 回 | — |
| 粉塵 | 粉塵の数 × 点スプライトの面積 | 容量を減らす。半解像度で描いて合成する |

§3.5 の GPU タイマーで、パスごとの時間を表示する。
シミュレーションと描画の時間の内訳は、論文の性能評価にもそのまま使える。

---

## 10. 確認項目

段階ごとに、次の項目を確認してからコミットする。

**段階 1**
- [ ] ウィンドウをリサイズしても、レンダーターゲットが正しく作り直される（高 DPI の画面を含む）
- [ ] カメラを近づけても遠ざけても、球の大きさが遠近に合っている
- [ ] 障害物が砂に埋まったとき、交差の境目が正しい（球面の深度）
- [ ] 影に縞模様（アクネ）や、浮いて見える隙間（peter-panning）がない
- [ ] SSAO が画面の端や、手前と奥の物体の境界で不自然に暗くならない
- [ ] 点描画とデバッグ色に戻せる
- [ ] タイムラインの巻き戻し中（一時停止中）も正しく描画される

**段階 2**
- [ ] 平滑化の強さを 0 にすると、段階 1 とほぼ同じ見た目になる
- [ ] 手前と奥の砂山の境界がにじまない
- [ ] 厚みの小さい所の縁が柔らかくなる

**段階 3**
- [ ] 静止した砂山からは粉塵が出ない
- [ ] 粉塵が砂山に刺さったように見えない（soft particle）
- [ ] 粉塵を OFF にしたときの画像が段階 2 と一致する

---

## 11. 注意点

- **OpenGL の状態:** ブレンド・深度書き込み・カリング・ビューポートの設定を、パスごとに明示的に戻す。ImGui の描画の前に、既定のフレームバッファとビューポートに戻すこと。
- **深度の精度:** near = 0.1、far = 100 の透視投影なので、遠方の深度の精度は低い。平滑化と SSAO には、深度バッファの値ではなく線形深度（R32F）を使う。
- **点スプライトの最大サイズ:** `GL_POINT_SIZE_RANGE` を超える大きさは切り詰められる。カメラを粒子に極端に近づけると、球が欠けることがある。近づく必要があるなら、ジオメトリシェーダーかインスタンシングで四角形を描く方式に替える。
- **粒子の密度:** 点検（[code_review.md](code_review.md) の A-3）の粒子密度の問題を直すと、粒子の数と描画半径の適切な値が変わる。描画半径は §4.1 の式で自動的に決めるようにしておく。
- **タイムライン:** 巻き戻すと粉塵の状態は戻らない。巻き戻したときは、粉塵をすべて消す。

---

## 12. 参考文献

1. W. J. van der Laan, S. Green, M. Sainz. Screen space fluid rendering with curvature flow. *Proc. ACM I3D*, pp. 91–98, 2009.
2. N. Truong, C. Yuksel. A narrow-range filter for screen-space fluid rendering. *Proc. ACM on Computer Graphics and Interactive Techniques (I3D)*, 1(1), 2018.
3. W. T. Reeves. Particle systems—a technique for modeling a class of fuzzy objects. *ACM Transactions on Graphics*, 2(2):91–108, 1983.
4. L. Williams. Casting curved shadows on curved surfaces. *Proc. SIGGRAPH '78*, pp. 270–274, 1978.（シャドウマップ）
5. R. Fernando. Percentage-closer soft shadows. *ACM SIGGRAPH 2005 Sketches*, 2005.（PCF の発展）
6. M. Mittring. Finding next gen: CryEngine 2. *ACM SIGGRAPH 2007 Courses*, 2007.（SSAO）
7. L. Bavoil, M. Sainz, R. Dimitrov. Image-space horizon-based ambient occlusion. *ACM SIGGRAPH 2008 Talks*, 2008.
8. M. Oren, S. K. Nayar. Generalization of Lambert's reflectance model. *Proc. SIGGRAPH '94*, pp. 239–246, 1994.
9. R. Bridson, J. Hourihan, M. Nordenstam. Curl-noise for procedural fluid flow. *ACM Transactions on Graphics (Proc. SIGGRAPH)*, 26(3), Article 46, 2007.
10. M. McGuire, L. Bavoil. Weighted blended order-independent transparency. *Journal of Computer Graphics Techniques*, 2(2):122–141, 2013.
11. Q. Meyer, J. Süßmuth, G. Sußner, M. Stamminger, G. Greiner. On floating-point normal vectors. *Computer Graphics Forum (Proc. EGSR)*, 29(4):1405–1409, 2010.（法線の八面体エンコード）

※ 書誌情報は既知のものであり、論文に引用する前に出版社のページで確認すること。
