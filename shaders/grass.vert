#version 330 core
// Grama 3D: uma folha por instância, numa grade presa ao mundo em volta da câmera. Nasce onde a
// camada de grama está pintada (mapa de pesos), em cima do relevo, e balança com o vento.
uniform mat4 uViewProj;
uniform vec3 uCamPos;
uniform float uTime;
uniform sampler2D uPaintWeights;
uniform vec4 uPaintRect;   // x0, z0, 1/lado
uniform vec4 uGrassChan;   // 1 nas camadas de grama
uniform vec4 uWaterChan;   // 1 nas camadas de água (não nasce grama na poça)
uniform sampler2D uTerrainH;
uniform vec4 uTerrainRect; // x0, z0, lado, ligado
uniform float uGroundY;
uniform int uSide;         // folhas por lado da grade
uniform float uSpacing;    // m entre folhas
uniform float uRadius;

out vec3 vPos;
out vec3 vNormal;
out float vT;      // 0 na raiz, 1 na ponta
out vec3 vTint;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

void main() {
    int id = gl_InstanceID;
    vec2 cell = vec2(id % uSide, id / uSide) - float(uSide / 2);
    vec2 base = floor(uCamPos.xz / uSpacing) * uSpacing; // grade presa ao mundo (não "nada" com a câmera)
    vec2 c = base + cell * uSpacing;
    float h1 = hash(c), h2 = hash(c + 17.3), h3 = hash(c + 41.7), h4 = hash(c + 3.1);
    vec2 p = c + (vec2(h1, h2) - 0.5) * uSpacing;
    float dist = length(p - uCamPos.xz);
    // densidade: peso da pintura; longe, só uma parte das folhas (mais largas)
    vec2 uv = (p - uPaintRect.xy) * uPaintRect.z;
    vec4 w = (uv.x >= 0.0 && uv.y >= 0.0 && uv.x <= 1.0 && uv.y <= 1.0) ? texture(uPaintWeights, uv) : vec4(0.0);
    float grass = dot(w, uGrassChan) * (1.0 - smoothstep(0.15, 0.45, dot(w, uWaterChan)));
    float keep = 1.0 - smoothstep(uRadius * 0.45, uRadius, dist) * 0.75;
    float fade = 1.0 - smoothstep(uRadius * 0.5, uRadius, dist); // baixa aos poucos até sumir no chão
    if (h3 > grass * keep || fade <= 0.0) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); return; } // fora (descartada)
    // chão: relevo ou plano
    float y = uGroundY;
    if (uTerrainRect.w > 0.5) {
        vec2 tuv = (p - uTerrainRect.xy) / uTerrainRect.z;
        if (tuv.x >= 0.0 && tuv.y >= 0.0 && tuv.x <= 1.0 && tuv.y <= 1.0) y = texture(uTerrainH, tuv).r;
    }
    // forma: folha fina que afina até a ponta, curva para frente; vento empurra a ponta
    int v = gl_VertexID;            // tira de 7 vértices: 3 pares + ponta
    float t = float(v / 2) / 3.0;
    float side = (v == 6) ? 0.0 : (float(v % 2) - 0.5);
    float height = (0.16 + 0.32 * h4) * mix(0.55, 1.0, grass) * fade;
    float width = (0.012 + 0.012 * h1) * (1.0 + smoothstep(uRadius * 0.45, uRadius, dist) * 1.5) * (1.0 - t * 0.85);
    float ang = h2 * 6.2831853;
    vec3 fwd = vec3(sin(ang), 0.0, cos(ang)), right = vec3(cos(ang), 0.0, -sin(ang));
    float bend = (0.15 + 0.35 * h3) * t * t;
    float wind = sin(uTime * 1.6 + p.x * 0.45 + p.y * 0.3) * 0.5 + sin(uTime * 3.7 + p.x * 1.3) * 0.15;
    vec3 off = fwd * bend * height + vec3(0.9, 0.0, 0.4) * wind * 0.22 * height * t * t;
    vec3 pos = vec3(p.x, y, p.y) + right * side * width + vec3(0.0, t * height * (1.0 - 0.25 * bend), 0.0) + off;
    vPos = pos;
    vNormal = normalize(cross(right, normalize(vec3(0.0, 1.0, 0.0) + fwd * bend * 2.0 * t)));
    vT = t;
    vTint = mix(vec3(0.8, 1.0, 0.85), vec3(1.25, 1.1, 0.65), h1 * h1); // variação de tom (algumas mais secas)
    gl_Position = uViewProj * vec4(pos, 1.0);
}
