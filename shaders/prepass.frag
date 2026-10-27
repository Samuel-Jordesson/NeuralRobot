#version 330 core
// Pré-passo: normal no mundo (a profundidade vai no depth buffer). Usado por SSAO, névoa e luz volumétrica.
in vec3 vNormal;
out vec4 FragColor;
void main() {
    vec3 n = normalize(vNormal);
    FragColor = vec4(gl_FrontFacing ? n : -n, 1.0);
}
