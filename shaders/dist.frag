#version 330 core
// Sombra das luzes do editor: grava a distância de cada ponto até a luz (mapa em cubo).
in vec3 vPos;
uniform vec3 uLightPos;
out vec4 FragColor;
void main() { FragColor = vec4(length(vPos - uLightPos)); }
