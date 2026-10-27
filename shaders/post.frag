#version 330 core
// Pós-processamento: exposição, bloom, aberração cromática, gradação de cor,
// tonemapping ACES (ajuste de Stephen Hill), vinheta e granulação de filme.
in vec2 vUV;
out vec4 FragColor;  // a = luma (para o FXAA)
uniform sampler2D uHDR;
uniform sampler2D uBloom;
uniform sampler2D uAdapt;
uniform int uAutoExposure;
uniform float uExposureEV;
uniform int uBloomOn;
uniform float uBloomStrength;
uniform float uChromatic;
uniform float uContrast;
uniform float uSaturation;
uniform float uTemperature;
uniform float uVignette;
uniform float uGrain;
uniform float uTime;

const mat3 kACESIn = mat3(0.59719, 0.07600, 0.02840, 0.35458, 0.90834, 0.13383, 0.04823, 0.01566, 0.83777);
const mat3 kACESOut = mat3(1.60475, -0.10208, -0.00327, -0.53108, 1.10813, -0.07276, -0.07367, -0.00605, 1.07602);

vec3 rrtOdt(vec3 v) {
    vec3 a = v * (v + 0.0245786) - 0.000090537;
    vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return a / b;
}
vec3 aces(vec3 c) { return clamp(kACESOut * rrtOdt(kACESIn * c), 0.0, 1.0); }

float hash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main() {
    vec2 q = vUV - 0.5;
    vec3 col;
    if (uChromatic > 0.001) {
        vec2 off = q * dot(q, q) * uChromatic * 0.06;
        col = vec3(texture(uHDR, vUV - off).r, texture(uHDR, vUV).g, texture(uHDR, vUV + off).b);
    } else {
        col = texture(uHDR, vUV).rgb;
    }
    if (uBloomOn == 1) col = mix(col, texture(uBloom, vUV).rgb, uBloomStrength);

    float exposure = exp2(uExposureEV);
    if (uAutoExposure == 1) {
        float avg = exp2(texture(uAdapt, vec2(0.5)).r);
        exposure *= clamp(0.20 / avg, 0.3, 3.0);
    } else {
        exposure *= 0.8;
    }
    col *= exposure;

    // balanço de branco simples e gradação (em linear, contraste em log em volta do cinza médio)
    col *= vec3(1.0 + 0.10 * uTemperature, 1.0 + 0.01 * uTemperature, 1.0 - 0.12 * uTemperature);
    float l = dot(col, vec3(0.2126, 0.7152, 0.0722));
    col = max(mix(vec3(l), col, uSaturation), 0.0);
    col = 0.18 * pow(col / 0.18 + 1e-6, vec3(uContrast));

    col = aces(col * 1.6);
    col *= 1.0 - uVignette * smoothstep(0.15, 0.85, dot(q, q) * 2.0);
    col = pow(col, vec3(1.0 / 2.2));
    // granulação mais visível nos tons médios/escuros
    float n = hash(gl_FragCoord.xy + fract(uTime * 7.13) * 917.0) - 0.5;
    col += n * uGrain * 0.08 * (1.0 - 0.7 * col);
    col += (hash(gl_FragCoord.xy * 1.37) - 0.5) / 255.0;
    FragColor = vec4(col, dot(col, vec3(0.299, 0.587, 0.114)));
}
