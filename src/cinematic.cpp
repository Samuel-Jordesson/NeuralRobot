#include "cinematic.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
float ss(float a, float b, float t) { return glm::smoothstep(a, b, t); }
glm::quat ax(float a, const glm::vec3& v) { return glm::angleAxis(a, v); }
const glm::vec3 X(1, 0, 0), Y(0, 1, 0), Z(0, 0, 1);

// Lugares no espaço do robô (ele termina andando para +Z; deitado, a cabeça fica para -Z).
const glm::vec3 kCan(-1.45f, 0.0f, -1.35f);
const float kCanTop = 0.82f;

// tempos dos planos (s)
constexpr float kS2 = 3.6f, kS3 = 5.6f, kS4 = 8.6f, kS5 = 11.0f, kS6 = 13.5f, kS7 = 16.2f;
constexpr float kWake = 10.2f, kWalk = 16.4f;
} // namespace

void Cinematic::start(const glm::vec3& anchor, float yaw) {
    anchor_ = anchor;
    yaw_ = yaw;
    t_ = 0.0f;
    active_ = true;
}

bool Cinematic::update(float dt) {
    if (!active_) return false;
    t_ += dt;
    if (t_ >= kLength) active_ = false;
    return active_;
}

glm::vec3 Cinematic::toWorld(const glm::vec3& l) const {
    return anchor_ + glm::vec3(glm::rotate(glm::mat4(1.0f), yaw_, Y) * glm::vec4(l, 0.0f));
}

glm::mat4 Cinematic::robotModel() const { return glm::translate(glm::mat4(1.0f), anchor_) * glm::rotate(glm::mat4(1.0f), yaw_, Y); }

glm::mat4 Cinematic::canModel() const {
    return glm::translate(glm::mat4(1.0f), toWorld(kCan)) * glm::rotate(glm::mat4(1.0f), yaw_ + 0.6f, Y);
}

float Cinematic::fade() const { return std::max(1.0f - ss(0.0f, 1.4f, t_), ss(kLength - 0.6f, kLength, t_)); }
float Cinematic::bars() const { return 1.0f - ss(kLength - 1.4f, kLength - 0.2f, t_); }

glm::vec3 Cinematic::endPos() const {
    const float walked = 0.75f * std::max(0.0f, kLength - kWalk - 0.35f);
    return toWorld(glm::vec3(0.0f, 0.0f, walked));
}

// ---- rato: sai de dentro da lixeira, pula no chão, corre até a cabeça, sobe e foge
glm::vec3 Cinematic::mousePos(float& heading) const {
    const float t = t_;
    (void)0;
    const glm::vec3 inCan = kCan + glm::vec3(0.0f, kCanTop - 0.14f, 0.0f), top = kCan + glm::vec3(0.12f, kCanTop - 0.04f, 0.1f);
    const glm::vec3 land = kCan + glm::vec3(0.45f, 0.0f, 0.35f), nearHead = head_ + glm::vec3(-0.22f, -head_.y, -0.05f);
    const glm::vec3 onHead = head_ + glm::vec3(0.0f, 0.13f, 0.0f), away = glm::vec3(1.8f, 0.0f, -2.4f);
    auto dirOf = [](glm::vec3 d) { return std::atan2(d.x, d.z); };
    if (t < 4.2f) { heading = dirOf(top - inCan); return glm::mix(inCan, top, ss(3.9f, 4.2f, t)); }    // aparece na borda
    if (t < 4.7f) { heading = dirOf(top - inCan); return top; }                                        // olha em volta
    if (t < 5.2f) {                                                                                    // pula
        float k = ss(4.7f, 5.2f, t);
        glm::vec3 p = glm::mix(top, land, k);
        p.y = glm::mix(top.y, 0.0f, k) + 0.18f * std::sin(k * 3.14159f);
        heading = dirOf(land - top);
        return p;
    }
    if (t < 8.6f) { // corre (com paradinhas de rato) até a cabeça
        float k = ss(5.4f, 8.5f, t);
        k = k + 0.04f * std::sin(k * 25.0f) * (1.0f - k);
        heading = dirOf(nearHead - land) + 0.25f * std::sin(t * 9.0f);
        return glm::mix(land, nearHead, k);
    }
    if (t < 9.4f) { heading = dirOf(onHead - nearHead); return glm::mix(nearHead, onHead, ss(8.6f, 9.3f, t)); } // sobe
    if (t < kWake + 0.25f) { heading = 1.2f + 0.4f * std::sin(t * 3.0f); return onHead; }              // fareja em cima
    float k = ss(kWake + 0.25f, kWake + 1.2f, t);                                                       // foge
    glm::vec3 p = glm::mix(onHead, away, k);
    p.y = std::max(0.0f, onHead.y * (1.0f - k * 3.0f));
    heading = dirOf(away - onHead);
    return p;
}

