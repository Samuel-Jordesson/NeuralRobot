#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aTangent;
layout(location = 4) in uvec4 aBones;
layout(location = 5) in vec4 aWeights;

const int MAX_BONES = 27;
uniform mat4 uBones[MAX_BONES];
uniform mat4 uModel;
uniform mat4 uViewProj;

out vec3 vPos;
out vec3 vNormal;
out vec4 vTangent;
out vec2 vUV;
out vec3 vDebug;

vec3 boneColor(uint b) {
    float h = float(b) * 0.61803398875;
    vec3 c = clamp(abs(fract(h + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0, 0.0, 1.0);
    return mix(vec3(0.15), c, 0.9);
}

void main() {
    mat4 skin = uBones[aBones.x] * aWeights.x + uBones[aBones.y] * aWeights.y +
                uBones[aBones.z] * aWeights.z + uBones[aBones.w] * aWeights.w;
    vec4 wp = uModel * (skin * vec4(aPos, 1.0));
    mat3 nm = mat3(uModel) * mat3(skin);
    vPos = wp.xyz;
    vNormal = nm * aNormal;
    vTangent = vec4(nm * aTangent.xyz, aTangent.w);
    vUV = aUV;
    vDebug = boneColor(aBones.x) * aWeights.x + boneColor(aBones.y) * aWeights.y +
             boneColor(aBones.z) * aWeights.z + boneColor(aBones.w) * aWeights.w;
    gl_Position = uViewProj * wp;
}
