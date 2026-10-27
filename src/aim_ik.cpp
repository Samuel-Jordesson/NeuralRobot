#define GLM_ENABLE_EXPERIMENTAL
#include "aim_ik.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace {

// Na mão (eixos canônicos do osso): +Y = para o pulso (dedos em -Y), +Z = lado do polegar,
// +X = normal da palma na mão direita (na esquerda a palma é -X).
// Empunhadura = "aperto de mão": palma da direita encostada no lado direito da empunhadura
// (normal para o lado esquerdo da arma, +Z), dedos para frente envolvendo a frente da
// empunhadura, polegar por cima.
glm::quat handFrameInGun(const glm::vec3& xAxis, const glm::vec3& fingers) {
    glm::vec3 hy = -fingers;
    hy = glm::normalize(hy - xAxis * glm::dot(hy, xAxis));
    glm::mat3 h(xAxis, hy, glm::cross(xAxis, hy)); // colunas = eixos da mão no espaço da arma
    return glm::normalize(glm::quat_cast(glm::transpose(h)));       // arma -> mão
}
glm::quat gripFrame(const GunDef& d) { return handFrameInGun(glm::vec3(0, 0, 1), d.fingers); }
// pistola: mão esquerda pelo lado esquerdo da empunhadura
const glm::quat kSupportPistol = handFrameInGun(glm::vec3(0, 0, 1), glm::normalize(glm::vec3(-0.6f, -0.8f, 0.0f)));
// fuzil: mão esquerda por baixo do guarda-mão, palma para cima/dentro, dedos envolvendo pela direita
const glm::quat kSupportRifle = handFrameInGun(glm::normalize(glm::vec3(0.0f, -0.7f, 0.7f)),
                                               glm::normalize(glm::vec3(-0.6f, -0.55f, -0.55f)));
// centro da empunhadura em relação ao pulso (dentro da mão fechada)
const glm::vec3 kPalmR(0.03f, -0.07f, 0.0f);
const glm::vec3 kPalmL(-0.03f, -0.07f, 0.0f);

glm::vec3 posOf(const glm::mat4& m) { return glm::vec3(m[3]); }
glm::quat rotOf(const glm::mat4& m) { return glm::normalize(glm::quat_cast(glm::mat3(m))); }

glm::quat eul(float x, float y, float z) {
    return glm::angleAxis(y, glm::vec3(0, 1, 0)) * glm::angleAxis(x, glm::vec3(1, 0, 0)) *
           glm::angleAxis(z, glm::vec3(0, 0, 1));
}

// Orientação da arma (espaço do modelo) apontando para 'dir' com o topo para cima.
glm::quat gunRotation(const glm::vec3& dir) {
    glm::vec3 up(0, 1, 0);
    glm::vec3 gx = -dir;
    glm::vec3 gy = glm::normalize(up - dir * glm::dot(up, dir));
    return glm::normalize(glm::quat_cast(glm::mat3(gx, gy, glm::cross(gx, gy))));
}

// IK analítico de 2 ossos: o cotovelo/joelho fica no plano que contém 'pole'.
void solveTwoBone(const Skeleton& sk, Pose& pose, Bone upper, Bone lower, Bone end, const glm::vec3& target,
                  const glm::vec3& pole) {
    glm::mat4 g[BONE_COUNT];
    computeGlobals(sk, pose, g);
    glm::vec3 H = posOf(g[upper]), E0 = posOf(g[lower]), W0 = posOf(g[end]);
    float a = glm::length(E0 - H), b = glm::length(W0 - E0);
    glm::vec3 toT = target - H;
    float d = std::clamp(glm::length(toT), std::fabs(a - b) + 1e-3f, a + b - 1e-3f);
    glm::vec3 dir = glm::normalize(toT);
    float cosA = std::clamp((a * a + d * d - b * b) / (2.0f * a * d), -1.0f, 1.0f);
    glm::vec3 bend = pole - H;
    bend -= dir * glm::dot(bend, dir);
    bend = glm::length(bend) > 1e-4f ? glm::normalize(bend) : glm::vec3(0, -1, 0);
    glm::vec3 E = H + dir * (a * cosA) + bend * (a * std::sqrt(1.0f - cosA * cosA));
    glm::vec3 T = H + dir * d;

    glm::quat q1 = glm::rotation(glm::normalize(E0 - H), glm::normalize(E - H));
    pose.rot[upper] = glm::normalize(glm::inverse(rotOf(g[sk.parent[upper]])) * q1 * rotOf(g[upper]));
    computeGlobals(sk, pose, g);
    glm::vec3 E1 = posOf(g[lower]), W1 = posOf(g[end]);
    glm::quat q2 = glm::rotation(glm::normalize(W1 - E1), glm::normalize(T - E1));
    pose.rot[lower] = glm::normalize(glm::inverse(rotOf(g[upper])) * q2 * rotOf(g[lower]));
}

