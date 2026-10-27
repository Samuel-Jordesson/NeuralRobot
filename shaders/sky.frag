#version 330 core
// Céu da tela (e captura do ambiente): atmosfera + nuvens + chão distante + disco do sol.
#include "common.glsl"

in vec2 vUV;
out vec4 FragColor;
uniform mat4 uInvViewProj;
uniform samplerCube uSkyCube;
uniform int uCapture;       // 1 = captura para IBL (sem disco do sol, câmera na origem)
uniform float uSunDisk;     // brilho do disco do sol
uniform vec3 uCloudSunColor; // luz do sol na altura das nuvens

float hg(float mu, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * PI * pow(1.0 + g2 - 2.0 * g * mu, 1.5));
}

void main() {
    vec4 w = uInvViewProj * vec4(vUV * 2.0 - 1.0, 1.0, 1.0);
    vec3 dir = normalize(w.xyz / w.w - (uCapture == 1 ? vec3(0.0) : uCamPos));
    vec3 atmo = texture(uSkyCube, vec3(dir.x, max(dir.y, 0.0), dir.z)).rgb;
    vec3 ambUp = textureLod(uSkyCube, vec3(0.0, 1.0, 0.0), 7.0).rgb;
    vec3 col = atmo;

    // chão distante (só aparece no reflexo/IBL e além do fim do mapa)
    if (dir.y < 0.0) {
        const vec3 albedo = vec3(0.3, 0.29, 0.27);
        vec3 ground = albedo / PI * uSunColor * max(uSunDir.y, 0.0) + albedo * ambUp;
        col = mix(atmo, ground, smoothstep(0.0, -0.06, dir.y));
    }

    float cloudA = 0.0;
    if (uClouds == 1 && dir.y > 0.0) {
        vec3 camP = uCapture == 1 ? vec3(0.0, 2.0, 0.0) : uCamPos;
        float t = (kCloudHeight - camP.y) / dir.y;
        vec2 pk = (camP.xz + dir.xz * t) / 1000.0;
        float lod = max(0.0, log2(t / 6000.0));
        float d = cloudDensity(pk, lod);
        if (d > 0.001) {
            // luz atravessando a nuvem em direção ao sol (2 amostras)
            vec2 toSun = length(uSunDir.xz) > 1e-3 ? normalize(uSunDir.xz) : vec2(0.0);
            float travel = clamp(0.35 / max(uSunDir.y, 0.08), 0.0, 3.0);
            float dl = cloudDensity(pk + toSun * travel * 0.3, lod) + cloudDensity(pk + toSun * travel, lod + 1.0);
            float light = exp(-dl * 1.4) * (1.0 - exp(-d * 3.0) * 0.5);
            float mu = dot(dir, uSunDir);
            float phase = 0.25 + 1.4 * hg(mu, 0.65);
            vec3 lit = uCloudSunColor * light * phase + ambUp * mix(1.15, 0.65, d);
            lit = mix(lit, atmo, 1.0 - exp(-t / 22000.0)); // perspectiva aérea
            cloudA = d * smoothstep(0.0, 0.1, dir.y);
            col = mix(col, lit, cloudA);
        }
    }

    if (uCapture == 0) {
        float mu = dot(dir, uSunDir);
        float disk = smoothstep(0.99996, 0.999985, mu);
        col += uSunColor * uSunDisk * disk * (1.0 - cloudA);
    }
    FragColor = vec4(col, 1.0);
}
