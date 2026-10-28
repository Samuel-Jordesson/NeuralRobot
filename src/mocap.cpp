#define GLM_ENABLE_EXPERIMENTAL
#include "mocap.h"

#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/scene.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace {

std::string shortName(const std::string& n) {
    size_t p = n.rfind(':');
    return p == std::string::npos ? n : n.substr(p + 1);
}

// Hierarquia do Mixamo achatada (pai sempre antes do filho).
struct SrcRig {
    std::vector<const aiNode*> nodes;
    std::vector<int> parent;
    std::vector<const aiNodeAnim*> chan;
    std::map<std::string, int> byName;
};

void flatten(const aiNode* n, int par, SrcRig& r, const std::map<std::string, const aiNodeAnim*>& chans) {
    int id = (int)r.nodes.size();
    r.nodes.push_back(n);
    r.parent.push_back(par);
    auto it = chans.find(n->mName.C_Str());
    r.chan.push_back(it == chans.end() ? nullptr : it->second);
    r.byName.emplace(shortName(n->mName.C_Str()), id);
    for (unsigned c = 0; c < n->mNumChildren; ++c) flatten(n->mChildren[c], id, r, chans);
}

glm::mat4 toGlm(const aiMatrix4x4& m) { return glm::transpose(glm::make_mat4(&m.a1)); }

template <class Key>
void bracket(const Key* k, unsigned n, double t, unsigned& a, unsigned& b, float& f) {
    if (n == 1 || t <= k[0].mTime) { a = b = 0; f = 0; return; }
    if (t >= k[n - 1].mTime) { a = b = n - 1; f = 0; return; }
    unsigned i = 0;
    while (i + 1 < n && k[i + 1].mTime <= t) ++i;
    a = i;
    b = i + 1;
    f = (float)((t - k[a].mTime) / (k[b].mTime - k[a].mTime));
}

glm::mat4 sampleLocal(const aiNodeAnim* c, double t) {
    unsigned a, b;
    float f;
    bracket(c->mPositionKeys, c->mNumPositionKeys, t, a, b, f);
    const aiVector3D &p0 = c->mPositionKeys[a].mValue, &p1 = c->mPositionKeys[b].mValue;
    glm::vec3 p = glm::mix(glm::vec3(p0.x, p0.y, p0.z), glm::vec3(p1.x, p1.y, p1.z), f);
    bracket(c->mRotationKeys, c->mNumRotationKeys, t, a, b, f);
    const aiQuaternion &q0 = c->mRotationKeys[a].mValue, &q1 = c->mRotationKeys[b].mValue;
    glm::quat q = glm::slerp(glm::quat(q0.w, q0.x, q0.y, q0.z), glm::quat(q1.w, q1.x, q1.y, q1.z), f);
    glm::vec3 s(1.0f);
    if (c->mNumScalingKeys) {
        bracket(c->mScalingKeys, c->mNumScalingKeys, t, a, b, f);
        const aiVector3D &s0 = c->mScalingKeys[a].mValue, &s1 = c->mScalingKeys[b].mValue;
        s = glm::mix(glm::vec3(s0.x, s0.y, s0.z), glm::vec3(s1.x, s1.y, s1.z), f);
    }
    return glm::translate(glm::mat4(1.0f), p) * glm::mat4_cast(q) * glm::scale(glm::mat4(1.0f), s);
}

void globalsAt(const SrcRig& r, double t, bool rest, std::vector<glm::mat4>& G) {
    G.resize(r.nodes.size());
    for (size_t i = 0; i < r.nodes.size(); ++i) {
        glm::mat4 local = (!rest && r.chan[i]) ? sampleLocal(r.chan[i], t) : toGlm(r.nodes[i]->mTransformation);
        G[i] = r.parent[i] < 0 ? local : G[r.parent[i]] * local;
    }
}

glm::vec3 posOf(const glm::mat4& m) { return glm::vec3(m[3]); }
glm::quat rotOf(const glm::mat4& m) {
    glm::mat3 r(m);
    r[0] = glm::normalize(r[0]);
    r[1] = glm::normalize(r[1]);
    r[2] = glm::normalize(r[2]);
    return glm::normalize(glm::quat_cast(r));
}

