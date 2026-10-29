# GPU 上の MLS-MPM による砂の大変形シミュレーション ― 理論と実装の解説

本書は、本リポジトリのプログラム（OpenGL 4.3 コンピュートシェーダーによる MPM 砂シミュレーション）を修士論文で説明・引用するための理論的な解説である。
各節の終わりに、対応するソースコードの位置を示す。
実装が標準的な定式化と異なる箇所は、本文中で **【実装との差異】** として明記した（詳細と修正案は [code_review.md](code_review.md) を参照）。

文献番号 [n] は [references.md](references.md) に対応する。

---

## 目次

1. [手法の位置づけ](#1-手法の位置づけ)
2. [記号](#2-記号)
3. [連続体力学の基礎](#3-連続体力学の基礎)
4. [Material Point Method の離散化](#4-material-point-method-の離散化)
5. [粒子と格子の間の転送 ― PIC・APIC・MLS-MPM](#5-粒子と格子の間の転送--picapicmls-mpm)
6. [1 時間ステップのアルゴリズム](#6-1-時間ステップのアルゴリズム)
7. [砂の構成則 ― Drucker–Prager 弾塑性](#7-砂の構成則--druckerprager-弾塑性)
8. [境界条件と剛体障害物（SDF）](#8-境界条件と剛体障害物sdf)
9. [GPU 実装](#9-gpu-実装)
10. [安定性とパラメータの選び方](#10-安定性とパラメータの選び方)
11. [評価機能](#11-評価機能)
12. [本実装の特徴と限界のまとめ](#12-本実装の特徴と限界のまとめ)

---

## 1. 手法の位置づけ

### 1.1 なぜ MPM か

土砂の崩落・堆積のような粉粒体の大変形を数値計算する方法は、大きく次の 2 つに分けられる。

| 方式 | 代表例 | 長所 | 短所 |
|---|---|---|---|
| 離散要素法 | DEM | 粒子間の接触を直接表現できる | 実際の粒子数（砂粒 1 個ずつ）に比例する計算量。実時間では小規模に限られる |
| 連続体として扱う方法 | FEM, SPH, **MPM** | 「粒の集まり」を連続体の構成則で表せるので、粒子数を砂粒の数より大幅に少なくできる | 構成則（材料モデル）の妥当性に結果が依存する |

連続体の方法のうち、メッシュに基づく FEM は大変形でメッシュが潰れるため、再メッシュが必要になる。
MPM（Material Point Method）[1, 2] は、

- 材料の状態（質量・速度・変形勾配・塑性の履歴）を **粒子（material point）** が運ぶ（Lagrange 的）
- 運動方程式は、毎ステップ作り直す **背景格子（grid）** の上で解く（Euler 的）

という hybrid Lagrangian–Eulerian 法である。
メッシュの絡まりが起きないこと、自己接触・分離・合体が格子上で自動的に扱われること、粒子が変形の履歴を持てることから、雪 [5]、砂 [7]、砂と水の混合 [8] など、CG 分野で大変形・塑性材料の標準的な手法になっている。

### 1.2 本実装の構成要素

本プログラムは、次の既存手法を組み合わせている。

| 要素 | 採用している手法 | 文献 |
|---|---|---|
| 離散化 | MPM（2次 B スプライン、または線形の形状関数） | [1, 2, 11] |
| 粒子と格子の転送 | APIC / MLS-MPM | [6, 9] |
| 弾性 | 対数（Hencky）ひずみによる St. Venant–Kirchhoff 型の超弾性 | [7] |
| 塑性 | Drucker–Prager 降伏条件、ひずみ空間での return mapping、硬化 | [7, 14] |
| 体積補正 | 塑性射影で失われた体積の累積 | [8] |
| 境界条件 | 格子速度への射影と Coulomb 摩擦 | [5, 11] |
| 剛体障害物 | 符号付き距離場（SDF）の 3D テクスチャ | [21, 23] |
| 並列化 | OpenGL 4.3 コンピュートシェーダー、格子を 3D テクスチャで保持 | [25, 26] |

---

## 2. 記号

| 記号 | 意味 | コード上の名前 |
|---|---|---|
| $p$, $i$ | 粒子の添字、格子点の添字 | `idx`, `node_coords` |
| $\mathbf{x}_p, \mathbf{v}_p$ | 粒子の位置、速度 | `position`, `velocity` |
| $m_p, V_p^0$ | 粒子の質量、初期（参照）体積 | `p_mass`, `p_vol` |
| $\mathbf{C}_p$ | APIC のアフィン速度行列（≒ 速度勾配） | `affineC` |
| $\mathbf{F}, \mathbf{F}_E, \mathbf{F}_P$ | 変形勾配、その弾性部分と塑性部分 | `deformation` は $\mathbf{F}_E$ |
| $\mathbf{U}\boldsymbol{\Sigma}\mathbf{V}^T$ | $\mathbf{F}_E$ の特異値分解 | `U, sig, V` |
| $\boldsymbol{\varepsilon} = \ln \boldsymbol{\Sigma}$ | 主対数（Hencky）ひずみ | `epsilon`, `e` |
| $\hat{\boldsymbol{\varepsilon}}$ | 偏差ひずみ $\boldsymbol\varepsilon - \frac{\mathrm{tr}\,\boldsymbol\varepsilon}{3}\mathbf{1}$ | `e_hat` |
| $\boldsymbol\tau, \mathbf{P}$ | Kirchhoff 応力、第 1 Piola–Kirchhoff 応力 | `stress` |
| $\mu, \lambda$ | Lamé 定数 | `p_mu`, `p_lambda` |
| $E, \nu$ | ヤング率、ポアソン比 | `E`, `Nu`（main.cpp） |
| $\phi_F, \alpha$ | 摩擦角、Drucker–Prager の係数 | `phi`, `alpha` |
| $q$ | 硬化の状態変数 | `q` |
| $v_c$ | 体積補正量 | `vc` |
| $w_{ip}$ | 格子点 $i$ と粒子 $p$ の重み $N(\mathbf{x}_i - \mathbf{x}_p)$ | `weight` |
| $\Delta x, \Delta t$ | 格子間隔、時間刻み | `dx`, `timestep` |

---

## 3. 連続体力学の基礎

### 3.1 運動方程式

連続体の運動は、運動量保存則（Cauchy の運動方程式）

$$
\rho \frac{D\mathbf{v}}{Dt} = \nabla \cdot \boldsymbol\sigma + \rho \mathbf{g}
$$

と質量保存則 $D\rho/Dt + \rho \nabla\cdot\mathbf{v} = 0$ で表される。ここで $\boldsymbol\sigma$ は Cauchy 応力、$\mathbf{g}$ は重力加速度である。
MPM では、粒子が一定の質量を運ぶことで質量保存は自動的に満たされ、運動量保存則だけを格子上で解く。

### 3.2 変形勾配と乗算分解

参照配置の点 $\mathbf{X}$ が現在配置の $\mathbf{x} = \boldsymbol\phi(\mathbf{X}, t)$ に移るとき、変形勾配 $\mathbf{F} = \partial\mathbf{x}/\partial\mathbf{X}$ は局所的な伸縮と回転を表し、$J = \det\mathbf{F}$ は体積比である。
$\mathbf{F}$ の時間発展は速度勾配を使って

$$
\dot{\mathbf{F}} = (\nabla\mathbf{v})\,\mathbf{F}
$$

と書ける。これを前進差分で離散化すると、MPM で使う更新式 $\mathbf{F}^{n+1} = (\mathbf{I} + \Delta t\, \nabla\mathbf{v})\mathbf{F}^n$ になる。

弾塑性体では、変形を弾性部分（応力を生む、元に戻る変形）と塑性部分（永久変形）に乗算分解する [15, 16]。

$$
\mathbf{F} = \mathbf{F}_E \mathbf{F}_P
$$

本実装は、粒子に $\mathbf{F}_E$ だけを保持する（コードの `deformation`）。
塑性部分は「return mapping で $\mathbf{F}_E$ から取り除かれた変形」として暗黙的に扱われ、保存はされない。

### 3.3 対数ひずみと超弾性

$\mathbf{F}_E$ を特異値分解 $\mathbf{F}_E = \mathbf{U}\boldsymbol\Sigma\mathbf{V}^T$ すると、$\mathbf{U}, \mathbf{V}$ は回転、$\boldsymbol\Sigma = \mathrm{diag}(\sigma_1, \sigma_2, \sigma_3)$ は主ストレッチになる。
主対数ひずみ（Hencky ひずみ）を $\boldsymbol\varepsilon = \ln\boldsymbol\Sigma$ と定義すると、$\mathrm{tr}\,\boldsymbol\varepsilon = \ln J_E$ は体積変化を、偏差成分 $\hat{\boldsymbol\varepsilon}$ はせん断（形状変化）を表す。

Klár ら [7] にならい、エネルギー密度を

$$
\psi(\mathbf{F}_E) = \mu\, \mathrm{tr}\!\left( (\ln\boldsymbol\Sigma)^2 \right) + \frac{\lambda}{2} \left( \mathrm{tr}\ln\boldsymbol\Sigma \right)^2
$$

とする（対数ひずみで書いた St. Venant–Kirchhoff 型のモデル）。応力は次のようになる。

$$
\mathbf{P} = \frac{\partial\psi}{\partial\mathbf{F}_E} = \mathbf{U}\left( 2\mu\boldsymbol\Sigma^{-1}\ln\boldsymbol\Sigma + \lambda\,\mathrm{tr}(\ln\boldsymbol\Sigma)\,\boldsymbol\Sigma^{-1} \right)\mathbf{V}^T,
\qquad
\boldsymbol\tau = \mathbf{P}\mathbf{F}_E^T = \mathbf{U}\left( 2\mu\boldsymbol\varepsilon + \lambda\,\mathrm{tr}(\boldsymbol\varepsilon)\mathbf{I} \right)\mathbf{U}^T
$$

主空間での Kirchhoff 応力 $\hat{\boldsymbol\tau} = 2\mu\boldsymbol\varepsilon + \lambda\,\mathrm{tr}(\boldsymbol\varepsilon)\mathbf{I}$ が、対数ひずみに対して **線形** になる点が重要である。
このため、次章の Drucker–Prager の降伏面を、ひずみ空間の単純な円錐として書くことができる。
Lamé 定数は $\mu = E/(2(1+\nu))$、$\lambda = E\nu/((1+\nu)(1-2\nu))$ で与える（`main.cpp`）。

> **【実装との差異】** `mpm_p2g.comp` では `stress = U * stress_diag * transpose(V)` に、さらに `transpose(F)` を掛けている。
> これは $\mathbf{U}\hat{\boldsymbol\tau}\mathbf{V}^T\mathbf{F}^T = \mathbf{U}\hat{\boldsymbol\tau}\boldsymbol\Sigma\mathbf{U}^T$ になり、正しい $\boldsymbol\tau$ に余分な $\boldsymbol\Sigma$ が掛かる。
> 小変形（$\boldsymbol\Sigma\approx\mathbf{I}$）では差は小さいが、10% のランダムな変形で相対誤差は約 13% になる（NumPy で確認）。
> コード中のコメントにも、近似であることが書かれている。

---

## 4. Material Point Method の離散化

### 4.1 弱形式と粒子による積分

運動方程式に試験関数を掛けて領域で積分した弱形式を、格子の形状関数 $N_i(\mathbf{x})$ で離散化する。
このとき体積積分を「粒子を積分点とする求積」で近似するのが MPM の考え方である [1, 11]。
格子点 $i$ に働く内力は

$$
\mathbf{f}_i = -\sum_p V_p^0\, \mathbf{P}_p \mathbf{F}_p^T \nabla w_{ip}
= -\sum_p V_p^0\, \boldsymbol\tau_p \nabla w_{ip}
$$

となる [5, 11]。粒子の体積として参照配置の体積 $V_p^0$ を使い、Kirchhoff 応力 $\boldsymbol\tau = J\boldsymbol\sigma$ と組み合わせる。

### 4.2 形状関数

本実装は 2 種類の形状関数を切り替えられる（起動時のコンソール入力で選ぶ）。
いずれも 1 次元の関数のテンソル積 $w_{ip} = N(\frac{x_p - x_i}{\Delta x})N(\frac{y_p - y_i}{\Delta x})N(\frac{z_p - z_i}{\Delta x})$ で 3 次元に拡張する。

**2次 B スプライン**（影響範囲 3×3×3 = 27 点、$C^1$ 連続）

$$
N(r) = \begin{cases}
\frac{3}{4} - r^2 & |r| < \frac{1}{2} \\
\frac{1}{2}\left(\frac{3}{2} - |r|\right)^2 & \frac{1}{2} \le |r| < \frac{3}{2} \\
0 & \text{otherwise}
\end{cases}
$$

**線形（テント関数）**（影響範囲 2×2×2 = 8 点、$C^0$ 連続）　$N(r) = \max(0, 1 - |r|)$

線形の形状関数は勾配がセル境界で不連続になる。
このため、粒子がセルをまたぐたびに力がとび（cell-crossing error / instability）[12]、MPM では精度と安定性の両方で不利になる。
GIMP [13] や B スプライン [12] はこの問題を緩和するために導入された。
本実装の 2 方式の比較は、この効果を定量化する実験として位置づけられる。

> **【実装との差異】** 起動時のメニューと `MpmSimulator` のログでは "Cubic B-Spline" と表示されるが、実装は **2次（quadratic）** B スプラインである。
> 論文では「2次 B スプライン」と書くこと。

コード: `mpm_p2g.comp` の 2.、`mpm_p2g_Linear.comp`

---

## 5. 粒子と格子の間の転送 ― PIC・APIC・MLS-MPM

### 5.1 PIC と FLIP

粒子の速度を格子へ移し、格子で更新した速度を粒子へ戻す転送の方法で、数値粘性（散逸）の量が決まる。

- **PIC** [3]: 格子の速度を補間した値で粒子速度を **置き換える**。安定だが、補間を往復するたびに速度場がなめらかになり、強い散逸が生じる（回転運動が止まる）。
- **FLIP** [4]: 格子での速度の **変化分** だけを粒子に足す。散逸は小さいが、ノイズが残りやすく不安定になりうる。

### 5.2 APIC

APIC（Affine Particle-In-Cell）[6] は、粒子に速度 $\mathbf{v}_p$ に加えて **アフィン速度行列** $\mathbf{C}_p$ を持たせ、粒子のまわりの速度場を $\mathbf{v}(\mathbf{x}) \approx \mathbf{v}_p + \mathbf{C}_p(\mathbf{x} - \mathbf{x}_p)$ と 1 次まで表現する。
これにより、PIC の安定性を保ったまま、角運動量を保存し、散逸を大幅に減らせる [6, 10]。

$$
\text{P2G: } (m\mathbf{v})_i = \sum_p w_{ip} m_p \left( \mathbf{v}_p + \mathbf{C}_p(\mathbf{x}_i - \mathbf{x}_p) \right),
\qquad
\text{G2P: } \mathbf{C}_p = \mathbf{B}_p \mathbf{D}_p^{-1},\quad \mathbf{B}_p = \sum_i w_{ip}\mathbf{v}_i(\mathbf{x}_i - \mathbf{x}_p)^T
$$

ここで慣性に相当するテンソル $\mathbf{D}_p = \sum_i w_{ip}(\mathbf{x}_i - \mathbf{x}_p)(\mathbf{x}_i - \mathbf{x}_p)^T$ は、2次 B スプラインでは粒子位置によらず $\mathbf{D}_p = \frac{\Delta x^2}{4}\mathbf{I}$（3次では $\frac{\Delta x^2}{3}\mathbf{I}$）になる [6]。
そのため $\mathbf{D}_p^{-1} = \frac{4}{\Delta x^2}\mathbf{I}$ という定数を使える。

### 5.3 MLS-MPM

MLS-MPM [9] は、APIC の $\mathbf{C}_p$ を移動最小二乗法（MLS）による速度勾配の近似とみなし、力の計算にも同じ近似を使う。
すると重みの勾配 $\nabla w_{ip}$ が

$$
\nabla w_{ip} \approx w_{ip}\,\mathbf{D}_p^{-1}(\mathbf{x}_i - \mathbf{x}_p)
$$

に置き換わり、格子の力は

$$
\mathbf{f}_i = -\sum_p w_{ip}\, V_p^0\, \frac{4}{\Delta x^2}\, \boldsymbol\tau_p\, (\mathbf{x}_i - \mathbf{x}_p)
$$

となる。また、変形勾配の更新にも $\nabla\mathbf{v} \approx \mathbf{C}_p$ を使う。

$$
\mathbf{F}_p^{n+1} = (\mathbf{I} + \Delta t\,\mathbf{C}_p)\,\mathbf{F}_p^n
$$

重みの勾配を計算しなくてよいので、P2G で運動量と力を 1 回のループでまとめて散布でき、実装が簡潔で計算も速い。
Taichi の 88 行の実装（mpm88）[27] はこの形式の代表例であり、本実装の P2G / G2P も同じ構造をしている。

> **【実装との差異（重大）】** `mpm_g2p.comp` では `dpos = (offset - weight_dist) * dx`（ワールド単位）に、係数 `4.0 * inv_dx` を掛けて $\mathbf{C}_p$ を計算している。
> その結果、$\mathbf{C}_p = \frac{4}{\Delta x}\sum w\,\mathbf{v}_i(\mathbf{x}_i - \mathbf{x}_p)^T$ となり、正しい値の **$\Delta x$ 倍**（grid 128 では約 1/85）になる。
> 線形な速度場 $\mathbf{v} = \mathbf{A}\mathbf{x}$ を与えた数値実験でも、$\mathbf{C}_p = \Delta x\,\mathbf{A}$ となることを確認した。
> mpm88 では、G2P の `dpos` を格子単位で計算しているため `4 * inv_dx` で正しくなる。本実装は P2G 側の書き方（ワールド単位）をそのまま G2P に持ち込んだと考えられる。
> この $\mathbf{C}_p$ は変形勾配の更新にも使われるため、ひずみと応力が本来の約 $\Delta x$ 倍に抑えられ、実質の剛性が大幅に低くなっている。
> また、アフィン運動量の転送がほとんど働かず、APIC は PIC に近い散逸的な挙動になっている。

コード: `mpm_p2g.comp` の 4.、`mpm_g2p.comp` の 2.〜3.

---

## 6. 1 時間ステップのアルゴリズム

`MpmSimulator::step()` は、次の 4 つのコンピュートシェーダーを順にディスパッチする。
各パスの間には `glMemoryBarrier` を置き、前のパスの書き込みが次のパスから見えることを保証している。

```
┌────────────┐   ┌───────────────────┐   ┌───────────────────────┐   ┌──────────────────────────┐
│ Setup      │ → │ P2G               │ → │ Grid                  │ → │ G2P                      │
│ 格子を0に   │   │ 粒子→格子          │   │ 速度化・重力・境界条件   │   │ 格子→粒子・F更新・塑性・移流 │
│ (格子並列)  │   │ (粒子並列, atomic) │   │ (格子並列)             │   │ (粒子並列, ping-pong)     │
└────────────┘   └───────────────────┘   └───────────────────────┘   └──────────────────────────┘
```

**Step 1 — Setup**（`mpm_setup.comp`）: 格子の運動量 $(m\mathbf{v})_i$ と質量 $m_i$ を 0 にする。

**Step 2 — P2G**（`mpm_p2g.comp`）: 各粒子について

1. $\mathbf{F}_{E,p}$ を SVD し、$\boldsymbol\tau_p$ を求める（§3.3）。
2. 周囲の格子点に、次の量を atomic に加算する。

$$
m_i \mathrel{+}= w_{ip} m_p,\qquad
(m\mathbf{v})_i \mathrel{+}= w_{ip}\left[ m_p\mathbf{v}_p + m_p\mathbf{C}_p(\mathbf{x}_i - \mathbf{x}_p) \right] - \Delta t\, w_{ip} V_p^0 \frac{4}{\Delta x^2}\boldsymbol\tau_p(\mathbf{x}_i - \mathbf{x}_p)
$$

力 $\mathbf{f}_i$ を $\Delta t$ 倍して運動量に足し込んでいるので、格子側では「内力を加えた後の運動量」が得られる。

**Step 3 — Grid**（`mpm_grid.comp`）: 質量のある格子点について

$$
\mathbf{v}_i = \frac{(m\mathbf{v})_i}{m_i} + \Delta t\,\mathbf{g}
$$

を計算する。その後、床と障害物の境界条件（§8）を適用し、$m_i\mathbf{v}_i$ を書き戻す。

**Step 4 — G2P**（`mpm_g2p.comp`）: 各粒子について

$$
\mathbf{v}_p = \sum_i w_{ip}\mathbf{v}_i,\qquad
\mathbf{C}_p = \frac{4}{\Delta x^2}\sum_i w_{ip}\mathbf{v}_i(\mathbf{x}_i - \mathbf{x}_p)^T,\qquad
\mathbf{F}_E^{tr} = (\mathbf{I} + \Delta t\,\mathbf{C}_p)\mathbf{F}_E^n
$$

を求める。$\mathbf{F}_E^{tr}$ に塑性の return mapping（§7）を適用して $\mathbf{F}_E^{n+1}$ を得たあと、位置を移流する。

$$
\mathbf{x}_p^{n+1} = \mathbf{x}_p^n + \Delta t\,\mathbf{v}_p
$$

時間積分は陽的（シンプレクティック Euler）である。
G2P は読み出し用と書き込み用の 2 本の粒子バッファを使い（ping-pong）、ステップの最後に役割を入れ替える。

---

## 7. 砂の構成則 ― Drucker–Prager 弾塑性

### 7.1 粉粒体の力学的な性質

乾燥した砂には次の性質がある。

1. **粘着力がない**: 引張にはまったく抵抗できず、ばらばらに分離する。
2. **摩擦で強度が決まる**: せん断に耐えられる限界（強度）が圧力に比例する（Mohr–Coulomb 則）。圧縮されているほど崩れにくい。
3. その結果、**安息角** を持つ山として堆積する。

この性質は、応力空間で圧力軸を中心とする円錐形の降伏面、すなわち Drucker–Prager 降伏条件 [14] で表される。

### 7.2 降伏条件

Klár ら [7] にならい、主 Kirchhoff 応力 $\hat{\boldsymbol\tau}$ の空間で、降伏条件を

$$
y(\hat{\boldsymbol\tau}) = \alpha\,\mathrm{tr}(\hat{\boldsymbol\tau}) + \left\|\hat{\boldsymbol\tau} - \tfrac{\mathrm{tr}\,\hat{\boldsymbol\tau}}{3}\mathbf{1}\right\|_F \le 0,
\qquad
\alpha = \sqrt{\frac{2}{3}}\,\frac{2\sin\phi_F}{3 - \sin\phi_F}
$$

と書く。$\phi_F$ は摩擦角である。圧縮（$\mathrm{tr}\,\hat{\boldsymbol\tau} < 0$）が強いほど、許されるせん断応力が大きくなる円錐になる。
§3.3 のモデルでは $\hat{\boldsymbol\tau} = 2\mu\boldsymbol\varepsilon + \lambda\,\mathrm{tr}(\boldsymbol\varepsilon)\mathbf{1}$ なので、

$$
\mathrm{tr}\,\hat{\boldsymbol\tau} = (3\lambda + 2\mu)\,\mathrm{tr}\,\boldsymbol\varepsilon,
\qquad
\hat{\boldsymbol\tau} - \tfrac{\mathrm{tr}\,\hat{\boldsymbol\tau}}{3}\mathbf{1} = 2\mu\,\hat{\boldsymbol\varepsilon}
$$

となる。これを代入して $2\mu$ で割ると、降伏条件はひずみ空間で

$$
\|\hat{\boldsymbol\varepsilon}\|_F + \frac{3\lambda + 2\mu}{2\mu}\,\alpha\,\mathrm{tr}(\boldsymbol\varepsilon) \le 0
$$

という円錐になる。左辺が次節の $\delta\gamma$ である。
そのため、射影（return mapping）を主対数ひずみの 3 次元空間で、解析的に行うことができる。

### 7.3 Return mapping（3 つの場合分け）

試行ひずみ $\boldsymbol\varepsilon = \ln\boldsymbol\Sigma^{tr}$ に対して、

$$
\delta\gamma = \|\hat{\boldsymbol\varepsilon}\|_F + \frac{3\lambda + 2\mu}{2\mu}\,\mathrm{tr}(\boldsymbol\varepsilon)\,\alpha
$$

を計算し（$d = 3$ 次元）、次のように場合分けする [7]（コードの `state` の値も併記する）。

| 場合 | 条件 | 処理 | `state` |
|---|---|---|---|
| I: 引張 | $\mathrm{tr}\,\boldsymbol\varepsilon > 0$ | 円錐の頂点へ射影: $\boldsymbol\varepsilon \leftarrow \mathbf{0}$（応力 0、$\mathbf{F}_E$ は回転だけになる） | 0 |
| II: 弾性 | $\delta\gamma \le 0$ | 変更しない | 1 |
| III: 塑性 | それ以外 | 円錐の側面へ射影: $\boldsymbol\varepsilon \leftarrow \boldsymbol\varepsilon - \delta\gamma\,\hat{\boldsymbol\varepsilon}/\|\hat{\boldsymbol\varepsilon}\|$ | 2 |

射影後に $\mathbf{F}_E^{n+1} = \mathbf{U}\exp(\boldsymbol\varepsilon)\mathbf{V}^T$ として変形勾配を作り直す。
場合 III では偏差方向にだけ射影するので、体積を変えずにせん断を逃がす（非関連流れ則、塑性による体積変化を起こさない）。

> **【実装との差異（軽微）】** コードでは `e_hat_norm < 1e-8` の場合も場合 I にしている。
> 圧縮されていてせん断が 0 の場合、本来は $\delta\gamma < 0$ で弾性（場合 II）になる。
> ただし浮動小数点ではほぼ成立しない条件なので、実際の影響は小さい。

### 7.4 硬化

射影の量を累積した状態変数 $q$ によって、摩擦角を変化させる [7]。

$$
q^{n+1} = q^n + \delta q,\qquad
\delta q = \begin{cases} \|\boldsymbol\varepsilon\|_F & \text{場合 I} \\ 0 & \text{場合 II} \\ \delta\gamma & \text{場合 III}\end{cases},
\qquad
\phi_F = h_0 + (h_1 q - h_3)\,e^{-h_2 q}
$$

Klár らの値は $h_0 = 35°, h_1 = 9°, h_2 = 0.2, h_3 = 10°$ である。
塑性変形が進むと、摩擦角は $h_0$ に向かって変化する。

> **【実装との差異】** 本実装は $h_3 = 3$ を使っており、$q = 0$ での摩擦角は 32° になる。
> 一方で、粒子の初期値 `alpha = 0.267765` は $\phi_F = 25°$（$h_3 = 10$ の場合）に相当する。
> そのため、1 ステップ目で $\alpha$ が不連続に変わる。

### 7.5 体積補正

場合 I（引張）で体積ひずみを捨てると、砂が少しずつ体積を「得て」膨張し、圧縮された状態に戻れなくなる。
Tampubolon ら [8] は、捨てた体積 $v_c \mathrel{+}= \ln\det\mathbf{F}_E^{tr} - \ln\det\mathbf{F}_E^{n+1}$ を粒子ごとに累積し、次のステップの体積ひずみ $\mathrm{tr}\,\boldsymbol\varepsilon$ に加えることで、これを補正した。
本実装もこの方法を採用している（`vc`）。

コード: `mpm_g2p.comp` の 4.〜6.

---

## 8. 境界条件と剛体障害物（SDF）

### 8.1 格子速度への境界条件

MPM では、境界条件を粒子ではなく **格子点の速度** に課す。
境界の外向き法線を $\mathbf{n}$、境界（障害物）自身の速度を $\mathbf{v}_{co}$ とする。
相対速度 $\mathbf{v}_{rel} = \mathbf{v}_i - \mathbf{v}_{co}$ の法線成分を $v_n = \mathbf{n}\cdot\mathbf{v}_{rel}$ とし、$v_n < 0$（境界へ侵入する向き）のときだけ、次の Coulomb 摩擦の処理を行う [5]。

$$
\mathbf{v}_t = \mathbf{v}_{rel} - v_n\mathbf{n},\qquad
\mathbf{v}_{rel} \leftarrow \begin{cases}
\mathbf{0} & \|\mathbf{v}_t\| \le -\mu_f v_n \ \text{(静止摩擦)} \\
\mathbf{v}_t + \mu_f v_n \dfrac{\mathbf{v}_t}{\|\mathbf{v}_t\|} & \text{otherwise (動摩擦)}
\end{cases},
\qquad \mathbf{v}_i = \mathbf{v}_{rel} + \mathbf{v}_{co}
$$

これは、法線方向の侵入を止めるのに必要な力積に比例する分だけ、接線方向の速度を減らすことに相当する。
離れる向きの速度は拘束しない（separating 境界）。

- **床**（無限平面 $\mathbf{n}\cdot\mathbf{x} = h$）: 上の摩擦処理に加えて、法線成分を $-e\,v_n$ に反転させている（反発係数 $e$ = `f_restitution` = 0.5）。
  連続体としての MPM では非弾性（$v_n \to 0$）とするのが普通で、格子速度を反発させると、境界付近の振動の原因になりうる。
- **障害物**: 剛体上の点の速度 $\mathbf{v}_{co} = \mathbf{v}_{trans} + \boldsymbol\omega\times(\mathbf{x}_i - \mathbf{x}_{center})$ を使う。
  障害物は速度・角速度で運動学的に動くだけで、砂から反力を受けない（一方向連成）。
  双方向の剛体連成は MLS-MPM の論文 [9] の主題の 1 つである。

> **【実装との差異】**
> - 境界判定で、格子点の位置を $(i + 0.5)\Delta x$（セル中心）としているが、P2G / G2P では $i\,\Delta x$ として重みを計算しているため、境界が半セルずれる。
> - 床以外の領域の端（6 面）に壁の条件がない。粒子が領域外へ出ると格子に寄与できなくなり、端の近くでは運動量の一部が失われる。

### 8.2 符号付き距離場（SDF）

障害物の形状は、メッシュから事前に計算した SDF $d(\mathbf{x})$（内部で負、外部で正）で表す。
$|\nabla d| = 1$ なので、$\nabla d$ がそのまま外向き法線になり、値が表面までの距離になる [23]。

- **生成**（`MeshResource::generateSDF`、CPU で 1 回だけ）: AABB に 10% の余白を加えた範囲を $64^3$ のボクセルに分ける。
  各ボクセルについて全三角形への最近接点 [22] を総当たりで求め（$O(R^3 N_{tri})$）、最近接三角形の面法線で内外を判定する。
  頂点や辺が最近接になる場合、面法線による判定は誤りうるので、角度重み付き擬似法線 [21] や一般化 winding number [24] を使うと頑健になる。
- **参照**（`mpm_grid.comp`）: 格子点を障害物のローカル座標へ逆変換し、3D テクスチャを線形補間でサンプリングする。
  法線は中心差分で求め、逆変換行列の転置でワールド座標へ戻す。
  距離は、x 方向の拡大率を掛けてワールド距離に換算している。そのため、非等方な拡大（Swing モードの棒 0.06×0.4×0.06）では距離が正しくない。

---

## 9. GPU 実装

### 9.1 データ配置

| データ | 形式 | 大きさ |
|---|---|---|
| 粒子 | SSBO（`std430`）兼 VBO。`MpmParticle` = 192 バイト × 2 本（ping-pong） | 10,000 粒子で 3.84 MB |
| 格子 | `GL_R32F` の 3D テクスチャ 4 枚（運動量 x, y, z、質量）。`GL_R32UI` の image として束縛 | $128^3 \times 16$ B = 32 MB |
| 物理パラメータ | UBO（`std140`）`MpmPhysics` = 112 バイト | — |
| 障害物 SDF | `GL_R32F` の 3D テクスチャ $64^3$、線形補間 | 1 MB |
| 履歴 | SSBO のリングバッファ（粒子 192 B × 粒子数 × 600 フレーム） | 10,000 粒子で約 1.15 GB |

C++ と GLSL の構造体のメモリ配置を一致させることが、正しく動くための前提になる。
`vec3` / `mat3` は `std430` でも 16 バイト境界に揃えられるので、`vec4` / `mat4` で確保して先頭の 3 成分だけを使う設計にしている（`docs/issues_and_fixes.md` の 1. を参照）。

### 9.2 格子を 3D テクスチャで持つことについて

格子座標をそのまま `ivec3` のインデックスとして `imageLoad` / `imageStore` / `imageAtomicCompSwap` でアクセスできるため、実装が単純になる。
一方で、

- OpenGL 4.3 コアには float の image atomic 加算がないため、**CAS ループ**（読み出し → 加算 → `imageAtomicCompSwap` → 失敗したら再試行）で代用している。
  1 粒子あたり 27 格子点 × 4 成分 = 108 回の CAS ループが走り、同じ格子点への書き込みが集中するほど再試行が増える。
- GPU 向けの MPM の最適化研究 [25] では、粒子をセル順にソートし、ワープ内やシェアードメモリで先に足し合わせてから global memory へ書く。
  これにより atomic の衝突を減らし、格子をスパースなブロック構造（SPGrid など）で持つことで、メモリと帯域を節約している。
  Taichi [26] や multi-GPU MPM [28] も同様の方針をとる。

本実装は、これらの最適化を行わない「素朴な」GPU 実装の基準としての位置づけになる。
`GL_NV_shader_atomic_float` のような拡張や、粒子のソートによる最適化の効果を比較することは、研究の発展として考えられる。

### 9.3 同期

| 境界 | バリア | 理由 |
|---|---|---|
| Setup → P2G → Grid → G2P | `GL_SHADER_IMAGE_ACCESS_BARRIER_BIT` | 格子 image の書き込みを、次のパスの読み出しに見せる |
| G2P → 次のステップ / 描画 | `GL_SHADER_STORAGE_BARRIER_BIT \| GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT` | 粒子 SSBO を、次の P2G と描画の頂点属性の両方から読む |

### 9.4 SVD

$3\times3$ の SVD は、$\mathbf{F}^T\mathbf{F}$ の巡回 Jacobi 法による固有値分解から求めている（3 反復）。
GPU では、分岐の少ない McAdams らの方法 [20] が広く使われている。

> **【実装との差異（重大）】** Jacobi 回転の角度の符号が逆で、非対角成分が 0 に収束しない。
> NumPy で再現した検証では、$\mathbf{F} = \mathbf{I} + 0.1\,\mathcal{N}(0,1)$ のとき、3 反復後に非対角成分が相対値で中央値 17% 残り、反復を増やしても減らなかった。
> 符号を正すと 0.2% 程度まで収束する。
> そのため、現状の `U`, `sig`, `V` は正確な SVD ではなく、応力と塑性判定の両方に誤差が入っている。

---

## 10. 安定性とパラメータの選び方

### 10.1 時間刻み（CFL 条件）

陽的な MPM の時間刻みは、弾性波（P 波）の速さ $c = \sqrt{(\lambda + 2\mu)/\rho}$ と、粒子の速さで制限される [11, 30]。

$$
\Delta t \le C\,\frac{\Delta x}{c + \max_p\|\mathbf{v}_p\|},\qquad C \approx 0.3\text{–}0.5
$$

本実装の既定値（$E = 5\times10^4$ Pa、$\nu = 0.3$、$\rho = 400$ kg/m³、$\Delta x = 1.5/128$ m）では $c \approx 13$ m/s、上限は $\Delta t \approx 2.7\times10^{-4}$ s になり、使用している $\Delta t = 10^{-3}$ s はこれより大きい。
現状で発散しないのは、§5.3 の $\mathbf{C}_p$ の問題で実質の剛性が低くなっているためと考えられる。
$\mathbf{C}_p$ を修正する場合は、$\Delta t$ を小さくするか（サブステップを増やす）、$E$ を下げる必要がある。

### 10.2 粒子の密度

2次 B スプラインの MPM では、1 セルあたり 8 粒子（各軸 2 個）程度がよく使われる [11]。
1 セルあたりの粒子数が少ないと、粒子のない格子点が生じ、数値的な破断（numerical fracture）が起きる。

本実装では、粒子の体積を $V_p = (\Delta x/2)^3$（1 セルに 8 粒子のときの値）に固定している。
しかし実際の初期配置は、一辺 0.3 m の立方体に 10,000 粒子で、grid 128 では **1 セルあたり約 0.6 粒子** である。
このため、$V_p$ の合計（$2.0\times10^{-3}$ m³）は実際の占有体積（$2.7\times10^{-2}$ m³）の約 1/13 になり、実効的な密度は 400 ではなく約 30 kg/m³ に相当する。
粒子数を変えても初期立方体の大きさが粒子数の立方根に比例して変わるため、この比はほぼ一定である。
論文で物理量を議論する場合は、$V_p = (\text{初期体積})/(\text{粒子数})$、$m_p = \rho V_p$ として整合をとるのが望ましい。

### 10.3 材料定数

| パラメータ | 本実装 | Klár ら [7] の砂 |
|---|---|---|
| 密度 $\rho$ | 400 kg/m³（実効は約 30） | 2200 kg/m³ |
| ヤング率 $E$ | $5\times10^4$ Pa | $3.537\times10^5$ Pa |
| ポアソン比 $\nu$ | 0.3 | 0.3 |
| 硬化 $h_0, h_1, h_2, h_3$ | 35, 9, 0.2, **3** | 35, 9, 0.2, 10 |
| 時間刻み | $10^{-3}$ s | CFL 条件から決定 |

CG 用の砂の MPM では、実物の砂より大幅に低いヤング率を使うのが一般的である（実物の剛性では時間刻みが極端に小さくなる）。
挙動を決めるのは主に摩擦角（安息角）であり、剛性は「十分に硬い」範囲にあればよい。

---

## 11. 評価機能

`MpmEvaluator` は、論文の実験に使う次の量を記録し、CSV に出力する。

| 項目 | 方法 | 用途 |
|---|---|---|
| パスごとの GPU 時間 | `GL_TIME_ELAPSED` のクエリ | Setup / P2G / Grid / G2P の時間の内訳。P2G の atomic の負荷の評価 |
| 数値破綻 | `mpm_evaluator.comp` で NaN / Inf と領域外の粒子を数える | 安定性の評価（パラメータ・補間方式ごとの比較） |
| 最大速度・AABB | atomic で集計 | 崩落の広がり（runout）や堆積形状の比較 |
| 代表粒子の状態 | VBO から読み戻す | 特定の粒子の軌跡 |
| アプリの FPS | ImGui のフレームレート | 実時間性の評価 |

読み戻しは CPU と GPU の同期を伴うため、性能を測るときは状態の読み戻しを OFF にする（性能の計測と状態の評価を分ける）のが望ましい。
初期配置は固定シード（`std::mt19937 engine(0)`）で生成されるので、実験を再現できる。

---

## 12. 本実装の特徴と限界のまとめ

**特徴**

1. MLS-MPM と Drucker–Prager 弾塑性（硬化・体積補正を含む）による砂のモデルを、OpenGL 4.3 のコンピュートシェーダーだけで実装し、統合 GPU でも実時間で動作する。
2. 格子を 3D テクスチャで持ち、空間座標から $O(1)$ で格子点にアクセスする単純な構成である。
3. SDF の 3D テクスチャで任意形状の剛体障害物を扱い、Shoot / Swing の操作で動的に介入できる。
4. 状態の巻き戻し（GPU 上のリングバッファ）と評価・ログ機能を備え、同じ条件での比較実験ができる。

**限界と、論文で記述・修正を検討すべき点**（[code_review.md](code_review.md) を参照）

1. SVD（Jacobi 法）の回転の符号の誤りにより、分解が収束しない。
2. G2P の $\mathbf{C}_p$ が $\Delta x$ 倍に小さく、変形勾配と APIC の転送が本来より大幅に弱い。
3. 粒子の体積・質量と実際の配置密度が一致しない（1 セル約 0.6 粒子）。
4. Kirchhoff 応力に余分な $\boldsymbol\Sigma$ が掛かる。硬化パラメータ $h_3$ と $\alpha$ の初期値が一致しない。
5. 線形補間で、2次 B スプライン用の $\mathbf{D}_p^{-1}$ を流用している。
6. 境界条件の半セルのずれ、領域端の壁の欠如、床の反発係数。
7. 障害物との連成は一方向で、非等方スケールでは SDF の距離が正しくない。

1〜3 はシミュレーション結果に大きく影響する。
修正すると挙動とパラメータの適切な値が変わるので、修正前後の比較を論文の考察に含めることもできる。
