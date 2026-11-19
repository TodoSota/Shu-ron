void jacobiEigen(inout mat3 S, out mat3 V) {
    V = mat3(1.0);

	// 工程3の通常接触入力で3巡の収束不足を確認し8巡を採用。
	// 各ペアは収束済みなら回転を省略する。最小反復数・最適性能を確定した値ではない。
    for (int iter = 0; iter < 8; ++iter) {
        for (int p = 0; p < 2; ++p)
        for (int q = p + 1; q < 3; ++q) {

            float app = S[p][p];
            float aqq = S[q][q];
            float apq = S[p][q];

            // すでにほぼ対角なら回転不要
            if (abs(apq) < 1e-6) continue;

            // (p,q) 平面の回転角
            float phi = 0.5 * atan(-2.0 * apq, aqq - app);
            float c = cos(phi);
            float s = sin(phi);

            // 回転行列 J を作り、S ← JᵀSJ、V ← VJ と累積する
            mat3 J = mat3(1.0);
            J[p][p] =  c; J[p][q] = s;
            J[q][p] = -s; J[q][q] = c;

            S = transpose(J) * S * J;
            V = V * J;
        }
    }
}

//------------------------------------------------------------------------------
// 3×3 行列の特異値分解 F = U Σ Vᵀ
//   目的: 変形勾配 F を「回転 U・主ストレッチ Σ・回転 V」に分け、
//         主ひずみ空間 (対角成分だけ) で構成則を評価できるようにする。
//   方法: FᵀF = V Σ² Vᵀ を Jacobi 法で求め、Σ = sqrt(固有値)、U = F V Σ⁻¹ とする。
//   注意: ・Σ は sqrt(max(λ, 1e-4)) により 0.01 以上に下限が設けられている
//           (極端な圧縮で Σ⁻¹ が発散するのを防ぐためのクランプであり、
//            「微小値を足す」処理ではない)。
//         ・det F < 0 (要素の反転) のときに U が反射を含むことへの対処はない。
//         ・特異値の大小の並べ替えは行わない (等方な構成則なので順序は不要)。
//------------------------------------------------------------------------------
// 特異値分解
void svd(mat3 F, out mat3 U, out mat3 Sigma, out mat3 V) {
    mat3 S = transpose(F) * F;

    jacobiEigen(S, V);

    Sigma = mat3(0.0);
	// ゼロ除算の恐れがあるので、微小な値を足す(エプシロン)
    Sigma[0][0] = sqrt(max(S[0][0], 1e-4));
    Sigma[1][1] = sqrt(max(S[1][1], 1e-4));
    Sigma[2][2] = sqrt(max(S[2][2], 1e-4));

    mat3 invSigma = mat3(0.0);

	invSigma[0][0] = 1.0 / Sigma[0][0];
	invSigma[1][1] = 1.0 / Sigma[1][1];
	invSigma[2][2] = 1.0 / Sigma[2][2];

    U = F * V * invSigma;
}


// C-9: enabled by the simulator host. Diagnostic-only tests may leave it disabled.
uniform bool validation_enabled = false;
layout(std430, binding = 6) buffer DeformationFault {
    uint fault_claim, fault_particle, fault_reason, fault_stage;
    mat4 fault_trial;
    vec4 fault_metrics;
};
bool invalidDeformation(mat3 F, mat3 U, mat3 sig, mat3 V, uint idx, uint stage) {
    if (!validation_enabled) return false;
    bool finite = true;
    for (int col=0; col<3; ++col) {
        finite = finite && !any(isnan(F[col])) && !any(isinf(F[col]));
        finite = finite && !any(isnan(U[col])) && !any(isinf(U[col]));
        finite = finite && !any(isnan(V[col])) && !any(isinf(V[col]));
    }
    mat3 du=transpose(U)*U-mat3(1.0), dv=transpose(V)*V-mat3(1.0);
    float eu=0.0, ev=0.0;
    for (int col=0; col<3; ++col) { eu+=dot(du[col],du[col]); ev+=dot(dv[col],dv[col]); }
    float error=sqrt(max(eu,ev)/3.0);
    float detF=determinant(F);
    uint reason=!finite || isnan(detF) || isinf(detF) || isnan(error) || isinf(error) ? 1u : (detF<=0.0 ? 2u : (error>5e-4 ? 3u : 0u));
    if (reason==0u) return false;
    if (atomicCompSwap(fault_claim,0u,1u)==0u) {
        fault_particle=idx; fault_reason=reason; fault_stage=stage;
        fault_trial=mat4(F);
        fault_metrics=vec4(detF,error,min(sig[0][0],min(sig[1][1],sig[2][2])),0.0);
    }
    return true;
}
