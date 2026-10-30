#version 330 core
#include "common.glsl"

in vec3 vPos;
in vec3 vNormal;
in float vT;
in vec3 vTint;
out vec4 FragColor;

void main() {
    vec3 V = normalize(uCamPos - vPos);
    vec3 N = normalize(vNormal);
    if (dot(N, V) < 0.0) N = -N; // folha dos dois lados
    N = normalize(mix(N, vec3(0.0, 1.0, 0.0), 0.45)); // luz mais macia (como um tufo)
    // raiz escura e úmida, ponta mais clara e amarelada
    vec3 albedo = mix(vec3(0.025, 0.06, 0.012), vec3(0.11, 0.24, 0.035), smoothstep(0.0, 1.0, vT)) * vTint;
    float ao = mix(0.35, 1.0, vT);
    vec3 col = shadePBR(albedo, 0.0, 0.55, ao, N, vPos);
    // luz atravessando a folha contra o sol
    float back = pow(max(dot(-V, uSunDir), 0.0), 3.0) * shadowFactor(vPos, vec3(0.0, 1.0, 0.0));
    col += albedo * uSunColor * back * 0.6 * vT;
    FragColor = vec4(col, 1.0);
}
