# MPM Sand Simulation

OpenGLのコンピュートシェーダーで砂の落下・堆積と、動く障害物との接触を計算する研究用シミュレーターです。格子の保存方式と粒子・格子間の補間方式を個別に選べます。

## デモ

![開発時の落下・堆積と障害物操作のデモ](media/demo4.gif)

開発時に収録した操作例です。現行版の方式比較と性能評価は条件を揃えた試験として扱います。現行版の性能値は掲載保留です。

## 機能

- 格子保存は3DテクスチャまたはSSBO、補間は2次Bスプラインまたは線形。計4通りを選択できます。
- OBJメッシュから符号付き距離場（SDF）を生成し、障害物をShootやSwingで操作できます。直前の操作も再実行できます。
- Points、Spheres、Surface、Splatの描画方式と、粒子の状態を表示するDebug Colorを選べます。
- 履歴から粒子と障害物の状態を復元できます。GPUパス時間、粒子状態、アプリFPSなどは設定に応じて記録し、CSVへ出力できます。

## ビルドと起動

WindowsでVisual Studio 2022（C++デスクトップ開発、v143）と、OpenGL 4.3以上に対応するGPU・ドライバーを使用します。GLEW、GLFW、GLMはNuGet依存関係です。

1. `GPUMPMTex3D.sln` を開き、NuGet依存関係を復元して `Debug | x64` または `Release | x64` でビルドします。
2. 実行時の作業ディレクトリをリポジトリのルートにします。シェーダー、`src/assets/object.obj`、`ImGui/Mplus1-Regular.ttf` を相対パスで読みます。ローカルにOBJがない場合は、向きが整った閉曲面の三角形メッシュを配置してください。
3. 起動コンソールでメモリ方式（`0` = 3D Texture、`1` = SSBO）と補間（`0` = Quadratic B-Spline、`1` = Linear）を選びます。基準方式は `0` → `0` です。画面の **Play** または **Step Forward** で計算を進めます。

既定は1万粒子、128³格子、1ステップ0.25 msです。**Sub-steps** は1描画フレームで行う計算回数を変更します。各ステップの刻みは固定です。

## ドキュメント

- [ソース構成](docs/architecture.md) — ファイルの配置と役割
- [実装詳細](docs/implementation.md) — GPUの計算パス、格子保存、SDF接触、履歴
- [問題と修正の記録](docs/issues_and_fixes.md) — 旧版で観察した問題、修正と確認範囲
