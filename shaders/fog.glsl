// Névoa de altura exponencial (mais densa perto do chão).
uniform float uFogDensity;   // densidade no chão (1/m)
uniform float uFogFalloff;   // queda com a altura (1/m)

float fogDensityAt(float h) { return uFogDensity * exp(-uFogFalloff * max(h, 0.0)); }

// Profundidade ótica analítica entre dois pontos.
float fogOptical(vec3 a, vec3 b) {
    float L = length(b - a);
    float ya = max(a.y, 0.0), yb = max(b.y, 0.0);
    float dy = yb - ya;
    float ea = exp(-uFogFalloff * ya);
    if (abs(dy) < 1e-3) return uFogDensity * L * ea;
    return uFogDensity * L * (ea - exp(-uFogFalloff * yb)) / (uFogFalloff * dy);
}

float phaseHG(float mu, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (12.566371 * pow(1.0 + g2 - 2.0 * g * mu, 1.5));
}
// espalhamento da névoa: pico forte para frente (brilho em volta do sol) + parte difusa
float fogPhase(float mu) { return mix(phaseHG(mu, 0.72), 0.0795775, 0.3); }
