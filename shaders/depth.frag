#version 330 core
in vec2 vUV;
uniform sampler2D uBaseTex;
uniform int uAlphaCut; // folhagem: recorte pelo alpha da textura
void main() {
    if (uAlphaCut == 1 && texture(uBaseTex, vUV).a < 0.5) discard;
}
