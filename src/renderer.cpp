#include "renderer.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

GLuint makeTex(GLenum internal, int w, int h, GLenum format, GLenum type, GLenum filter) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, format, type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

GLuint makeFbo(GLuint color, GLuint depth = 0) {
    GLuint f;
    glGenFramebuffers(1, &f);
    glBindFramebuffer(GL_FRAMEBUFFER, f);
    if (color) glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
    if (depth) glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) std::fprintf(stderr, "[render] FBO incompleto\n");
    return f;
}

GLuint makeCubeTex(int res, bool mips) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_CUBE_MAP, t);
    int levels = mips ? (int)std::log2((float)res) + 1 : 1;
    for (int l = 0; l < levels; ++l)
        for (int f = 0; f < 6; ++f)
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, l, GL_RGBA16F, std::max(1, res >> l), std::max(1, res >> l), 0,
                         GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, mips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, levels - 1);
    for (GLenum w : {GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_TEXTURE_WRAP_R})
        glTexParameteri(GL_TEXTURE_CUBE_MAP, w, GL_CLAMP_TO_EDGE);
    return t;
}

// Matrizes (proj * view)^-1 de cada face da cubemap, na convenção do OpenGL.
glm::mat4 faceInvViewProj(int f) {
    static const glm::vec3 dirs[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    static const glm::vec3 ups[6] = {{0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};
    glm::mat4 p = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
    return glm::inverse(p * glm::lookAt(glm::vec3(0.0f), dirs[f], ups[f]));
}

// Transmitância da atmosfera do chão até o sol (mesmos coeficientes de atmosphere.glsl).
glm::vec3 sunTransmittance(const glm::vec3& dir) {
    const double R = 6371e3, A = 6471e3;
    const glm::dvec3 kR(5.5e-6, 13.0e-6, 22.4e-6);
    const glm::dvec3 kOzone = glm::dvec3(0.650e-6, 1.881e-6, 0.085e-6) * 1.9;
    const double kM = 21e-6, hR = 8e3, hM = 1.2e3;
    glm::dvec3 ro(0.0, R + 2.0, 0.0), rd(dir);
    auto sphere = [&](double r, double& t0, double& t1) {
        double b = glm::dot(ro, rd), c = glm::dot(ro, ro) - r * r, d = b * b - c;
        if (d < 0.0) return false;
        d = std::sqrt(d);
        t0 = -b - d;
        t1 = -b + d;
        return true;
    };
    double g0, g1, a0, a1;
    if (sphere(R, g0, g1) && g0 > 0.0) return glm::vec3(0.0f); // sol abaixo do horizonte
    sphere(A, a0, a1);
    const int n = 64;
    double ds = a1 / n, odR = 0.0, odM = 0.0;
    for (int i = 0; i < n; ++i) {
        double h = glm::length(ro + rd * (ds * (i + 0.5))) - R;
        odR += std::exp(-h / hR) * ds;
        odM += std::exp(-h / hM) * ds;
    }
    glm::dvec3 tau = (kR + kOzone) * odR + glm::dvec3(kM * 1.1 * odM);
    return glm::vec3(glm::exp(-tau));
}

// Quad genérico: centro c, eixos (já com meia-largura) ax/ay, uv de -1..1 (ou y 0..1 em rastros).
template <class V>
void pushQuad(std::vector<V>& out, const glm::vec3& c, const glm::vec3& ax, const glm::vec3& ay, const glm::vec4& col,
              float kind, bool streak) {
    float y0 = streak ? 0.0f : -1.0f;
    const glm::vec3 p[4] = {c - ax - ay, c + ax - ay, c + ax + ay, c - ax + ay};
    const glm::vec2 uv[4] = {{-1, y0}, {1, y0}, {1, 1}, {-1, 1}};
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    for (int i : idx) out.push_back({{p[i].x, p[i].y, p[i].z}, {uv[i].x, uv[i].y}, {col.r, col.g, col.b, col.a}, kind});
}

// Faixa voltada para a câmera indo de 'a' (cauda) até 'b' (cabeça).
template <class V>
void pushStreak(std::vector<V>& out, const glm::vec3& a, const glm::vec3& b, float width, const glm::vec3& camPos,
                const glm::vec4& col, float kind) {
    glm::vec3 axis = b - a;
    if (glm::length(axis) < 1e-5f) return;
    glm::vec3 side = glm::cross(axis, camPos - (a + b) * 0.5f);
    if (glm::length(side) < 1e-6f) return;
    side = glm::normalize(side) * width;
    pushQuad(out, (a + b) * 0.5f, side, axis * 0.5f, col, kind, true);
}

// Perfil de GPU por passo (GA_PROFILE=1): mede com timer queries e imprime médias de vez em quando.
struct GpuProfiler {
    bool on = std::getenv("GA_PROFILE") != nullptr;
    std::vector<const char*> names;
    std::vector<GLuint> queries;
    std::vector<double> sums;
    int frame = 0, used = 0;
    void mark(const char* name) {
        if (!on) return;
        if (used >= (int)queries.size()) {
            GLuint q;
            glGenQueries(1, &q);
            queries.push_back(q);
            names.push_back(name);
            sums.push_back(0.0);
        }
        names[used] = name;
        glQueryCounter(queries[used++], GL_TIMESTAMP);
    }
    void endFrame() {
        if (!on) return;
        GLuint64 prev = 0;
        for (int i = 0; i < used; ++i) {
            GLuint64 t;
            glGetQueryObjectui64v(queries[i], GL_QUERY_RESULT, &t);
            if (i > 0) sums[i] += (t - prev) / 1e6;
            prev = t;
        }
        if (++frame % 60 == 0) {
            std::printf("[gpu]");
            for (int i = 1; i < used; ++i) std::printf(" %s %.2f", names[i], sums[i] / 60.0);
            std::printf(" ms\n");
            std::fill(sums.begin(), sums.end(), 0.0);
        }
        used = 0;
    }
};
GpuProfiler g_prof;

// unidades de textura fixas usadas pela iluminação
enum TexUnit { kUnitIrradiance = 4, kUnitEnv = 5, kUnitAO = 6, kUnitShadow = 7, kUnitSkyCube = 8, kUnitNoise = 9 };

// Textura de ruído que repete sem emenda (256x256), lida pelos shaders no lugar de ruído procedural:
// R = fbm grande (4 células), G = ruído médio (32), B = ruído fino (128), A = fbm (8 células).
GLuint makeNoiseTexture() {
    const int N = 256;
    auto lattice = [](int x, int y, int period, uint32_t seed) {
        x = ((x % period) + period) % period;
        y = ((y % period) + period) % period;
        uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 2246822519u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return ((h ^ (h >> 16)) & 0xFFFF) / 65535.0f;
    };
    auto value = [&](float x, float y, int period, uint32_t seed) {
        int ix = (int)std::floor(x), iy = (int)std::floor(y);
        float fx = x - ix, fy = y - iy;
        fx = fx * fx * (3.0f - 2.0f * fx);
        fy = fy * fy * (3.0f - 2.0f * fy);
        float a = lattice(ix, iy, period, seed), b = lattice(ix + 1, iy, period, seed);
        float c = lattice(ix, iy + 1, period, seed), d = lattice(ix + 1, iy + 1, period, seed);
        return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
    };
    auto fbm = [&](float u, float v, int cells, int octaves, uint32_t seed) {
        float f = 0.0f, amp = 0.5f;
        for (int o = 0; o < octaves; ++o) {
            int c = cells << o;
            f += amp * value(u * c, v * c, c, seed + o);
            amp *= 0.5f;
        }
        return f;
    };
    std::vector<float> ch[4];
    for (auto& c : ch) c.resize(N * N);
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            float u = (float)x / N, v = (float)y / N;
            int i = y * N + x;
            ch[0][i] = fbm(u, v, 4, 6, 11u);
            ch[1][i] = value(u * 32, v * 32, 32, 23u);
            ch[2][i] = value(u * 128, v * 128, 128, 37u);
            ch[3][i] = fbm(u, v, 8, 5, 51u);
        }
    std::vector<unsigned char> px(N * N * 4);
    for (int c = 0; c < 4; ++c) {
        auto mm = std::minmax_element(ch[c].begin(), ch[c].end());
        float lo = *mm.first, hi = *mm.second;
        for (int i = 0; i < N * N; ++i) px[i * 4 + c] = (unsigned char)std::lround((ch[c][i] - lo) / (hi - lo) * 255.0f);
    }
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, N, N, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    return t;
}

} // namespace

