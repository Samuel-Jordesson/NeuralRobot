#version 330 core
#include "common.glsl"

in vec3 vPos;
in vec3 vNormal;
in vec3 vLocal;
out vec4 FragColor;

uniform int uMaterial; // 0 = chão, 1 = bloco, 2 = bola, 3 = metal liso (cápsulas), 4 = madeira (porta)
uniform vec3 uColor;
uniform vec3 uBoxMin, uBoxMax; // limites do bloco (desgaste nas quinas)

// Linhas anti-serrilhadas com largura em metros; somem com a distância (evita moiré).
float grid(vec2 p, float spacing, float lineW) {
    vec2 fw = fwidth(p);
    vec2 d = abs(fract(p / spacing - 0.5) - 0.5) * spacing;
    vec2 a = 1.0 - smoothstep(vec2(lineW * 0.5) - fw, vec2(lineW * 0.5) + fw, d);
    float fade = clamp(1.0 - max(fw.x, fw.y) / spacing * 3.0, 0.0, 1.0);
    return max(a.x, a.y) * fade;
}

// Altura do microrrelevo do concreto (para a normal).
float concreteHeight(vec2 p) { return texture(uNoise, p * (7.0 / 128.0)).b * 0.6 + texture(uNoise, p * (23.0 / 128.0)).b * 0.4; }

vec3 bumpNormal(vec3 N, vec3 T, vec3 B, vec2 p, float strength) {
    const float e = 0.01;
    float h = concreteHeight(p);
    float hx = concreteHeight(p + vec2(e, 0.0)) - h;
    float hy = concreteHeight(p + vec2(0.0, e)) - h;
    // reduz o relevo de longe (senão vira ruído serrilhado)
    float fade = clamp(1.0 - length(fwidth(p)) * 12.0, 0.0, 1.0);
    return normalize(N - (T * hx + B * hy) / e * strength * 0.004 * fade);
}

