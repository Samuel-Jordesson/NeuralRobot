#version 330 core
// HUD: 0 = mira de um ponto, 1 = tecla "E".
in vec2 vUV;
out vec4 FragColor;
uniform vec4 uRect;
uniform int uType;
uniform float uAlpha;

float sdBox(vec2 p, vec2 b) {
    vec2 d = abs(p) - b;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

void main() {
    vec2 p = (vUV - 0.5) * uRect.zw; // pixels a partir do centro
    if (uType == 0) {
        float r = length(p) * 16.0 / uRect.z; // desenhada em 16 px e escalada
        float dotA = 1.0 - smoothstep(1.8, 2.8, r);
        float outline = 1.0 - smoothstep(2.8, 4.0, r);
        FragColor = vec4(vec3(dotA), max(dotA, outline * 0.55) * uAlpha);
        return;
    }
    vec2 h = uRect.zw * 0.5 - 1.5;
    float d = sdBox(p, h - 7.0) - 7.0;
    float fill = 1.0 - smoothstep(-0.5, 0.5, d);
    float border = (1.0 - smoothstep(0.8, 1.8, abs(d + 1.6))) * fill;
    float s = uRect.w / 40.0;
    vec2 q = p / s;
    float e = min(min(sdBox(q - vec2(-4.6, 0.0), vec2(1.7, 8.0)), sdBox(q - vec2(0.4, -6.3), vec2(6.7, 1.7))),
                  min(sdBox(q - vec2(-0.2, 0.0), vec2(6.1, 1.6)), sdBox(q - vec2(0.4, 6.3), vec2(6.7, 1.7))));
    float letter = 1.0 - smoothstep(-0.6, 0.6, e * s);
    float ink = max(border, letter);
    vec3 col = mix(vec3(0.06), vec3(1.0), ink);
    FragColor = vec4(col, max(fill * 0.62, ink) * uAlpha);
}