bool Renderer::init(const std::string& dir, int width, int height) {
    auto ld = [&](Shader& s, const char* vs, const char* fs) { return s.load(dir + "/" + vs, dir + "/" + fs); };
    bool ok = ld(skinned_, "skinned.vert", "character.frag") && ld(skinnedDepth_, "skinned.vert", "depth.frag") &&
              ld(skinnedPre_, "skinned.vert", "prepass.frag") && ld(world_, "static.vert", "world.frag") &&
              ld(worldDepth_, "static.vert", "depth.frag") && ld(worldPre_, "static.vert", "prepass.frag") &&
              ld(sky_, "fullscreen.vert", "sky.frag") && ld(skyAtmo_, "fullscreen.vert", "sky_atmo.frag") &&
              ld(prefilter_, "fullscreen.vert", "prefilter.frag") && ld(irradiance_, "fullscreen.vert", "irradiance.frag") &&
              ld(ssao_, "fullscreen.vert", "ssao.frag") && ld(ssaoBlur_, "fullscreen.vert", "ssao_blur.frag") &&
              ld(volumetric_, "fullscreen.vert", "volumetric.frag") && ld(fogComp_, "fullscreen.vert", "fog_composite.frag") &&
              ld(bloomDown_, "fullscreen.vert", "bloom_down.frag") && ld(bloomUp_, "fullscreen.vert", "bloom_up.frag") &&
              ld(lum_, "fullscreen.vert", "luminance.frag") && ld(adapt_, "fullscreen.vert", "adapt.frag") &&
              ld(post_, "fullscreen.vert", "post.frag") && ld(fxaa_, "fullscreen.vert", "fxaa.frag") &&
              ld(fx_, "fx.vert", "fx.frag") && ld(decal_, "fx.vert", "decal.frag") && ld(hud_, "hud.vert", "hud.frag");
    if (!ok) return false;

    // samplers da iluminação sempre nas mesmas unidades (tipos diferentes não podem dividir unidade)
    for (Shader* sh : {&skinned_, &world_, &sky_, &volumetric_, &fogComp_}) {
        sh->use();
        sh->set("uIrradiance", (int)kUnitIrradiance);
        sh->set("uEnvMap", (int)kUnitEnv);
        sh->set("uAOTex", (int)kUnitAO);
        sh->set("uShadowMap", (int)kUnitShadow);
        sh->set("uSkyCube", (int)kUnitSkyCube);
        sh->set("uNoise", (int)kUnitNoise);
    }

    cube_ = makeCube();
    sphere_ = makeSphere(48, 24);
    plane_ = makePlane(600.0f);
    glGenVertexArrays(1, &emptyVao_);
    for (auto& m : identityBones_) m = glm::mat4(1.0f);
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    noiseTex_ = makeNoiseTexture();
    glActiveTexture(GL_TEXTURE0 + kUnitNoise);
    glBindTexture(GL_TEXTURE_2D, noiseTex_);
    glActiveTexture(GL_TEXTURE0);

    // buffer dinâmico dos efeitos (quads montados na CPU a cada frame)
    glGenVertexArrays(1, &fxVao_);
    glGenBuffers(1, &fxVbo_);
    glBindVertexArray(fxVao_);
    glBindBuffer(GL_ARRAY_BUFFER, fxVbo_);
    const GLsizei st = sizeof(FxVertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, st, (void*)offsetof(FxVertex, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, st, (void*)offsetof(FxVertex, uv));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, st, (void*)offsetof(FxVertex, color));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, st, (void*)offsetof(FxVertex, kind));
    glBindVertexArray(0);

    // céu: atmosfera (128), ambiente bruto (64), reflexo pré-filtrado e irradiância
    skyCube_ = makeCubeTex(kSkyRes, true);
    envRaw_ = makeCubeTex(kEnvRes, true);
    envPref_ = makeCubeTex(kEnvRes, true);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envPref_);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, kEnvMips - 1);
    irrCube_ = makeCubeTex(kIrrRes, false);
    glGenFramebuffers(1, &captureFbo_);

    // exposição automática: log da luminância com mips + adaptação 1x1 (ping-pong)
    lumTex_ = makeTex(GL_RG16F, kLumRes, kLumRes, GL_RG, GL_FLOAT, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, lumTex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
    glGenerateMipmap(GL_TEXTURE_2D);
    lumFbo_ = makeFbo(lumTex_);
    for (int i = 0; i < 2; ++i) {
        adaptTex_[i] = makeTex(GL_R32F, 1, 1, GL_RED, GL_FLOAT, GL_NEAREST);
        adaptFbo_[i] = makeFbo(adaptTex_[i]);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    gs_.applyPreset(gs_.preset);
    createShadowMap();
    w_ = width;
    h_ = height;
    createTargets();
    return true;
}

void Renderer::createShadowMap() {
    if (shadowTex_) {
        glDeleteTextures(1, &shadowTex_);
        glDeleteFramebuffers(1, &shadowFbo_);
    }
    shadowRes_ = gs_.shadowRes;
    shadowLayers_ = gs_.shadowCascades;
    glGenTextures(1, &shadowTex_);
    glBindTexture(GL_TEXTURE_2D_ARRAY, shadowTex_);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT16, shadowRes_, shadowRes_, shadowLayers_, 0, GL_DEPTH_COMPONENT,
                 GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float border[4] = {1, 1, 1, 1};
    glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, border);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glGenFramebuffers(1, &shadowFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, shadowTex_, 0, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::destroyTargets() {
    if (!preFbo_) return;
    GLuint fbos[] = {preFbo_, aoFbo_, aoBlurFbo_, msFbo_, hdrFbo_, volFbo_, fogFbo_, ldrFbo_};
    GLuint texs[] = {preNormal_, preDepth_, aoTex_, aoBlurTex_, hdrTex_, volTex_, fogTex_, ldrTex_};
    glDeleteFramebuffers(8, fbos);
    glDeleteTextures(8, texs);
    glDeleteRenderbuffers(1, &msColor_);
    glDeleteRenderbuffers(1, &msDepth_);
    glDeleteFramebuffers(kBloomMips, bloomFbo_);
    glDeleteTextures(kBloomMips, bloomTex_);
    preFbo_ = 0;
}

void Renderer::createTargets() {
    rw_ = std::max(1, (int)std::lround(w_ * gs_.renderScale));
    rh_ = std::max(1, (int)std::lround(h_ * gs_.renderScale));

    // pré-passo: normais + profundidade
    preNormal_ = makeTex(GL_RGBA16F, rw_, rh_, GL_RGBA, GL_FLOAT, GL_NEAREST);
    preDepth_ = makeTex(GL_DEPTH_COMPONENT24, rw_, rh_, GL_DEPTH_COMPONENT, GL_FLOAT, GL_NEAREST);
    preFbo_ = makeFbo(preNormal_, preDepth_);
    aoTex_ = makeTex(GL_R8, std::max(1, rw_ / 2), std::max(1, rh_ / 2), GL_RED, GL_UNSIGNED_BYTE, GL_LINEAR);
    aoFbo_ = makeFbo(aoTex_);
    aoBlurTex_ = makeTex(GL_R8, std::max(1, rw_ / 2), std::max(1, rh_ / 2), GL_RED, GL_UNSIGNED_BYTE, GL_LINEAR);
    aoBlurFbo_ = makeFbo(aoBlurTex_);

    // cena HDR com MSAA
    samples_ = gs_.msaa > 1 ? gs_.msaa : 0;
    GLint maxSamples = 4;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    samples_ = std::min(samples_, (int)maxSamples);
    glGenRenderbuffers(1, &msColor_);
    glBindRenderbuffer(GL_RENDERBUFFER, msColor_);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples_, GL_R11F_G11F_B10F, rw_, rh_);
    glGenRenderbuffers(1, &msDepth_);
    glBindRenderbuffer(GL_RENDERBUFFER, msDepth_);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples_, GL_DEPTH_COMPONENT24, rw_, rh_);
    glGenFramebuffers(1, &msFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, msFbo_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msColor_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msDepth_);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) std::fprintf(stderr, "[render] FBO MSAA incompleto\n");

    hdrTex_ = makeTex(GL_R11F_G11F_B10F, rw_, rh_, GL_RGB, GL_FLOAT, GL_LINEAR);
    hdrFbo_ = makeFbo(hdrTex_);
    volTex_ = makeTex(GL_RGBA16F, std::max(1, rw_ / 2), std::max(1, rh_ / 2), GL_RGBA, GL_FLOAT, GL_LINEAR);
    volFbo_ = makeFbo(volTex_);
    fogTex_ = makeTex(GL_R11F_G11F_B10F, rw_, rh_, GL_RGB, GL_FLOAT, GL_LINEAR);
    fogFbo_ = makeFbo(fogTex_);
    for (int i = 0; i < kBloomMips; ++i) {
        bloomW_[i] = std::max(1, rw_ >> (i + 1));
        bloomH_[i] = std::max(1, rh_ >> (i + 1));
        bloomTex_[i] = makeTex(GL_R11F_G11F_B10F, bloomW_[i], bloomH_[i], GL_RGB, GL_FLOAT, GL_LINEAR);
        bloomFbo_[i] = makeFbo(bloomTex_[i]);
    }
    ldrTex_ = makeTex(GL_RGBA8, rw_, rh_, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);
    ldrFbo_ = makeFbo(ldrTex_);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::resize(int width, int height) {
    if (width <= 0 || height <= 0 || (width == w_ && height == h_)) return;
    w_ = width;
    h_ = height;
    destroyTargets();
    createTargets();
}

