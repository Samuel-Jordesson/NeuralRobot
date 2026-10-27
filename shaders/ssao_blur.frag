#version 330 core
// Desfoque 5x5 que respeita bordas (pesos pela diferença de profundidade).
in vec2 vUV;
out float FragAO;
uniform sampler2D uAO;
uniform sampler2D uDepth;
uniform mat4 uProj;

float viewZ(vec2 uv) {
    float ndc = texture(uDepth, uv).r * 2.0 - 1.0;
    return -uProj[3][2] / (ndc + uProj[2][2]);
}

void main() {
    vec2 texel = 1.0 / vec2(textureSize(uAO, 0));
    float z0 = viewZ(vUV);
    float sum = 0.0, wsum = 0.0;
    for (int y = -2; y <= 2; ++y)
        for (int x = -2; x <= 2; ++x) {
            vec2 uv = vUV + vec2(x, y) * texel;
            float w = exp(-abs(viewZ(uv) - z0) / (abs(z0) * 0.02 + 0.01));
            sum += texture(uAO, uv).r * w;
            wsum += w;
        }
    FragAO = sum / wsum;
}
