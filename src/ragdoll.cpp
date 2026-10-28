#define GLM_ENABLE_EXPERIMENTAL
#include "ragdoll.h"

#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace {

// partículas: articulações do corpo + topo da cabeça, ponta das mãos e dos pés
enum P {
    HIP, SPN, CHS, NCK, HTOP,
    SHL, ELL, WRL, HTL,
    SHR, ELR, WRR, HTR,
    THL, KNL, ANL, TOL,
    THR, KNR, ANR, TOR,
    COUNT
};
constexpr float kStep = 1.0f / 120.0f;
constexpr int kIters = 10;

struct Src { Bone bone; bool tail; float r; };
const Src kSrc[COUNT] = {
    {HIPS, false, 0.13f},  {SPINE, false, 0.12f}, {CHEST, false, 0.14f}, {NECK, false, 0.07f}, {HEAD, true, 0.11f},
    {UARM_L, false, 0.07f}, {FARM_L, false, 0.05f}, {HAND_L, false, 0.04f}, {HAND_L, true, 0.04f},
    {UARM_R, false, 0.07f}, {FARM_R, false, 0.05f}, {HAND_R, false, 0.04f}, {HAND_R, true, 0.04f},
    {THIGH_L, false, 0.09f}, {SHIN_L, false, 0.06f}, {FOOT_L, false, 0.05f}, {FOOT_L, true, 0.04f},
    {THIGH_R, false, 0.09f}, {SHIN_R, false, 0.06f}, {FOOT_R, false, 0.05f}, {FOOT_R, true, 0.04f},
};

glm::vec3 pointOf(const Skeleton& sk, const glm::mat4 g[BONE_COUNT], const Src& s) {
    glm::vec3 local = s.tail ? sk.tail[s.bone] - sk.joint[s.bone] : glm::vec3(0.0f);
    return glm::vec3(g[s.bone] * glm::vec4(local, 1.0f));
}

glm::quat frame(glm::vec3 x, glm::vec3 y) {
    x = glm::normalize(x);
    y = glm::normalize(y - x * glm::dot(y, x));
    return glm::normalize(glm::quat_cast(glm::mat3(x, y, glm::cross(x, y))));
}

// Rotação do osso: a do pai, girada para o osso apontar de 'd0' (neutro) para 'd' (agora).
glm::quat swing(const glm::quat& parent, const glm::vec3& d0, const glm::vec3& d) {
    glm::vec3 a = glm::normalize(parent * d0), b = glm::normalize(d);
    return glm::normalize(glm::rotation(a, b) * parent);
}

} // namespace

void Ragdoll::addStick(int a, int b, bool minOnly, float scale) {
    sticks_.push_back({a, b, 0.0f, minOnly, scale});
}

void Ragdoll::start(const Skeleton& sk, const glm::mat4 globals[BONE_COUNT], const glm::mat4& model, const glm::vec3& vel,
                    const glm::vec3& impulse, const glm::vec3& hitPoint) {
    active_ = true;
    time_ = 0.0f;
    model_ = model;
    invModel_ = glm::inverse(model);
    parts_.assign(COUNT, {});
    rest_.assign(COUNT, glm::vec3(0.0f));
    glm::mat4 neutral[BONE_COUNT];
    computeGlobals(sk, Pose(), neutral);
    for (int i = 0; i < COUNT; ++i) {
        glm::vec3 p = glm::vec3(model * glm::vec4(pointOf(sk, globals, kSrc[i]), 1.0f));
        float k = std::exp(-glm::length(p - hitPoint) / 0.35f); // o tiro empurra mais perto de onde pegou
        glm::vec3 v = vel + impulse * k;
        parts_[i] = {p, p - v * kStep, kSrc[i].r};
        rest_[i] = pointOf(sk, neutral, kSrc[i]);
    }

    sticks_.clear();
    // ossos
    const int chain[][2] = {{HIP, SPN}, {SPN, CHS}, {CHS, NCK}, {NCK, HTOP}, {CHS, SHL}, {CHS, SHR}, {SHL, ELL},
                            {ELL, WRL}, {WRL, HTL}, {SHR, ELR}, {ELR, WRR}, {WRR, HTR}, {HIP, THL}, {HIP, THR},
                            {THL, KNL}, {KNL, ANL}, {ANL, TOL}, {THR, KNR}, {KNR, ANR}, {ANR, TOR}};
    for (auto& c : chain) addStick(c[0], c[1]);
    // travas: tronco e quadril quase rígidos, cabeça presa ao peito, pés e punhos firmes
    const int brace[][2] = {{SHL, SHR}, {THL, THR}, {HIP, CHS}, {SPN, SHL}, {SPN, SHR}, {THL, SPN}, {THR, SPN},
                            {THL, CHS}, {THR, CHS}, {HIP, SHL}, {HIP, SHR}, {SHL, NCK}, {SHR, NCK}, {CHS, HTOP},
                            {SHL, HTOP}, {SHR, HTOP}, {KNL, TOL}, {KNR, TOR}, {ELL, HTL}, {ELR, HTR}};
    for (auto& c : brace) addStick(c[0], c[1]);
    // limites: joelho e cotovelo não dobram até fechar
    addStick(THL, ANL, true, 0.55f);
    addStick(THR, ANR, true, 0.55f);
    addStick(SHL, WRL, true, 0.45f);
    addStick(SHR, WRR, true, 0.45f);
    // comprimentos da pose neutra (os ossos de verdade), não da pose em que morreu
    for (Stick& st : sticks_) st.len = glm::length(rest_[st.a] - rest_[st.b]) * st.scale;
}

