#version 330 core
// Oclusão de ambiente em espaço de tela (hemisfério orientado pela normal).
in vec2 vUV;
out float FragAO;
uniform sampler2D uDepth;
uniform sampler2D uNormal;
uniform mat4 uProj;
uniform mat4 uInvProj;
uniform mat4 uView;
uniform vec3 uKernel[16];
uniform float uRadius;
uniform float uIntensity;

float ign(vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }

float viewZ(vec2 uv) {
    float ndc = texture(uDepth, uv).r * 2.0 - 1.0;
    return -uProj[3][2] / (ndc + uProj[2][2]);
}

void main() {
    float d = texture(uDepth, vUV).r;
    if (d >= 1.0) { FragAO = 1.0; return; }
    vec4 pv = uInvProj * vec4(vUV * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    vec3 P = pv.xyz / pv.w;
    vec3 N = normalize(mat3(uView) * texture(uNormal, vUV).xyz);
    float a = ign(gl_FragCoord.xy) * 6.2831853;
    vec3 rv = vec3(cos(a), sin(a), 0.0);
    vec3 T = normalize(rv - N * dot(rv, N));
    mat3 TBN = mat3(T, cross(N, T), N);
    // raio cresce um pouco com a distância para não sumir de longe
    float radius = uRadius * (1.0 + max(-P.z - 10.0, 0.0) * 0.04);
    float occ = 0.0;
    for (int i = 0; i < 16; ++i) {
        vec3 S = P + TBN * uKernel[i] * radius;
        vec4 o = uProj * vec4(S, 1.0);
        vec2 uv = o.xy / o.w * 0.5 + 0.5;
        float sz = viewZ(uv);
        float range = smoothstep(0.0, 1.0, radius / abs(P.z - sz));
        occ += (sz >= S.z + 0.02 * radius ? 1.0 : 0.0) * range;
    }
    FragAO = pow(clamp(1.0 - occ / 16.0, 0.0, 1.0), 1.0 + uIntensity * 1.5);
}