void Renderer::applySettings(const GraphicsSettings& gs) {
    bool targets = gs.msaa != gs_.msaa || gs.renderScale != gs_.renderScale;
    bool shadow = gs.shadowRes != shadowRes_ || gs.shadowCascades != shadowLayers_;
    bool env = gs.clouds != gs_.clouds || gs.cloudCoverage != gs_.cloudCoverage;
    gs_ = gs;
    if (targets) {
        destroyTargets();
        createTargets();
    }
    if (shadow) createShadowMap();
    if (env) envDirty_ = true;
}

// Sol pela hora do dia: nasce a leste, passa inclinado (latitude ~35°) e se põe a oeste.
void Renderer::updateSun() {
    float theta = (gs_.timeOfDay - 6.25f) / 12.5f * (float)M_PI;
    const float tilt = glm::radians(35.0f);
    glm::vec3 d(std::cos(theta), std::sin(theta) * std::cos(tilt), -std::sin(theta) * std::sin(tilt));
    float az = glm::radians(gs_.sunAzimuth);
    d = glm::vec3(d.x * std::cos(az) + d.z * std::sin(az), d.y, -d.x * std::sin(az) + d.z * std::cos(az));
    sunDir_ = glm::normalize(d);
    sunColor_ = 4.6f * sunTransmittance(sunDir_);
    // as nuvens (1.8 km) ainda pegam sol quando ele já está se pondo no chão: atravessa menos ar
    glm::vec3 up = glm::normalize(glm::vec3(sunDir_.x, std::max(sunDir_.y, -0.02f) + 0.045f, sunDir_.z));
    cloudSunColor_ = 4.6f * glm::mix(sunTransmittance(up), glm::vec3(1.0f, 0.75f, 0.6f), 0.05f);
}

