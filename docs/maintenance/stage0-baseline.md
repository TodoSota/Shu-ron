# 工程0: 現状の固定

実施日: 2026-11-15〜16。状態: 完了（2026-11-16、復元照合・両構成ビルド/起動・実GPU情報取得済み）。物理式の正しさを保証する記録ではない。

## 保存と復元

リポジトリルートで `powershell -NoProfile -File tools/CaptureBaseline.ps1 -Label baseline` を実行する。
出力された固有ディレクトリの `COMPLETE.txt` がある場合だけ成功とする。
`head.txt`、`status.txt`、作業ツリー/ステージ差分、`files/`、SHA-256付き `manifest.json` を保存する。
tracked/非ignoredのuntrackedファイルに加え、ignoredの `src/assets/` と `packages/` も保存する。
ファイルごとにコピー前・コピー後の元ファイルと保存先のハッシュを照合する。編集中・アプリ実行中は取得しない。

復元は新しい空のディレクトリに `files/` の内容をコピーし、manifestの各パスのSHA-256を照合する。
既存作業ディレクトリへ上書きしない。削除済みtrackedファイルは保存されない。
Git履歴、ビルド生成物、マシン固有の設定、上記以外のignoredファイルは含まれない。
Git履歴が必要ならheadのコミットを別途取得する。パッチ単独ではuntracked/ignoredの資産を復元できない。
保存先はローカルの `artifacts/baselines/`（Git対象外）。別媒体への複製は未実施。

## 作業開始時のソース

- HEAD: `7ffa1686ac15560a4c05b1e8c26aeb2389a229c6`。
- 既存変更: `imgui.ini`、`src/main.cpp`、TEXのP2G/G2P（Bスプライン/線形）、Gridの計7ファイル。
- 未追跡: AGENTS.md、PLANS.md、詳細計画ディレクトリ。
- 初回成功保存: artifacts/baselines/20261115-193522-892-pre-stage0。保存ツールとignore設定を追加後、プログラム無変更の状態を取得。後から追加した本記録文書はこの保存には含まれない。
- 失敗した保存先はCOMPLETEがなく、基準に使用しない。

| 点検ID | 現ファイル（src/以下） |
|---|---|
| A-1、B-2 | mpm/shaders/3dtexture/mpm_p2g_3dtex.comp、mpm_p2g_Linear_3dtex.comp、mpm_g2p_3dtex.comp、mpm_g2p_Linear_3dtex.comp |
| A-2、B-3、B-7 | mpm/shaders/3dtexture/mpm_g2p_3dtex.comp |
| A-3、B-1、C-3 | main.cpp（初期化の実体はmpm/IMpmSimulator.cppも参照） |
| B-5、B-6 | mpm/shaders/3dtexture/mpm_grid_3dtex.comp（SDF生成はcore/MeshResource.h） |

対応済み判定・数値検証は後工程。古い点検コメントを根拠に実行文を変更しない。

## 基準実験条件（ソース確認）

| 項目 | 条件 |
|---|---|
| 起動入力 | 保存方式0=TEX、補間0=2次Bスプライン。起動表示のCubicは既知の名称不整合C-1 |
| 粒子/格子/領域 | 10,000 / 128³ / [0,1.5]³ |
| 初期配置 | 立方体、中心(0.75,0.75,0.75)、一辺0.3、seed=0のmt19937。色用乱数も同じengineを消費 |
| 初期粒子状態 | v=0、C=0、F=I、alpha=0.267765、q=0、vc=0、state=1 |
| 材料 | E=50,000、ν=0.3、密度400。μ=E/[2(1+ν)]、λ=Eν/[(1+ν)(1−2ν)] |
| 体積/質量 | 1粒子: 0.3³/10,000=2.7e-6、質量=0.00108。合計体積0.027、質量10.8（設定からの計算値） |
| 刻み/重力 | dt=0.001、subSteps=1、重力(0,-9.8,0)。subSteps増加はdt縮小ではない |
| 床/接触 | 床y=0.1、摩擦0.6、床反発設定0.5。TEXの侵入法線速度は反発させず除去。接触半径dx/2、overlap=dx×0.005 |
| 障害物 | src/assets/object.obj、CPU生成SDF 64³、位置(0.75,0.3,0.75)、scale=(0.2,0.2,0.2)。操作なし |
| 描画 | 1280×720で生成、Spheres、影/SSAO/粉塵ON、radiusScale=1.3。画面DPIと実Framebuffer寸法は実行時確認待ち |
| 同期/評価/履歴 | SwapInterval指定1、評価6スイッチOFF、評価間隔10、代表粒子0/500。履歴600フレーム、進行時に記録 |
| 初期状態 | 停止中。Camera Control。既存imgui.iniも保存する |