// Osso nosso <- osso do Mixamo (e o filho que define a direção do osso na T-pose)
struct BoneMap { Bone bone; const char* src; const char* dirChild; };
const BoneMap kMap[kBodyBones] = {
    {HIPS, "Hips", "Spine"},          {SPINE, "Spine", "Spine1"},      {CHEST, "Spine2", "Neck"},
    {NECK, "Neck", "Head"},           {HEAD, "Head", "HeadTop_End"},
    {CLAV_L, "LeftShoulder", "LeftArm"},   {UARM_L, "LeftArm", "LeftForeArm"},
    {FARM_L, "LeftForeArm", "LeftHand"},   {HAND_L, "LeftHand", "LeftHandMiddle1"},
    {CLAV_R, "RightShoulder", "RightArm"}, {UARM_R, "RightArm", "RightForeArm"},
    {FARM_R, "RightForeArm", "RightHand"}, {HAND_R, "RightHand", "RightHandMiddle1"},
    {THIGH_L, "LeftUpLeg", "LeftLeg"},     {SHIN_L, "LeftLeg", "LeftFoot"},   {FOOT_L, "LeftFoot", "LeftToeBase"},
    {THIGH_R, "RightUpLeg", "RightLeg"},   {SHIN_R, "RightLeg", "RightFoot"}, {FOOT_R, "RightFoot", "RightToeBase"},
};

float lowestFootPoint(const Skeleton& sk, const Pose& p) {
    glm::mat4 g[BONE_COUNT], skin[BONE_COUNT];
    computeGlobals(sk, p, g);
    computeSkinMatrices(sk, g, skin);
    float m = 1e9f;
    for (Bone f : {FOOT_L, FOOT_R})
        for (const glm::vec3& o : {sk.heelOffset, sk.toeOffset})
            m = std::min(m, (skin[f] * glm::vec4(sk.bindJoint[f] + o, 1.0f)).y);
    return m;
}

} // namespace

Pose MocapClip::sample(float phase) const {
    const int n = (int)frames.size();
    float f = phase + phaseOffset;
    f = (f - std::floor(f)) * n;
    int i0 = (int)std::floor(f) % n;
    int i1 = (i0 + 1) % n;
    return blendPose(frames[i0], frames[i1], f - std::floor(f));
}

