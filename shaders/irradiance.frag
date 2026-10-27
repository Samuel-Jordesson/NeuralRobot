#version 330 core
// Irradiância difusa do céu: média da radiância ponderada pelo cosseno (= E / pi).
in vec2 vUV;
out vec4 FragColor;
uniform mat4 uInvViewProj;
uniform samplerCube uSrc;

const float PI = 3.14159265;

void main() {
    vec4 w = uInvViewProj * vec4(vUV * 2.0 - 1.0, 1.0, 1.0);
    vec3 N = normalize(w.xyz / w.w);
    vec3 up = abs(N.y) < 0.999 ? vec3(0, 1, 0) : vec3(1, 0, 0);
    vec3 T = normalize(cross(up, N)), B = cross(N, T);
    const int RINGS = 12, SEG = 24;
    vec3 sum = vec3(0.0);
    for (int r = 0; r < RINGS; ++r) {
        float u = (float(r) + 0.5) / float(RINGS);
        float cosT = sqrt(1.0 - u), sinT = sqrt(u); // distribuição cosseno
        for (int s = 0; s < SEG; ++s) {
            float phi = (float(s) + 0.5 + 0.5 * float(r & 1)) / float(SEG) * 2.0 * PI;
            vec3 L = T * (cos(phi) * sinT) + B * (sin(phi) * sinT) + N * cosT;
            sum += textureLod(uSrc, L, 3.0).rgb;
        }
    }
    FragColor = vec4(sum / float(RINGS * SEG), 1.0);
}