粒子数変更では領域も拡縮する。今回は既定値を保持する。seedだけで異なる標準ライブラリ間の同一乱数分布実装は保証しない。
障害物はコメントと異なり領域内にある。落下試験で障害物なしを仮定しない。
CSVだけでは条件を保存できず、評価機能の正しさも未検証なので本記録と併用する。

## 環境と検証記録

- Visual Studio Community 2022 17.14.45（17.14.37906.3）、MSVCディレクトリ14.44.35207、Toolset v143。
- インストール済みSDK: 10.0.19041.0、10.0.26100.0。今回のビルド出力では10.0.26100.0のヘッダーを使用。
- NuGet実体確認: glew-2.2.0=2.2.0.1、glfw=3.4.0、glm=1.0.2。
- OS列挙GPU: Intel UHD Graphics（32.0.101.7082）、NVIDIA GeForce RTX 4060 Laptop GPU（31.0.15.3652）。実際のGLコンテキストのGPU/GL/GLSLは未確認。
- Debug x64: Build成功、12警告、0エラー。C4244、C4099、C4005、LNK4042を含む。工程0では修正しない。
- Release x64: Build成功、146警告、0エラー。C4819（コードページ932とUTF-8の不整合）などを含む。工程0では設定を変更しない。
- ビルドログ: `artifacts/baselines/debug-build.log`、`release-build.log`。履歴保全のため再試験では別名を使う。
- 使用コマンド: VS2022の `MSBuild/Current/Bin/MSBuild.exe GPUMPMTex3D.sln /t:Build /p:Configuration=Debug /p:Platform=x64 /v:minimal /nologo`（ReleaseはConfigurationを変更）。
- Win32、数値試験、性能試験は未実施。DebugのGPU描画・操作は下記の利用者報告を参照。その他は以下の手順で確認する。

保存検証: 1,824ファイルを保存。manifestを読み直して保存先全ファイルを独立に再ハッシュし、不一致0件。object.obj、フォント、NuGet依存を含む。利用者変更の7ファイルは作業後も保存時とハッシュ一致。git diff --check成功。

最初の保存試行（20261115-193508-364-pre-stage0）はパッチ出力引数の組み立てエラーで失敗。修正して上記の保存を取得。失敗先にはCOMPLETEがなく使用禁止。

## 実行確認手順（着手時点。完了結果は末尾参照）

1. アプリを閉じた状態で保存が成功していることを確認する。
2. リポジトリルートを作業ディレクトリとして `x64/Debug/GPUMPMTex3D.exe` を起動する。コンソールで0、0を入力する。
3. SDF生成を待ち、フォント・OBJ・シェーダーの読込エラー、GLエラー、粒子表示の有無を記録する。
4. 初期停止中でsubSteps=1を確認し、操作を加えず `Step Forward (1 Frame)` を1回クリックする。画面が応答し、履歴が進むか記録する。
5. 通常終了し、実行前後のgit差分を確認する。imgui.iniの変化は自動保存として区別し、元の利用者変更を破棄しない。
6. Release x64でも同条件で起動する。日時・構成・エラー全文・実際のGPU/GL情報が得られたかを追記する。

