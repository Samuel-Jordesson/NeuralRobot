#version 330 core
// Aplica a névoa na cena: usa a luz volumétrica (meia resolução, reamostrada respeitando bordas)
// ou a névoa analítica quando ela está desligada.
#include "common.glsl"
#include "fog.glsl"

in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uScene;
uniform sampler2D uDepth;
uniform sampler2D uVol;
uniform int uUseVol;
uniform mat4 uInvViewProj;
uniform mat4 uProj;
uniform float uSkyFogDist;

float viewZ(float d) { return -uProj[3][2] / ((d * 2.0 - 1.0) + uProj[2][2]); }

void main() {
    vec3 scene = texture(uScene, vUV).rgb;
    float z = texture(uDepth, vUV).r;
    vec4 fog;
    if (uUseVol == 1) {
        // 4 texels de meia resolução mais próximos, ponderados pela semelhança de profundidade
        vec2 lowSize = vec2(textureSize(uVol, 0));
        vec2 fp = vUV * lowSize - 0.5;
        ivec2 base = ivec2(floor(fp));
        vec2 f = fract(fp);
        float zc = viewZ(z);
        vec4 sum = vec4(0.0);
        float wsum = 0.0;
        for (int j = 0; j < 2; ++j)
            for (int i = 0; i < 2; ++i) {
                ivec2 c = clamp(base + ivec2(i, j), ivec2(0), ivec2(lowSize) - 1);
                vec2 uvc = (vec2(c) + 0.5) / lowSize;
                float zs = viewZ(texture(uDepth, uvc).r);
                float bw = (i == 0 ? 1.0 - f.x : f.x) * (j == 0 ? 1.0 - f.y : f.y);
                float w = bw / (1e-3 + abs(zs - zc) / max(abs(zc), 0.1)) + 1e-5;
                sum += texelFetch(uVol, c, 0) * w;
                wsum += w;
            }
        fog = sum / wsum;
    } else {
        vec4 w = uInvViewProj * vec4(vUV * 2.0 - 1.0, z * 2.0 - 1.0, 1.0);
        vec3 P = w.xyz / w.w;
        vec3 rd = normalize(P - uCamPos);
        float dist = z >= 1.0 ? uSkyFogDist : length(P - uCamPos);
        float od = fogOptical(uCamPos, uCamPos + rd * dist);
        float T = exp(-od);
        vec3 amb = texture(uIrradiance, vec3(0.0, 1.0, 0.0)).rgb * 0.7;
        // sem sombras na névoa: o sol entra mais fraco para não clarear lugares à sombra
        fog = vec4((uSunColor * fogPhase(dot(rd, uSunDir)) * 0.6 + amb) * (1.0 - T), T);
    }
    FragColor = vec4(scene * fog.a + fog.rgb, 1.0);
}