void Renderer::fullscreen() {
    glBindVertexArray(emptyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

// Recalcula a atmosfera (quando o sol muda) e a luz do céu com nuvens (algumas vezes por segundo).
void Renderer::updateEnvironment(float time) {
    bool sunMoved = glm::length(sunDir_ - skyCubeSun_) > 1e-4f;
    if (!sunMoved && !envDirty_ && std::fabs(time - envTime_) < 0.25f) return;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glBindFramebuffer(GL_FRAMEBUFFER, captureFbo_);
    auto renderFaces = [&](Shader& sh, GLuint tex, int res, int level) {
        glViewport(0, 0, res, res);
        for (int f = 0; f < 6; ++f) {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, tex, level);
            sh.set("uInvViewProj", faceInvViewProj(f));
            fullscreen();
        }
    };

    if (sunMoved) {
        skyAtmo_.use();
        skyAtmo_.set("uSunDir", sunDir_);
        skyAtmo_.set("uSkyIntensity", skyIntensity_);
        renderFaces(skyAtmo_, skyCube_, kSkyRes, 0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, skyCube_);
        glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
        // média do céu (para a fumaça dos tiros)
        float px[4];
        glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_Y, (int)std::log2((float)kSkyRes), GL_RGBA, GL_FLOAT, px);
        skyAmbient_ = glm::vec3(px[0], px[1], px[2]);
        skyCubeSun_ = sunDir_;
    }

    // ambiente com nuvens e chão
    glActiveTexture(GL_TEXTURE0 + kUnitSkyCube);
    glBindTexture(GL_TEXTURE_CUBE_MAP, skyCube_);
    sky_.use();
    sky_.set("uCapture", 1);
    sky_.set("uSunDir", sunDir_);
    sky_.set("uSunColor", sunColor_);
    sky_.set("uCloudSunColor", cloudSunColor_);
    sky_.set("uTime", time);
    sky_.set("uClouds", gs_.clouds ? 1 : 0);
    sky_.set("uCloudCoverage", gs_.cloudCoverage);
    renderFaces(sky_, envRaw_, kEnvRes, 0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envRaw_);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envRaw_);
    prefilter_.use();
    prefilter_.set("uSrc", 0);
    prefilter_.set("uSrcRes", (float)kEnvRes);
    for (int m = 0; m < kEnvMips; ++m) {
        prefilter_.set("uRoughness", (float)m / (kEnvMips - 1));
        renderFaces(prefilter_, envPref_, kEnvRes >> m, m);
    }
    irradiance_.use();
    irradiance_.set("uSrc", 0);
    renderFaces(irradiance_, irrCube_, kIrrRes, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    envTime_ = time;
    envDirty_ = false;
}