実GPU情報の確認手段、復元先でのビルド/起動は未完。起動成功だけで数値検証合格とはしない。
履歴だけで約1.15 GBを要求するため、確保失敗は条件を変えず記録する。
工程0は保存照合・復元確認・ビルド・GPU起動の証跡が揃ってから完了とする。

## 利用者による実行確認（報告日: 2026-11-16）

リポジトリルートのPowerShellから `.\x64\Debug\GPUMPMTex3D.exe` を実行。
入力0/0（TEX＋2次Bスプライン）を提示ログで確認。利用者は起動、Step Forward (1 Frame)、Playが問題なく動くと報告。
SDF 64³の生成完了もログで確認。専用の数値検証や追加診断は行っていない。
利用者によるVisual Studioでの再ビルドが前報にあるため、2026-11-15に生成した実行ファイルとのバイナリ同一性は未確認。

「各種シェーダーを変更しての実行」も成功したとの報告がある。ただし具体的な変更内容・方式・条件は不明なので、全方式の合格とは扱わない。

提示された起動ログ:

```text
OBJ Warn: Material file [ object.mtl ] not found in a path :
Failed to load material file(s). Use default material.

SDF Generation Started... (Resolution: 64)
Progress: 0%
Progress: 15%
Progress: 31%
Progress: 46%
Progress: 62%
Progress: 78%
Progress: 93%
SDF Generation Completed!
Select MPM Memory Mode:
  0: 3D Texture (Base)
  1: SSBO (Test)
> 0
Select MPM Interpolation Method:
  0: Quadratic B-Spline
  1: Linear
> 0
Creating Simulator: 3D Texture mode
Loaded MPM Shaders: Cubic B-Spline Interpolation
```

- object.mtl未検出の警告があり、ログでは既定マテリアルへフォールバック。今回の起動を阻害していない。配布資産の照合（R-7）の確認材料として保持する。
- Cubic B-Splineの表示は既知のC-1。選択0の2次Bスプラインとの名称不整合であり、工程0では変更しない。
- 先のフォントassertはexeの直接起動時に発生し、ルートからの起動で解消したと利用者が報告。起動手順に作業ディレクトリを明記する。
- 実際に選択されたGPU、OpenGL/GLSLバージョン、Release起動、復元先での再現は引き続き未確認。
### 2026-11-16 利用者によるRelease確認と表示不具合

- ルートからRelease x64を起動し、TEX＋2次Bスプライン（提示ログ0/0）、前回と同様の操作に問題なしと利用者が報告。SDF生成完了、MTL警告、Cubic表示はDebugと同様。Release起動は確認済みに更新。
- Play、Step Forward、視点FPS/Orbitに文字化けの報告あり。静的確認: vcxprojのDebug Win32/x64には/source-charset:utf-8があり、Release両構成にはない。main.cppにはu8付きの記号・日本語ラベルがある。ReleaseビルドのC4819とも整合し、入力文字コードの構成差が原因の有力候補。表示との因果の確定は設定統一後の再ビルド・画面確認で行う。
- 対応案: 全構成のソース/実行文字コードを/utf-8で統一し、既存/source-charset指定の重複を整理。今回はプログラム・ビルド設定は未変更。非ASCIIの通常リテラルへの影響も確認する。
- 工程0は作業中。保存状態の復元再現と実GLコンテキスト情報は未確認。

## 復元・実GPU確認（2026-11-16、Codex実施）

保存元 `artifacts/baselines/20261115-193522-892-pre-stage0/files/` を、新規の
`artifacts/baselines/restore-20261116/` へコピー。manifestの1,824ファイルを復元先で照合し、不一致0件。
元の作業ツリー、初回保存、利用者のexeを上書きしていない。

