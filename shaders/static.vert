#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;

uniform mat4 uModel;
uniform mat4 uViewProj;

out vec3 vPos;
out vec3 vNormal;
out vec3 vLocal;

void main() {
    vec4 wp = uModel * vec4(aPos, 1.0);
    vPos = wp.xyz;
    vNormal = mat3(uModel) * aNormal; // só rotação + escala uniforme/positiva
    vLocal = aPos;
    gl_Position = uViewProj * wp;
}