bool loadMixamoClip(const std::string& path, const Skeleton& sk, MocapClip& out) {
    Assimp::Importer imp;
    // Junta os nós auxiliares de pivô do FBX: cada osso vira um nó só com a animação completa.
    imp.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
    const aiScene* sc = imp.ReadFile(path, 0);
    if (!sc || !sc->mNumAnimations) {
        std::fprintf(stderr, "[mocap] falha ao ler %s: %s\n", path.c_str(), sc ? "sem animacao" : imp.GetErrorString());
        return false;
    }
    const aiAnimation* anim = sc->mAnimations[0];
    std::map<std::string, const aiNodeAnim*> chans;
    for (unsigned i = 0; i < anim->mNumChannels; ++i) chans[anim->mChannels[i]->mNodeName.C_Str()] = anim->mChannels[i];

    SrcRig src;
    flatten(sc->mRootNode, -1, src, chans);
    int srcIdx[kBodyBones], dirIdx[kBodyBones];
    for (const BoneMap& m : kMap) {
        auto a = src.byName.find(m.src), b = src.byName.find(m.dirChild);
        if (a == src.byName.end() || b == src.byName.end()) {
            std::fprintf(stderr, "[mocap] %s: osso %s/%s nao encontrado\n", path.c_str(), m.src, m.dirChild);
            return false;
        }
        srcIdx[m.bone] = a->second;
        dirIdx[m.bone] = b->second;
    }

    std::vector<glm::mat4> rest, G;
    globalsAt(src, 0.0, true, rest);

    // Alinhamento: leva cada osso da nossa pose neutra para a orientação que ele teria na T-pose do Mixamo.
    glm::quat align[kBodyBones], restRot[kBodyBones];
    for (int b = 0; b < kBodyBones; ++b) {
        restRot[b] = rotOf(rest[srcIdx[b]]);
        glm::vec3 mixDir = glm::normalize(posOf(rest[dirIdx[b]]) - posOf(rest[srcIdx[b]]));
        glm::vec3 ourDir = glm::normalize(sk.tail[b] - sk.joint[b]);
        align[b] = b == HIPS ? glm::quat(1, 0, 0, 0) : glm::rotation(ourDir, mixDir);
    }

    const int hips = srcIdx[HIPS];
    const float scale = sk.joint[THIGH_L].y / posOf(rest[srcIdx[THIGH_L]]).y;
    const double tps = anim->mTicksPerSecond > 0 ? anim->mTicksPerSecond : 30.0;
    const double dur = anim->mDuration;
    const int n = std::max(2, (int)std::lround(dur));

    globalsAt(src, 0.0, false, G);
    glm::vec3 hipStart = posOf(G[hips]);
    globalsAt(src, dur, false, G);
    glm::vec3 travel = posOf(G[hips]) - hipStart;
    travel.y = 0.0f;

    out.frames.assign(n, Pose());
    std::vector<float> leftReach(n);
    glm::vec3 meanOffset(0.0f);
    for (int k = 0; k < n; ++k) {
        double t = dur * k / n;
        globalsAt(src, t, false, G);
        glm::quat world[kBodyBones];
        Pose& p = out.frames[k];
        for (int b = 0; b < kBodyBones; ++b) { // dedos ficam neutros
            glm::quat delta = rotOf(G[srcIdx[b]]) * glm::inverse(restRot[b]);
            world[b] = glm::normalize(delta * align[b]);
            int par = sk.parent[b];
            p.rot[b] = par < 0 ? world[b] : glm::normalize(glm::inverse(world[par]) * world[b]);
        }
        // root motion removido (quem move é a física); fica só o balanço do quadril
        glm::vec3 hip = posOf(G[hips]) - travel * (float)(k / (double)n) - posOf(rest[hips]);
        p.rootOffset = hip * scale;
        meanOffset += p.rootOffset;
        leftReach[k] = posOf(G[srcIdx[FOOT_L]]).z - posOf(G[hips]).z;
    }
    meanOffset /= (float)n;
    float lowest = 1e9f;
    for (Pose& p : out.frames) {
        p.rootOffset.x -= meanOffset.x;
        p.rootOffset.z -= meanOffset.z;
        lowest = std::min(lowest, lowestFootPoint(sk, p));
    }
    // proporções diferentes: apoia o ponto mais baixo do ciclo no chão (preserva a fase de voo da corrida)
    for (Pose& p : out.frames) p.rootOffset.y -= lowest;

    out.duration = (float)(dur / tps);
    out.cycleLength = glm::length(travel) * scale;
    out.phaseOffset = (float)(std::max_element(leftReach.begin(), leftReach.end()) - leftReach.begin()) / n;
    std::printf("[mocap] %s: %d quadros, %.2f s, passada %.2f m, velocidade %.2f m/s\n",
                shortName(path.substr(path.find_last_of('/') + 1)).c_str(), n, out.duration, out.cycleLength,
                out.naturalSpeed());
    return true;
}

float clipMeanYaw(const MocapClip& c) {
    glm::vec2 sum(0.0f);
    for (const Pose& p : c.frames) {
        // peito (costas na parede): quadril * coluna * peito, todos neutros = identidade
        glm::vec3 f = p.rot[HIPS] * p.rot[SPINE] * p.rot[CHEST] * glm::vec3(0, 0, 1);
        sum += glm::normalize(glm::vec2(f.x, f.z) + glm::vec2(1e-6f));
    }
    return std::atan2(sum.x, sum.y);
}

void removeClipYaw(MocapClip& c) {
    glm::quat q = glm::angleAxis(-clipMeanYaw(c), glm::vec3(0, 1, 0));
    for (Pose& p : c.frames) {
        p.rot[HIPS] = glm::normalize(q * p.rot[HIPS]);
        p.rootOffset = q * p.rootOffset;
    }
}
