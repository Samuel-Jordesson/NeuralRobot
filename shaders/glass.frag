#version 330 core
#include "common.glsl"
// Vidro (portas): tinge o que está atrás cor por cor (blend com duas saídas: reflexo + transmissão).
// Cor clara = quase transparente; escura = fumê. De lado reflete mais (Fresnel).
in vec3 vPos;
in vec3 vNormal;
in vec3 vLocal;
layout(location = 0, index = 0) out vec4 FragColor; // reflexo (somado)
layout(location = 0, index = 1) out vec4 Transmit;  // o que passa do fundo (multiplica)
uniform vec3 uColor;

void main() {
    vec3 N = normalize(vNormal);
    if (!gl_FrontFacing) N = -N;
    vec3 V = normalize(uCamPos - vPos);
    float fres = 0.04 + 0.96 * pow(1.0 - max(dot(N, V), 0.0), 5.0);
    vec3 refl = shadePBR(vec3(0.0), 0.0, 0.04, 1.0, N, vPos); // só o brilho/reflexo
    FragColor = vec4(refl, 1.0);
    Transmit = vec4(uColor * (1.0 - fres), 1.0);
}