void Ragdoll::step(const World& w, float dt) {
    if (!active_) return;
    time_ += dt;
    const glm::vec3 g(0.0f, -kGravity * 0.75f, 0.0f);
    int n = std::min(12, std::max(1, (int)std::round(dt / kStep)));
    for (int s = 0; s < n; ++s) {
        for (Particle& p : parts_) {
            glm::vec3 v = (p.p - p.prev) * 0.995f;
            p.prev = p.p;
            p.p += v + g * kStep * kStep;
        }
        for (int it = 0; it < kIters; ++it) {
            for (const Stick& st : sticks_) {
                Particle &a = parts_[st.a], &b = parts_[st.b];
                glm::vec3 d = b.p - a.p;
                float len = glm::length(d);
                if (len < 1e-6f || (st.minOnly && len >= st.len)) continue;
                glm::vec3 corr = d * (0.5f * (len - st.len) / len);
                a.p += corr;
                b.p -= corr;
            }
            // colisão: chão e caixas (empurra para fora e segura com atrito)
            for (Particle& p : parts_) {
                bool hit = false;
                if (p.p.y - p.r < w.groundY) { p.p.y = w.groundY + p.r; hit = true; }
                for (const Box& b : w.boxes) {
                    if (!b.solid) continue;
                    glm::vec3 q = glm::clamp(p.p, b.min, b.max);
                    glm::vec3 d = p.p - q;
                    float dl = glm::length(d);
                    if (dl >= p.r) continue;
                    if (dl > 1e-5f) {
                        p.p = q + d / dl * p.r;
                    } else { // centro dentro da caixa: sai pela face mais próxima
                        glm::vec3 lo = p.p - b.min, hi = b.max - p.p;
                        float best = lo.x; int axis = 0; float sign = -1.0f;
                        for (int a = 0; a < 3; ++a) {
                            if (lo[a] < best) { best = lo[a]; axis = a; sign = -1.0f; }
                            if (hi[a] < best) { best = hi[a]; axis = a; sign = 1.0f; }
                        }
                        p.p[axis] = sign > 0.0f ? b.max[axis] + p.r : b.min[axis] - p.r;
                    }
                    hit = true;
                }
                if (hit) p.prev = glm::mix(p.prev, p.p, 0.35f); // atrito
            }
        }
    }
}

void Ragdoll::apply(const Skeleton& sk, Pose& pose, glm::mat4& model) const {
    if (!active_) return;
    model = model_;
    glm::vec3 P[COUNT];
    for (int i = 0; i < COUNT; ++i) P[i] = glm::vec3(invModel_ * glm::vec4(parts_[i].p, 1.0f));
    const std::vector<glm::vec3>& R0 = rest_;

    glm::quat rot[BONE_COUNT];
    rot[HIPS] = frame(P[THL] - P[THR], P[SPN] - P[HIP]) * glm::inverse(frame(R0[THL] - R0[THR], R0[SPN] - R0[HIP]));
    rot[CHEST] = frame(P[SHL] - P[SHR], P[NCK] - P[CHS]) * glm::inverse(frame(R0[SHL] - R0[SHR], R0[NCK] - R0[CHS]));
    rot[SPINE] = glm::slerp(rot[HIPS], rot[CHEST], 0.5f);
    rot[NECK] = swing(rot[CHEST], R0[HTOP] - R0[NCK], P[HTOP] - P[NCK]);
    rot[HEAD] = rot[NECK];
    rot[CLAV_L] = rot[CLAV_R] = rot[CHEST];
    auto limb = [&](Bone b, Bone parent, int from, int to) { rot[b] = swing(rot[parent], R0[to] - R0[from], P[to] - P[from]); };
    limb(UARM_L, CLAV_L, SHL, ELL);
    limb(FARM_L, UARM_L, ELL, WRL);
    limb(HAND_L, FARM_L, WRL, HTL);
    limb(UARM_R, CLAV_R, SHR, ELR);
    limb(FARM_R, UARM_R, ELR, WRR);
    limb(HAND_R, FARM_R, WRR, HTR);
    limb(THIGH_L, HIPS, THL, KNL);
    limb(SHIN_L, THIGH_L, KNL, ANL);
    limb(FOOT_L, SHIN_L, ANL, TOL);
    limb(THIGH_R, HIPS, THR, KNR);
    limb(SHIN_R, THIGH_R, KNR, ANR);
    limb(FOOT_R, SHIN_R, ANR, TOR);

    // rotações globais -> locais (os dedos continuam relativos à mão)
    for (int b = 0; b < kBodyBones; ++b) {
        int par = sk.parent[b];
        pose.rot[b] = par < 0 ? rot[b] : glm::normalize(glm::inverse(rot[par]) * rot[b]);
    }
    pose.rootOffset = P[HIP] - sk.joint[HIPS];
}