bool Cinematic::mouseVisible() const { return t_ > 3.85f && t_ < kWake + 1.3f; }

glm::mat4 Cinematic::mouseModel(float scale) const {
    float h;
    glm::vec3 p = mousePos(h);
    // o arquivo do rato é comprido em X: gira para o focinho apontar para +Z do rato
    return glm::translate(glm::mat4(1.0f), toWorld(p)) * glm::rotate(glm::mat4(1.0f), yaw_ + h, Y) *
           glm::rotate(glm::mat4(1.0f), -1.5708f, Y) * glm::scale(glm::mat4(1.0f), glm::vec3(scale));
}

// ---- robô
Pose Cinematic::codePose(const Skeleton& sk, const MocapClip& walk) const {
    const float t = t_;
    const float hipY = sk.joint[HIPS].y;
    Pose p;
    if (t < kS6) {
        // deitado de costas -> senta (o quadril gira de -90° a -12°; as pernas ficam no chão)
        float a = -1.5708f;
        if (t > kS5) a = glm::mix(-1.5708f, -0.21f, ss(kS5 + 0.2f, kS6 - 0.3f, t));
        p.rot[HIPS] = ax(a, X);
        p.rot[THIGH_L] = p.rot[THIGH_R] = ax(-1.5708f - a, X);
        p.rootOffset.y = 0.13f - hipY;
        // respira dormindo
        p.rot[CHEST] = ax(0.03f * std::sin(t * 2.2f) * (1.0f - ss(kWake, kWake + 0.3f, t)), X);
        // acorda: a cabeça levanta de susto, olha para os lados
        float jolt = ss(kWake, kWake + 0.15f, t) * (1.0f - 0.5f * ss(kWake + 0.4f, kS5, t));
        p.rot[NECK] = ax(0.55f * jolt, X);
        p.rot[HEAD] = ax(0.35f * std::sin((t - kWake) * 5.0f) * ss(kWake + 0.2f, kWake + 0.5f, t) * (1.0f - ss(kS5, kS6, t)), Y);
        // sentado: braços apoiados atrás, depois no colo
        float sit = ss(kS5 + 0.2f, kS6 - 0.2f, t);
        p.rot[UARM_L] = ax(0.35f * sit, X) * ax(0.15f, Z);
        p.rot[UARM_R] = ax(0.35f * sit, X) * ax(-0.15f, Z);
        p.rot[FARM_L] = p.rot[FARM_R] = ax(-0.5f * sit, X);
        if (t < kWake) { // onde a cabeça está deitada (o rato sobe nela e o close mira nela)
            glm::mat4 g[BONE_COUNT];
            computeGlobals(sk, p, g);
            head_ = glm::vec3(g[HEAD] * glm::vec4(0.0f, 0.12f, 0.0f, 1.0f));
        }
        return p;
    }
    // em pé (corte de elipse): olha para as mãos sem entender
    Pose look;
    {
        float up = ss(kS6, kS6 + 0.9f, t);
        look.rot[UARM_L] = ax(-0.35f * up, X) * ax(-0.12f * up, Z);
        look.rot[UARM_R] = ax(-0.35f * up, X) * ax(0.12f * up, Z);
        look.rot[FARM_L] = ax(-1.05f * up, X) * ax(0.5f * std::sin(t * 1.3f) * up, Y);
        look.rot[FARM_R] = ax(-1.05f * up, X) * ax(-0.5f * std::sin(t * 1.1f + 1.0f) * up, Y);
        look.rot[HAND_L] = ax(0.6f * std::sin(t * 1.7f) * up, Z);
        look.rot[HAND_R] = ax(-0.6f * std::sin(t * 1.5f + 0.5f) * up, Z);
        look.rot[NECK] = ax(0.35f * up, X);
        // inclina a cabeça de um lado para o outro (confuso)
        look.rot[HEAD] = ax(0.35f * up, X) * ax(0.25f * std::sin(t * 1.4f) * up, Z);
        look.rot[SPINE] = ax(0.08f * up, X);
    }
    if (t < kWalk) return look;
    // anda para a frente (sai do olhar devagar)
    const float walked = 0.75f * std::max(0.0f, t - kWalk - 0.35f) + 0.5f * ss(kWalk, kWalk + 0.7f, t) * 0.0f;
    const float phase = std::fmod(walked / std::max(0.1f, walk.cycleLength), 1.0f);
    Pose w = walk.valid() ? walk.sample(phase) : Pose();
    w.rootOffset.x = 0.0f;
    w.rootOffset.z = 0.0f;
    Pose out = blendPose(look, w, ss(kWalk, kWalk + 0.7f, t));
    out.rootOffset.z += walked;
    return out;
}