// Ombro (clavícula) vai na direção do alvo quando o braço sozinho não alcança.
void reachClavicle(const Skeleton& sk, Pose& pose, Bone clav, Bone upper, Bone lower, Bone end, const glm::vec3& target) {
    glm::mat4 g[BONE_COUNT];
    computeGlobals(sk, pose, g);
    float len = glm::length(posOf(g[lower]) - posOf(g[upper])) + glm::length(posOf(g[end]) - posOf(g[lower]));
    glm::vec3 C = posOf(g[clav]), S = posOf(g[upper]);
    float excess = glm::length(target - S) - len * 0.97f;
    if (excess <= 0.0f) return;
    glm::vec3 toS = glm::normalize(S - C), toT = glm::normalize(target - C);
    float ang = std::min(std::acos(std::clamp(glm::dot(toS, toT), -1.0f, 1.0f)), excess / glm::length(S - C));
    ang = std::min(ang, 0.45f);
    glm::vec3 axis = glm::cross(toS, toT);
    if (glm::length(axis) < 1e-4f) return;
    glm::quat q = glm::angleAxis(ang, glm::normalize(axis));
    pose.rot[clav] = glm::normalize(glm::inverse(rotOf(g[sk.parent[clav]])) * q * rotOf(g[clav]));
}

// Braço inteiro até a mão: clavícula + 2 ossos + orientação da mão.
void armTo(const Skeleton& sk, Pose& p, bool right, const glm::mat4& hand, const glm::vec3& pole) {
    const Bone clav = right ? CLAV_R : CLAV_L, up = right ? UARM_R : UARM_L, lo = right ? FARM_R : FARM_L,
               en = right ? HAND_R : HAND_L;
    reachClavicle(sk, p, clav, up, lo, en, posOf(hand));
    glm::mat4 g[BONE_COUNT];
    computeGlobals(sk, p, g);
    solveTwoBone(sk, p, up, lo, en, posOf(hand), posOf(g[up]) + pole);
    computeGlobals(sk, p, g);
    p.rot[en] = glm::normalize(glm::inverse(rotOf(g[lo])) * rotOf(hand));
}


// Matriz presa ao peito: 'local' foi pensado com o personagem na pose neutra.
glm::mat4 onChest(const Skeleton& sk, const glm::mat4 globals[BONE_COUNT], const glm::mat4& local) {
    return globals[CHEST] * glm::translate(glm::mat4(1.0f), -sk.joint[CHEST]) * local;
}

// Fuzil com a empunhadura em 'grip' (pose neutra) e o cano na direção 'barrel'; 'up' = topo.
glm::mat4 rifleAt(const GunDef& d, const glm::vec3& grip, const glm::vec3& barrel, const glm::vec3& up) {
    glm::vec3 x = -glm::normalize(barrel);
    glm::vec3 y = glm::normalize(up - x * glm::dot(up, x));
    glm::mat3 r(x, y, glm::cross(x, y));
    return glm::translate(glm::mat4(1.0f), grip) * glm::mat4(r) * glm::translate(glm::mat4(1.0f), -d.grip);
}

} // namespace

glm::mat4 blendRigid(const glm::mat4& a, const glm::mat4& b, float t) {
    if (t <= 0.0f) return a;
    if (t >= 1.0f) return b;
    float sa = glm::length(glm::vec3(a[0])), sb = glm::length(glm::vec3(b[0]));
    glm::quat qa = glm::quat_cast(glm::mat3(a) / sa), qb = glm::quat_cast(glm::mat3(b) / sb);
    glm::mat4 m = glm::mat4_cast(glm::slerp(qa, qb, t)) * glm::scale(glm::mat4(1.0f), glm::vec3(glm::mix(sa, sb, t)));
    m[3] = glm::vec4(glm::mix(glm::vec3(a[3]), glm::vec3(b[3]), t), 1.0f);
    return m;
}

glm::mat4 gunInHand(const glm::mat4& handGlobal, const GunDef& d) {
    return handGlobal * glm::translate(glm::mat4(1.0f), kPalmR) * glm::mat4_cast(gripFrame(d)) *
           glm::translate(glm::mat4(1.0f), -d.grip);
}