| 確認 | 結果 |
|---|---|
| 復元先Debug x64フルビルド | 成功、12警告、0エラー |
| 復元先Release x64フルビルド | 成功、146警告、0エラー |
| 復元先Debugの無変更アプリ起動 | 0/0入力、SDF生成とTEX Bスプライン読込完了。60秒後にCloseMainWindowで終了要求、終了コード0 |
| 復元先Releaseの無変更アプリ起動 | 0/0入力、SDF生成とTEX Bスプライン読込完了。60秒後にCloseMainWindowで終了要求、終了コード0 |
| 診断コピー | 物理更新を含む3フレーム、各フレーム末glFinish後のglGetErrorは0、終了コード0 |

診断は復元先main.cppだけに一時的に `tools/BaselineDiagnostics.h` 呼出し、各フレームstepFrame=true、
3フレーム後の終了要求を追加して実施。実際のアプリのGLコンテキスト内で情報を取得した。
物理式・シェーダー・材料・描画設定は変更していない。診断変更後のmain.cppは
`artifacts/baselines/diagnostic-main-20261116.cpp`、診断exeは復元先Debugの `GPUMPMTex3D-diagnostic.exe` に識別して保存。
その後、復元先main.cppを保存時と同一の内容に戻し、更新時刻を更新して確実に再コンパイルした。
復元先のimgui.iniは実行による自動保存で変わり得る。初回保存そのものは変更していない。

実コンテキストの取得結果（過去の利用者実行のGPUを遡って保証するものではない）:

| 項目 | 値 |
|---|---|
| Vendor / Renderer | Intel / Intel(R) UHD Graphics |
| OpenGL | 4.6.0 - Build 32.0.101.7082 |
| GLSL | 4.60 - Build 32.0.101.7082 |
| Framebuffer | 1280×720 |
| GL_MAX_3D_TEXTURE_SIZE | 2048 |
| GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS | 16 |
| GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS | 1024 |
| GL_MAX_SHADER_STORAGE_BLOCK_SIZE | 1,073,741,824 bytes |

履歴全体の確保容量と1ブロックのアクセス上限は別の制約である。この上限取得と3フレーム実行は、
600フレーム全域の記録・復元の検証ではない。履歴容量・アクセス範囲の確認はC-4/R-3で行う。
RTX 4060での実行は本試験では行っていない。GPU選択や性能比較条件は後続試験ごとに明記する。

証跡（すべてartifacts/baselines/以下、既存ログを上書きせず新規名で保存）:

- restore-debug-20261116.log、restore-release-20261116.log: 無変更の復元先フルビルド。
- diagnostic-build-20261116.log、diagnostic-full-20261116-stdout.txt / -stderr.txt: 診断ビルドとGPU実行。
- restore-debug-original-20261116.log: 診断変更を除去したDebug再ビルド。
- restore-debug-final-20261116-stdout.txt / -stderr.txt、restore-release-final-20261116-stdout.txt / -stderr.txt: 無変更アプリ起動。
- verification-20261116.json: 復元照合・環境・診断結果の要約。

初回smokeスクリプトは終了コードが空になる不備があり、起動ログのみを確認した。
Process.Handleの保持とRefreshを追加して再試験した。最初の診断試行も20秒で閉じる設定では初期化待ちが不足したため、
120秒を上限とし3フレームで自動終了する試験を別名で実施した。失敗・途中のログは成功証跡と混同しない。
通常アプリの起動には `tools/RunBaselineSmoke.ps1 -RestoreRoot artifacts/baselines/restore-20261116 -Tag <未使用の識別名> -Configuration Debug` を使用。
診断を再現する場合は保存済みのdiagnostic-mainとヘッダーを隔離コピーに適用してビルドする。元のソースへ適用しない。

この確認は再現可能な作業基準の確立を目的とする。画面の見た目・手動操作は利用者報告に基づき、
Codexの自動確認は読込ログ、プロセス終了、診断出力に基づく。数値精度・長時間安定性・全方式・Win32・全履歴は未検証。
工程0の最終判定: 完了。ソース保存と復元、Debug/Release x64ビルド・起動、利用者による操作確認、実コンテキスト情報を記録した。次は工程1の検証準備。Releaseの文字化け、MTL警告、C-1は未修正の既知事項として保持する。
