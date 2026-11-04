#version 330 core
// Foto de um modelo para o fundo (impostor): só a cor da textura, recortada pelo alpha, e um
// escurecido leve embaixo da copa (sombra própria), sem luz da cena (a luz vem no jogo).
in vec3 vPos;
in vec2 vUV;
uniform sampler2D uBaseTex;
uniform int uAlphaCut;
uniform vec2 uHeightRange; // y mínimo e máximo do modelo
out vec4 FragColor;
void main() {
    vec4 c = texture(uBaseTex, vUV);
    if (uAlphaCut == 1 && c.a < 0.5) discard;
    float k = clamp((vPos.y - uHeightRange.x) / max(uHeightRange.y - uHeightRange.x, 0.01), 0.0, 1.0);
    FragColor = vec4(c.rgb * (0.62 + 0.38 * k), 1.0);
}
