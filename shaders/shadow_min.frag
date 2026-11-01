#version 330 core
// Engorda as sombras em 1 texel: cada texel fica com a profundidade mais perto do sol entre ele e os
// vizinhos. Fecha frestas finas que o mapa de sombra não resolve (ex.: teto que termina rente a uma
// parede oca), por onde a luz vazava para dentro dos lugares fechados.
uniform sampler2DArray uSrc;
uniform int uLayer;
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec2 sz = textureSize(uSrc, 0).xy;
    float d = 1.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            d = min(d, texelFetch(uSrc, ivec3(clamp(p + ivec2(x, y), ivec2(0), sz - 1), uLayer), 0).r);
    gl_FragDepth = d;
}
