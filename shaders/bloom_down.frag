#version 330 core
// Redução do bloom (13 amostras, Jimenez 2014). No primeiro nível usa média de Karis
// para pontos muito brilhantes (clarão do tiro, sol) não piscarem.
in vec2 vUV;
out vec3 FragColor;
uniform sampler2D uSrc;
uniform int uFirst;

float lumaW(vec3 c) { return 1.0 / (1.0 + dot(c, vec3(0.2126, 0.7152, 0.0722))); }

void main() {
    vec2 t = 1.0 / vec2(textureSize(uSrc, 0));
    vec3 a = texture(uSrc, vUV + t * vec2(-2, 2)).rgb, b = texture(uSrc, vUV + t * vec2(0, 2)).rgb,
         c = texture(uSrc, vUV + t * vec2(2, 2)).rgb, d = texture(uSrc, vUV + t * vec2(-2, 0)).rgb,
         e = texture(uSrc, vUV).rgb, f = texture(uSrc, vUV + t * vec2(2, 0)).rgb,
         g = texture(uSrc, vUV + t * vec2(-2, -2)).rgb, h = texture(uSrc, vUV + t * vec2(0, -2)).rgb,
         i = texture(uSrc, vUV + t * vec2(2, -2)).rgb, j = texture(uSrc, vUV + t * vec2(-1, 1)).rgb,
         k = texture(uSrc, vUV + t * vec2(1, 1)).rgb, l = texture(uSrc, vUV + t * vec2(-1, -1)).rgb,
         m = texture(uSrc, vUV + t * vec2(1, -1)).rgb;
    if (uFirst == 1) {
        vec3 g0 = (a + b + d + e) * 0.25, g1 = (b + c + e + f) * 0.25, g2 = (d + e + g + h) * 0.25,
             g3 = (e + f + h + i) * 0.25, g4 = (j + k + l + m) * 0.25;
        float w0 = lumaW(g0), w1 = lumaW(g1), w2 = lumaW(g2), w3 = lumaW(g3), w4 = lumaW(g4);
        FragColor = (g0 * w0 * 0.125 + g1 * w1 * 0.125 + g2 * w2 * 0.125 + g3 * w3 * 0.125 + g4 * w4 * 0.5) /
                    (w0 * 0.125 + w1 * 0.125 + w2 * 0.125 + w3 * 0.125 + w4 * 0.5);
        FragColor = min(FragColor, vec3(500.0));
    } else {
        FragColor = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
    }
}
