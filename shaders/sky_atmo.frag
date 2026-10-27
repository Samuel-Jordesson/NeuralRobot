#version 330 core
// Atmosfera pura (sem nuvens nem sol), renderizada numa cubemap quando o sol muda.
#include "atmosphere.glsl"

in vec2 vUV;
out vec4 FragColor;
uniform mat4 uInvViewProj;
uniform vec3 uSunDir;
uniform float uSkyIntensity;

void main() {
    vec4 w = uInvViewProj * vec4(vUV * 2.0 - 1.0, 1.0, 1.0);
    vec3 dir = normalize(w.xyz / w.w);
    dir = normalize(vec3(dir.x, max(dir.y, 0.0), dir.z)); // abaixo do horizonte: cor do horizonte
    FragColor = vec4(atmosphere(dir, uSunDir, uSkyIntensity), 1.0);
}
