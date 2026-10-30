#version 330 core
// Pintura no modelo (editor): decalque projetado. A caixa do carimbo é desenhada e cada pixel da
// tela que cai dentro dela é pintado, seguindo a forma de verdade da superfície (telha ondulada,
// quina, degrau). Escurece/tinge por baixo (blend multiplicativo), então respeita a luz do lugar.
out vec4 FragColor;

uniform sampler2D uDepth;    // profundidade do pré-passo
uniform sampler2D uNormalTex; // normal no mundo do pré-passo
uniform sampler2D uNoise;
uniform sampler2D uStainTex;
uniform int uHasTex;
uniform mat4 uInvViewProj;
uniform mat4 uInvModel;      // mundo -> caixa do carimbo (-0,5..0,5)
uniform vec3 uStainNormal;
uniform vec2 uScreen;
uniform float uStrength, uSoft, uSeed;

void main() {
    vec2 uv = gl_FragCoord.xy / uScreen;
    float d = texture(uDepth, uv).r;
    if (d >= 1.0) discard; // céu
    vec4 w = uInvViewProj * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    vec3 P = w.xyz / w.w;
    vec3 l = (uInvModel * vec4(P, 1.0)).xyz;
    if (any(greaterThan(abs(l), vec3(0.5)))) discard;
    // superfície virada demais em relação ao carimbo (a outra face da quina): some suave
    vec3 n = texture(uNormalTex, uv).xyz;
    float facing = smoothstep(0.2, 0.55, dot(normalize(n), uStainNormal));
    if (facing <= 0.0) discard;
    vec2 q = l.xy * 2.0; // -1..1 no carimbo
    float r = length(q);
    float mask = (1.0 - smoothstep(1.0 - max(uSoft, 0.02), 1.0, r)) * facing;
    mask *= 1.0 - smoothstep(0.35, 0.5, abs(l.z)); // apaga nas pontas da espessura
    vec3 tint;
    if (uHasTex == 1) {
        vec4 t = texture(uStainTex, q * 0.5 + 0.5);
        tint = t.rgb;
        mask *= t.a;
    } else {
        // sujeira: manchas escuras e amarronzadas, mais fortes no meio e se desfazendo na borda
        vec2 p = q * 0.3 + vec2(uSeed, uSeed * 1.7) * 5.0;
        float nz = texture(uNoise, p).a * 0.65 + texture(uNoise, p * 2.3 + 0.4).r * 0.35;
        mask *= smoothstep(0.42, 0.7, nz + (1.0 - r) * 0.3);
        float streak = texture(uNoise, vec2(q.x * 0.1, q.y * 0.02) + uSeed).g; // escorrido
        tint = vec3(0.45, 0.38, 0.30) * (0.75 + 0.35 * streak);
    }
    float k = clamp(mask * uStrength, 0.0, 1.0);
    if (k <= 0.002) discard;
    FragColor = vec4(mix(vec3(1.0), tint, k), 1.0);
}