// Cascatas: divisão "prática" (mistura log/linear), cada uma cobrindo a esfera do pedaço do frustum,
// com o centro preso à grade de texels para a sombra não tremer quando a câmera anda.
void Renderer::computeCascades(const RenderScene& s) {
    const int n = shadowLayers_;
    const float nearD = 0.1f, farD = gs_.shadowDistance, lambda = 0.85f;
    glm::mat4 inv = glm::inverse(s.view);
    glm::vec3 right(inv[0]), up(inv[1]), fwd = -glm::vec3(inv[2]);
    float tanY = 1.0f / s.proj[1][1], tanX = 1.0f / s.proj[0][0];
    // sol rente/abaixo do horizonte: segura a luz em ~2° para as sombras não ficarem infinitas
    glm::vec3 L = sunDir_;
    if (L.y < 0.035f) L = glm::normalize(glm::vec3(L.x, 0.0f, L.z) + glm::vec3(0.0f, 0.035f, 0.0f));
    glm::vec3 lightUp = std::fabs(L.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
    glm::mat4 lv = glm::lookAt(glm::vec3(0.0f), -L, lightUp);
    float prev = nearD;
    for (int i = 0; i < n; ++i) {
        float k = (float)(i + 1) / n;
        float split = lambda * nearD * std::pow(farD / nearD, k) + (1.0f - lambda) * (nearD + (farD - nearD) * k);
        glm::vec3 corners[8];
        int c = 0;
        for (float d : {prev, split})
            for (float sx : {-1.0f, 1.0f})
                for (float sy : {-1.0f, 1.0f})
                    corners[c++] = s.camPos + fwd * d + right * (sx * d * tanX) + up * (sy * d * tanY);
        glm::vec3 center(0.0f);
        for (auto& p : corners) center += p / 8.0f;
        float radius = 0.0f;
        for (auto& p : corners) radius = std::max(radius, glm::length(p - center));
        radius = std::ceil(radius * 16.0f) / 16.0f;
        float texel = 2.0f * radius / shadowRes_;
        glm::vec4 cl = lv * glm::vec4(center, 1.0f);
        cl.x = std::floor(cl.x / texel) * texel;
        cl.y = std::floor(cl.y / texel) * texel;
        glm::mat4 lp = glm::ortho(cl.x - radius, cl.x + radius, cl.y - radius, cl.y + radius, -cl.z - radius - 80.0f,
                                  -cl.z + radius);
        cascadeVP_[i] = lp * lv;
        cascadeSplit_[i] = split;
        cascadeTexel_[i] = texel;
        prev = split;
    }
}

void Renderer::setLightingUniforms(Shader& sh, const RenderScene& s) {
    glm::mat4 inv = glm::inverse(s.view);
    sh.set("uSunDir", sunDir_);
    sh.set("uSunColor", sunColor_);
    sh.set("uCamPos", s.camPos);
    sh.set("uCamFwd", -glm::vec3(inv[2]));
    sh.set("uTime", s.time);
    sh.setMat4Array("uCascadeVP", cascadeVP_, shadowLayers_);
    glm::vec4 splits(1e9f), texel(0.01f);
    for (int i = 0; i < shadowLayers_; ++i) {
        splits[i] = cascadeSplit_[i];
        texel[i] = cascadeTexel_[i];
    }
    sh.set("uCascadeSplits", splits);
    sh.set("uCascadeTexel", texel);
    sh.set("uCascadeCount", shadowLayers_);
    sh.set("uShadowSoftness", gs_.shadowSoftness);
    sh.set("uShadowDistance", gs_.shadowDistance);
    sh.set("uEnvMaxLod", (float)(kEnvMips - 1));
    sh.set("uUseSSAO", gs_.ssao ? 1 : 0);
    sh.set("uRenderSize", glm::vec2((float)rw_, (float)rh_));
    sh.set("uCharPos", s.characterPos);
    sh.set("uClouds", gs_.clouds ? 1 : 0);
    sh.set("uCloudCoverage", gs_.cloudCoverage);
    sh.set("uCloudShadows", gs_.cloudShadows ? 1 : 0);
    const Weapon* fw = s.weapon && s.weapon->flashTime > 0.0f                 ? s.weapon
                       : s.enemyWeapon && s.enemyWeapon->flashTime > 0.0f ? s.enemyWeapon
                                                                           : nullptr;
    bool flash = fw != nullptr;
    sh.set("uFlashPos", flash ? fw->flashPos : glm::vec3(0.0f, -1000.0f, 0.0f));
    sh.set("uFlashColor", flash ? glm::vec3(9.0f, 6.0f, 2.8f) : glm::vec3(0.0f));
    // névoa de altura
    sh.set("uFogDensity", 0.0022f * gs_.fogDensity);
    sh.set("uFogFalloff", 0.06f);
}

void Renderer::drawWorld(Shader& sh, const RenderScene& s, Pass pass) {
    bool color = pass == Pass::Color;
    if (pass != Pass::Shadow) {
        if (color) sh.set("uMaterial", 0);
        sh.set("uModel", glm::mat4(1.0f));
        plane_.draw();
    }
    for (const Box& b : s.world->boxes) {
        if (b.hidden) continue;
        glm::mat4 m = glm::translate(glm::mat4(1.0f), (b.min + b.max) * 0.5f) * glm::scale(glm::mat4(1.0f), b.max - b.min);
        sh.set("uModel", m);
        if (color) {
            sh.set("uMaterial", 1);
            sh.set("uColor", b.color);
            sh.set("uBoxMin", b.min);
            sh.set("uBoxMax", b.max);
        }
        cube_.draw();
    }
    for (const Ball& b : s.world->balls) {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), b.pos) * glm::mat4_cast(b.rot) * glm::scale(glm::mat4(1.0f), glm::vec3(b.radius));
        sh.set("uModel", m);
        if (color) { sh.set("uMaterial", 2); sh.set("uColor", b.color); }
        sphere_.draw();
    }
    for (const RenderScene::Panel& p : s.panels) {
        sh.set("uModel", p.model);
        if (color) {
            sh.set("uMaterial", p.material);
            sh.set("uColor", p.color);
        }
        cube_.draw();
    }
    if (color) {
        // cápsulas de latão (pequenas demais para fazer sombra)
        sh.set("uMaterial", 3);
        sh.set("uColor", glm::vec3(0.95f, 0.72f, 0.33f));
        for (const Weapon* w : {s.weapon, s.enemyWeapon}) {
            if (!w) continue;
            for (const Shell& sh2 : w->shells) {
                glm::mat4 m = glm::translate(glm::mat4(1.0f), sh2.pos) * glm::mat4_cast(sh2.rot) *
                              glm::scale(glm::mat4(1.0f), glm::vec3(0.022f, 0.009f, 0.009f) * sh2.scale);
                sh.set("uModel", m);
                cube_.draw();
            }
        }
    }
}

