#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// Esqueleto gerado para o personagem (o GLB não tem rig utilizável).
// Convenção: personagem olha para +Z, Y para cima, lado esquerdo do personagem = +X.
enum Bone {
    HIPS, SPINE, CHEST, NECK, HEAD,
    CLAV_L, UARM_L, FARM_L, HAND_L,
    CLAV_R, UARM_R, FARM_R, HAND_R,
    THIGH_L, SHIN_L, FOOT_L,
    THIGH_R, SHIN_R, FOOT_R,
    FING1_L, FING2_L, FING3_L, THUMB_L,
    FING1_R, FING2_R, FING3_R, THUMB_R,
    BONE_COUNT
};

// Ossos do corpo (os que vêm da captura de movimento); os dedos ficam depois.
constexpr int kBodyBones = FOOT_R + 1;

struct Skeleton {
    int parent[BONE_COUNT];
    const char* name[BONE_COUNT];

    // Pose da malha como veio no arquivo (o boneco está em pose "A").
    glm::vec3 bindJoint[BONE_COUNT];
    glm::vec3 bindTail[BONE_COUNT];

    // Pose neutra das animações (braços para baixo). bindRot leva cada osso
    // da orientação do arquivo para a orientação neutra.
    glm::vec3 joint[BONE_COUNT];
    glm::vec3 tail[BONE_COUNT];
    glm::quat bindRot[BONE_COUNT];

    // Calcanhar e ponta do pé relativos à articulação do tornozelo (para apoiar no chão).
    glm::vec3 heelOffset, toeOffset;

    // Mãos [0] = esquerda, [1] = direita. Na pose do arquivo: direção dos dedos, lado do
    // polegar e costas da mão (usados para separar as peças da mão entre os ossos dos dedos).
    glm::vec3 handAxis[2], handLat[2], handBack[2];
    // Na pose neutra: eixo para fechar os dedos e o polegar em direção à palma.
    glm::vec3 curlAxis[2], thumbAxis[2];

    static Skeleton makeCrashDummy();
};

struct Pose {
    glm::quat rot[BONE_COUNT]; // rotação local relativa ao pai (neutra = identidade)
    glm::vec3 rootOffset{0.0f};

    Pose();
};

Pose blendPose(const Pose& a, const Pose& b, float t);
// Soma aditiva: aplica as rotações de 'add' por cima de 'base' com peso w.
Pose addPose(const Pose& base, const Pose& add, float w);

// Fecha a mão: amount 0 = esticada, ~0.3 relaxada, 1 = segurando algo. side 0 = esquerda, 1 = direita.
void setFingerCurl(const Skeleton& sk, Pose& p, int side, float amount, float thumb);

void computeGlobals(const Skeleton& sk, const Pose& p, glm::mat4 out[BONE_COUNT]);
void computeSkinMatrices(const Skeleton& sk, const glm::mat4 globals[BONE_COUNT], glm::mat4 out[BONE_COUNT]);
