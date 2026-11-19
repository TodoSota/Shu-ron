# 工程5 線形2方式の格子接続と転送方式の検討

## 格子接続

SSBO線形版のSetup・Gridは格子バッファを使用していた一方、P2G・G2Pは画像ユニットを参照していた。P2G・G2Pをbinding 2のstd430バッファへ接続し、節点をx最速の16バイトに揃えた。P2GはuintのCASで浮動小数点のビット表現を加算する。G2Pは入力粒子から位置を読み、結果を出力側に代入する。C++側の格子サイズ設定とパス間バリアを使用する。

通常のC++シミュレーターを使い、Intel UHD GraphicsでTEXとSSBOの線形パイプラインを各1ステップ実行した。格子128³、内部の粒子1個、外力・障害物接触なし、dt=.00025秒、初期速度(.2,-.1,.3) m/s。両方式で位置誤差0、速度誤差1.67e-8 m/s、GLエラー0。位置2e-7 m、速度1e-6 m/sの判定に合格した。証跡は `artifacts/baselines/stage5-linear-{0,1}/`。

## B-4の導出と採用

セル内座標を `t = x/dx - floor(x/dx)`、節点ビットを `s∈{0,1}³`、節点変位を `r = dx(s-t)` とする。各軸の重みは下側が `1-t_j`、上側が `t_j`、3次元の重み `w_s` はその積。8節点が揃うと `Σw_s=1`、`Σw_s r_s=0`。軸間の交差項が消えるため、

```text
D_p = Σ w_s r_s r_sᵀ = dx² diag(t_j (1-t_j))
B_p = Σ w_s v_s r_sᵀ
C_p = B_p D_p⁻¹
```

定数＋アフィン速度場では `B_p = C_exact D_p`。固定係数 `4/dx²` で正確に回収できるのは該当軸がセル中央の場合。現在の線形G2Pはさらに `4/dx` を使うため、ワールド単位で回収したCには小さすぎる。

修正前の実GPU試験でこの差を再現した。dt=0、セル内座標(.25,.5,.75)、入力の `Cxx=.4 s⁻¹` に対し、TEX/SSBOとも回収値は `.00351564 s⁻¹`。旧式から計算した `.00351563 s⁻¹` と一致する。証跡は `artifacts/baselines/stage5-affine-{0,1}/`。

応力散布の形 `−Δt V_p τ_p (w_s D_p⁻¹ r_s)` の各軸を展開すると、完全なセル内部では `w_s D_p⁻¹ r_s = ∇w_s`。その軸では符号と `1/dx`、残り2軸では重みの積になる。応力項はこの形で評価できるため、明示的な逆数と微小値の積を避けられる。

## 方針選択に関わる境界条件

| 候補 | メリットと残る検証 |
|---|---|
| APICを維持し、位置依存のDpを使う | P2Gのアフィン運動量項を保ち、完全なセル内部でアフィン場を回収する。節点面・稜線・頂点ではDpが階数を失う。擬似逆で失った方向をゼロにする案は、その方向のCと応力にも影響する。近傍で切り替える場合は閾値と連続性を検証する。 |
| 重み勾配で通常MPMに整理する | 内力を `−Vτ∇w` とし、変形更新に格子速度の勾配を使う。完全なセル内の内力は上の式と一致する。運動量の転送方法と節点面での片側勾配、質量ゼロの節点の扱いを決める必要がある。 |

節点面のすぐ内側では、どちらの式でも重みが小さい節点へ有限の力が入る。例えば単一粒子、密度400 kg/m³、dx=1.5/128 m、100 Paの単軸応力、面からの無次元距離1e-7では、対象節点の応力による加速度の大きさは `τ/(ρ dx t)≈2.13e8 m/s²`。他粒子の質量がない場合の解析値で、シミュレーション中の測定値は今後確認する。

CPUの独立参照は中央・非対称・面近傍・面・稜線・頂点の6条件で合格。完全な内部のC回収と応力散布の等価性、節点面での階数低下、質量ゼロの扱いによる勾配誤差を確認した。証跡は `artifacts/baselines/stage5-linear-reference/`。

利用者の選択により、APICを維持し、位置依存のDpを採用した。G2PのC回収とP2Gの応力散布で共通の対角逆数を使う。

```text
Q_j = t_j(1-t_j)
D_reg⁻¹[j,j] = 1/(dx² max(Q_j, 1/16))
```

1/16は今回の実装で試験する正則化の下限。中央の二次モーメント1/4に対して、逆数の最大を中央値の4倍に制限する。面から約0.067セル以内ではその軸の回収を `Q_j/max(Q_j,1/16)` 倍に減衰する。節点面では法線方向がゼロになり、内側から連続につながる。完全なセルの内部で下限に達しない軸は解析式のまま。

応力による格子速度の増分は、小質量に対応する重みが力積にも掛かるため、単一粒子では `Δt V_p/m_p × ‖τ‖ × √3/(dx/16)` を上界に持つ。上界は応力の大きさとdtに依存する。軸別減衰区間ではアフィン再現性が低下し、非対角応力で離散トルクが変わり得る。完全な支持域で線形運動量の内力相殺は維持する。領域端で欠ける重みは既存の方針で除外する。

