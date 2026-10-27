#define GLM_ENABLE_EXPERIMENTAL
#include "skeleton.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

Skeleton Skeleton::makeCrashDummy() {
    Skeleton s{};
    // Medidas tiradas das peças da malha (metros, pés em y = 0).
    // 'aim' != 0: direção do osso na pose neutra (braços abaixados a partir da pose A).
    struct Def { Bone b; int parent; glm::vec3 j, t, aim; const char* name; };
    const glm::vec3 none(0.0f);
    const glm::vec3 upperArm(0.16f, -0.985f, 0.0f), foreArm(0.10f, -0.985f, 0.10f), hand(0.06f, -0.99f, 0.10f);
    const Def defs[kBodyBones] = {
        {HIPS,    -1,      {0.000f, 0.930f, -0.010f}, {0.000f, 1.060f, -0.010f}, none, "hips"},
        {SPINE,   HIPS,    {0.000f, 1.060f, -0.010f}, {0.000f, 1.150f, -0.020f}, none, "spine"},
        {CHEST,   SPINE,   {0.000f, 1.150f, -0.020f}, {0.000f, 1.460f, -0.040f}, none, "chest"},
        {NECK,    CHEST,   {0.000f, 1.460f, -0.040f}, {0.000f, 1.530f, -0.020f}, none, "neck"},
        {HEAD,    NECK,    {0.000f, 1.530f, -0.020f}, {0.000f, 1.680f,  0.000f}, none, "head"},

        {CLAV_L,  CHEST,   { 0.030f, 1.400f, -0.070f}, { 0.170f, 1.375f, -0.090f}, none, "clavicle_l"},
        {UARM_L,  CLAV_L,  { 0.170f, 1.375f, -0.090f}, { 0.345f, 1.175f, -0.105f}, upperArm, "upperarm_l"},
        {FARM_L,  UARM_L,  { 0.345f, 1.175f, -0.105f}, { 0.515f, 1.045f, -0.005f}, foreArm, "forearm_l"},
        {HAND_L,  FARM_L,  { 0.515f, 1.045f, -0.005f}, { 0.610f, 0.900f,  0.040f}, hand, "hand_l"},

        {CLAV_R,  CHEST,   {-0.030f, 1.400f, -0.070f}, {-0.170f, 1.375f, -0.090f}, none, "clavicle_r"},
        {UARM_R,  CLAV_R,  {-0.170f, 1.375f, -0.090f}, {-0.345f, 1.175f, -0.105f}, upperArm * glm::vec3(-1, 1, 1), "upperarm_r"},
        {FARM_R,  UARM_R,  {-0.345f, 1.175f, -0.105f}, {-0.515f, 1.045f, -0.005f}, foreArm * glm::vec3(-1, 1, 1), "forearm_r"},
        {HAND_R,  FARM_R,  {-0.515f, 1.045f, -0.005f}, {-0.610f, 0.900f,  0.040f}, hand * glm::vec3(-1, 1, 1), "hand_r"},

        {THIGH_L, HIPS,    { 0.100f, 0.880f,  0.000f}, { 0.130f, 0.490f, -0.020f}, none, "thigh_l"},
        {SHIN_L,  THIGH_L, { 0.130f, 0.490f, -0.020f}, { 0.165f, 0.110f, -0.070f}, none, "shin_l"},
        {FOOT_L,  SHIN_L,  { 0.165f, 0.110f, -0.070f}, { 0.170f, 0.020f,  0.110f}, none, "foot_l"},

        {THIGH_R, HIPS,    {-0.100f, 0.880f,  0.000f}, {-0.130f, 0.490f, -0.020f}, none, "thigh_r"},
        {SHIN_R,  THIGH_R, {-0.130f, 0.490f, -0.020f}, {-0.165f, 0.110f, -0.070f}, none, "shin_r"},
        {FOOT_R,  SHIN_R,  {-0.165f, 0.110f, -0.070f}, {-0.170f, 0.020f,  0.110f}, none, "foot_r"},
    };

    // Pai sempre antes do filho. A pose neutra é obtida aplicando a reorientação (bindRot)
    // de cada osso; filhos sem 'aim' herdam a reorientação do pai (ex.: dedos seguem a mão).
    auto addBone = [&](int i, int parent, const glm::vec3& j, const glm::vec3& t, const glm::vec3& aimDir, const char* name) {
        s.parent[i] = parent;
        s.name[i] = name;
        s.bindJoint[i] = j;
        s.bindTail[i] = t;
        glm::quat parentRot = parent >= 0 ? s.bindRot[parent] : glm::quat(1, 0, 0, 0);
        s.joint[i] = parent >= 0 ? s.joint[parent] + parentRot * (j - s.bindJoint[parent]) : j;
        glm::vec3 bindDir = t - j;
        s.bindRot[i] = glm::length(aimDir) > 0.0f ? glm::rotation(glm::normalize(bindDir), glm::normalize(aimDir)) : parentRot;
        s.tail[i] = s.joint[i] + s.bindRot[i] * bindDir;
    };

    for (const Def& d : defs) {
        glm::vec3 t = d.t;
        if (d.b == HAND_L || d.b == HAND_R) t = d.j + glm::normalize(d.t - d.j) * 0.105f; // palma termina nos nós dos dedos
        addBone(d.b, d.parent, d.j, t, d.aim, d.name);
    }

    // Dedos (medidos nas peças da mão): 3 falanges para os 4 dedos juntos + polegar.
    const Bone f1[2] = {FING1_L, FING1_R}, f2[2] = {FING2_L, FING2_R}, f3[2] = {FING3_L, FING3_R}, th[2] = {THUMB_L, THUMB_R};
    const Bone hands[2] = {HAND_L, HAND_R};
    const char* names[2][4] = {{"fingers1_l", "fingers2_l", "fingers3_l", "thumb_l"},
                               {"fingers1_r", "fingers2_r", "fingers3_r", "thumb_r"}};
    for (int k = 0; k < 2; ++k) {
        Bone h = hands[k];
        const float side = k == 0 ? 1.0f : -1.0f;
        glm::vec3 W = s.bindJoint[h];
        glm::vec3 ax = glm::normalize(s.bindTail[h] - W);
        glm::vec3 lat = glm::normalize(glm::cross(ax, glm::vec3(0, 1, 0)));
        if (lat.z < 0.0f) lat = -lat; // lado do polegar (para frente)
        glm::vec3 nrm = glm::cross(lat, ax) * side; // costas da mão
        s.handAxis[k] = ax;
        s.handLat[k] = lat;
        s.handBack[k] = nrm;
        addBone(f1[k], h, W + ax * 0.105f, W + ax * 0.142f, glm::vec3(0.0f), names[k][0]);
        addBone(f2[k], f1[k], W + ax * 0.142f, W + ax * 0.173f, glm::vec3(0.0f), names[k][1]);
        addBone(f3[k], f2[k], W + ax * 0.173f, W + ax * 0.225f, glm::vec3(0.0f), names[k][2]);
        addBone(th[k], h, W + ax * 0.040f + lat * 0.025f, W + ax * 0.135f + lat * 0.090f, glm::vec3(0.0f), names[k][3]);
        // eixo de dobrar os dedos em direção à palma, já na orientação neutra
        glm::vec3 palm = -nrm;
        s.curlAxis[k] = glm::normalize(s.bindRot[h] * glm::cross(ax, palm));
        s.thumbAxis[k] = glm::normalize(s.bindRot[h] * glm::cross(glm::normalize(ax * 0.3f + lat), palm));
    }
    s.heelOffset = glm::vec3(0.0f, -0.105f, -0.07f);
    s.toeOffset = glm::vec3(0.0f, -0.105f, 0.185f);
    return s;
}