void main() {
    vec3 N = normalize(vNormal);
    vec3 albedo;
    float rough;
    float metal = 0.0;

    if (uMaterial == 3) {
        albedo = uColor;
        rough = 0.3;
        metal = 1.0;
    } else if (uMaterial == 4) {
        // madeira pintada gasta: veios ao longo da altura da peça (coordenadas locais do cubo)
        vec2 p = vec2(vLocal.x * 3.0 + vLocal.z * 3.0, vLocal.y * 0.4);
        float grain = texture(uNoise, vec2(p.x * 0.9, p.y * 0.08)).r;
        float fine = texture(uNoise, vLocal.xy * (19.0 / 8.0) + vLocal.zy).b;
        albedo = uColor * (0.72 + 0.4 * grain) * (0.92 + 0.12 * fine);
        rough = 0.7 + 0.15 * fine;
    } else if (uMaterial == 0) {
        // concreto em placas de 4 m
        vec2 p = vPos.xz;
        float big = texture(uNoise, p / 50.0).r;              // variação grande (manchas, desgaste)
        float mid = texture(uNoise, p * (1.3 / 32.0)).g;
        float fine = texture(uNoise, p * (11.0 / 128.0)).b;
        vec2 cell = floor(p / 4.0);
        float slab = hash12(cell) * 0.08;       // cada placa com um tom
        albedo = vec3(0.27, 0.265, 0.25) * (0.68 + 0.55 * big) * (0.88 + 0.2 * mid) * (0.9 + 0.16 * fine) * (0.94 + slab);
        float stain = smoothstep(0.62, 0.8, texture(uNoise, p * (0.21 / 8.0) + 0.31).a);
        albedo *= 1.0 - 0.4 * stain;            // manchas escuras (óleo/umidade)
        // pontinhos de agregado (pedrinhas) no concreto
        float speck = smoothstep(0.78, 0.9, texture(uNoise, p * (37.0 / 128.0) + 0.5).b);
        albedo *= 1.0 - 0.18 * speck;
        float joint = grid(p, 4.0, 0.03);
        albedo *= 1.0 - 0.5 * joint;
        rough = clamp(0.86 + 0.08 * mid - 0.22 * stain + 0.05 * joint, 0.4, 1.0);
        N = bumpNormal(N, vec3(1, 0, 0), vec3(0, 0, 1), p, 1.0);
    } else if (uMaterial == 5) {
        // terreno: terra com manchas de grama seca (a pintura do editor vai por cima)
        vec2 p = vPos.xz;
        float big = texture(uNoise, p / 40.0).r, mid = texture(uNoise, p * (1.7 / 32.0)).g, fine = texture(uNoise, p * (13.0 / 128.0)).b;
        vec3 dirt = vec3(0.30, 0.24, 0.17) * (0.8 + 0.4 * mid);
        vec3 grass = vec3(0.22, 0.27, 0.12) * (0.8 + 0.4 * fine);
        albedo = mix(dirt, grass, smoothstep(0.45, 0.7, big)) * (0.9 + 0.2 * fine);
        rough = 0.92;
        N = bumpNormal(N, vec3(1, 0, 0), vec3(0, 0, 1), p, 1.4);
    } else if (uMaterial == 1) {
        // bloco de concreto pintado: cor do bloco + sujeira na base + quinas gastas
        vec3 an = abs(N);
        vec2 p = an.x > 0.5 ? vPos.zy : (an.y > 0.5 ? vPos.xz : vPos.xy);
        vec3 T = an.x > 0.5 ? vec3(0, 0, 1) : vec3(1, 0, 0);
        vec3 B = an.y > 0.5 ? vec3(0, 0, 1) : vec3(0, 1, 0);
        float big = texture(uNoise, p * 0.15 + uBoxMin.xz * 0.1).r;
        float fine = texture(uNoise, p * (9.0 / 128.0)).b;
        vec3 paint = uColor * (0.85 + 0.25 * big) * (0.95 + 0.08 * fine);
        // distância até a quina mais próxima
        vec3 dmin = vPos - uBoxMin, dmax = uBoxMax - vPos;
        vec3 de = min(dmin, dmax);
        float edge = 1e3;
        if (an.x < 0.5) edge = min(edge, de.x);
        if (an.y < 0.5) edge = min(edge, de.y);
        if (an.z < 0.5) edge = min(edge, de.z);
        float wear = (1.0 - smoothstep(0.0, 0.012 + 0.03 * fine, edge)) * smoothstep(0.55, 0.8, big * 0.7 + fine * 0.5);
        vec3 concrete = vec3(0.32, 0.31, 0.30) * (0.85 + 0.2 * fine);
        albedo = mix(paint, concrete, wear);
        float grime = (1.0 - smoothstep(0.0, 0.45 + 0.3 * big, vPos.y - uBoxMin.y)) * (1.0 - an.y);
        albedo *= 1.0 - 0.35 * grime;
        // vazamentos de chuva escorrendo nas faces verticais
        if (an.y < 0.5) albedo *= 1.0 - 0.12 * smoothstep(0.55, 0.9, texture(uNoise, vec2(p.x * 3.0, p.y * 0.25) / 32.0).g);
        rough = mix(0.62 + 0.1 * fine, 0.9, max(wear, grime * 0.5));
        N = bumpNormal(N, T, B, p, 0.6);
    } else {
        // bola de borracha/plástico com faixa
        vec3 l = normalize(vLocal);
        float band = 1.0 - smoothstep(0.22, 0.25, abs(l.y));
        float dots = smoothstep(0.92, 0.93, abs(l.x));
        albedo = mix(uColor, vec3(0.92), max(band, dots));
        rough = 0.38 + 0.1 * texture(uNoise, l.xz * (20.0 / 128.0)).b;
    }

    applyPaint(vPos, normalize(vNormal), albedo, N, rough);
    vec3 col = shadePBR(albedo, metal, rough, 1.0, N, vPos);
    FragColor = vec4(col, 1.0);
}
