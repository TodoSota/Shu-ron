# ソース構成

ビルドの入口は [GPUMPMTex3D.sln](../GPUMPMTex3D.sln)、アプリケーションの入口は [src/main.cpp](../src/main.cpp) です。GPUの計算パスとデータ配置は[実装詳細](implementation.md)に記載しています。

## アプリケーション・操作

| ファイル | 役割 |
|---|---|
| [main.cpp](../src/main.cpp) | ウィンドウ・GL・ImGuiの初期化、方式入力、メインループ、描画とUI、CSV保存 |
| [SimulationSetup.h](../src/SimulationSetup.h) / [SimulationSetup.cpp](../src/SimulationSetup.cpp) | `SimulationSettings` と `SimulationResources` の定義、物理既定値、GPU上限検査、候補生成 |
| [SimulationRun.h](../src/SimulationRun.h) / [SimulationRun.cpp](../src/SimulationRun.cpp) | `SimulationRun` による実行制御、診断時の障害物・操作復元、履歴・粉塵・FPSの更新、再起動 |
| [core/Window.h](../src/core/Window.h) | GLFWウィンドウ、OpenGL初期化、入力取得、画面更新 |
| [core/Camera.h](../src/core/Camera.h) | 視点、投影・ビュー行列、カメラ操作 |
| [core/Shader.cpp](../src/core/Shader.cpp) | GLSLの読込と相対include展開、コンパイル、リンク |
| [core/Errorcheck.cpp](../src/core/Errorcheck.cpp) | OpenGLエラーの確認 |
| [controller/InteractionController.cpp](../src/controller/InteractionController.cpp) | Shoot・Swing、軌道プレビュー、操作の再実行 |
| [core/MeshResource.h](../src/core/MeshResource.h) | OBJの読込、描画用メッシュとSDFの生成・保持 |
| [sdf/SDFInstance.h](../src/sdf/SDFInstance.h) | 障害物ごとの姿勢・速度・変換行列と履歴用スナップショット |

## 物理・評価・履歴

| ファイル・フォルダ | 役割 |
|---|---|
| [mpm/IMpmSimulator.cpp](../src/mpm/IMpmSimulator.cpp) | 粒子の初期配置、物理UBOの更新、変形異常の診断保存 |
| [mpm/IMpmObject.cpp](../src/mpm/IMpmObject.cpp) | 計算と描画で使う粒子VAO/VBOのダブルバッファ |
| [mpm/3dtexture/](../src/mpm/3dtexture/) | `MpmObject3DTEX` が格子画像を所有し、`MpmSimulator3DTEX` が計算パスを実行 |
| [mpm/ssbo/](../src/mpm/ssbo/) | `MpmObjectSSBO` が格子バッファを所有し、`MpmSimulatorSSBO` が計算パスを実行 |
| [mpm/shaders/](../src/mpm/shaders/) | 保存方式別の計算パス、評価・履歴用シェーダー、`common/` の共有GLSL定義 |
| [mpm/MpmParticle.h](../src/mpm/MpmParticle.h)・[MpmPhysics.h](../src/mpm/MpmPhysics.h) | 粒子バッファと物理UBOのCPU側レイアウト |
| [mpm/MpmEvaluator.cpp](../src/mpm/MpmEvaluator.cpp) | GPUパス計測、粒子評価、代表粒子とFPSの記録、CSV出力 |
| [mpm/TimelineManager.cpp](../src/mpm/TimelineManager.cpp) | GPU粒子履歴とCPU障害物履歴の記録・復元 |

## 描画・その他

| ファイル・フォルダ | 役割 |
|---|---|
| [render/Renderer.cpp](../src/render/Renderer.cpp) | 描画パスと描画設定UI |
| [render/](../src/render/) | Surface、Splat、影、SSAO、粉塵、描画ターゲット、GLSL |
| `src/assets/`・[ImGui/](../ImGui/) | 実行時に読むOBJと、UIライブラリ・フォント |
| [legacy/](../legacy/) | 旧実装と旧移流シェーダー。一部のC++ファイルは現行プロジェクトにも登録 |
| [tools/validation/](../tools/validation/) | 独立参照、GPUパス、C++シミュレーターの回帰試験 |
