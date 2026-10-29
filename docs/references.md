# 参考文献リスト

このプログラムを理解し、修士論文で引用するための文献である。番号は [theory.md](theory.md) と [code_review.md](code_review.md) の文献番号に対応する。
「★」は、このプログラムを理解するうえで特に重要なもの（最初に読むとよいもの）。
「(DOI 確認済み)」は、出版社または Crossref の記録で書誌情報を確認したもの。それ以外も既知の書誌情報だが、論文に引用する前に出版社のページで巻・号・論文番号を確認すること。

---

## 1. MPM の原論文と基礎

**[1]** ★ D. Sulsky, Z. Chen, H. L. Schreyer. A particle method for history-dependent materials. *Computer Methods in Applied Mechanics and Engineering*, 118(1–2):179–196, 1994. doi:10.1016/0045-7825(94)90112-0
― MPM の原論文。粒子を積分点とする弱形式の離散化。

**[2]** D. Sulsky, S.-J. Zhou, H. L. Schreyer. Application of a particle-in-cell method to solid mechanics. *Computer Physics Communications*, 87(1–2):236–252, 1995. doi:10.1016/0010-4655(94)00170-7
― MPM を固体力学へ適用した初期の論文。

**[3]** F. H. Harlow. The particle-in-cell computing method for fluid dynamics. *Methods in Computational Physics*, 3:319–343, 1964.
― PIC 法の原典。

**[4]** J. U. Brackbill, H. M. Ruppel. FLIP: A method for adaptively zoned, particle-in-cell calculations of fluid flows in two dimensions. *Journal of Computational Physics*, 65(2):314–343, 1986. doi:10.1016/0021-9991(86)90211-1
― FLIP 法の原典。

## 2. コンピュータグラフィックスにおける MPM

**[5]** ★ A. Stomakhin, C. Schroeder, L. Chai, J. Teran, A. Selle. A material point method for snow simulation. *ACM Transactions on Graphics (Proc. SIGGRAPH)*, 32(4), Article 102, 2013. doi:10.1145/2461912.2461948
― CG 分野で MPM が広まるきっかけになった論文。格子速度の衝突処理（Coulomb 摩擦）、弾塑性の乗算分解。

**[6]** ★ C. Jiang, C. Schroeder, A. Selle, J. Teran, A. Stomakhin. The affine particle-in-cell method. *ACM Transactions on Graphics (Proc. SIGGRAPH)*, 34(4), Article 51, 2015. doi:10.1145/2766996
― APIC。アフィン速度行列 C_p、2次 B スプラインで D_p = Δx²/4 I になること。

**[7]** ★ G. Klár, T. Gast, A. Pradhana, C. Fu, C. Schroeder, C. Jiang, J. Teran. Drucker–Prager elastoplasticity for sand animation. *ACM Transactions on Graphics (Proc. SIGGRAPH)*, 35(4), Article 103, 2016. doi:10.1145/2897824.2925906 (DOI 確認済み)
― 本プログラムの砂の構成則の直接の出典。対数ひずみの超弾性、ひずみ空間での Drucker–Prager の return mapping、硬化則とパラメータ。

**[8]** ★ A. P. Tampubolon, T. Gast, G. Klár, C. Fu, J. Teran, C. Jiang, K. Museth. Multi-species simulation of porous sand and water mixtures. *ACM Transactions on Graphics (Proc. SIGGRAPH)*, 36(4), Article 105, 2017. doi:10.1145/3072959.3073651 (DOI 確認済み)
― 体積補正（volume correction, `vc`）の出典。

**[9]** ★ Y. Hu, Y. Fang, Z. Ge, Z. Qu, Y. Zhu, A. Pradhana, C. Jiang. A moving least squares material point method with displacement discontinuity and two-way rigid body coupling. *ACM Transactions on Graphics (Proc. SIGGRAPH)*, 37(4), Article 150, 2018. doi:10.1145/3197517.3201293 (DOI 確認済み)
― MLS-MPM。本プログラムの P2G / G2P の構造（力の項を ∇w ではなく D_p⁻¹(x_i − x_p) で書く形）の出典。剛体との双方向連成も扱う。