// ---- câmera: um plano por trecho, corte seco entre eles
void Cinematic::codeCamera(float aspect, glm::mat4& view, glm::mat4& proj, glm::vec3& eye, float& focus) const {
    const float t = t_;
    glm::vec3 e, c;
    float mh;
    const glm::vec3 m = mousePos(mh);
    const float walked = 0.75f * std::max(0.0f, t - kWalk - 0.35f);
    if (t < kS2) { // 1) plano de estabelecimento: sala escura, robô no chão, lixeira; empurra devagar
        float k = ss(0.0f, kS2, t);
        e = glm::mix(glm::vec3(3.2f, 1.9f, 1.9f), glm::vec3(2.6f, 1.55f, 1.3f), k);
        c = glm::vec3(-0.6f, 0.25f, -0.6f);
    } else if (t < kS3) { // 2) detalhe da lixeira: o rato aparece na borda
        e = glm::vec3(-0.55f, 0.62f, -0.15f);
        c = kCan + glm::vec3(0.05f, kCanTop - 0.02f, 0.0f);
    } else if (t < kS4) { // 3) travelling baixo, rente ao chão, acompanhando o rato pelo lado
        e = m + glm::vec3(-0.3f, 0.12f, 0.8f); // pelo lado de fora (não passa por dentro do robô)
        c = m + glm::vec3(0.0f, 0.04f, 0.0f);
    } else if (t < kS5) { // 4) close do rosto: o rato sobe, o robô acorda
        e = head_ + glm::vec3(0.75f, 0.55f, -0.45f);
        c = head_ + glm::vec3(0.0f, 0.03f, 0.0f);
    } else if (t < kS6) { // 5) plano médio: senta
        float k = ss(kS5, kS6, t);
        e = glm::mix(glm::vec3(2.1f, 0.75f, 0.2f), glm::vec3(1.9f, 0.85f, 0.1f), k);
        c = glm::vec3(0.0f, 0.5f, -0.35f);
    } else if (t < kS7) { // 6) por cima do ombro: o que ele vê são as próprias mãos
        e = glm::vec3(0.55f, 1.95f, -1.0f);
        c = glm::vec3(0.0f, 1.15f, 0.35f);
    } else { // 7) plano geral de frente: ele anda até a câmera, o jogo começa
        float k = ss(kS7, kLength, t);
        e = glm::mix(glm::vec3(0.8f, 1.45f, 5.2f), glm::vec3(0.6f, 1.6f, 4.8f), k);
        c = glm::vec3(0.0f, 1.3f, walked);
    }
    eye = toWorld(e);
    const glm::vec3 target = toWorld(c);
    view = glm::lookAt(eye, target, Y);
    // lente de 50 mm (sensor de 36 mm): imagem 2,39:1 dentro das barras
    const float halfW = 18.0f / 50.0f;
    const float fovY = 2.0f * std::atan(halfW / std::max(0.1f, aspect));
    proj = glm::perspective(fovY, aspect, 0.03f, 300.0f);
    focus = glm::length(target - eye);
}