Pose::Pose() {
    for (auto& r : rot) r = glm::quat(1, 0, 0, 0);
}

Pose blendPose(const Pose& a, const Pose& b, float t) {
    if (t <= 0.0f) return a;
    if (t >= 1.0f) return b;
    Pose r;
    for (int i = 0; i < BONE_COUNT; ++i) r.rot[i] = glm::slerp(a.rot[i], b.rot[i], t);
    r.rootOffset = glm::mix(a.rootOffset, b.rootOffset, t);
    return r;
}

Pose addPose(const Pose& base, const Pose& add, float w) {
    Pose r;
    const glm::quat id(1, 0, 0, 0);
    for (int i = 0; i < BONE_COUNT; ++i) r.rot[i] = glm::normalize(base.rot[i] * glm::slerp(id, add.rot[i], w));
    r.rootOffset = base.rootOffset + add.rootOffset * w;
    return r;
}

void setFingerCurl(const Skeleton& sk, Pose& p, int side, float amount, float thumb) {
    const Bone f1 = side == 0 ? FING1_L : FING1_R, f2 = side == 0 ? FING2_L : FING2_R;
    const Bone f3 = side == 0 ? FING3_L : FING3_R, th = side == 0 ? THUMB_L : THUMB_R;
    const glm::vec3 axis = sk.curlAxis[side];
    p.rot[f1] = glm::angleAxis(1.15f * amount, axis);
    p.rot[f2] = glm::angleAxis(1.25f * amount, axis);
    p.rot[f3] = glm::angleAxis(0.75f * amount, axis);
    p.rot[th] = glm::angleAxis(0.9f * thumb, sk.thumbAxis[side]);
}

void computeGlobals(const Skeleton& sk, const Pose& p, glm::mat4 out[BONE_COUNT]) {
    for (int i = 0; i < BONE_COUNT; ++i) {
        int par = sk.parent[i];
        glm::mat4 local;
        if (par < 0)
            local = glm::translate(glm::mat4(1.0f), sk.joint[i] + p.rootOffset) * glm::mat4_cast(p.rot[i]);
        else
            local = glm::translate(glm::mat4(1.0f), sk.joint[i] - sk.joint[par]) * glm::mat4_cast(p.rot[i]);
        out[i] = par < 0 ? local : out[par] * local;
    }
}

// Vértice do arquivo -> espaço do osso na pose neutra -> pose animada.
void computeSkinMatrices(const Skeleton& sk, const glm::mat4 globals[BONE_COUNT], glm::mat4 out[BONE_COUNT]) {
    for (int i = 0; i < BONE_COUNT; ++i)
        out[i] = globals[i] * glm::mat4_cast(sk.bindRot[i]) * glm::translate(glm::mat4(1.0f), -sk.bindJoint[i]);
}
