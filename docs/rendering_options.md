# 砂の見かけを向上する描画手法の検討

現在のプログラムは、粒子を陰影のない点（`GL_POINTS`）で描画しており、見かけがリアルではない。
本書では、土砂らしい質感を得るための描画手法を比べ、実装の順序を提案する。

文献番号 [n] は本書末尾の参考文献に対応する。

---

## 1. 前提の整理

### 1.1 MPM の格子は使えるか

- **格子の質量テクスチャ（`gridIMA`、128³）は使える。**
  P2G のたびに作られる密度場そのものであり、描画するときにも中身が残っている（消えるのは次のステップの Setup のとき）。
  質量の勾配 $-\nabla m$ がそのまま表面の法線になる。
  - 解像度は格子と同じ Δx = 1.5/128 ≈ 1.2 cm になる。
  - 点検（[code_review.md](code_review.md) の A-3）のとおり 1 セルあたり約 0.6 粒子と粒子がまばらなので、そのままでは穴の多いガタガタの面になる。平滑化が必要である。
  - 格子の境界判定の位置が半セルずれている点（同 B-5）にも注意する。
- **SDF は使えない。** 今ある SDF は障害物の形状だけを表す。砂の SDF は別に作る必要があるが、描画のためだけに作る価値は低い。

### 1.2 法線の求め方

| 手法 | 法線の求め方 |
|---|---|
| 球インポスタ | 球の式から解析的に求まる |
| スクリーン空間の方法 | 平滑化した深度の微分から求める |
| Splatting | 格子の質量の勾配から求めるのが一番安上がり（近傍粒子の PCA でもよい） |
| Marching Cubes | 密度場の勾配から求める |

### 1.3 奥行きソートは必要か

- **必要なのは Gaussian Splatting だけである。** 前から順に α 合成するため、毎フレーム粒子をソートする必要がある [6]。
- **Surface Splatting は不要。** 深度パス → 加算で属性を蓄積 → 正規化、という deferred 方式 [4] で、順序に依存しない。
- **粉塵も不要。** Weighted Blended OIT [9] か加算合成で描けば、ソートせずに半透明を表現できる。

---

## 2. 手法の比較

| 手法 | 見え方 | 法線 | ソート | 費用・リスク |
|---|---|---|---|---|
| ① 球インポスタ（点スプライトの中で球を描く）+ シャドウマップ + SSAO | 粒々した砂山。影と AO で立体感が大きく出る | 解析的 | 不要 | 最小。今の `GL_POINTS` をそのまま拡張できる |
| ② スクリーン空間で面を張る [1, 2] | 連続した面。平滑化の強さで、粒状から滑らかな面まで調整できる | 平滑化した深度から | 不要 | 中。描画パスが 4〜5 本。100k 粒子でも実時間で動く |
| ③ Surface Splatting [3, 4] | 向きを持った円盤で面を張る | 格子の勾配 | 不要（deferred） | 中。②と見た目が近く、②より手間が多い |
| ④ Gaussian Splatting [6] | 半透明の楕円体。変形勾配 F から共分散 Σ = F Σ₀ Fᵀ を作れる（MPM + 3DGS の先行例 [7]） | 実質なし（陰影付けが苦手） | 毎フレーム必要 | 大。3DGS の強みは写真から学習した見た目なので、合成の砂ではその強みが活きない |
| ⑤ 格子の質量場 + GPU Marching Cubes [8, 10] | 本物のメッシュ。書き出しや再照明に向く | 密度の勾配 | 不要 | 中〜大。解像度が格子に縛られ、粒子がまばらだと穴が開く。異方性カーネル [5] で改善できる |
| ⑥ 粉塵: 副次粒子 + 柔らかいビルボード | 舞い上がる粉塵 | 不要 | 不要（WBOIT [9]） | 小〜中。G2P で速度が大きい粒子や引張状態（state 0）の粒子から発生させる |

### 2.1 各手法の補足

**① 球インポスタ**
点スプライトのフラグメントシェーダーで、円の内側だけを残して球面の深度と法線を計算する（`gl_FragDepth` を書く）。
光源から見た深度を同じ方法で描けばシャドウマップになり、画面の深度と法線から SSAO を計算できる。
②と⑥で使う深度・法線のバッファも、ここでまとめて作れる。

**② スクリーン空間で面を張る**
1. 粒子を球インポスタとして描き、最前面の深度を得る。
2. 深度をバイラテラル系のフィルタで平滑化する。境界で面がにじまないように、narrow-range filter [2] を使う。
3. 平滑化した深度の微分から法線を求めて陰影を付ける。
4. 加算合成で厚みのパスを作り、薄い部分を少し透かす。

平滑化の半径を小さくすると粒状に、大きくすると滑らかな面になる。
UI から①と切り替えられるようにすると、比較が容易になる。

