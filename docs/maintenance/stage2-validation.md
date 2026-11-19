# 工程2: 基準方式の連続計算と境界検証

実施日: 2026-11-16。対象: TEX＋2次Bスプライン。
状態: 完了（以下の限定条件）。旧dt=0.001の刻み比較は不合格を保持し、基準dt=0.00025を採用。
全方式・全速度・任意の変形での安定性を保証する試験ではない。

## 作業基準と変更

AGENTS.md、PLANS.md、点検台帳、工程0・1記録、工程2引継ぎを確認。
開始時HEADは `7ffa1686ac15560a4c05b1e8c26aeb2389a229c6`。
tracked差分は `.gitignore`、`imgui.ini`、`main.cpp`、TEXシェーダー5本。
AGENTS.md、PLANS.md、maintenance資料、toolsは未追跡だった。
既存のSVD・C係数・応力・塑性・硬化・体積/質量・半径の手動変更を保持した。

今回の実行文の変更:

- Gridと基準G2Pの質量除外条件を `m<=0` に変更。正の小質量を捨てない。
- Gridの上側境界を `coords>=size-3` に修正し、下側と同じ3節点にした。
- mainの既定dtを0.001から0.00025秒に変更。subStepsや材料定数の変更はない。
- 基準P2G/G2P、Grid、mainの今回の確認箇所の古いコメントを修正。
  時間刻み、退化・反転、高速移動の未解決事項は点検IDを残した。
- 独立GPU試験ツールと本記録を追加。構造体、材料、粒子配置、GPU選択、ビルド設定は変更していない。

GridはTEX線形とも共用するため、この2修正は線形にも作用する。mainの既定dtは他方式にも共通。
線形G2Pの旧閾値や物理式は未変更であり、線形方式の検証完了とはしない。
SSBOへの展開は工程4以降。

## 条件、接続、事前判定基準

Intel UHD Graphics / Intel、OpenGL 4.3.0、GLSL 4.30、driver 32.0.101.7082。
非表示GLFWコンテキスト。既定GPU選択を維持し、描画・既存評価・履歴・SwapBuffersなし。
性能は比較していない。

立方体10,000粒子は現在のC++初期化probeから取得。seed=0、中心(0.75,0.75,0.75)、一辺0.3。
格子128³、領域[0,1.5]³、E=50000、ν=0.3、密度400、重力(0,-9.8,0)、床y=0.1、摩擦0.6。
総体積0.027、総質量10.8、平均0.596粒子/セルを保持。配置密度を増やした実験はしていない。
1ステップごとに4パスを実行。subStepsを増やしてdtを小さくしたという扱いはしていない。

本体 `MpmSimulator3DTEX::step` のUBO binding 1、粒子SSBO binding 0/1、
R32F格子をR32UI image binding 0〜3へ接続する設定、dispatch寸法、バリアを静的照合した。
試験は現在の4シェーダー全体をコンパイルし、Setup→P2G→Grid→G2Pを連続実行する。
各パス間は本体同様IMAGEバリア、G2P後はSTORAGE|VERTEXバリア、粒子バッファを交換。
直接CPU読戻し前だけBUFFER_UPDATEバリアとglFinishを追加する。
本体アプリの評価・描画・履歴を含む実行経路を検証したものではない。

落下・壁試験のSDFは、identity行列とAABB[10,11]³を明示設定し、
全格子点をSDF判定の外へ置いた。既定障害物を操作しないだけの試験ではない。
接触半径試験だけは後述の合成平面SDFを有効にした。

各出力のplan.jsonをGPU実行前に保存し、以下の閾値を結果に合わせて緩めていない。

