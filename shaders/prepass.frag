#version 330 core
// Pré-passo: normal no mundo (a profundidade vai no depth buffer). Usado por SSAO, névoa e luz volumétrica.
in vec3 vNormal;
in vec2 vUV;
uniform sampler2D uBaseTex;
uniform int uAlphaCut; // folhagem: recorte pelo alpha da textura
out vec4 FragColor;
void main() {
    if (uAlphaCut == 1 && texture(uBaseTex, vUV).a < 0.5) discard;
    vec3 n = normalize(vNormal);
    FragColor = vec4(gl_FrontFacing ? n : -n, 1.0);
}
