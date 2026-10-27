#version 330 core
// Efeitos de tiro. Tipos: 0 faísca (rastro), 1 fumaça/poeira, 2 rastro da bala, 3 clarão do cano.
// Aditivos (0, 2, 3) saem como cor * intensidade; a fumaça usa alpha normal.
in vec2 vUV;
in vec4 vColor;
flat in int vKind;
out vec4 FragColor;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

void main() {
    vec2 uv = vUV;
    if (vKind == 0) {          // faísca: uv.x atravessa, uv.y ao longo (0 cauda, 1 cabeça)
        float a = (1.0 - uv.x * uv.x) * smoothstep(0.0, 0.6, uv.y);
        FragColor = vec4(vColor.rgb * a * vColor.a, 1.0);
    } else if (vKind == 1) {   // fumaça: disco macio com borda irregular
        float r = length(uv);
        float ang = atan(uv.y, uv.x);
        float edge = 0.75 + 0.15 * sin(ang * 5.0 + vColor.a * 7.0) + 0.1 * sin(ang * 9.0);
        float a = (1.0 - smoothstep(edge * 0.35, edge, r)) * vColor.a;
        FragColor = vec4(vColor.rgb, a);
    } else if (vKind == 2) {   // rastro da bala
        float a = exp(-uv.x * uv.x * 5.0) * uv.y;
        FragColor = vec4(vColor.rgb * a * vColor.a, 1.0);
    } else {                   // clarão: núcleo + estrela de 4/6 pontas
        float r = length(uv);
        float ang = atan(uv.y, uv.x);
        float star = pow(abs(cos(ang * 2.0)), 18.0) + 0.6 * pow(abs(cos(ang * 3.0 + 0.5)), 30.0);
        float core = exp(-r * r * 14.0);
        float rays = star * exp(-r * 2.6) * (1.0 - smoothstep(0.7, 1.0, r));
        float a = (core * 1.6 + rays) * vColor.a;
        FragColor = vec4(vColor.rgb * a, 1.0);
    }
}