| 試験 | 条件と閾値 |
|---|---|
| 定数移流 | 0.05秒、v=(0.2,-0.1,0.3)、重力0。最大位置誤差2e-4 m、最大速度誤差2e-3 m/s |
| 重力 | 0.05秒、初速0。更新後速度で進める離散Euler解に同じ閾値で比較 |
| 落下・堆積 | 1秒。毎ステップ有限性・保存Fのdet>0・全27節点支持域を確認。最終RMS速度≤0.1 m/s |
| dt比較 | 同じ1秒で粒子位置RMS差≤0.02 m、重心差≤0.01 m、AABB各成分最大差≤0.03 m |
| 境界節点 | 6面の外向き/内向き、上下角、床滑り/静止/離脱/床上。速度誤差≤2e-6 m/s |
| 壁へ接近 | 6面と対角上下角、初速1/10/100 m/s（角は各成分）。dt=.001、最大100ステップ。異常で停止 |
| 応力散布 | identity、圧縮、回転付き圧縮、せん断。dt=.001。独立eigh(FFᵀ)による応力と区分的Bスプラインから節点力積を計算。相対誤差≤5e-4、質量誤差≤3e-6 |
| 接触半径 | 16³の線形な平面SDF、等方identity、dx/2の内外・半径変更・離脱・摩擦。速度誤差≤2e-6 m/s |

異常検知時は時刻、直前・異常後の粒子バイナリを保存する。
壁試験は保存Fに加え `(I+dt C_new)F_old` の射影前detも確認。
落下試験で全ステップ確認したのは保存Fのdetであり、射影前の退化・反転への全面対応ではない。

## 結果と原因の切り分け

### 小質量節点

修正前の定数移流は位置誤差2.53e-4 m、速度誤差0.01008 m/sで不合格。
重力も速度誤差0.00753 m/sで不合格だった。
試験側だけで除外条件を変更した対照実験により、小質量の除外が定数場を崩すことを確認。
低密度な端の節点を除外すると、G2Pの重み和が1より小さくなり、速度損失と人工的なCが発生する。

修正後の連続試験:

| 試験 | 最大位置誤差 [m] | 最大速度誤差 [m/s] | 判定 |
|---|---|---|---|
| 定数移流 | 1.63e-6 | 3.42e-7 | 合格 |
| 重力 | 1.00e-7 | 4.47e-7 | 合格 |

対照実験の再実行でも旧閾値の速度誤差0.01154、正質量全節点では3.17e-7。
GPU加算順で誤差は多少変わるが、閾値の合否は明確に異なる。ビット一致は要求していない。

### 境界・接触半径

修正前18条件中、上端3面と上側角の4条件が不合格。
`size-3`節点で速度が残った。修正後は18条件すべて合格、GLエラー0。
床の節点位置はi·dxであり、y=8dxは接触、9dxは非接触となることを確認。

壁接近は1/10 m/sの16条件が100ステップ合格。
100 m/sの8条件は最初のステップで支持域が外れ、位置も領域外となった。
この8件は制約の再現であり、正常系の合格数に含めない。
境界帯へ届く前に1ステップで飛び越す移動を防ぐ機能はない。速度1/10の合格から一般的な安全上限は導けない。

接触半径は合成平面SDFの5条件すべて合格、最大誤差2.38e-8 m/s、GLエラー0。
`p_radius=dx/2`はSDFに対する格子節点の接触距離として使われる。
床は `dist<0` による別の節点拘束であり、粒子中心を常に床+半径以上に保つ処理ではない。
描画半径はparticleSpacing等から別に設定される。
OBJ由来のSDF、非等方変換、実障害物の接触精度は工程3のB-6に残る。

### 応力・質量・落下

有限dtのP2G節点力積の相対誤差は、圧縮7.03e-6、回転付き7.09e-6、せん断4.51e-4。
identityは絶対誤差0。4条件すべて事前基準内、GLエラー0。
落下後の各最終状態をP2Gへ再散布した質量は10.8000007 kg、
float設定値に対する相対誤差1.27e-9以下。毎ステップの質量テクスチャを読んだ試験ではない。
全ステップの支持域逸脱0と組み合わせて記録する。

dt=.001/.0005/.00025はそれぞれ1000/2000/4000ステップを完走。
NaN/Inf、保存detF≤0、支持域逸脱、GLエラーなし。
最終RMS速度は0.00252/0.00150/0.00222 m/s。
dt=.00025の最終AABBはおよそ(0.203,0.0928,0.202)〜(1.321,0.192,1.289)。
床直下まで粒子中心が分布することは、格子境界の離散化による制約として残す。

