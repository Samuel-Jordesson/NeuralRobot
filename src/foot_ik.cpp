#define GLM_ENABLE_EXPERIMENTAL
#include "foot_ik.h"

#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace {

glm::vec3 posOf(const glm::mat4& m) { return glm::vec3(m[3]); }
glm::quat rotOf(const glm::mat4& m) { return glm::normalize(glm::quat_cast(glm::mat3(m))); }

float approach(float cur, float target, float rate, float dt) {
    return cur + (target - cur) * (1.0f - std::exp(-rate * dt));
}

} // namespace

void FootIK::apply(const Skeleton& sk, Pose& pose, const glm::mat4& model, const World& world, float weight, float dt) {
    if (!enabled) weight = 0.0f;
    const Bone feet[2] = {FOOT_L, FOOT_R}, thighs[2] = {THIGH_L, THIGH_R}, shins[2] = {SHIN_L, SHIN_R};
    glm::mat4 g[BONE_COUNT], skin[BONE_COUNT];
    computeGlobals(sk, pose, g);
    computeSkinMatrices(sk, g, skin);

    // 1) altura do terreno sob o calcanhar e a base dos dedos de cada pé (só superfícies
    //    alcançáveis por um passo). A ponta pode passar da quina, como num pé de verdade.
    const float baseY = model[3].y;
    const float maxY = baseY + kStepHeight + 0.25f;
    float plant[2];
    for (int i = 0; i < 2; ++i) {
        Bone f = feet[i];
        glm::vec3 heelLocal = sk.bindJoint[f] + sk.heelOffset;
        glm::vec3 ballLocal = glm::mix(heelLocal, sk.bindJoint[f] + sk.toeOffset, 0.7f);
        glm::vec3 heelM = glm::vec3(skin[f] * glm::vec4(heelLocal, 1.0f));
        glm::vec3 ballM = glm::vec3(skin[f] * glm::vec4(ballLocal, 1.0f));
        // pé apoiado = sola perto do chão na animação; pé em balanço só sobe (passa por cima do degrau)
        plant[i] = 1.0f - glm::smoothstep(0.04f, 0.16f, std::min(heelM.y, ballM.y));
        glm::vec3 heel = glm::vec3(model * glm::vec4(heelM, 1.0f));
        glm::vec3 ball = glm::vec3(model * glm::vec4(ballM, 1.0f));
        float ground = std::max(world.groundHeightAt(heel.x, heel.z, maxY), world.groundHeightAt(ball.x, ball.z, maxY));
        float off = ground - baseY;
        off = glm::mix(std::max(off, 0.0f), off, plant[i]);
        off_[i] = approach(off_[i], off * weight, 18.0f, dt);
    }

    // 2) a pelve acompanha o pé apoiado mais baixo (sobe ou desce); o outro joelho dobra
    float pelvisTarget;
    if (plant[0] > 0.5f && plant[1] > 0.5f) pelvisTarget = std::min(off_[0], off_[1]);
    else if (plant[0] > 0.5f) pelvisTarget = off_[0];
    else if (plant[1] > 0.5f) pelvisTarget = off_[1];
    else pelvisTarget = std::min(off_[0], off_[1]); // os dois no ar (corrida)
    pelvisTarget = std::clamp(pelvisTarget, -0.5f, 0.5f);
    pelvis_ = approach(pelvis_, pelvisTarget, 12.0f, dt);
    if (std::fabs(off_[0]) < 1e-4f && std::fabs(off_[1]) < 1e-4f && std::fabs(pelvis_) < 1e-4f) return;
    pose.rootOffset.y += pelvis_;
    computeGlobals(sk, pose, g);

    // 3) IK analítico de 2 ossos por perna, mantendo a orientação do pé da animação
    for (int i = 0; i < 2; ++i) {
        Bone th = thighs[i], sh = shins[i], f = feet[i];
        float lift = off_[i] - pelvis_; // quanto o tornozelo precisa subir em relação à pose atual
        if (std::fabs(lift) < 1e-4f) continue;
        glm::quat footWorld = rotOf(g[f]);
        glm::vec3 H = posOf(g[th]), K = posOf(g[sh]), A = posOf(g[f]);
        glm::vec3 T = A + glm::vec3(0.0f, lift, 0.0f);

        // joelho: lei dos cossenos
        float a = glm::length(K - H), b = glm::length(A - K);
        float d = std::clamp(glm::length(T - H), std::fabs(a - b) + 1e-3f, a + b - 1e-3f);
        float cur = std::acos(std::clamp(glm::dot(glm::normalize(H - K), glm::normalize(A - K)), -1.0f, 1.0f));
        float des = std::acos(std::clamp((a * a + b * b - d * d) / (2.0f * a * b), -1.0f, 1.0f));
        glm::vec3 axis = glm::cross(K - H, A - K);
        if (glm::length(axis) < 1e-4f) axis = rotOf(g[th]) * glm::vec3(1, 0, 0); // perna reta: dobra para frente
        axis = glm::normalize(axis);
        glm::quat shinWorld = glm::angleAxis(cur - des, axis) * rotOf(g[sh]);
        pose.rot[sh] = glm::normalize(glm::inverse(rotOf(g[th])) * shinWorld);
        computeGlobals(sk, pose, g);

        // quadril: gira a coxa para o tornozelo apontar para o alvo (joelho segue para frente)
        glm::vec3 A2 = posOf(g[f]);
        glm::quat swing = glm::rotation(glm::normalize(A2 - H), glm::normalize(T - H));
        glm::quat thighWorld = swing * rotOf(g[th]);
        pose.rot[th] = glm::normalize(glm::inverse(rotOf(g[sk.parent[th]])) * thighWorld);
        computeGlobals(sk, pose, g);

        pose.rot[f] = glm::normalize(glm::inverse(rotOf(g[sh])) * footWorld);
        computeGlobals(sk, pose, g);
    }
}
