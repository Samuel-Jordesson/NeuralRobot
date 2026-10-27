#version 330 core
#include "common.glsl"

in vec3 vPos;
in vec3 vNormal;
in vec4 vTangent;
in vec2 vUV;
in vec3 vDebug;
out vec4 FragColor;

uniform sampler2D uBaseTex;
uniform sampler2D uMRTex;
uniform sampler2D uNormalTex;
uniform sampler2D uEmissiveTex;
uniform vec3 uEmissive;
uniform int uUseAO;
uniform float uMetallic;
uniform float uRoughness;
uniform int uDebug;

void main() {
    vec3 N = normalize(vNormal);
    if (!gl_FrontFacing) N = -N; // material double-sided
    vec3 T = normalize(vTangent.xyz - N * dot(N, vTangent.xyz));
    vec3 B = cross(N, T) * vTangent.w;
    vec3 tn = texture(uNormalTex, vUV).xyz * 2.0 - 1.0;
    N = normalize(T * tn.x + B * tn.y + N * tn.z);

    vec3 albedo = texture(uBaseTex, vUV).rgb;
    vec3 mr = texture(uMRTex, vUV).rgb;
    float rough = clamp(mr.g * uRoughness, 0.05, 1.0);
    float metal = clamp(mr.b * uMetallic, 0.0, 1.0);
    float ao = uUseAO == 1 ? mr.r : 1.0;
    vec3 emissive = texture(uEmissiveTex, vUV).rgb * uEmissive;
    if (uDebug == 1) { albedo = vDebug; metal = 0.0; rough = 0.8; emissive = vec3(0.0); }

    vec3 col = shadePBR(albedo, metal, rough, ao, N, vPos) + emissive;
    FragColor = vec4(col, 1.0);
}
