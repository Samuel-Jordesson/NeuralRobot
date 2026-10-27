// Iluminação compartilhada: PBR (GGX), sombras do sol em cascata (CSM) com PCF,
// luz do céu por IBL (irradiância + reflexo pré-filtrado), SSAO e sombra das nuvens.
uniform vec3 uSunDir;      // direção PARA o sol
uniform vec3 uSunColor;    // iluminância do sol já atenuada pela atmosfera
uniform vec3 uCamPos;
uniform vec3 uCamFwd;
uniform float uTime;

uniform sampler2DArrayShadow uShadowMap;
uniform mat4 uCascadeVP[4];
uniform vec4 uCascadeSplits;  // distância final de cada cascata
uniform vec4 uCascadeTexel;   // tamanho de um texel da cascata no mundo (m)
uniform int uCascadeCount;
uniform float uShadowSoftness;
uniform float uShadowDistance;

uniform samplerCube uIrradiance; // radiância média cosseno-ponderada (E/pi)
uniform samplerCube uEnvMap;     // céu pré-filtrado por rugosidade nos mips
uniform float uEnvMaxLod;

uniform sampler2D uAOTex;
uniform int uUseSSAO;
uniform vec2 uRenderSize;

uniform vec3 uCharPos;     // sombra de contato (só sem SSAO)
uniform vec3 uFlashPos;    // luz pontual do clarão do tiro
uniform vec3 uFlashColor;  // zero quando não há tiro

uniform sampler2D uNoise; // R fbm grande, G médio, B fino, A fbm (repete sem emenda)

uniform int uClouds;
uniform float uCloudCoverage;
uniform int uCloudShadows;

const float PI = 3.14159265;

// ---------------------------------------------------------------- ruído
float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
// ruído por pixel estável (interleaved gradient noise)
float ign(vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }

// ---------------------------------------------------------------- nuvens (camada a 1.8 km)
const float kCloudHeight = 1800.0;
const vec2 kWind = vec2(0.012, 0.005); // km/s

float cloudDensity(vec2 pKm, float lod) {
    vec2 p = pKm * 0.45 + kWind * uTime;
    float n = textureLod(uNoise, p * 0.25, lod).r * 0.72 + textureLod(uNoise, p * 0.93 + 0.37, lod).a * 0.28;
    float th = 1.0 - uCloudCoverage;
    return smoothstep(th - 0.1, th + 0.22, n);
}

// Sombra das nuvens no chão: projeta o ponto até a camada de nuvens na direção do sol.
float cloudShadow(vec3 P) {
    if (uClouds == 0 || uCloudShadows == 0 || uSunDir.y < 0.02) return 1.0;
    vec2 xz = P.xz + uSunDir.xz / uSunDir.y * (kCloudHeight - P.y);
    return 1.0 - 0.75 * cloudDensity(xz / 1000.0, 1.0);
}

// ---------------------------------------------------------------- sombras
const vec2 kPoisson[16] = vec2[](
    vec2(-0.94201624, -0.39906216), vec2(0.94558609, -0.76890725), vec2(-0.09418410, -0.92938870),
    vec2(0.34495938, 0.29387760), vec2(-0.91588581, 0.45771432), vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543, 0.27676845), vec2(0.97484398, 0.75648379), vec2(0.44323325, -0.97511554),
    vec2(0.53742981, -0.47373420), vec2(-0.26496911, -0.41893023), vec2(0.79197514, 0.19090188),
    vec2(-0.24188840, 0.99706507), vec2(-0.81409955, 0.91437590), vec2(0.19984126, 0.78641367),
    vec2(0.14383161, -0.14100790));

int cascadeIndex(vec3 P, bool dither) {
    float d = dot(P - uCamPos, uCamFwd);
    int i = 0;
    while (i < uCascadeCount - 1 && d > uCascadeSplits[i]) ++i;
    if (dither && i < uCascadeCount - 1) {
        // transição pontilhada para a próxima cascata nos últimos 15% (sem costura visível)
        float start = i == 0 ? 0.0 : uCascadeSplits[i - 1];
        float f = (d - start) / (uCascadeSplits[i] - start);
        if (f > 0.85 && ign(gl_FragCoord.xy) < (f - 0.85) / 0.15) ++i;
    }
    return i;
}