**③ Surface Splatting**
各粒子を、法線方向を向いた円盤（surfel）として描き、重なりを重み付き平均して連続した面にする [3, 4]。
見た目は②に近いが、法線と円盤の大きさを粒子ごとに用意する必要があるので、②より手間が多い。

**④ Gaussian Splatting**
F から異方性のある楕円体を作れる点は、研究として面白い（変形の様子がそのまま見かけに出る）。
一方で、毎フレームのソートが必要であり、陰影付けも苦手で、写真から学習した見た目という 3DGS の強みも活かせない。
getdepth-gs の実装（ソートと描画）を流用できるので、試す段階になったら参照する。

**⑤ Marching Cubes**
格子の質量場をそのまま等値面にできるので、追加の場を作らずに済む。
ただし、解像度が格子に縛られる点と、粒子がまばらだと穴が開く点が問題になる。
粒子を細かい格子へ異方性カーネルで散布し直す [5] と滑らかになるが、そのぶん計算量が増える。
メッシュの書き出しや、再照明のための形状が必要になったときの選択肢として残しておく。

**⑥ 粉塵**
G2P で、速度が大きい粒子や引張状態（state 0）の粒子から副次粒子を発生させ、柔らかいビルボードとして描く。
発生条件をシミュレーションの状態に結びつけるので、論文で「物理に基づく二次効果」として説明しやすい。

### 2.2 どの方法とも組み合わせられる質感の工夫

- 粒子ごとの色のばらつき（今の石フラグ `padding[0]` の拡張）
- トライプラナーのノイズによる細かい凹凸（法線の摂動）
- 深度の差による接地面の暗さ（AO）

---

## 3. 進め方の提案

1. **① 球インポスタ + Blinn-Phong の陰影 + シャドウマップ + SSAO**
   - 一番安く、見かけの改善がいちばん大きい。
   - ②と⑥で使う深度・法線のバッファもここで作る。
2. **② スクリーン空間で面を張る**
   - UI で①（粒状）と②（連続面）を切り替えられるようにする。
   - 厚みパスも作り、薄い部分を少し透かす。
3. **⑥ 粉塵**
   - WBOIT で描くのでソートは要らない。

Gaussian Splatting（④）は、F から異方性を出すという研究上の面白さはあるが、ソートが必要で陰影も苦手なので、今回の目的には後回しにする。
Marching Cubes（⑤）は、メッシュの書き出しが必要になったときの選択肢として残す。

いずれの段階も描画だけを変更し、シミュレーションのコードには手を付けない。

---

## 参考文献

1. W. J. van der Laan, S. Green, M. Sainz. Screen space fluid rendering with curvature flow. *Proc. ACM SIGGRAPH Symposium on Interactive 3D Graphics and Games (I3D)*, pp. 91–98, 2009.
2. N. Truong, C. Yuksel. A narrow-range filter for screen-space fluid rendering. *Proc. ACM on Computer Graphics and Interactive Techniques (I3D)*, 1(1), 2018.
3. M. Zwicker, H. Pfister, J. van Baar, M. Gross. Surface splatting. *Proc. SIGGRAPH 2001*, pp. 371–378, 2001.
4. M. Botsch, A. Hornung, M. Zwicker, L. Kobbelt. High-quality surface splatting on today's GPUs. *Proc. Eurographics/IEEE VGTC Symposium on Point-Based Graphics*, pp. 17–24, 2005.
5. J. Yu, G. Turk. Reconstructing surfaces of particle-based fluids using anisotropic kernels. *ACM Transactions on Graphics*, 32(1), Article 5, 2013.
6. B. Kerbl, G. Kopanas, T. Leimkühler, G. Drettakis. 3D Gaussian splatting for real-time radiance field rendering. *ACM Transactions on Graphics (Proc. SIGGRAPH)*, 42(4), Article 139, 2023.
7. T. Xie, Z. Zong, Y. Qiu, X. Li, Y. Feng, Y. Yang, C. Jiang. PhysGaussian: Physics-integrated 3D Gaussians for generative dynamics. *Proc. IEEE/CVF CVPR*, 2024.
8. W. E. Lorensen, H. E. Cline. Marching cubes: A high resolution 3D surface construction algorithm. *Proc. SIGGRAPH '87*, pp. 163–169, 1987.
9. M. McGuire, L. Bavoil. Weighted blended order-independent transparency. *Journal of Computer Graphics Techniques*, 2(2):122–141, 2013.
10. C. Dyken, G. Ziegler, C. Theobalt, H.-P. Seidel. High-speed marching cubes using HistoPyramids. *Computer Graphics Forum*, 27(8):2028–2039, 2008.

※ 書誌情報は既知のものであり、論文に引用する前に出版社のページで確認すること。