**[10]** C. Jiang, C. Schroeder, J. Teran. An angular momentum conserving affine-particle-in-cell method. *Journal of Computational Physics*, 338:137–164, 2017. doi:10.1016/j.jcp.2017.02.050
― APIC の角運動量保存の解析。

**[11]** ★ C. Jiang, C. Schroeder, J. Teran, A. Stomakhin, A. Selle. The material point method for simulating continuum materials. *ACM SIGGRAPH 2016 Courses*, 2016. doi:10.1145/2897826.2927348
― MPM の教科書的な講義ノート。連続体力学の基礎から、離散化、構成則、実装、時間刻みの選び方、1 セルあたりの粒子数の目安まで網羅している。**最初に読むべき資料。**

## 3. MPM の数値解析・発展

**[12]** M. Steffen, R. M. Kirby, M. Berzins. Analysis and reduction of quadrature errors in the material point method (MPM). *International Journal for Numerical Methods in Engineering*, 76(6):922–948, 2008. doi:10.1002/nme.2360 (DOI 確認済み)
― 線形の形状関数で起こる cell-crossing error の解析と、B スプラインによる改善。線形補間と 2次 B スプラインの比較の理論的な根拠。

**[13]** S. G. Bardenhagen, E. M. Kober. The generalized interpolation material point method. *Computer Modeling in Engineering & Sciences*, 5(6):477–495, 2004.
― GIMP。粒子に大きさを持たせて cell-crossing を緩和する方法。

**[18]** A. de Vaucorbeil, V. P. Nguyen, S. Sinaie, J. Y. Wu. Material point method after 25 years: Theory, implementation, and applications. *Advances in Applied Mechanics*, 53:185–398, Elsevier, 2020. doi:10.1016/bs.aams.2019.11.001
― MPM の包括的なレビュー（工学分野）。関連研究の章を書くのに便利。

**[19]** S. Dunatunga, K. Kamrin. Continuum modelling and simulation of granular flows through their many phases. *Journal of Fluid Mechanics*, 779:483–513, 2015. doi:10.1017/jfm.2015.383
― 粉粒体の MPM を工学的に検証した論文（固体的・流体的・離散的な相の遷移）。

## 4. 連続体力学・塑性論

**[14]** D. C. Drucker, W. Prager. Soil mechanics and plastic analysis or limit design. *Quarterly of Applied Mathematics*, 10(2):157–165, 1952. doi:10.1090/qam/48291
― Drucker–Prager 降伏条件の原典。

**[15]** J. Bonet, R. D. Wood. *Nonlinear Continuum Mechanics for Finite Element Analysis*, 2nd ed. Cambridge University Press, 2008.
― 変形勾配、応力の種類（Cauchy / Kirchhoff / Piola–Kirchhoff）、超弾性の標準的な教科書。

**[16]** J. C. Simo, T. J. R. Hughes. *Computational Inelasticity*. Springer, 1998.
― 弾塑性の乗算分解と return mapping アルゴリズムの標準的な教科書。

**[17]** E. A. de Souza Neto, D. Perić, D. R. J. Owen. *Computational Methods for Plasticity: Theory and Applications*. Wiley, 2008.
― Drucker–Prager を含む降伏条件の return mapping の詳細。

## 5. 数値計算・幾何処理

**[20]** A. McAdams, A. Selle, R. Tamstorf, J. Teran, E. Sifakis. Computing the singular value decomposition of 3×3 matrices with minimal branching and elementary floating point operations. Technical Report TR1690, Department of Computer Sciences, University of Wisconsin–Madison, 2011. https://research.cs.wisc.edu/techreports/2011/TR1690.pdf
― GPU 向けの 3×3 SVD。Jacobi 法の回転の作り方と、反転（det F < 0）の扱い。本プログラムの SVD を修正するときの参考になる。

**[21]** J. A. Bærentzen, H. Aanæs. Signed distance computation using the angle weighted pseudonormal. *IEEE Transactions on Visualization and Computer Graphics*, 11(3):243–253, 2005. doi:10.1109/TVCG.2005.49 (DOI 確認済み)
― メッシュからの SDF の符号判定を頑健にする方法。

**[22]** C. Ericson. *Real-Time Collision Detection*. Morgan Kaufmann, 2004.
― 点と三角形の最近接点（`closestPointOnTriangle` のアルゴリズム、5.1.5 節）。

