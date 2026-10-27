#version 330 core
// Saída final: FXAA (Lottes) + ajuste para o tamanho da janela (escala de resolução).
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uSrc;   // LDR com luma no alfa
uniform int uFxaa;

void main() {
    if (uFxaa == 0) { FragColor = vec4(texture(uSrc, vUV).rgb, 1.0); return; }
    vec2 rcp = 1.0 / vec2(textureSize(uSrc, 0));
    float lNW = texture(uSrc, vUV + vec2(-1, -1) * rcp).a;
    float lNE = texture(uSrc, vUV + vec2(1, -1) * rcp).a;
    float lSW = texture(uSrc, vUV + vec2(-1, 1) * rcp).a;
    float lSE = texture(uSrc, vUV + vec2(1, 1) * rcp).a;
    vec4 cM = texture(uSrc, vUV);
    float lM = cM.a;
    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));
    if (lMax - lMin < max(0.0312, lMax * 0.125)) { FragColor = vec4(cM.rgb, 1.0); return; }

    vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), (lNW + lSW) - (lNE + lSE));
    float reduce = max((lNW + lNE + lSW + lSE) * 0.25 * (1.0 / 8.0), 1.0 / 128.0);
    float rcpMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
    dir = clamp(dir * rcpMin, vec2(-8.0), vec2(8.0)) * rcp;

    vec3 rgbA = 0.5 * (texture(uSrc, vUV + dir * (1.0 / 3.0 - 0.5)).rgb + texture(uSrc, vUV + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 rgbB = rgbA * 0.5 + 0.25 * (texture(uSrc, vUV + dir * -0.5).rgb + texture(uSrc, vUV + dir * 0.5).rgb);
    float lB = dot(rgbB, vec3(0.299, 0.587, 0.114));
    FragColor = vec4((lB < lMin || lB > lMax) ? rgbA : rgbB, 1.0);
}
