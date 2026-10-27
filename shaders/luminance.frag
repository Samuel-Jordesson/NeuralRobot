#version 330 core
// Log da luminância para a exposição automática. Sai (log*peso, peso): a média dos mips
// dá a média ponderada, com o centro da tela pesando mais.
in vec2 vUV;
out vec2 FragLum;
uniform sampler2D uSrc;
void main() {
    vec3 c = textureLod(uSrc, vUV, 0.0).rgb;
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    vec2 q = vUV - 0.5;
    float w = exp(-dot(q, q) * 6.0);
    FragLum = vec2(log2(clamp(l, 1e-3, 1e4)) * w, w);
}
