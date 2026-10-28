#define GLM_ENABLE_EXPERIMENTAL
#include "skeleton.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

namespace {

// Osso do corpo: articulação e ponta na pose do arquivo. 'aim' != 0: direção do osso na pose
// neutra (braços abaixados a partir da pose A/T do arquivo).
struct Def { Bone b; int parent; glm::vec3 j, t, aim; const char* name; };

const glm::vec3 kNone(0.0f);
const glm::vec3 kUpperArm(0.16f, -0.985f, 0.0f), kForeArm(0.10f, -0.985f, 0.10f), kHand(0.06f, -0.99f, 0.10f);
const glm::vec3 kMirror(-1, 1, 1);

// Monta o esqueleto a partir dos ossos do corpo. 'palm' = do pulso até os nós dos dedos;
// os dedos são gerados ao longo da mão com proporções fixas.
Skeleton build(const Def (&defs)[kBodyBones], float palm, const glm::vec3& heel, const glm::vec3& toe) {
    Skeleton s{};
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
        if (d.b == HAND_L || d.b == HAND_R) t = d.j + glm::normalize(d.t - d.j) * palm; // palma termina nos nós dos dedos
        addBone(d.b, d.parent, d.j, t, d.aim, d.name);
    }

    // Dedos: 3 falanges para os 4 dedos juntos + polegar (medidas do boneco, escaladas pela palma).
    const float k = palm / 0.105f;
    const Bone f1[2] = {FING1_L, FING1_R}, f2[2] = {FING2_L, FING2_R}, f3[2] = {FING3_L, FING3_R}, th[2] = {THUMB_L, THUMB_R};
    const Bone hands[2] = {HAND_L, HAND_R};
    const char* names[2][4] = {{"fingers1_l", "fingers2_l", "fingers3_l", "thumb_l"},
                               {"fingers1_r", "fingers2_r", "fingers3_r", "thumb_r"}};
    for (int side = 0; side < 2; ++side) {
        Bone h = hands[side];
        const float sign = side == 0 ? 1.0f : -1.0f;
        glm::vec3 W = s.bindJoint[h];
        glm::vec3 ax = glm::normalize(s.bindTail[h] - W);
        glm::vec3 lat = glm::normalize(glm::cross(ax, glm::vec3(0, 1, 0)));
        if (lat.z < 0.0f) lat = -lat; // lado do polegar (para frente)
        glm::vec3 nrm = glm::cross(lat, ax) * sign; // costas da mão
        s.handAxis[side] = ax;
        s.handLat[side] = lat;
        s.handBack[side] = nrm;
        addBone(f1[side], h, W + ax * 0.105f * k, W + ax * 0.142f * k, kNone, names[side][0]);
        addBone(f2[side], f1[side], W + ax * 0.142f * k, W + ax * 0.173f * k, kNone, names[side][1]);
        addBone(f3[side], f2[side], W + ax * 0.173f * k, W + ax * 0.225f * k, kNone, names[side][2]);
        addBone(th[side], h, W + (ax * 0.040f + lat * 0.025f) * k, W + (ax * 0.135f + lat * 0.090f) * k, kNone, names[side][3]);
        // eixo de dobrar os dedos em direção à palma, já na orientação neutra
        glm::vec3 palmN = -nrm;
        s.curlAxis[side] = glm::normalize(s.bindRot[h] * glm::cross(ax, palmN));
        s.thumbAxis[side] = glm::normalize(s.bindRot[h] * glm::cross(glm::normalize(ax * 0.3f + lat), palmN));
    }
    s.heelOffset = heel;
    s.toeOffset = toe;
    return s;
}

} // namespace

