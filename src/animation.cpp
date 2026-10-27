#include "animation.h"

#include <algorithm>
#include <cmath>
#include <cstring>

// Animações procedurais (keyframes descritos por funções da fase do passo).
// Eixos locais em repouso = eixos do modelo:
//   rotação X negativa = membro vai para frente, X positiva no joelho = dobra para trás,
//   X negativa no cotovelo = dobra para frente, Z positiva no braço esquerdo = abre para fora.

namespace {

const float TAU = 6.28318530718f;
const glm::vec3 AX(1, 0, 0), AY(0, 1, 0), AZ(0, 0, 1);

glm::quat rx(float a) { return glm::angleAxis(a, AX); }
glm::quat eul(float x, float y, float z) {
    return glm::angleAxis(y, AY) * glm::angleAxis(x, AX) * glm::angleAxis(z, AZ);
}

float wrap01(float x) { return x - std::floor(x); }
// "Sino" periódico centrado em c (fase 0..1) com largura w.
float bump(float s, float c, float w) {
    float d = s - c;
    d -= std::round(d);
    return std::exp(-(d * d) / (w * w));
}
float approach(float cur, float target, float rate, float dt) {
    return cur + (target - cur) * (1.0f - std::exp(-rate * dt));
}
float smooth01(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

struct Gait {
    float thighAmp, thighBias;
    float kneeBase, kneeStance, kneeSwing, swingPeak;
    float footPush;
    float armAmp, armBias, armOut, elbow, elbowAmp;
    float spineLean, hipYaw, chestYaw, hipRoll;
};

const Gait kWalkGait = {0.42f, -0.04f, 0.05f, 0.22f, 1.05f, 0.73f, 0.35f,
                        0.30f, 0.06f, 0.09f, 0.18f, 0.22f,
                        0.04f, 0.10f, 0.12f, 0.035f};
const Gait kRunGait = {0.60f, -0.18f, 0.12f, 0.50f, 1.85f, 0.70f, 0.45f,
                       0.55f, -0.10f, 0.22f, 1.35f, 0.30f,
                       0.20f, 0.16f, 0.22f, 0.05f};

// s = fase da perna (0 = calcanhar tocando o chão com a perna à frente)
void leg(Pose& p, Bone thigh, Bone shin, Bone foot, float s, const Gait& g, float roll) {
    // coxa atinge o ponto mais à frente um pouco antes do contato
    float th = g.thighBias - g.thighAmp * std::cos(TAU * (s + 0.05f));
    float kn = g.kneeBase + g.kneeStance * bump(s, 0.12f, 0.08f) + g.kneeSwing * bump(s, g.swingPeak, 0.15f);
    float ft = -(th + kn) * 0.9f + g.footPush * bump(s, 0.56f, 0.07f) - 0.12f * bump(s, 0.02f, 0.05f);
    p.rot[thigh] = eul(th, 0.0f, -roll);
    p.rot[shin] = rx(kn);
    p.rot[foot] = rx(ft);
}

// O braço balança em oposição à perna do mesmo lado.
void arm(Pose& p, Bone ua, Bone fa, Bone hand, float s, const Gait& g, float side) {
    float c = std::cos(TAU * s);
    float sw = g.armBias + g.armAmp * c;
    float el = -(g.elbow + g.elbowAmp * (0.5f - 0.5f * c));
    p.rot[ua] = eul(sw, 0.0f, side * g.armOut);
    p.rot[fa] = rx(el);
    p.rot[hand] = eul(0.0f, 0.0f, -side * 0.05f);
}

Pose gaitPose(float phase, const Gait& g) {
    Pose p;
    float c = std::cos(TAU * phase);
    float roll = g.hipRoll * std::sin(TAU * phase);
    p.rot[HIPS] = eul(0.0f, -g.hipYaw * c, roll);
    p.rot[SPINE] = eul(g.spineLean * 0.6f, g.hipYaw * c * 0.5f, -roll * 0.6f);
    p.rot[CHEST] = eul(g.spineLean * 0.4f, (g.chestYaw + g.hipYaw * 0.5f) * c, -roll * 0.4f);
    p.rot[NECK] = eul(-g.spineLean * 0.4f, -g.chestYaw * c * 0.5f, 0.0f);
    p.rot[HEAD] = eul(-g.spineLean * 0.4f, -g.chestYaw * c * 0.5f, 0.0f);

    leg(p, THIGH_L, SHIN_L, FOOT_L, phase, g, roll);
    leg(p, THIGH_R, SHIN_R, FOOT_R, wrap01(phase + 0.5f), g, roll);
    arm(p, UARM_L, FARM_L, HAND_L, phase, g, 1.0f);
    arm(p, UARM_R, FARM_R, HAND_R, wrap01(phase + 0.5f), g, -1.0f);
    return p;
}

Pose idlePose(float t) {
    Pose p;
    float br = std::sin(t * 1.7f);   // respiração
    float sway = std::sin(t * 0.55f); // transferência de peso
    p.rot[HIPS] = eul(0.0f, 0.03f * std::sin(t * 0.23f), 0.022f * sway);
    p.rot[SPINE] = eul(0.012f * br, 0.0f, -0.016f * sway);
    p.rot[CHEST] = eul(0.018f * br, 0.0f, -0.008f * sway);
    p.rot[NECK] = eul(-0.01f * br, 0.0f, 0.0f);
    float look = 0.2f * std::sin(t * 0.31f) * std::sin(t * 0.17f + 1.3f);
    p.rot[HEAD] = eul(0.04f * std::sin(t * 0.27f + 0.5f), look, 0.02f * std::sin(t * 0.4f));

    p.rot[CLAV_L] = eul(0.0f, 0.0f, 0.012f * br);
    p.rot[CLAV_R] = eul(0.0f, 0.0f, -0.012f * br);
    p.rot[UARM_L] = eul(0.03f, 0.0f, 0.07f + 0.012f * br);
    p.rot[UARM_R] = eul(0.03f, 0.0f, -0.07f - 0.012f * br);
    p.rot[FARM_L] = rx(-0.16f);
    p.rot[FARM_R] = rx(-0.16f);
    p.rot[HAND_L] = eul(0.0f, 0.0f, -0.04f);
    p.rot[HAND_R] = eul(0.0f, 0.0f, 0.04f);

    float roll = 0.022f * sway;
    p.rot[THIGH_L] = eul(-0.02f, 0.0f, -roll);
    p.rot[THIGH_R] = eul(-0.02f, 0.0f, -roll);
    p.rot[SHIN_L] = rx(0.04f);
    p.rot[SHIN_R] = rx(0.04f);
    p.rot[FOOT_L] = rx(-0.02f);
    p.rot[FOOT_R] = rx(-0.02f);
    return p;
}

Pose jumpUpPose() {
    Pose p;
    p.rot[SPINE] = eul(0.08f, 0.0f, 0.0f);
    p.rot[CHEST] = eul(0.05f, 0.0f, 0.0f);
    p.rot[HEAD] = eul(-0.12f, 0.0f, 0.0f);
    p.rot[THIGH_L] = eul(-1.05f, 0.0f, 0.0f);
    p.rot[SHIN_L] = rx(1.55f);
    p.rot[FOOT_L] = rx(-0.25f);
    p.rot[THIGH_R] = eul(-0.12f, 0.0f, 0.0f);
    p.rot[SHIN_R] = rx(0.55f);
    p.rot[FOOT_R] = rx(0.35f);
    p.rot[UARM_L] = eul(0.45f, 0.0f, 0.30f);
    p.rot[FARM_L] = rx(-0.50f);
    p.rot[UARM_R] = eul(-0.95f, 0.0f, -0.30f);
    p.rot[FARM_R] = rx(-0.90f);
    return p;
}

Pose fallPose() {
    Pose p;
    p.rot[SPINE] = eul(-0.04f, 0.0f, 0.0f);
    p.rot[HEAD] = eul(0.08f, 0.0f, 0.0f);
    p.rot[THIGH_L] = eul(-0.55f, 0.0f, 0.04f);
    p.rot[SHIN_L] = rx(0.85f);
    p.rot[FOOT_L] = rx(-0.15f);
    p.rot[THIGH_R] = eul(-0.10f, 0.0f, -0.04f);
    p.rot[SHIN_R] = rx(0.45f);
    p.rot[FOOT_R] = rx(0.15f);
    p.rot[UARM_L] = eul(-0.25f, 0.0f, 0.85f);
    p.rot[FARM_L] = rx(-0.45f);
    p.rot[UARM_R] = eul(-0.25f, 0.0f, -0.85f);
    p.rot[FARM_R] = rx(-0.45f);
    return p;
}

Pose landPose() {
    Pose p;
    p.rot[SPINE] = eul(0.30f, 0.0f, 0.0f);
    p.rot[CHEST] = eul(0.12f, 0.0f, 0.0f);
    p.rot[NECK] = eul(-0.15f, 0.0f, 0.0f);
    p.rot[HEAD] = eul(-0.20f, 0.0f, 0.0f);
    const float th = -0.95f, kn = 1.55f;
    p.rot[THIGH_L] = eul(th, 0.0f, 0.05f);
    p.rot[THIGH_R] = eul(th, 0.0f, -0.05f);
    p.rot[SHIN_L] = rx(kn);
    p.rot[SHIN_R] = rx(kn);
    p.rot[FOOT_L] = rx(-(th + kn));
    p.rot[FOOT_R] = rx(-(th + kn));
    p.rot[UARM_L] = eul(-0.35f, 0.0f, 0.25f);
    p.rot[UARM_R] = eul(-0.35f, 0.0f, -0.25f);
    p.rot[FARM_L] = rx(-0.60f);
    p.rot[FARM_R] = rx(-0.60f);
    return p;
}

} // namespace

// Desloca a raiz para que o ponto mais baixo dos pés fique no chão (y = 0).
void Animator::groundFeet(Pose& p, float weight) const {
    if (weight <= 0.0f) return;
    glm::mat4 g[BONE_COUNT], skin[BONE_COUNT];
    computeGlobals(sk_, p, g);
    computeSkinMatrices(sk_, g, skin);
    float minY = 1e9f;
    for (Bone f : {FOOT_L, FOOT_R}) {
        glm::vec3 heel = sk_.bindJoint[f] + sk_.heelOffset;
        glm::vec3 toe = sk_.bindJoint[f] + sk_.toeOffset;
        for (const glm::vec3& pt : {heel, toe}) minY = std::min(minY, (skin[f] * glm::vec4(pt, 1.0f)).y);
    }
    p.rootOffset.y -= minY * weight;
}

void Animator::setClips(const MocapClip* walk, const MocapClip* run) {
    walkClip_ = walk && walk->valid() ? walk : nullptr;
    runClip_ = run && run->valid() ? run : nullptr;
}

float Animator::walkSpeed() const { return walkClip_ ? walkClip_->naturalSpeed() : kWalkSpeed; }
float Animator::runSpeed() const { return runClip_ ? runClip_->naturalSpeed() : kSprintSpeed; }

// Passadas: captura de movimento quando disponível, senão o ciclo procedural (apoiado no chão).
Pose Animator::walkPose(float phase) const {
    if (walkClip_) return walkClip_->sample(phase);
    Pose p = gaitPose(phase, kWalkGait);
    groundFeet(p, 1.0f);
    return p;
}

Pose Animator::runPose(float phase) const {
    if (runClip_) return runClip_->sample(phase);
    Pose p = gaitPose(phase, kRunGait);
    groundFeet(p, 1.0f);
    return p;
}

void Animator::update(const AnimInput& in) {
    const float dt = in.dt;
    time_ += dt;
    speedSm_ = approach(speedSm_, in.speed, 12.0f, dt);
    const float speed = speedSm_;

    // Fase do passo avança pela distância percorrida -> pés não deslizam.
    const float ws = walkSpeed(), rs = runSpeed();
    float r = std::clamp((speed - ws) / (rs - ws), 0.0f, 1.0f);
    float walkLen = walkClip_ ? walkClip_->cycleLength : 1.5f;
    float runLen = runClip_ ? runClip_->cycleLength : 3.3f;
    float cycleLen = walkLen + (runLen - walkLen) * r;
    if (in.grounded) phase_ = wrap01(phase_ + (in.backwards ? -1.0f : 1.0f) * speed * dt / cycleLen);

    float moveTarget = std::clamp(speed / (0.7f * ws), 0.0f, 1.0f);
    if (in.grounded) moveW_ = approach(moveW_, moveTarget, 8.0f, dt);

    Pose idle = idlePose(time_);
    groundFeet(idle, 1.0f);
    Pose loco = blendPose(walkPose(phase_), runPose(phase_), smooth01(r));
    Pose ground = blendPose(idle, loco, moveW_);

    airW_ = approach(airW_, in.grounded ? 0.0f : 1.0f, in.grounded ? 18.0f : 9.0f, dt);
    float fallT = std::clamp((-in.verticalVel + 1.5f) / 5.0f, 0.0f, 1.0f);
    Pose air = blendPose(jumpUpPose(), fallPose(), smooth01(fallT));

    if (in.landed) {
        landAmp_ = std::clamp((in.landImpact - 1.5f) / 9.0f, 0.15f, 1.0f);
        landW_ = 1.0f;
    }
    landW_ = std::max(0.0f, landW_ - dt / 0.38f);

    Pose land = landPose();
    groundFeet(land, 1.0f);
    Pose p = blendPose(ground, air, airW_);
    float lw = landAmp_ * smooth01(landW_) * (1.0f - 0.55f * moveW_);
    p = blendPose(p, land, lw);

    // Inclina o corpo para dentro da curva.
    float leanTarget = std::clamp(-in.turnRate * speed * 0.035f, -0.3f, 0.3f);
    lean_ = approach(lean_, leanTarget, 8.0f, dt);
    p.rot[HIPS] = glm::angleAxis(lean_, AZ) * p.rot[HIPS];

    // Pernas viradas para o movimento, tronco compensa para continuar virado para a frente.
    legYaw_ = approach(legYaw_, in.legYaw * moveW_, 10.0f, dt);
    if (std::fabs(legYaw_) > 1e-4f) {
        p.rot[HIPS] = glm::angleAxis(legYaw_, AY) * p.rot[HIPS];
        p.rot[SPINE] = glm::angleAxis(-legYaw_ * 0.55f, AY) * p.rot[SPINE];
        p.rot[CHEST] = glm::angleAxis(-legYaw_ * 0.45f, AY) * p.rot[CHEST];
    }
    pose_ = p;

    if (!in.grounded) state_ = in.verticalVel > 0.0f ? "pulando" : "caindo";
    else if (lw > 0.15f) state_ = "aterrissando";
    else if (speed < 0.2f) state_ = "parado";
    else if (r < 0.5f) state_ = "andando";
    else state_ = "correndo";
}

void Animator::forceClip(const char* clip, float phase, float time) {
    Pose p;
    if (!std::strcmp(clip, "walk")) p = walkPose(phase);
    else if (!std::strcmp(clip, "jog")) p = blendPose(walkPose(phase), runPose(phase), 0.5f);
    else if (!std::strcmp(clip, "run")) p = runPose(phase);
    else if (!std::strcmp(clip, "jump")) p = jumpUpPose();
    else if (!std::strcmp(clip, "fall")) p = fallPose();
    else if (!std::strcmp(clip, "land")) { p = landPose(); groundFeet(p, 1.0f); }
    else if (!std::strcmp(clip, "bind")) p = Pose();
    else { p = idlePose(time); groundFeet(p, 1.0f); }
    pose_ = p;
    state_ = clip;
}