| dtの組 | 位置RMS差 [m] | 重心差 [m] | AABB差 [m] | 判定 |
|---|---|---|---|---|
| .001 / .0005 | .00785 | .00111 | .03399 | 不合格 |
| .0005 / .00025 | .00739 | .00105 | .01743 | 合格 |

発散しないことと、刻み比較に合格することは区別する。
初期dt=.001を基準方式として全面合格とはしない。追加のdt=.000125は別出力へ実行した。

dt=.000125も8000ステップを完走し、有限性・保存detF・支持域・堆積の基準に合格。
dt=.00025/.000125の位置RMS差は.00756 m、重心差.00110 m、AABB差.02131 mで合格。
これにより.0005/.00025/.000125の隣接2組が事前基準に合格した。
粒子位置誤差が単調減少する収束次数は示していないため、厳密解への収束証明とはしない。
概算CFL目安も踏まえて基準dt=.00025を採用し、mainの既定値を更新した。
4方式共通の既定値変更であり、未検証方式を合格とするものではない。
subSteps=1の1フレーム当たり物理時間は以前の1/4になる。実時間同期の追加はしていない。
変更後の初期化probeで起動・再設定ともdt=.00025を読戻し確認した。

## 再現と証跡

出力はすべて既存ignore下の `artifacts/baselines/`。既存出力名では停止する。
Python＋NumPyとリポジトリ同梱GLFWを使用し、追加パッケージのダウンロードなし。

```powershell
python tools/validation/build_init.py --output artifacts/baselines/stage2-init-NEW
python tools/validation/run.py --output artifacts/baselines/stage2-regression-NEW --init-probe artifacts/baselines/stage2-init-NEW/probe.exe
python tools/validation/stage2.py --output artifacts/baselines/stage2-continuous-NEW --regression artifacts/baselines/stage2-regression-NEW
python tools/validation/boundary.py --output artifacts/baselines/stage2-boundary-NEW
python tools/validation/low_mass.py --output artifacts/baselines/stage2-lowmass-NEW --regression artifacts/baselines/stage2-regression-NEW
python tools/validation/wall_trajectory.py --output artifacts/baselines/stage2-wall-NEW
python tools/validation/stress_scatter.py --output artifacts/baselines/stage2-stress-NEW
python tools/validation/contact_radius.py --output artifacts/baselines/stage2-contact-NEW
python tools/validation/stage2.py --output artifacts/baselines/stage2-fine-NEW --regression artifacts/baselines/stage2-regression-NEW --dts .000125 --fall-only
python tools/validation/summarize_stage2.py --output artifacts/baselines/stage2-summary-NEW --runs artifacts/baselines/stage2-continuous-NEW artifacts/baselines/stage2-fine-NEW --regression artifacts/baselines/stage2-regression-NEW
```

上の連続試験の既定引数は旧dt=.001を含む比較を再現するため、数値不合格で終了する。
採用した刻み付近だけを再試験する場合は `--dts .0005 .00025 .000125` を指定する。

`stage2.py`の終了コード1は数値不合格も表す。reportの各判定を確認する。
`--fall-only --dts`の単一dtの合格は、その条件で完走・堆積した意味であり、刻み収束の合格ではない。
壁・切り分け・集計ツールは診断を含むため終了コードだけで合格を判断しない。