Skeleton Skeleton::makeCrashDummy() {
    // Medidas tiradas das peças da malha (metros, pés em y = 0).
    const Def defs[kBodyBones] = {
        {HIPS,    -1,      {0.000f, 0.930f, -0.010f}, {0.000f, 1.060f, -0.010f}, kNone, "hips"},
        {SPINE,   HIPS,    {0.000f, 1.060f, -0.010f}, {0.000f, 1.150f, -0.020f}, kNone, "spine"},
        {CHEST,   SPINE,   {0.000f, 1.150f, -0.020f}, {0.000f, 1.460f, -0.040f}, kNone, "chest"},
        {NECK,    CHEST,   {0.000f, 1.460f, -0.040f}, {0.000f, 1.530f, -0.020f}, kNone, "neck"},
        {HEAD,    NECK,    {0.000f, 1.530f, -0.020f}, {0.000f, 1.680f,  0.000f}, kNone, "head"},

        {CLAV_L,  CHEST,   { 0.030f, 1.400f, -0.070f}, { 0.170f, 1.375f, -0.090f}, kNone, "clavicle_l"},
        {UARM_L,  CLAV_L,  { 0.170f, 1.375f, -0.090f}, { 0.345f, 1.175f, -0.105f}, kUpperArm, "upperarm_l"},
        {FARM_L,  UARM_L,  { 0.345f, 1.175f, -0.105f}, { 0.515f, 1.045f, -0.005f}, kForeArm, "forearm_l"},
        {HAND_L,  FARM_L,  { 0.515f, 1.045f, -0.005f}, { 0.610f, 0.900f,  0.040f}, kHand, "hand_l"},

        {CLAV_R,  CHEST,   {-0.030f, 1.400f, -0.070f}, {-0.170f, 1.375f, -0.090f}, kNone, "clavicle_r"},
        {UARM_R,  CLAV_R,  {-0.170f, 1.375f, -0.090f}, {-0.345f, 1.175f, -0.105f}, kUpperArm * kMirror, "upperarm_r"},
        {FARM_R,  UARM_R,  {-0.345f, 1.175f, -0.105f}, {-0.515f, 1.045f, -0.005f}, kForeArm * kMirror, "forearm_r"},
        {HAND_R,  FARM_R,  {-0.515f, 1.045f, -0.005f}, {-0.610f, 0.900f,  0.040f}, kHand * kMirror, "hand_r"},

        {THIGH_L, HIPS,    { 0.100f, 0.880f,  0.000f}, { 0.130f, 0.490f, -0.020f}, kNone, "thigh_l"},
        {SHIN_L,  THIGH_L, { 0.130f, 0.490f, -0.020f}, { 0.165f, 0.110f, -0.070f}, kNone, "shin_l"},
        {FOOT_L,  SHIN_L,  { 0.165f, 0.110f, -0.070f}, { 0.170f, 0.020f,  0.110f}, kNone, "foot_l"},

        {THIGH_R, HIPS,    {-0.100f, 0.880f,  0.000f}, {-0.130f, 0.490f, -0.020f}, kNone, "thigh_r"},
        {SHIN_R,  THIGH_R, {-0.130f, 0.490f, -0.020f}, {-0.165f, 0.110f, -0.070f}, kNone, "shin_r"},
        {FOOT_R,  SHIN_R,  {-0.165f, 0.110f, -0.070f}, {-0.170f, 0.020f,  0.110f}, kNone, "foot_r"},
    };
    return build(defs, 0.105f, glm::vec3(0.0f, -0.105f, -0.07f), glm::vec3(0.0f, -0.105f, 0.185f));
}

Skeleton Skeleton::makeFromRig(const RigJoints& r) {
    const glm::vec3 neck = r.neck;
    const Def defs[kBodyBones] = {
        {HIPS,    -1,      r.pelvis,      r.spine,               kNone, "hips"},
        {SPINE,   HIPS,    r.spine,       r.chest,               kNone, "spine"},
        {CHEST,   SPINE,   r.chest,       neck,                  kNone, "chest"},
        {NECK,    CHEST,   neck,          r.head,                kNone, "neck"},
        {HEAD,    NECK,    r.head,        r.headTop,             kNone, "head"},
        {CLAV_L,  CHEST,   r.clavicle[0], r.shoulder[0],         kNone, "clavicle_l"},
        {UARM_L,  CLAV_L,  r.shoulder[0], r.elbow[0],            kUpperArm, "upperarm_l"},
        {FARM_L,  UARM_L,  r.elbow[0],    r.wrist[0],            kForeArm, "forearm_l"},
        {HAND_L,  FARM_L,  r.wrist[0],    r.knuckles[0],         kHand, "hand_l"},
        {CLAV_R,  CHEST,   r.clavicle[1], r.shoulder[1],         kNone, "clavicle_r"},
        {UARM_R,  CLAV_R,  r.shoulder[1], r.elbow[1],            kUpperArm * kMirror, "upperarm_r"},
        {FARM_R,  UARM_R,  r.elbow[1],    r.wrist[1],            kForeArm * kMirror, "forearm_r"},
        {HAND_R,  FARM_R,  r.wrist[1],    r.knuckles[1],         kHand * kMirror, "hand_r"},
        {THIGH_L, HIPS,    r.hip[0],      r.knee[0],             kNone, "thigh_l"},
        {SHIN_L,  THIGH_L, r.knee[0],     r.ankle[0],            kNone, "shin_l"},
        {FOOT_L,  SHIN_L,  r.ankle[0],    r.toe[0],              kNone, "foot_l"},
        {THIGH_R, HIPS,    r.hip[1],      r.knee[1],             kNone, "thigh_r"},
        {SHIN_R,  THIGH_R, r.knee[1],     r.ankle[1],            kNone, "shin_r"},
        {FOOT_R,  SHIN_R,  r.ankle[1],    r.toe[1],              kNone, "foot_r"},
    };
    float palm = glm::length(r.knuckles[1] - r.wrist[1]);
    float ankleH = r.ankle[0].y;
    float heelBack = 0.06f, toeFront = glm::length(glm::vec2(r.toe[0].x - r.ankle[0].x, r.toe[0].z - r.ankle[0].z)) + 0.09f;
    return build(defs, palm, glm::vec3(0.0f, -ankleH, -heelBack), glm::vec3(0.0f, -ankleH, toeFront));
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