void Renderer::drawSkinned(Shader& sh, const CharacterModel& m, const glm::mat4& model, const glm::mat4* bones,
                           bool material) {
    sh.set("uModel", model);
    sh.setMat4Array("uBones", bones, BONE_COUNT);
    if (!material) {
        m.draw();
        return;
    }
    sh.set("uBaseTex", 0);
    sh.set("uMRTex", 1);
    sh.set("uNormalTex", 2);
    sh.set("uEmissiveTex", 3);
    for (size_t i = 0; i < m.parts.size(); ++i) {
        const CharacterModel::Material& mat = m.materials[m.parts[i].material];
        sh.set("uMetallic", mat.metallicFactor);
        sh.set("uRoughness", mat.roughnessFactor);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, mat.texBase);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, mat.texMR);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, mat.texNormal);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, mat.texEmissive);
        sh.set("uEmissive", mat.emissiveFactor);
        sh.set("uUseAO", mat.useOcclusion ? 1 : 0);
        m.drawPart(i);
    }
}

// Mundo + personagem + arma com o par de shaders do passo.
void Renderer::drawScene(Shader& worldSh, Shader& skinSh, const RenderScene& s, Pass pass, const glm::mat4& vp) {
    bool color = pass == Pass::Color;
    if (color) {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
    }
    worldSh.use();
    worldSh.set("uViewProj", vp);
    if (color) setLightingUniforms(worldSh, s);
    drawWorld(worldSh, s, pass);
    glDisable(GL_CULL_FACE); // materiais double-sided
    skinSh.use();
    skinSh.set("uViewProj", vp);
    if (color) {
        setLightingUniforms(skinSh, s);
        skinSh.set("uDebug", s.debugWeights ? 1 : 0);
    }
    if (color) skinSh.set("uDebug", 0);
    for (const RenderScene::Prop& p : s.props) drawSkinned(skinSh, *p.mesh, p.model, identityBones_, color);
    if (color) skinSh.set("uDebug", s.debugWeights ? 1 : 0);
    drawSkinned(skinSh, *s.character, s.characterModel, s.bones, color);
    if (color) skinSh.set("uDebug", 0);
    for (const RenderScene::Actor& a : s.actors) drawSkinned(skinSh, *a.mesh, a.model, a.bones, color);
    for (const RenderScene::Prop& g : s.guns) drawSkinned(skinSh, *g.mesh, g.model, identityBones_, color);
}

void Renderer::drawFx(const std::vector<FxVertex>& v) {
    if (v.empty()) return;
    glBindVertexArray(fxVao_);
    glBindBuffer(GL_ARRAY_BUFFER, fxVbo_);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(FxVertex), v.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)v.size());
}

void Renderer::drawEffects(const RenderScene& s) {
    if (s.weapon) drawWeaponFx(s, *s.weapon);
    if (s.enemyWeapon) drawWeaponFx(s, *s.enemyWeapon);
}

void Renderer::drawWeaponFx(const RenderScene& s, const Weapon& w) {
    const glm::mat4 vp = s.proj * s.view;
    glm::mat4 inv = glm::inverse(s.view);
    glm::vec3 camRight(inv[0]), camUp(inv[1]);

    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);

    // 1) marcas de tiro: multiplicam a cor da superfície
    std::vector<FxVertex> verts;
    for (size_t i = 0; i < w.decals.size(); ++i) {
        const Decal& d = w.decals[i];
        glm::vec3 b = glm::cross(d.normal, d.tangent);
        pushQuad(verts, d.pos + d.normal * 0.002f, d.tangent * d.size, b * d.size,
                 glm::vec4(1, 1, 1, (float)(i % 17) / 17.0f), 0.0f, false);
    }
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-2.0f, -2.0f);
    glBlendFunc(GL_DST_COLOR, GL_ZERO);
    decal_.use();
    decal_.set("uViewProj", vp);
    drawFx(verts);
    glDisable(GL_POLYGON_OFFSET_FILL);

    // 2) fumaça e poeira (alpha), da mais distante para a mais próxima
    verts.clear();
    std::vector<const Particle*> smoke;
    for (const Particle& p : w.particles) if (p.smoke) smoke.push_back(&p);
    std::sort(smoke.begin(), smoke.end(), [&](const Particle* a, const Particle* b) {
        return glm::length(a->pos - s.camPos) > glm::length(b->pos - s.camPos);
    });
    glm::vec3 ambient = skyAmbient_ + sunColor_ * 0.12f;
    for (const Particle* p : smoke) {
        float t = p->age / p->life;
        float a = 0.35f * (1.0f - t) * std::min(1.0f, p->age * 20.0f);
        pushQuad(verts, p->pos, camRight * p->size, camUp * p->size, glm::vec4(p->color * ambient, a), 1.0f, false);
    }
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    fx_.use();
    fx_.set("uViewProj", vp);
    drawFx(verts);

    // 3) aditivos: faíscas, rastro da bala, clarão do cano
    verts.clear();
    for (const Particle& p : w.particles) {
        if (p.smoke) continue;
        float t = 1.0f - p.age / p.life;
        pushStreak(verts, p.pos - p.vel * 0.025f, p.pos, p.size, s.camPos, glm::vec4(p.color, t), 0.0f);
    }
    for (const Tracer& t : w.tracers) {
        float head = std::min(t.traveled, t.dist), tail = std::max(0.0f, t.traveled - 2.0f);
        if (head - tail < 0.01f) continue;
        pushStreak(verts, t.from + t.dir * tail, t.from + t.dir * head, 0.006f, s.camPos, glm::vec4(7.0f, 5.5f, 3.0f, 1.0f), 2.0f);
    }
    if (w.flashTime > 0.0f) {
        float k = w.flashTime / 0.05f;
        glm::vec3 rx = camRight * std::cos(w.flashAngle) + camUp * std::sin(w.flashAngle);
        glm::vec3 ry = glm::cross(glm::normalize(s.camPos - w.flashPos), rx);
        float size = 0.16f * (0.8f + 0.4f * k);
        pushQuad(verts, w.flashPos, rx * size, ry * size, glm::vec4(14.0f, 9.0f, 4.0f, k), 3.0f, false);
    }
    glBlendFunc(GL_ONE, GL_ONE);
    drawFx(verts);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
}

