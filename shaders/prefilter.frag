#version 330 core
// Reflexo do céu pré-filtrado (GGX) para cada nível de rugosidade.
in vec2 vUV;
out vec4 FragColor;
uniform mat4 uInvViewProj;
uniform samplerCube uSrc;
uniform float uRoughness;
uniform float uSrcRes;

const float PI = 3.14159265;

float radicalInverse(uint b) {
    b = (b << 16u) | (b >> 16u);
    b = ((b & 0x55555555u) << 1u) | ((b & 0xAAAAAAAAu) >> 1u);
    b = ((b & 0x33333333u) << 2u) | ((b & 0xCCCCCCCCu) >> 2u);
    b = ((b & 0x0F0F0F0Fu) << 4u) | ((b & 0xF0F0F0F0u) >> 4u);
    b = ((b & 0x00FF00FFu) << 8u) | ((b & 0xFF00FF00u) >> 8u);
    return float(b) * 2.3283064365386963e-10;
}

void main() {
    vec4 w = uInvViewProj * vec4(vUV * 2.0 - 1.0, 1.0, 1.0);
    vec3 N = normalize(w.xyz / w.w);
    if (uRoughness < 0.01) { FragColor = vec4(textureLod(uSrc, N, 0.0).rgb, 1.0); return; }
    vec3 up = abs(N.y) < 0.999 ? vec3(0, 1, 0) : vec3(1, 0, 0);
    vec3 T = normalize(cross(up, N)), B = cross(N, T);
    float a = uRoughness * uRoughness;
    const uint COUNT = 48u;
    vec3 sum = vec3(0.0);
    float wsum = 0.0;
    for (uint i = 0u; i < COUNT; ++i) {
        vec2 xi = vec2(float(i) / float(COUNT), radicalInverse(i));
        float phi = 2.0 * PI * xi.x;
        float cosT = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
        float sinT = sqrt(1.0 - cosT * cosT);
        vec3 H = T * (cos(phi) * sinT) + B * (sin(phi) * sinT) + N * cosT;
        vec3 L = 2.0 * dot(N, H) * H - N;
        float NdL = dot(N, L);
        if (NdL <= 0.0) continue;
        // amostragem filtrada: lê de um mip proporcional ao ângulo sólido da amostra
        float NdH = cosT;
        float d = NdH * NdH * (a * a - 1.0) + 1.0;
        float D = a * a / (PI * d * d);
        float pdf = D / 4.0 + 1e-4;
        float saSample = 1.0 / (float(COUNT) * pdf);
        float saTexel = 4.0 * PI / (6.0 * uSrcRes * uSrcRes);
        float lod = max(0.5 * log2(saSample / saTexel) + 1.0, 0.0);
        sum += textureLod(uSrc, L, lod).rgb * NdL;
        wsum += NdL;
    }
    FragColor = vec4(sum / max(wsum, 1e-4), 1.0);
}