**[23]** S. Osher, R. Fedkiw. *Level Set Methods and Dynamic Implicit Surfaces*. Springer, 2003.
― 符号付き距離関数の性質（|∇d| = 1 など）。

**[24]** A. Jacobson, L. Kavan, O. Sorkine-Hornung. Robust inside-outside segmentation using generalized winding numbers. *ACM Transactions on Graphics (Proc. SIGGRAPH)*, 32(4), Article 33, 2013. doi:10.1145/2461912.2461916
― 穴のあるメッシュなどでも内外を頑健に判定する方法。

## 6. GPU 実装

**[25]** ★ M. Gao, X. Wang, K. Wu, A. Pradhana, E. Sifakis, C. Yuksel, C. Jiang. GPU optimization of material point methods. *ACM Transactions on Graphics (Proc. SIGGRAPH Asia)*, 37(6), Article 254, 2018. doi:10.1145/3272127.3275044 (DOI 確認済み)
― GPU での MPM の最適化（粒子のソート、ワープ単位での集約、スパース格子）。本プログラムの性能の比較対象・関連研究として重要。

**[26]** Y. Hu, T.-M. Li, L. Anderson, J. Ragan-Kelley, F. Durand. Taichi: a language for high-performance computation on spatially sparse data structures. *ACM Transactions on Graphics (Proc. SIGGRAPH Asia)*, 38(6), Article 201, 2019. doi:10.1145/3355089.3356506

**[27]** Taichi 公式サンプル `mpm88.py`（88 行の MLS-MPM）および `taichi_elements`（砂の Drucker–Prager を含む MPM）. https://github.com/taichi-dev/taichi , https://github.com/taichi-dev/taichi_elements
― 本プログラムの P2G / G2P と構造がほぼ同じ参照実装。式を 1 行ずつ比較すると、A-2（C の係数）の違いがわかる。ソフトウェアなので、引用するときはバージョンとアクセス日を書く。

**[28]** X. Wang, Y. Qiu, S. R. Slattery, Y. Fang, M. Li, S.-C. Zhu, Y. Zhu, M. Tang, D. Manocha, C. Jiang. A massively parallel and scalable multi-GPU material point method. *ACM Transactions on Graphics (Proc. SIGGRAPH)*, 39(4), Article 30, 2020. doi:10.1145/3386569.3392442

**[29]** The Khronos Group. *The OpenGL Graphics System: A Specification (Version 4.3, Core Profile)*, 2012；*The OpenGL Shading Language, Version 4.30*, 2012. https://registry.khronos.org/OpenGL/
― コンピュートシェーダー、image の atomic 演算、`std140` / `std430` のメモリ配置、`glMemoryBarrier` の仕様。

## 7. 補助的な教科書

**[30]** R. Bridson. *Fluid Simulation for Computer Graphics*, 2nd ed. CRC Press, 2015.
― 格子と粒子の hybrid 法（PIC / FLIP）、CFL 条件、境界条件の考え方。

**[31]** J. Fern, A. Rohe, K. Soga, E. Alonso (eds.). *The Material Point Method for Geotechnical Engineering: A Practical Guide*. CRC Press, 2019.
― 地盤工学（土砂崩れ・斜面崩壊）での MPM の応用。研究の応用面（土砂の大規模変形の再現）の位置づけに使える。

**[32]** X. Zhang, Z. Chen, Y. Liu. *The Material Point Method: A Continuum-Based Particle Method for Extreme Loading Cases*. Academic Press, 2016.
― MPM の教科書（工学分野）。

---

## 読む順番の提案

1. **[11]**（SIGGRAPH Course）の第 1〜3 章と MPM のアルゴリズムの章で、全体像をつかむ。
2. **[9]**（MLS-MPM）の 4 章と、**[27]** の mpm88 を、本プログラムの `mpm_p2g.comp` / `mpm_g2p.comp` と並べて読む。
3. **[7]**（Klár ら）の 4〜7 章を、`mpm_g2p.comp` の 4.〜5. と対応させて読む。体積補正は **[8]** の該当節。
4. **[6]**（APIC）で、C_p と D_p の意味を確認する。
5. 関連研究・性能比較のために **[25]** と **[18]**。