void Renderer::drawHud(const RenderScene& s) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    hud_.use();
    hud_.set("uScreen", glm::vec2((float)w_, (float)h_));
    glBindVertexArray(emptyVao_);
    if (s.crosshairAlpha > 0.01f) {
        hud_.set("uType", 0);
        hud_.set("uAlpha", s.crosshairAlpha);
        // mira acompanha o tamanho da janela (16 px numa janela de 900 de altura)
        float size = std::round(std::clamp(h_ / 900.0f * 16.0f, 10.0f, 40.0f));
        hud_.set("uRect", glm::vec4(std::round(w_ * 0.5f - size * 0.5f), std::round(h_ * 0.5f - size * 0.5f), size, size));
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
    if (s.promptAlpha > 0.01f) {
        glm::vec4 clip = s.proj * s.view * glm::vec4(s.promptWorld, 1.0f);
        if (clip.w > 0.05f) {
            glm::vec2 ndc = glm::vec2(clip) / clip.w;
            float size = std::round(std::max(30.0f, h_ * 0.042f));
            glm::vec2 px((ndc.x * 0.5f + 0.5f) * w_, (0.5f - ndc.y * 0.5f) * h_);
            hud_.set("uType", 1);
            hud_.set("uAlpha", s.promptAlpha);
            hud_.set("uRect", glm::vec4(std::round(px.x - size * 0.5f), std::round(px.y - size * 0.5f), size, size));
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }
    }
    glDisable(GL_BLEND);
}

