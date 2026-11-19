# 工程4 SSBO 2次Bスプラインの検証

2026-11-19の工程4終了時点の記録。開始時の差分、基準条件、対象の点検IDは[引継ぎ](stage4-handoff.md)、線形版の後続変更は[工程5記録](stage5-linear.md)を参照。

## 実装

- P2GとG2PはTEXのJacobi8巡、Kirchhoff応力、塑性分岐、h3=10、C回収係数を採用。G2Pの移流は入力位置から書き出す。
- Gridは正の小質量を維持し、節点位置、床摩擦、上下各3節点の拘束、SDFの座標・勾配距離・相対速度をTEXと揃えた。
- Setupを含む4パスの格子はbinding 2のstd430バッファで、x最速、1節点16バイト。128³で32 MiB。P2GはバッファのuintにCASを行う。使用GPUの上限は1 GiB/ブロック、16 SSBO binding。
- C-9の96バイトの診断バッファ、メモリバリア、異常情報と直前状態の保存を共通の `IMpmSimulator` へ移した。両方式ともP2G/G2P直後に診断を読み、失敗時はバッファ交換・ステップ加算・評価より先に戻る。mainの停止・履歴条件は既存の共通経路を通る。
- SSBO線形版は従来のGridを同内容の `mpm_grid_Linear_ssbo.comp` 経由でロードする。線形版の転送接続と物理式は工程5。

## 独立試験

GPUは既定で選ばれたIntel(R) UHD Graphics、OpenGL 4.3 / GLSL 4.30（32.0.101.7082、Pythonの検証コンテキスト）。x64のDebugとReleaseを隔離ビルド。

| 試験 | 結果 | 証跡名 |
|---|---|---|
| 移植前のTEX回帰・停止 | 回帰108判定およびP2G/G2P停止・保存・resetが合格 | stage4-tex-regression、stage4-tex-host |
| 移植後の独立参照 | TEX/SSBO各108判定。旧式の検出4判定を含む。SVD・応力・塑性・定数/アフィン/回転転送・初期化 | stage4-verified-0-regression、stage4-verified-1-regression |
| SSBO境界・平面接触 | 境界18、接触6が合格。接触の最大絶対誤差2.39e-8 | stage4-boundary、stage4-contact_radius |
| SSBO壁接近 | 壁・角の1/10 m/sの16条件に合格。100 m/sの8条件は1ステップで支持域逸脱 | stage4-wall |
| SSBO小質量 | 50ステップ定数移流の最大位置誤差1.64e-6 m、最大速度誤差3.44e-7 m/s。旧閾値の対照は速度誤差.0177 m/s | stage4-low-mass-verified |
| SSBO応力散布 | 4条件合格。最大相対誤差4.51e-4、質量相対誤差1.41e-8 | stage4-stress |
| SSBO異常入力 | 9条件合格。P2G入力/G2P試行で異常の段階と入力保持を確認 | stage4-stage3_fault |
| C++停止ライフサイクル | 実シミュレータークラスで2段階の保存・非swap・停止ラッチとreset後の再開を確認 | stage4-verified-0-host、stage4-verified-1-host |
| x64隔離ビルド | Debug/Release成功。警告あり | stage4-verified-Debug.log、stage4-verified-Release.log |

証跡は `artifacts/baselines/`。C++試験の診断バイナリは各reportが示す `artifacts/diagnostics/` の下に保存。
作業途中のリンク失敗（Grid/G2Pのローカルサイズ宣言の欠落）と、古い初期化プローブのハッシュ拒否を確認した。宣言を復元し、プローブを再ビルドしたうえで、合格証跡を新しい出力先へ取得した。
停止試験の初回並行実行では、診断先をディレクトリ全体の増分で数えて他プローブの出力を含んだ。再試験では各プローブが標準出力に記した保存先を検査した。
小質量試験の初回は計画のdt=.001と読み込んだUBOのdt=.00025が一致せず、位置比較が不合格だった。試験側で計画値を転送し、同じ誤差基準で再試験した。

## 方式間比較

1万粒子・seed=0、領域1.5 m、格子128³、dt=.00025 s、材料・床・障害物条件を揃える。描画・VSync・評価・履歴なし。
同一初期粒子を両GPUパイプラインへアップロードし、集約順による丸め差を許容する。`stage4_compare.py` のplanへ方式差を計測する前に閾値を書き出した。

