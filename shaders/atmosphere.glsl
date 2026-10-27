// Céu físico: espalhamento simples de Rayleigh (azul do céu) + Mie (brilho em volta do sol, neblina).
// Unidades em metros; observador 2 m acima do chão.
const float kPlanetR = 6371e3;
const float kAtmoR = 6471e3;
const vec3 kRayleigh = vec3(5.5e-6, 13.0e-6, 22.4e-6);
const float kMieCoef = 21e-6;
const float kRayleighH = 8e3;
const float kMieH = 1.2e3;
const float kMieG = 0.76;
// Ozônio só absorve (deixa o céu do crepúsculo azul em vez de amarelado). Usa a densidade do
// Rayleigh como aproximação da camada (coluna ~15 km equivalente).
const vec3 kOzone = vec3(0.650, 1.881, 0.085) * 1e-6 * 1.9;

vec2 raySphere(vec3 ro, vec3 rd, float r) {
    float b = dot(ro, rd), c = dot(ro, ro) - r * r;
    float d = b * b - c;
    if (d < 0.0) return vec2(1e9, -1e9);
    d = sqrt(d);
    return vec2(-b - d, -b + d);
}

// Radiância do céu na direção rd (sem o disco do sol).
vec3 atmosphere(vec3 rd, vec3 sunDir, float sunIntensity) {
    const float PI_ = 3.14159265;
    vec3 ro = vec3(0.0, kPlanetR + 2.0, 0.0);
    float tMax = raySphere(ro, rd, kAtmoR).y;
    vec2 g = raySphere(ro, rd, kPlanetR);
    if (g.x > 0.0) tMax = min(tMax, g.x);

    const int N = 16, M = 8;
    float ds = tMax / float(N);
    float mu = dot(rd, sunDir);
    float pR = 3.0 / (16.0 * PI_) * (1.0 + mu * mu);
    float gg = kMieG * kMieG;
    float pM = 3.0 / (8.0 * PI_) * ((1.0 - gg) * (1.0 + mu * mu)) / ((2.0 + gg) * pow(1.0 + gg - 2.0 * kMieG * mu, 1.5));

    float odR = 0.0, odM = 0.0;
    vec3 sumR = vec3(0.0), sumM = vec3(0.0);
    for (int i = 0; i < N; ++i) {
        vec3 x = ro + rd * (ds * (float(i) + 0.5));
        float h = length(x) - kPlanetR;
        float hr = exp(-h / kRayleighH) * ds, hm = exp(-h / kMieH) * ds;
        odR += hr;
        odM += hm;
        // caminho até o sol (sombra do planeta no pôr do sol)
        if (raySphere(x, sunDir, kPlanetR).x > 0.0) continue;
        float tl = raySphere(x, sunDir, kAtmoR).y;
        float dl = tl / float(M);
        float lR = 0.0, lM = 0.0;
        for (int j = 0; j < M; ++j) {
            vec3 xl = x + sunDir * (dl * (float(j) + 0.5));
            float hl = max(length(xl) - kPlanetR, 0.0);
            lR += exp(-hl / kRayleighH) * dl;
            lM += exp(-hl / kMieH) * dl;
        }
        vec3 att = exp(-((kRayleigh + kOzone) * (odR + lR) + kMieCoef * 1.1 * (odM + lM)));
        sumR += att * hr;
        sumM += att * hm;
    }
    return sunIntensity * (pR * kRayleigh * sumR + pM * kMieCoef * sumM);
}