glm::mat4 handForGun(const glm::mat4& gun, const GunDef& d) {
    return gun * glm::translate(glm::mat4(1.0f), d.grip) * glm::mat4_cast(glm::inverse(gripFrame(d))) *
           glm::translate(glm::mat4(1.0f), -kPalmR);
}

glm::mat4 slotMatrix(const Skeleton& sk, const glm::mat4 globals[BONE_COUNT], int slot, const GunDef& d) {
    if (slot == 0) {
        // cintura: cano para baixo, topo do ferrolho para frente, lado esquerdo da arma encostado
        // no quadril; um pouco inclinada (cano para trás), como num coldre de cintura
        glm::mat3 r(glm::vec3(0, 1, 0), glm::vec3(0, 0, 1), glm::vec3(1, 0, 0)); // colunas: eixos X, Y, Z da arma
        glm::quat q = glm::angleAxis(glm::radians(-12.0f), glm::vec3(1, 0, 0)) * glm::quat_cast(r);
        const glm::vec3 holster(-0.170f, 0.030f, -0.065f); // empunhadura, relativa ao quadril
        return globals[HIPS] * glm::translate(glm::mat4(1.0f), holster) * glm::mat4_cast(q) *
               glm::translate(glm::mat4(1.0f), -d.grip);
    }
    // costas (estilo bandoleira): deitados de lado contra as costas, cruzados em X.
    // 1: empunhadura embaixo à direita, cano subindo por trás do ombro esquerdo;
    // 2: empunhadura em cima à direita (coronha passa do ombro direito), cano descendo para a esquerda.
    if (slot == 1)
        return onChest(sk, globals, rifleAt(d, {-0.15f, 0.98f, -0.215f}, {0.42f, 0.90f, 0.0f}, {-0.9f, 0.42f, 0.0f}));
    return onChest(sk, globals, rifleAt(d, {-0.13f, 1.36f, -0.265f}, {0.42f, -0.90f, 0.0f}, {0.9f, 0.42f, 0.0f}));
}

glm::mat4 rifleShoulderMatrix(const Skeleton& sk, const glm::mat4 globals[BONE_COUNT], int slot, const GunDef& d) {
    // de pé ao lado do ombro direito, cano para cima e um pouco para frente
    if (slot == 2)
        return onChest(sk, globals, rifleAt(d, {-0.30f, 1.40f, 0.02f}, {0.05f, 0.95f, 0.30f}, {0.2f, 0.0f, -1.0f}));
    return onChest(sk, globals, rifleAt(d, {-0.30f, 1.05f, 0.05f}, {0.10f, 0.80f, 0.60f}, {0.2f, 0.0f, -1.0f}));
}

void applyCarryGrip(const Skeleton& sk, Pose& pose, const GunDef& d, float weight) {
    if (weight <= 0.001f) return;
    glm::mat4 g[BONE_COUNT];
    computeGlobals(sk, pose, g);
    // cano para frente e um pouco para baixo, como quem anda com a arma na mão
    glm::vec3 dir = glm::normalize(glm::vec3(-0.08f, -0.55f, 1.0f));
    glm::quat hand = gunRotation(dir) * glm::inverse(gripFrame(d));
    glm::quat local = glm::normalize(glm::inverse(rotOf(g[FARM_R])) * hand);
    pose.rot[HAND_R] = glm::slerp(pose.rot[HAND_R], local, weight);
}