void Renderer::render(const RenderScene& s, float dt, GLuint targetFbo) {
    const glm::mat4 vp = s.proj * s.view;
    const glm::mat4 invVP = glm::inverse(vp);
    const float skyFogDist = 700.0f;
    g_prof.mark("inicio");
    updateSun();
    updateEnvironment(s.time);
    g_prof.mark("ceu");

    // --- 1) sombras do sol em cascata
    computeCascades(s);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glViewport(0, 0, shadowRes_, shadowRes_);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_CULL_FACE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.6f, 2.0f);
    for (int i = 0; i < shadowLayers_; ++i) {
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, shadowTex_, 0, i);
        glClear(GL_DEPTH_BUFFER_BIT);
        drawScene(worldDepth_, skinnedDepth_, s, Pass::Shadow, cascadeVP_[i]);
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    g_prof.mark("sombras");

    // --- 2) pré-passo: normais e profundidade (SSAO, névoa e luz volumétrica)
    glBindFramebuffer(GL_FRAMEBUFFER, preFbo_);
    glViewport(0, 0, rw_, rh_);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_CULL_FACE);
    drawScene(worldPre_, skinnedPre_, s, Pass::Prepass, vp);
    glDisable(GL_DEPTH_TEST);
    g_prof.mark("prepasso");

    // --- 3) SSAO + desfoque que respeita bordas
    glm::mat4 invProj = glm::inverse(s.proj);
    if (gs_.ssao) {
        static glm::vec3 kernel[16];
        static bool kernelInit = false;
        if (!kernelInit) {
            uint32_t seed = 12345u;
            auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.0f; };
            for (int i = 0; i < 16; ++i) {
                glm::vec3 v(rnd() * 2.0f - 1.0f, rnd() * 2.0f - 1.0f, rnd() * 0.9f + 0.1f);
                v = glm::normalize(v) * rnd();
                float k = (float)i / 16.0f;
                kernel[i] = v * (0.1f + 0.9f * k * k);
            }
            kernelInit = true;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, aoFbo_);
        glViewport(0, 0, std::max(1, rw_ / 2), std::max(1, rh_ / 2));
        ssao_.use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, preDepth_);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, preNormal_);
        ssao_.set("uDepth", 0);
        ssao_.set("uNormal", 1);
        ssao_.set("uProj", s.proj);
        ssao_.set("uInvProj", invProj);
        ssao_.set("uView", s.view);
        glUniform3fv(glGetUniformLocation(ssao_.id(), "uKernel"), 16, &kernel[0].x);
        ssao_.set("uRadius", gs_.ssaoRadius);
        ssao_.set("uIntensity", gs_.ssaoIntensity);
        fullscreen();
        glBindFramebuffer(GL_FRAMEBUFFER, aoBlurFbo_);
        ssaoBlur_.use();
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, aoTex_);
        ssaoBlur_.set("uDepth", 0);
        ssaoBlur_.set("uAO", 1);
        ssaoBlur_.set("uProj", s.proj);
        fullscreen();
    }

    g_prof.mark("ssao");
    // texturas de iluminação usadas daqui em diante
    glActiveTexture(GL_TEXTURE0 + kUnitIrradiance);
    glBindTexture(GL_TEXTURE_CUBE_MAP, irrCube_);
    glActiveTexture(GL_TEXTURE0 + kUnitEnv);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envPref_);
    glActiveTexture(GL_TEXTURE0 + kUnitAO);
    glBindTexture(GL_TEXTURE_2D, aoBlurTex_);
    glActiveTexture(GL_TEXTURE0 + kUnitShadow);
    glBindTexture(GL_TEXTURE_2D_ARRAY, shadowTex_);
    glActiveTexture(GL_TEXTURE0 + kUnitSkyCube);
    glBindTexture(GL_TEXTURE_CUBE_MAP, skyCube_);

    // --- 4) cena em HDR com MSAA
    glBindFramebuffer(GL_FRAMEBUFFER, msFbo_);
    glViewport(0, 0, rw_, rh_);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    drawScene(world_, skinned_, s, Pass::Color, vp);

    g_prof.mark("cena");
    // céu só onde não há geometria
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    sky_.use();
    setLightingUniforms(sky_, s);
    sky_.set("uCapture", 0);
    sky_.set("uCloudSunColor", cloudSunColor_);
    sky_.set("uInvViewProj", invVP);
    sky_.set("uSunDisk", 45.0f);
    fullscreen();
    glDepthMask(GL_TRUE);

    // efeitos transparentes por cima de tudo (testam profundidade, não escrevem)
    drawEffects(s);
    glDepthFunc(GL_LESS);
    glDisable(GL_DEPTH_TEST);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, msFbo_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, hdrFbo_);
    glBlitFramebuffer(0, 0, rw_, rh_, 0, 0, rw_, rh_, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    g_prof.mark("ceu+fx");

    // --- 5) luz volumétrica (meia resolução) e névoa
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, preDepth_);
    if (gs_.volumetric) {
        glBindFramebuffer(GL_FRAMEBUFFER, volFbo_);
        glViewport(0, 0, std::max(1, rw_ / 2), std::max(1, rh_ / 2));
        volumetric_.use();
        setLightingUniforms(volumetric_, s);
        volumetric_.set("uDepth", 0);
        volumetric_.set("uInvViewProj", invVP);
        volumetric_.set("uSteps", gs_.volumetricSteps);
        volumetric_.set("uVolIntensity", gs_.volumetricIntensity);
        volumetric_.set("uMarchDist", std::min(gs_.shadowDistance, 80.0f));
        volumetric_.set("uSkyFogDist", skyFogDist);
        fullscreen();
    }
    g_prof.mark("volumetrica");
    glBindFramebuffer(GL_FRAMEBUFFER, fogFbo_);
    glViewport(0, 0, rw_, rh_);
    fogComp_.use();
    setLightingUniforms(fogComp_, s);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, hdrTex_);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, volTex_);
    fogComp_.set("uDepth", 0);
    fogComp_.set("uScene", 1);
    fogComp_.set("uVol", 2);
    fogComp_.set("uUseVol", gs_.volumetric ? 1 : 0);
    fogComp_.set("uInvViewProj", invVP);
    fogComp_.set("uProj", s.proj);
    fogComp_.set("uSkyFogDist", skyFogDist);
    fullscreen();

    g_prof.mark("nevoa");
    // --- 6) bloom: reduz em 6 níveis e amplia somando
    if (gs_.bloom) {
        bloomDown_.use();
        bloomDown_.set("uSrc", 0);
        glActiveTexture(GL_TEXTURE0);
        for (int i = 0; i < kBloomMips; ++i) {
            glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo_[i]);
            glViewport(0, 0, bloomW_[i], bloomH_[i]);
            glBindTexture(GL_TEXTURE_2D, i == 0 ? fogTex_ : bloomTex_[i - 1]);
            bloomDown_.set("uFirst", i == 0 ? 1 : 0);
            fullscreen();
        }
        bloomUp_.use();
        bloomUp_.set("uSrc", 0);
        bloomUp_.set("uRadius", 1.0f);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        for (int i = kBloomMips - 1; i > 0; --i) {
            glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo_[i - 1]);
            glViewport(0, 0, bloomW_[i - 1], bloomH_[i - 1]);
            glBindTexture(GL_TEXTURE_2D, bloomTex_[i]);
            fullscreen();
        }
        glDisable(GL_BLEND);
    }

    // --- 7) exposição automática
    if (gs_.autoExposure) {
        glBindFramebuffer(GL_FRAMEBUFFER, lumFbo_);
        glViewport(0, 0, kLumRes, kLumRes);
        lum_.use();
        lum_.set("uSrc", 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, fogTex_);
        fullscreen();
        glBindTexture(GL_TEXTURE_2D, lumTex_);
        glGenerateMipmap(GL_TEXTURE_2D);
        int next = 1 - adaptCur_;
        glBindFramebuffer(GL_FRAMEBUFFER, adaptFbo_[next]);
        glViewport(0, 0, 1, 1);
        adapt_.use();
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, adaptTex_[adaptCur_]);
        adapt_.set("uLum", 0);
        adapt_.set("uPrev", 1);
        adapt_.set("uMaxLod", std::log2((float)kLumRes));
        adapt_.set("uDt", dt);
        adapt_.set("uReset", exposureReset_ ? 1 : 0);
        fullscreen();
        adaptCur_ = next;
        exposureReset_ = false;
    } else {
        exposureReset_ = true;
    }

    g_prof.mark("bloom+exp");
    // --- 8) pós-processamento -> LDR
    glBindFramebuffer(GL_FRAMEBUFFER, ldrFbo_);
    glViewport(0, 0, rw_, rh_);
    post_.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fogTex_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, bloomTex_[0]);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, adaptTex_[adaptCur_]);
    post_.set("uHDR", 0);
    post_.set("uBloom", 1);
    post_.set("uAdapt", 2);
    post_.set("uAutoExposure", gs_.autoExposure ? 1 : 0);
    post_.set("uExposureEV", gs_.exposure);
    post_.set("uBloomOn", gs_.bloom ? 1 : 0);
    post_.set("uBloomStrength", 0.045f * gs_.bloomIntensity);
    post_.set("uChromatic", gs_.chromaticAberration);
    post_.set("uContrast", gs_.contrast);
    post_.set("uSaturation", gs_.saturation);
    post_.set("uTemperature", gs_.temperature);
    post_.set("uVignette", gs_.vignette);
    post_.set("uGrain", gs_.filmGrain);
    post_.set("uTime", s.time);
    fullscreen();

    g_prof.mark("post");
    // --- 9) FXAA + escala para a janela, depois o HUD
    glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
    glViewport(0, 0, w_, h_);
    fxaa_.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ldrTex_);
    fxaa_.set("uSrc", 0);
    fxaa_.set("uFxaa", gs_.fxaa ? 1 : 0);
    fullscreen();
    drawHud(s);
    glEnable(GL_DEPTH_TEST);
    g_prof.mark("pos+fxaa");
    g_prof.endFrame();
}