物理モデルはJacobi8巡、Kirchhoff応力、硬化・塑性分岐、小質量、境界・SDFを2次Bスプラインと整合させた。両線形方式で変形異常の診断・停止を有効にした。SSBOはSetupとGridを共通化し、補間固有の処理はP2GとG2Pに置く。

## 検証

GPUの独立ノード試験はTEX/SSBO各10条件に合格。定数・アフィン・回転、float32の面の両側、厳密な面・稜線・頂点、支持域が領域端で欠ける場合を検証した。最大誤差は質量3.74e-8（相対）、応力力積7.99e-7（相対）、速度1.79e-8 m/s、C 3.48e-6 s⁻¹。観測した応力由来の節点速度増分は最大1.202 m/s。証跡は `stage5-dp-final-fixtures-{0,1}`。

厳密な節点位置の最初の試験では、セル座標からワールド座標を往復するときのfloat32丸めで、参照した節点とGPUが使った節点がずれた。二進で表現できるdx=1/128を独立ノード試験に用い、同じ誤差閾値で再試験した。連続計算は基準条件のdx=1.5/128。

C++プローブでは入力Cxx=.4 s⁻¹に対して両方式とも.399999 s⁻¹を回収。P2GとG2P異常の保存・非swap・停止・リセット後の再開も確認した（`stage5-dp-verified-host-{0,1}`）。ライフサイクル試験の初回はアフィン分離試験のdt=0が残ったためG2P異常を起こせず、dt=.00025へ戻して再試験した。

Debug／Release x64の隔離ビルド成功（`stage5-Debug.log`、`stage5-Release.log`）。2次Bスプラインは最終実装で両方式各108判定が合格した（`stage5-dp-reg-{0,1}`）。

初回ステップを両方式で比較し、全格子のゼロ化と基準粒子数での総質量を確認した。立方体と球の床堆積1秒および箱SDFの4条件には、工程4と同じ方式間比較閾値を使用する。条件と許容差は `artifacts/baselines/stage5-dp-first/plan.json`。

連続計算は1万粒子、格子128³、領域1.5 m、dt=.00025 s、描画・評価・履歴なし。定数移流と重力の独立短時間試験（各.05秒、dt=.001）も両方式で合格した。初回位置差はゼロ、速度差は最大4.66e-10 m/s、質量相対誤差は1.71e-9以下。終端6条件は事前の閾値をすべて満たした。

| 終端条件 | TEX/SSBOの位置RMS差 (mm) |
|---|---:|
| 立方体・床堆積1秒 | 0.448 |
| 球・床堆積1秒 | 0.338 |
| 箱SDF・静止 .3秒 | 0.0484 |
| 箱SDF・並進 .3秒 | 0.0384 |
| 箱SDF・回転 .3秒 | 0.0948 |
| 箱SDF・回転（Swing比率） .3秒 | 0.302 |

終端の最大重心差.0280 mm、最大AABB端点差1.275 mm、最大RMS速度差.000328 m/s。証跡は `stage5-dp-comparison/report.json` と `stage5-dp-{0,1}-{cube,sphere,obstacles}/`。比較前に線形シェーダーのコメントを更新し、実行文の一致を確認した。

TEX/SSBOの線形同士は補間・正則化・材料・境界を揃えた格子保存方式の比較。2次Bスプラインとの比較では、支持域と補間関数の差に加え、線形側の面近傍の正則化も効く。時間刻みや材料を変更した条件、長時間・任意高速・主画面の全連携はこの検証範囲に含まれない。

例としてTEXの立方体1秒堆積で、線形と工程4の2次Bスプラインの位置RMS差は43.7 mm、AABB端点差は最大210 mm。初期条件は同じ。格子保存方式の許容差はこの補間方式間の比較には適用しない。

## 再現

```powershell
$runRoot = 'artifacts/baselines/stage5-replay'
python tools/validation/build_init.py --output "$runRoot/init"
python tools/validation/stage3_mesh.py --output "$runRoot/mesh" --build-template "$runRoot/init/build.cmd"
python tools/validation/stage5_linear_reference.py --output "$runRoot/reference"
foreach ($index in 0,1) {
    $modeFlags = @()
    if ($index -eq 1) { $modeFlags += '--ssbo' }
    $regression = "$runRoot/stage5-dp-reg-$index"
    python tools/validation/run.py @modeFlags --output $regression --init-probe "$runRoot/init/probe.exe"
    python tools/validation/stage3_host.py --linear @modeFlags --output "$runRoot/host-$index" --build-template "$runRoot/init/build.cmd"
    python tools/validation/stage5_linear_gpu.py @modeFlags --output "$runRoot/fixtures-$index"
    python tools/validation/stage2.py --linear @modeFlags --output "$runRoot/stage5-dp-$index-cube" --regression $regression --dts .00025
    python tools/validation/stage2.py --linear @modeFlags --output "$runRoot/stage5-dp-$index-sphere" --regression $regression --dts .00025 --initial-shape sphere --fall-only
    python tools/validation/stage3_obstacles.py --linear @modeFlags --output "$runRoot/stage5-dp-$index-obstacles" --regression $regression --mesh "$runRoot/mesh/box.bin"
}
python tools/validation/stage4_compare.py --linear --continuous-root $runRoot --regression "$runRoot/stage5-dp-reg-0" --output "$runRoot/comparison"
```