void applyAimPose(const Skeleton& sk, Pose& pose, const GunDef& d, const glm::vec3& target, float weight, float recoil) {
    if (weight <= 0.001f) return;
    Pose p = pose;
    glm::mat4 g[BONE_COUNT];
    computeGlobals(sk, p, g);

    // 1) tronco e cabeça acompanham a mira
    glm::vec3 dd = target - posOf(g[NECK]);
    if (glm::length(dd) < 0.3f) dd = glm::vec3(0, 0, 1);
    dd = glm::normalize(dd);
    float pitch = std::asin(std::clamp(dd.y, -0.95f, 0.95f));
    float yaw = std::atan2(dd.x, dd.z);
    p.rot[SPINE] = p.rot[SPINE] * eul(-pitch * 0.25f, yaw * 0.3f, 0.0f);
    p.rot[CHEST] = p.rot[CHEST] * eul(-pitch * 0.30f, yaw * 0.4f, 0.0f);
    p.rot[HEAD] = p.rot[HEAD] * eul(-pitch * 0.30f, yaw * 0.2f, 0.10f); // cabeça inclina para a mira
    computeGlobals(sk, p, g);

    // 2) arma na linha do olho direito, apontando para o alvo (+ coice)
    glm::vec3 neck = posOf(g[NECK]);
    dd = glm::normalize(target - neck);
    glm::vec3 grip = neck + glm::vec3(-0.03f, 0.03f + recoil * 0.02f, 0.0f) + dd * (0.42f - 0.05f * recoil);
    glm::vec3 aim = glm::normalize(target - grip);
    glm::vec3 flipAxis = glm::cross(aim, glm::vec3(0, 1, 0));
    if (glm::length(flipAxis) > 1e-4f) aim = glm::angleAxis(recoil * 0.30f, glm::normalize(flipAxis)) * aim;
    glm::quat gunRot = gunRotation(aim);

    // 3) braço direito: IK até o pulso; mão orientada pela arma
    glm::quat handR = gunRot * glm::inverse(gripFrame(d));
    glm::vec3 wristR = grip - handR * kPalmR;
    solveTwoBone(sk, p, UARM_R, FARM_R, HAND_R, wristR, posOf(g[UARM_R]) + glm::vec3(-0.35f, -0.6f, -0.25f));
    computeGlobals(sk, p, g);
    p.rot[HAND_R] = glm::normalize(glm::inverse(rotOf(g[FARM_R])) * handR);

    // 4) mão esquerda apoiando a empunhadura pelo lado esquerdo
    glm::vec3 support = grip + gunRot * (d.support - d.grip);
    glm::quat handL = gunRot * glm::inverse(kSupportPistol);
    glm::vec3 wristL = support - handL * kPalmL;
    solveTwoBone(sk, p, UARM_L, FARM_L, HAND_L, wristL, posOf(g[UARM_L]) + glm::vec3(0.35f, -0.6f, -0.25f));
    computeGlobals(sk, p, g);
    p.rot[HAND_L] = glm::normalize(glm::inverse(rotOf(g[FARM_L])) * handL);

    pose = blendPose(pose, p, weight);
}

void applyRifleTorso(const Skeleton& sk, Pose& pose, const glm::vec3& target, float weight) {
    if (weight <= 0.001f) return;
    glm::mat4 g[BONE_COUNT];
    computeGlobals(sk, pose, g);
    glm::vec3 dd = target - posOf(g[NECK]);
    if (glm::length(dd) < 0.3f) dd = glm::vec3(0, 0, 1);
    dd = glm::normalize(dd);
    float pitch = std::asin(std::clamp(dd.y, -0.95f, 0.95f)) * weight;
    float yaw = std::atan2(dd.x, dd.z) * weight;
    const float blade = -0.38f * weight; // ombro esquerdo para frente
    pose.rot[SPINE] = pose.rot[SPINE] * eul(-pitch * 0.25f, yaw * 0.3f + blade * 0.45f, 0.0f);
    pose.rot[CHEST] = pose.rot[CHEST] * eul(-pitch * 0.30f, yaw * 0.4f + blade * 0.55f, 0.0f);
    // cabeça desfaz o giro do tronco e encosta na coronha
    pose.rot[NECK] = pose.rot[NECK] * eul(0.0f, -blade * 0.5f, 0.0f);
    pose.rot[HEAD] = pose.rot[HEAD] * eul(-pitch * 0.30f + 0.10f * weight, yaw * 0.2f - blade * 0.5f, 0.16f * weight);
}

glm::mat4 rifleHold(const Skeleton& sk, const Pose& pose, const GunDef& d, const glm::vec3& target, float aimW,
                    float recoil) {
    glm::mat4 g[BONE_COUNT];
    computeGlobals(sk, pose, g);
    // pronto baixo: coronha perto do ombro, cano para frente, para baixo e cruzando o corpo
    glm::mat4 carry = onChest(sk, g, rifleAt(d, {-0.11f, 1.10f, 0.12f}, {0.48f, -0.60f, 1.0f}, {0.15f, 1.0f, 0.0f}));
    if (aimW <= 0.001f) return carry;
    // mira: olho direito na linha de mira, cano apontando para o alvo
    glm::vec3 eye = glm::vec3(g[HEAD] * glm::vec4(-0.035f, 0.085f, 0.075f, 1.0f));
    glm::vec3 dir = target - eye;
    dir = glm::length(dir) > 0.3f ? glm::normalize(dir) : glm::vec3(0, 0, 1);
    glm::vec3 side = glm::cross(dir, glm::vec3(0, 1, 0));
    if (glm::length(side) > 1e-4f) dir = glm::angleAxis(recoil * 0.06f, glm::normalize(side)) * dir; // cano sobe no coice
    glm::mat4 aim = glm::translate(glm::mat4(1.0f), eye - dir * (0.035f * recoil)) * glm::mat4_cast(gunRotation(dir)) *
                    glm::translate(glm::mat4(1.0f), -d.eye);
    return blendRigid(carry, aim, aimW);
}

