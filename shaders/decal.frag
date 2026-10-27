#version 330 core
// Marca de tiro desenhada com blend multiplicativo (escurece a superfície por baixo).
in vec2 vUV;
in vec4 vColor;
flat in int vKind;
out vec4 FragColor;

void main() {
    float r = length(vUV);
    if (r > 1.0) discard;
    float ang = atan(vUV.y, vUV.x);
    float jag = 0.08 * sin(ang * 7.0 + vColor.a * 13.0) + 0.05 * sin(ang * 13.0 + vColor.a * 3.0);
    float hole = 1.0 - smoothstep(0.16, 0.22, r + jag * 0.3);
    float chip = 1.0 - smoothstep(0.22, 0.48 + jag, r);   // lascado em volta do furo
    float soot = 1.0 - smoothstep(0.35, 1.0, r);          // fuligem
    vec3 c = vec3(1.0);
    c *= mix(1.0, 0.6, soot);
    c *= mix(1.0, 0.45, chip);
    c = mix(c, vec3(0.03), hole);
    FragColor = vec4(c, 1.0);
}
