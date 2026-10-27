#version 330 core
uniform vec4 uRect;   // x, y, largura, altura em pixels (origem no canto superior esquerdo)
uniform vec2 uScreen;
out vec2 vUV;

void main() {
    const vec2 corners[6] = vec2[](vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 0), vec2(1, 1), vec2(0, 1));
    vec2 c = corners[gl_VertexID];
    vUV = c;
    vec2 px = uRect.xy + c * uRect.zw;
    gl_Position = vec4(px.x / uScreen.x * 2.0 - 1.0, 1.0 - px.y / uScreen.y * 2.0, 0.0, 1.0);
}