void applyRightHand(const Skeleton& sk, Pose& pose, const glm::mat4& hand, float weight) {
    if (weight <= 0.001f) return;
    Pose p = pose;
    armTo(sk, p, true, hand, glm::vec3(-0.45f, -0.55f, -0.25f));
    pose = blendPose(pose, p, weight);
}

void applySupportHand(const Skeleton& sk, Pose& pose, const GunDef& d, float weight) {
    if (weight <= 0.001f) return;
    glm::mat4 g[BONE_COUNT];
    computeGlobals(sk, pose, g);
    glm::mat4 gun = gunInHand(g[HAND_R], d);
    glm::mat4 hand = gun * glm::translate(glm::mat4(1.0f), d.support) * glm::mat4_cast(glm::inverse(kSupportRifle)) *
                     glm::translate(glm::mat4(1.0f), -kPalmL);
    Pose p = pose;
    armTo(sk, p, false, hand, glm::vec3(0.30f, -0.65f, -0.10f));
    pose = blendPose(pose, p, weight);
}

void applyHandReach(const Skeleton& sk, Pose& pose, const HandReach& r) {
    if (r.arm <= 0.001f && r.lean <= 0.001f && r.crouch <= 0.001f && r.look <= 0.001f) return;
    glm::mat4 g[BONE_COUNT];

    // 1) agacha um pouco: o quadril desce e as pernas dobram com os tornozelos no lugar
    if (r.crouch > 0.001f) {
        computeGlobals(sk, pose, g);
        const Bone th[2] = {THIGH_L, THIGH_R}, sh[2] = {SHIN_L, SHIN_R}, ft[2] = {FOOT_L, FOOT_R};
        glm::vec3 ankle[2];
        glm::quat footRot[2];
        for (int i = 0; i < 2; ++i) { ankle[i] = posOf(g[ft[i]]); footRot[i] = rotOf(g[ft[i]]); }
        pose.rootOffset.y -= r.crouch;
        pose.rootOffset.z -= r.crouch * 0.6f; // quadril vai para trás para equilibrar o tronco
        for (int i = 0; i < 2; ++i) {
            computeGlobals(sk, pose, g);
            solveTwoBone(sk, pose, th[i], sh[i], ft[i], ankle[i], posOf(g[sh[i]]) + glm::vec3(0, 0, 0.5f));
            computeGlobals(sk, pose, g);
            pose.rot[ft[i]] = glm::normalize(glm::inverse(rotOf(g[sh[i]])) * footRot[i]);
        }
    }
    // 2) tronco inclina para frente
    if (r.lean > 0.001f) {
        pose.rot[SPINE] = pose.rot[SPINE] * eul(r.lean * 0.45f, 0.0f, 0.0f);
        pose.rot[CHEST] = pose.rot[CHEST] * eul(r.lean * 0.55f, 0.0f, 0.0f);
        // braço esquerdo continua pendurado (senão vai junto com o tronco e fica para trás)
        pose.rot[UARM_L] = eul(-r.lean * 0.8f, 0.0f, 0.0f) * pose.rot[UARM_L];
    }
    // 3) cabeça olha para o alvo
    if (r.look > 0.001f) {
        computeGlobals(sk, pose, g);
        glm::vec3 d = r.lookAt - posOf(g[HEAD]);
        if (glm::length(d) > 0.05f) {
            glm::vec3 local = glm::inverse(glm::mat3(g[NECK])) * glm::normalize(d); // direção vista do pescoço
            float pitch = std::asin(std::clamp(-local.y, -0.9f, 0.9f)) * 0.8f;
            float yaw = std::clamp(std::atan2(local.x, local.z), -1.0f, 1.0f) * 0.7f;
            pose.rot[HEAD] = glm::slerp(pose.rot[HEAD], pose.rot[HEAD] * eul(pitch, yaw, 0.0f), r.look);
        }
    }
    // 4) braço direito até o alvo
    if (r.arm > 0.001f) {
        Pose p = pose;
        armTo(sk, p, true, r.hand, r.pole);
        pose = blendPose(pose, p, r.arm);
    }
}
