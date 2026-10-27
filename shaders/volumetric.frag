#version 330 core
// Luz volumétrica: marcha pelo raio da câmera acumulando luz do sol espalhada na névoa,
// testando o mapa de sombra em cada passo (raios de sol entre as sombras). Meia resolução.
#include "common.glsl"
#include "fog.glsl"

in vec2 vUV;
out vec4 FragColor;  // rgb = luz espalhada, a = transmitância
uniform sampler2D uDepth;
uniform mat4 uInvViewProj;
uniform int uSteps;
uniform float uVolIntensity;
uniform float uMarchDist;
uniform float uSkyFogDist;

void main() {
    float z = texture(uDepth, vUV).r;
    vec4 w = uInvViewProj * vec4(vUV * 2.0 - 1.0, z * 2.0 - 1.0, 1.0);
    vec3 P = w.xyz / w.w;
    vec3 rd = normalize(P - uCamPos);
    float dist = z >= 1.0 ? uSkyFogDist : length(P - uCamPos);
    vec3 end = uCamPos + rd * dist;

    float mu = dot(rd, uSunDir);
    vec3 sunScatter = uSunColor * fogPhase(mu) * uVolIntensity;
    vec3 amb = texture(uIrradiance, vec3(0.0, 1.0, 0.0)).rgb * 0.7;

    float march = min(dist, uMarchDist);
    float dt = march / float(uSteps);
    float t = dt * ign(gl_FragCoord.xy);
    float T = 1.0;
    vec3 L = vec3(0.0);
    for (int i = 0; i < uSteps; ++i) {
        vec3 x = uCamPos + rd * t;
        float s = fogDensityAt(x.y) * dt;
        float vis = shadowFast(x) * cloudShadow(x);
        L += T * (sunScatter * vis + amb) * (1.0 - exp(-s));
        T *= exp(-s);
        t += dt;
    }
    // resto do caminho sem sombras
    if (dist > march) {
        float od = fogOptical(uCamPos + rd * march, end);
        L += T * (sunScatter * cloudShadow(end) + amb) * (1.0 - exp(-od));
        T *= exp(-od);
    }
    FragColor = vec4(L, T);
}
