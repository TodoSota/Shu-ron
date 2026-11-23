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

// Constitutive logarithms retain the historical minimum stretch. The SVD
// itself uses the measured lengths so clamping cannot de-normalize U.
const float minMaterialStretch = 0.01;

void svd(mat3 F, out mat3 U, out mat3 Sigma, out mat3 V) {
    mat3 S = transpose(F) * F;
    jacobiEigen(S, V);

    mat3 FV = F * V;
    Sigma = mat3(0.0);
    for (int axis = 0; axis < 3; ++axis) {
        float stretch = length(FV[axis]);
        Sigma[axis][axis] = stretch;
        U[axis] = FV[axis] / max(stretch, 1e-20);
    }
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