// ---- tocando pela animação editável
Pose Cinematic::robotPose(const Skeleton& sk, const MocapClip& walk) const {
    if (!hasClip_) return codePose(sk, walk);
    codePose(sk, walk); // mede a cabeça (o rato continua feito por código)
    return clip_.sample(std::min(t_, clip_.length));
}

void Cinematic::camera(float aspect, glm::mat4& view, glm::mat4& proj, glm::vec3& eye, float& focus) const {
    if (!hasClip_ || clip_.camKeys.empty()) { codeCamera(aspect, view, proj, eye, focus); return; }
    // chave de câmera: posição e giro no espaço do robô; entre duas chaves quase juntas = corte seco
    const auto& ks = clip_.camKeys;
    size_t j = 0;
    while (j < ks.size() && ks[j].t < t_) ++j;
    AnimClip::CamKey k;
    if (j == 0) k = ks.front();
    else if (j >= ks.size()) k = ks.back();
    else {
        const auto &a = ks[j - 1], &b = ks[j];
        float f = (t_ - a.t) / std::max(1e-4f, b.t - a.t);
        k = {t_, glm::mix(a.p, b.p, f), glm::normalize(glm::slerp(a.q, b.q, f)), a.focal + (b.focal - a.focal) * f,
             a.focus + (b.focus - a.focus) * f};
    }
    const glm::quat q = glm::angleAxis(yaw_, Y) * k.q;
    eye = toWorld(k.p);
    view = glm::inverse(glm::translate(glm::mat4(1.0f), eye) * glm::mat4_cast(q));
    const float halfW = 18.0f / std::max(5.0f, k.focal);
    proj = glm::perspective(2.0f * std::atan(halfW / std::max(0.1f, aspect)), aspect, 0.03f, 300.0f);
    focus = k.focus;
}

bool Cinematic::useClip(const std::string& path, const Skeleton& sk, const MocapClip& walk) {
    hasClip_ = false;
    AnimClip c;
    if (!c.load(path)) {
        // primeira vez: grava a cena feita por código como animação (15 chaves por segundo)
        c = AnimClip();
        c.name = "abertura";
        c.length = kLength;
        c.loop = false;
        c.linear = true;
        const float keep = t_;
        const float cuts[] = {kS2, kS3, kS4, kS5, kS6, kS7};
        std::vector<float> times;
        for (float t = 0.0f; t <= kLength + 1e-3f; t += 1.0f / 15.0f) times.push_back(t);
        for (float ct : cuts) { times.push_back(ct - 0.002f); times.push_back(ct); } // corte: duas chaves juntas
        std::sort(times.begin(), times.end());
        for (float t : times) {
            t_ = t;
            Pose p = codePose(sk, walk);
            for (int b = 0; b < BONE_COUNT; ++b) c.bones[b].push_back({t, p.rot[b]});
            c.root.push_back({t, p.rootOffset});
            glm::mat4 v, pr;
            glm::vec3 e;
            float focus;
            codeCamera(16.0f / 9.0f, v, pr, e, focus);
            // câmera no espaço do robô (o mesmo "palco" do editor de animação)
            glm::mat4 w = glm::inverse(v);
            glm::quat q = glm::inverse(glm::angleAxis(yaw_, Y)) * glm::normalize(glm::quat_cast(glm::mat3(w)));
            glm::vec3 local = glm::vec3(glm::rotate(glm::mat4(1.0f), -yaw_, Y) * glm::vec4(glm::vec3(w[3]) - anchor_, 0.0f));
            c.camKeys.push_back({t, local, q, 50.0f, focus});
        }
        t_ = keep;
        if (!c.save(path)) return false;
        std::printf("[cena] animação da abertura gravada em %s\n", path.c_str());
    }
    clip_ = c;
    hasClip_ = true;
    return true;
}
