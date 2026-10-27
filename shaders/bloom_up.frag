#version 330 core
// Ampliação do bloom com filtro tenda 3x3 (somado ao nível maior com blend aditivo).
in vec2 vUV;
out vec3 FragColor;
uniform sampler2D uSrc;
uniform float uRadius;

void main() {
    vec2 t = uRadius / vec2(textureSize(uSrc, 0));
    vec3 s = texture(uSrc, vUV).rgb * 4.0;
    s += (texture(uSrc, vUV + t * vec2(-1, 0)).rgb + texture(uSrc, vUV + t * vec2(1, 0)).rgb +
          texture(uSrc, vUV + t * vec2(0, -1)).rgb + texture(uSrc, vUV + t * vec2(0, 1)).rgb) * 2.0;
    s += texture(uSrc, vUV + t * vec2(-1, -1)).rgb + texture(uSrc, vUV + t * vec2(1, -1)).rgb +
         texture(uSrc, vUV + t * vec2(-1, 1)).rgb + texture(uSrc, vUV + t * vec2(1, 1)).rgb;
    FragColor = s / 16.0;
}