float shadowFactor(vec3 P, vec3 N) {
    float d = dot(P - uCamPos, uCamFwd);
    if (d > uShadowDistance) return 1.0;
    int i = cascadeIndex(P, true);
    float ndl = clamp(dot(N, uSunDir), 0.0, 1.0);
    float texelW = uCascadeTexel[i];
    vec3 p = P + N * texelW * (1.5 + 2.5 * (1.0 - ndl));
    vec3 c = (uCascadeVP[i] * vec4(p, 1.0)).xyz * 0.5 + 0.5;
    if (c.z > 1.0) return 1.0;
    float texel = 1.0 / float(textureSize(uShadowMap, 0).x);
    // penumbra com tamanho constante no mundo (~4 cm), girada por pixel
    float radius = clamp(uShadowSoftness * 0.035 / texelW, 1.0, 7.0) * texel;
    float a = ign(gl_FragCoord.xy) * 6.2831853;
    mat2 R = mat2(cos(a), sin(a), -sin(a), cos(a));
    float sum = 0.0;
    for (int k = 0; k < 16; ++k)
        sum += texture(uShadowMap, vec4(c.xy + R * kPoisson[k] * radius, float(i), c.z - 0.00015));
    float s = sum / 16.0;
    return mix(s, 1.0, smoothstep(uShadowDistance * 0.85, uShadowDistance, d));
}

// Uma amostra só (usada na luz volumétrica).
float shadowFast(vec3 P) {
    float d = dot(P - uCamPos, uCamFwd);
    if (d > uShadowDistance) return 1.0;
    int i = cascadeIndex(P, false);
    vec3 c = (uCascadeVP[i] * vec4(P, 1.0)).xyz * 0.5 + 0.5;
    if (c.z > 1.0 || any(lessThan(c.xy, vec2(0.0))) || any(greaterThan(c.xy, vec2(1.0)))) return 1.0;
    return texture(uShadowMap, vec4(c.xy, float(i), c.z - 0.0003));
}

// Sombra de contato suave sob o personagem (só quando o SSAO está desligado).
float contactShadow(vec3 P, vec3 N) {
    if (uUseSSAO == 1 || N.y < 0.5) return 1.0;
    float dy = uCharPos.y - P.y;
    if (dy < -0.05 || dy > 2.0) return 1.0;
    vec2 d = P.xz - uCharPos.xz;
    float r = 0.38 + dy * 0.3;
    float k = exp(-dot(d, d) / (r * r)) * clamp(1.0 - dy / 2.0, 0.0, 1.0);
    return 1.0 - 0.55 * k;
}

// ---------------------------------------------------------------- PBR
// Aproximação analítica do BRDF integrado (Karis, "Physically Based Shading on Mobile")
vec2 envBRDFApprox(float rough, float NdV) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = rough * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NdV)) * r.x + r.y;
    return vec2(-1.04, 1.04) * a004 + r.zw;
}

vec3 shadePBR(vec3 albedo, float metal, float rough, float ao, vec3 N, vec3 P) {
    vec3 V = normalize(uCamPos - P);
    vec3 L = uSunDir;
    vec3 H = normalize(V + L);
    float NdL = max(dot(N, L), 0.0);
    float NdV = max(dot(N, V), 1e-4);
    float NdH = max(dot(N, H), 0.0);
    float VdH = max(dot(V, H), 0.0);

    vec3 F0 = mix(vec3(0.04), albedo, metal);
    float a = rough * rough;
    float a2 = a * a;
    float dd = NdH * NdH * (a2 - 1.0) + 1.0;
    float D = a2 / (PI * dd * dd);
    float k = (rough + 1.0) * (rough + 1.0) / 8.0;
    float G = (NdV / (NdV * (1.0 - k) + k)) * (NdL / (NdL * (1.0 - k) + k));
    vec3 F = F0 + (1.0 - F0) * pow(1.0 - VdH, 5.0);
    vec3 spec = D * G * F / max(4.0 * NdV * NdL, 1e-4);
    vec3 kd = (1.0 - F) * (1.0 - metal);

    float sh = NdL > 0.0 ? shadowFactor(P, N) * cloudShadow(P) : 0.0;
    vec3 direct = (kd * albedo / PI + spec) * uSunColor * NdL * sh;

    // clarão do tiro (difusa, queda 1/d²)
    vec3 Lf = uFlashPos - P;
    float d2 = dot(Lf, Lf);
    direct += (1.0 - metal) * albedo / PI * uFlashColor * max(dot(N, Lf * inversesqrt(d2)), 0.0) / (d2 + 0.04);

    // luz do céu (IBL)
    float ssao = uUseSSAO == 1 ? texture(uAOTex, gl_FragCoord.xy / uRenderSize).r : 1.0;
    float occl = ao * ssao * contactShadow(P, N);
    vec3 irr = texture(uIrradiance, N).rgb;
    vec3 R = reflect(-V, N);
    vec3 pref = textureLod(uEnvMap, R, rough * uEnvMaxLod).rgb;
    vec2 ab = envBRDFApprox(rough, NdV);
    vec3 Fa = F0 * ab.x + ab.y;
    float specOcc = clamp(pow(NdV + occl, exp2(-16.0 * rough - 1.0)) - 1.0 + occl, 0.0, 1.0);
    vec3 ambient = (1.0 - Fa) * (1.0 - metal) * albedo * irr * occl + pref * Fa * specOcc;
    return direct + ambient;
}