| 証跡 | 内容 |
|---|---|
| stage2-init-20261116-a / stage2-regression-20261116-a | 修正前probeと工程1回帰92判定合格 |
| stage2-continuous-20261116-a | 修正前連続試験。短時間の移流・重力が不合格 |
| stage2-boundary-before-20261116-a | 上側4件の不合格 |
| stage2-lowmass-20261116-a / -b | 試験内の閾値切替による原因切分け。-aの当時のツールも保存 |
| stage2-init-20261116-b / stage2-regression-20261116-b | 実行文修正後の工程1回帰 |
| stage2-continuous-20261116-b | 修正後の5連続試験と3刻みの比較。不合格を含め保存 |
| stage2-boundary-after-20261116-b | 境界18条件合格 |
| stage2-wall-20261116-a | 壁接近16正常条件と8高速失敗の直前/異常後粒子 |
| stage2-stress-20261116-b | 応力散布4条件合格 |
| stage2-contact-20261116-a | 等方平面SDFの接触半径5条件合格 |
| stage2-summary-20261116-b | 既存3刻みの最終質量・比較再集計 |
| stage2-fine-20261116-c | dt=.000125、8000ステップの落下・堆積合格 |
| stage2-summary-20261116-final | 4刻みの比較・最終質量、実行文の一致を確認。粗い刻みの不合格を保持 |
| stage2-init-20261116-final / stage2-regression-20261116-final | コメント更新後のprobe再ビルド・工程1回帰92判定合格 |
| stage2-init-20261116-default / stage2-regression-20261116-default | 既定dt変更後のprobeと92判定合格。既知不具合検出5件を含む |
| stage2-lowmass-20261116-final | 終了コードを含む最終診断ツールで旧閾値の検出・修正後合格を確認 |
| stage2-build-debug.log / stage2-build-release.log | x64ビルド成功。出力・中間物を隔離し利用者exeを保持 |
| stage2-default-debug.log / stage2-default-release.log | 既定dt変更後のDebug/Release x64増分ビルド成功。別出力先 |

連続試験-bの実行中にP2G/G2Pの旧コメントを更新したため、ケース間でソースハッシュが異なる。
コンパイル済みGLSL全文と当時のstage2.pyを保存した。
集計ツールは全コンパイル済みGLSLからコメントと空白だけを除外し、現在の実行文と一致することを検査する。
全ケースの実行文は同じであり、ソースのバイト一致と混同しない。

応力散布の最初の試行 `stage2-stress-20261116-a` はNumPy真偽値のJSON保存で停止。
boolへの変換を追加した-bが成功証跡であり、-aを数値合格として使わない。

## 制約と次の工程

本体Debug/Release x64はビルド済み。ビルド設定の既存警告は維持。
Codexによる新しい本体exeの画面操作・描画との統合実行は未実施。
利用者による実行確認は末尾に記録し、独立実GPU試験と区別する。
長時間・複数GPU・配置密度比較・移動障害物・任意のdt/速度は未検証。
C-2球配置、C-9退化/反転とB-6実SDFの対応は工程3に残す。
工程2は立方体・固定材料・格子128・障害物なしの1秒落下、基準dt=.00025という範囲で完了。
次は工程3の球初期化・実SDF・退化/反転。基準式やSDF境界を変えた場合は本試験を再実施する。

## 利用者による実行確認（2026-11-16）

工程2の案内後、利用者から実行時に特にエラーなしとの報告を受領。
全体的に重厚感が増した印象があり、Linearへ変更した場合も発散は見られなかったとのこと。
提示ログではメモリ方式0、補間方式0の入力、3D Textureの生成とBスプライン読込、
続いて3D Textureの生成とLinear読込を確認した。
利用者の観察と読込ログによる確認であり、試験時間・操作の全手順・実GPU・使用exeの構成は未特定。
長時間安定性、評価・履歴・再開などを含む全統合試験の完了とはしない。

重厚感は主観的な観察として保持する。同じ更新回数では物理時間が以前の1/4になるため、
見かけの動きの遅さを物理式の正しさの証明とは扱わない。
Linearはまだ工程5の式の整合化前であり、今回発散しなかったことから、
過去の修正が不完全だった、または正しい式での発散が解決したとは判断しない。
dt縮小・Grid共通修正の影響を含む現条件での観察として記録する。

起動表示の `Cubic B-Spline Interpolation` は既知の名称不整合C-1。
選択肢のQuadraticと起動表示が異なるが、この表示だけから補間式が3次だとは判断しない。
名称の統一は工程6に残す。今回の追記でソース・試験ツールは変更していない。

## 終了確認と次回の参照先

利用者による工程2の終了確認を受領。[工程3引継ぎ](stage3-handoff.md)に開始手順を整理した。
工程2では、既存差分の保持→工程1回帰→既知解のある単純な連続試験→
条件を一つ変えた対照実験→最小修正→回帰→同一物理時間の刻み比較、の順で原因と影響を確認した。
画面上の印象、数値判定、既知失敗を区別し、合格を得るために閾値を緩めることはしていない。
この終了整理は文書のみであり、工程3の着手やGPU試験の追加を意味しない。
