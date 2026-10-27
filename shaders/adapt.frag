#version 330 core
// Adaptação do olho: aproxima a luminância média aos poucos (escuro -> claro mais rápido).
in vec2 vUV;
out float FragLum;
uniform sampler2D uLum;
uniform sampler2D uPrev;
uniform float uMaxLod;
uniform float uDt;
uniform int uReset;
void main() {
    vec2 m = textureLod(uLum, vec2(0.5), uMaxLod).rg;
    float cur = m.x / max(m.y, 1e-6);
    float prev = texture(uPrev, vec2(0.5)).r;
    float rate = cur > prev ? 2.2 : 1.4;
    FragLum = uReset == 1 ? cur : prev + (cur - prev) * (1.0 - exp(-uDt * rate));
}