- 初回ステップは位置最大差1e-4 dx、速度最大差1e-5 m/s、C/Fと塑性状態の成分差5e-4、質量相対誤差3e-6。両方式のSetupが全ノードをゼロ化することも確認。
- 床堆積1秒／障害物.3秒の終端比較は位置RMS 0.5 dx、重心0.1 dx、AABB端点dx、RMS速度差.02 m/s。セル幅を尺度とした形状差と、堆積判定の速度基準.1 m/sより小さい速度差を要求する。
- 立方体・球の初回位置差はともにゼロ、最大速度差4.66e-10 m/s、最大状態差5.66e-8、質量相対誤差1e-9未満。両方式でSetupの全格子ゼロ化を確認。証跡は `stage4-first`。

終端比較6条件はすべて合格。位置RMS・重心差・AABB端点差はmm、RMS速度差はm/s。

| 条件 | 位置RMS | 重心差 | AABB端点差 | RMS速度差 |
|---|---:|---:|---:|---:|
| 立方体・床堆積1秒 | 0.311 | 0.0202 | 0.335 | 1.26e-5 |
| 球・床堆積1秒 | 0.234 | 0.0123 | 0.684 | 4.34e-5 |
| 箱SDF・静止 | 0.233 | 0.00193 | 0.0950 | 1.06e-4 |
| 箱SDF・並進 | 0.182 | 0.00761 | 0.0506 | 1.14e-4 |
| 箱SDF・回転 | 0.125 | 0.00394 | 0.0593 | 7.82e-5 |
| 箱SDF・回転（Swing比率） | 0.926 | 0.0478 | 1.540 | 6.53e-4 |

両方式で1秒後の粒子RMS速度は.00179〜.00212 m/s。箱SDFの4条件は両方式とも接触し、観測した最大侵入量は9.41 mm（上限2 dx=23.44 mm）。終端比較と最終ソースでの初回ステップ再試験は `stage4-comparison/report.json`、連続計算の独立判定と粒子出力は `stage4-{0,1}-{cube,sphere,obstacles}/`。
再試験前にP2Gの点検コメントを更新し、共有C++の改行を揃えた。実行文が一致することを確認し、同じ初期化データで連続計算と終端比較を照合した。

連続接触の入力は工程3で実生成した箱SDF（17³）。工程4でも現ソースで再生成し、SHA-256が一致した（`stage4-mesh`）。

## 再現手順

リポジトリルートで実行し、新規の証跡ディレクトリを指定する。

```powershell
$runRoot = 'artifacts/baselines/stage4-replay'
python tools/validation/build_init.py --output "$runRoot/init"
python tools/validation/stage3_mesh.py --output "$runRoot/mesh" --build-template "$runRoot/init/build.cmd"
foreach ($index in 0,1) {
    $modeFlags = @()
    if ($index -eq 1) { $modeFlags += '--ssbo' }
    $regression = "$runRoot/stage4-final-$index-regression"
    python tools/validation/run.py @modeFlags --output $regression --init-probe "$runRoot/init/probe.exe"
    python tools/validation/stage3_host.py @modeFlags --output "$runRoot/host-$index" --build-template "$runRoot/init/build.cmd"
    foreach ($shape in 'cube','sphere') {
        python tools/validation/stage2.py @modeFlags --output "$runRoot/stage4-$index-$shape" --regression $regression --dts .00025 --initial-shape $shape --fall-only
    }
    python tools/validation/stage3_obstacles.py @modeFlags --output "$runRoot/stage4-$index-obstacles" --regression $regression --mesh "$runRoot/mesh/box.bin"
}
python tools/validation/stage4_compare.py --continuous-root $runRoot --regression "$runRoot/stage4-final-0-regression" --output "$runRoot/comparison"
```

## 検証範囲

GPU試験は記録した既定GPU、粒子条件と物理時間に限定。主画面の描画・評価・履歴を含む全操作は工程8。任意刻み・長時間・高速条件・SDF稜線や大曲率・全GPUは未検証。性能ベンチマークとSDF事前生成時間の計測は今回の試験に含めていない。

