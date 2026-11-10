#include "mocap_video.h"

#include "aim_ik.h"
#include "anim_editor.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

namespace {
// Menor giro que leva a direção a até b.
glm::quat rotBetween(glm::vec3 a, glm::vec3 b) {
    a = glm::normalize(a);
    b = glm::normalize(b);
    float d = glm::dot(a, b);
    if (d > 0.99999f) return glm::quat(1, 0, 0, 0);
    if (d < -0.9999f) {
        glm::vec3 ax = std::fabs(a.x) < 0.9f ? glm::cross(a, glm::vec3(1, 0, 0)) : glm::cross(a, glm::vec3(0, 1, 0));
        return glm::angleAxis(3.14159265f, glm::normalize(ax));
    }
    glm::vec3 c = glm::cross(a, b);
    return glm::normalize(glm::quat(1.0f + d, c.x, c.y, c.z));
}

// Orientação com X = lado esquerdo e Y = para cima (Z = frente).
glm::quat frame(glm::vec3 x, const glm::vec3& up) {
    x = glm::normalize(x);
    glm::vec3 y = glm::normalize(up - x * glm::dot(up, x));
    return glm::normalize(glm::quat_cast(glm::mat3(x, y, glm::cross(x, y))));
}

// Base ortonormal a partir de um eixo principal e um segundo eixo aproximado.
glm::mat3 basis(glm::vec3 a, glm::vec3 b) {
    a = glm::normalize(a);
    b = glm::normalize(b - a * glm::dot(b, a));
    return glm::mat3(a, b, glm::cross(a, b));
}

struct VFrame {
    float t = 0.0f;
    glm::vec3 L[33];
    float handW[2] = {0, 0};
    glm::vec3 H[2][21];
    float headW = 0.0f;
    glm::quat head{1, 0, 0, 0};
};

bool readFrames(const std::string& path, std::vector<VFrame>& out) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream l(line);
        std::string k;
        if (!(l >> k) || k[0] == '#') continue;
        if (k == "f") { out.emplace_back(); l >> out.back().t; }
        else if (out.empty()) continue;
        else if (k == "p") { for (glm::vec3& p : out.back().L) l >> p.x >> p.y >> p.z; }
        else if (k == "h") {
            int s;
            l >> s;
            if (s < 0 || s > 1) continue;
            l >> out.back().handW[s];
            for (glm::vec3& p : out.back().H[s]) l >> p.x >> p.y >> p.z;
        } else if (k == "q") {
            glm::quat& q = out.back().head;
            l >> out.back().headW >> q.x >> q.y >> q.z >> q.w;
            q = glm::normalize(q);
        }
    }
    return !out.empty();
}

// Pose de um quadro do vídeo.
Pose solveFrame(const Skeleton& sk, const VFrame& f, bool seated) {
    const glm::vec3* L = f.L;
    const glm::vec3 midHip = (L[23] + L[24]) * 0.5f, midSh = (L[11] + L[12]) * 0.5f, earMid = (L[7] + L[8]) * 0.5f;
    const glm::vec3 up(0, 1, 0);
    glm::quat W[BONE_COUNT];
    for (int b = 0; b < BONE_COUNT; ++b) W[b] = sk.parent[b] >= 0 ? W[sk.parent[b]] : glm::quat(1, 0, 0, 0);
    auto limb = [&](int b, const glm::vec3& dir) {
        const glm::quat parent = W[sk.parent[b]];
        W[b] = rotBetween(parent * (sk.tail[b] - sk.joint[b]), dir) * parent;
    };

    // tronco: sentado, o quadril fica de pé e só gira junto com os ombros; o peito pega a inclinação
    glm::vec3 side = L[11] - L[12];
    if (seated) W[HIPS] = frame(glm::vec3(side.x, 0.0f, side.z), up);
    else W[HIPS] = frame(L[23] - L[24], midSh - midHip);
    W[CHEST] = frame(side, midSh - midHip);
    W[SPINE] = glm::slerp(W[HIPS], W[CHEST], 0.5f);
    // cabeça: giro do rosto quando ele aparece; senão, pelas orelhas
    glm::quat headPose = frame(L[7] - L[8], earMid - midSh);
    W[HEAD] = glm::slerp(headPose, f.head, std::clamp(f.headW, 0.0f, 1.0f));
    W[NECK] = glm::slerp(W[CHEST], W[HEAD], 0.5f);
    W[CLAV_L] = W[CLAV_R] = W[CHEST];

    // braços
    limb(UARM_L, L[13] - L[11]);
    limb(FARM_L, L[15] - L[13]);
    limb(UARM_R, L[14] - L[12]);
    limb(FARM_R, L[16] - L[14]);

    // mãos: base (dedos, lado do polegar) na pose neutra -> base medida
    const int handB[2] = {HAND_L, HAND_R}, wrist[2] = {15, 16}, pinky[2] = {17, 18}, index[2] = {19, 20};
    const int f1[2] = {FING1_L, FING1_R}, f2[2] = {FING2_L, FING2_R}, f3[2] = {FING3_L, FING3_R}, th[2] = {THUMB_L, THUMB_R};
    for (int s = 0; s < 2; ++s) {
        const int h = handB[s];
        glm::mat3 neutral = basis(sk.tail[h] - sk.joint[h], sk.bindRot[h] * sk.handLat[s]);
        // pelo corpo: pulso -> meio entre indicador e mindinho; lado do polegar = indicador - mindinho
        glm::vec3 tip = (L[pinky[s]] + L[index[s]]) * 0.5f;
        glm::quat fromBody = glm::normalize(glm::quat_cast(basis(tip - L[wrist[s]], L[index[s]] - L[pinky[s]]) * glm::transpose(neutral)));
        // pela mão detectada (bem mais precisa: punho girado, joinha, palmas juntas)
        const glm::vec3* H = f.H[s];
        const float w = std::clamp(f.handW[s], 0.0f, 1.0f);
        glm::quat fromHand = glm::normalize(glm::quat_cast(basis(H[9] - H[0], H[5] - H[17]) * glm::transpose(neutral)));
        if (glm::dot(fromBody, fromHand) < 0.0f) fromHand = -fromHand;
        W[h] = glm::normalize(glm::slerp(fromBody, fromHand, w));

        // dedos (os 4 juntos): média das direções de cada falange; polegar: da base até a ponta
        auto avgDir = [&](int a) {
            glm::vec3 d(0.0f);
            for (int k : {5, 9, 13, 17}) d += glm::normalize(H[k + a + 1] - H[k + a]);
            return d;
        };
        Pose relaxed;
        setFingerCurl(sk, relaxed, s, 0.35f, 0.2f);
        auto finger = [&](int b, const glm::vec3& dir) {
            glm::quat rest = W[sk.parent[b]] * relaxed.rot[b];
            if (w < 0.01f) { W[b] = rest; return; }
            limb(b, dir);
            if (glm::dot(W[b], rest) < 0.0f) W[b] = -W[b];
            W[b] = glm::normalize(glm::slerp(rest, W[b], w));
        };
        finger(f1[s], avgDir(0));
        finger(f2[s], avgDir(1));
        finger(f3[s], avgDir(2));
        // polegar: sem a mão detectada, o ponto do polegar do corpo ainda diz para onde ele aponta (joinha)
        {
            const int thumbLm[2] = {21, 22};
            limb(th[s], L[thumbLm[s]] - L[wrist[s]]);
            glm::quat fromPose = W[th[s]];
            limb(th[s], H[4] - H[2]);
            if (glm::dot(W[th[s]], fromPose) < 0.0f) W[th[s]] = -W[th[s]];
            W[th[s]] = glm::normalize(glm::slerp(fromPose, W[th[s]], w));
        }
    }

    // pernas
    if (seated) {
        const glm::vec3 fwd = W[HIPS] * glm::vec3(0, 0, 1), left = W[HIPS] * glm::vec3(1, 0, 0);
        limb(THIGH_L, fwd - up * 0.08f + left * 0.12f);
        limb(SHIN_L, -up - fwd * 0.05f);
        limb(FOOT_L, fwd - up * 0.3f);
        limb(THIGH_R, fwd - up * 0.08f - left * 0.12f);
        limb(SHIN_R, -up - fwd * 0.05f);
        limb(FOOT_R, fwd - up * 0.3f);
    } else {
        limb(THIGH_L, L[25] - L[23]);
        limb(SHIN_L, L[27] - L[25]);
        limb(FOOT_L, L[31] - L[27]);
        limb(THIGH_R, L[26] - L[24]);
        limb(SHIN_R, L[28] - L[26]);
        limb(FOOT_R, L[32] - L[28]);
    }

    Pose p;
    for (int b = 0; b < BONE_COUNT; ++b)
        p.rot[b] = sk.parent[b] < 0 ? W[b] : glm::normalize(glm::inverse(W[sk.parent[b]]) * W[b]);
    // pés no chão: o pé mais baixo encosta onde estaria na pose neutra
    glm::mat4 G[BONE_COUNT], G0[BONE_COUNT];
    computeGlobals(sk, p, G);
    computeGlobals(sk, Pose(), G0);
    auto lowest = [&](const glm::mat4* g) {
        float y = 1e9f;
        for (int b : {FOOT_L, FOOT_R}) {
            y = std::min(y, g[b][3].y);
            y = std::min(y, (g[b] * glm::vec4(sk.tail[b] - sk.joint[b], 1.0f)).y);
        }
        return y;
    };
    p.rootOffset.y = lowest(G0) - lowest(G);
    return p;
}
// ---------------------------------------------------------------- ajustes feitos à mão
// anims/<nome>.ajustes corrige o que a detecção errou, trecho por trecho (entra e sai suave):
//   mao esq|dir t0 t1 rampa  x y z  dedos fx fy fz  palma px py pz  cotovelo ex ey ez  curva polegar
//     pulso em (x y z) metros a partir do meio dos ombros (+X esquerda do personagem, +Y cima, +Z frente),
//     dedos = para onde a mão aponta, palma = para onde a palma olha, cotovelo = para onde o cotovelo vai
//     (do ombro), curva 0..1 fecha os dedos, polegar 0..1 dobra o polegar para a palma
//   tronco t0 t1 rampa graus     inclina o tronco para a frente (+) ou para trás (-)
//   cabeca t0 t1 rampa graus     inclina a cabeça para baixo (+) ou para cima (-)
struct Fix {
    enum Kind { Hand, Torso, Head } kind = Hand;
    bool right = false;
    float t0 = 0, t1 = 0, ramp = 0.15f;
    glm::vec3 pos{0.0f}, fingers{0, 0, 1}, palm{0, -1, 0}, elbow{0.45f, -0.55f, -0.25f};
    float curl = 0.3f, thumb = 0.2f, deg = 0.0f;
    float weight(float t) const {
        if (t < t0 - ramp || t > t1 + ramp) return 0.0f;
        float a = ramp > 0.0f ? glm::smoothstep(t0 - ramp, t0, t) : 1.0f;
        float b = ramp > 0.0f ? 1.0f - glm::smoothstep(t1, t1 + ramp, t) : 1.0f;
        return std::min(a, b);
    }
};

std::vector<Fix> readFixes(const std::string& path) {
    std::vector<Fix> out;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream l(line);
        std::string k, side;
        if (!(l >> k) || k[0] == '#') continue;
        Fix f;
        if (k == "mao") {
            l >> side >> f.t0 >> f.t1 >> f.ramp >> f.pos.x >> f.pos.y >> f.pos.z >> f.fingers.x >> f.fingers.y >> f.fingers.z >>
                f.palm.x >> f.palm.y >> f.palm.z >> f.elbow.x >> f.elbow.y >> f.elbow.z >> f.curl >> f.thumb;
            f.right = side == "dir";
            if (l) out.push_back(f);
        } else if (k == "tronco" || k == "cabeca") {
            f.kind = k == "tronco" ? Fix::Torso : Fix::Head;
            l >> f.t0 >> f.t1 >> f.ramp >> f.deg;
            if (l) out.push_back(f);
        }
    }
    return out;
}

void applyFixes(const Skeleton& sk, Pose& p, const std::vector<Fix>& fixes, float t) {
    const glm::quat id(1, 0, 0, 0);
    for (const Fix& f : fixes) { // inclinações primeiro (as mãos são medidas a partir dos ombros)
        float w = f.weight(t);
        if (w <= 0.0f || f.kind == Fix::Hand) continue;
        glm::quat r = glm::angleAxis(glm::radians(f.deg) * w * (f.kind == Fix::Torso ? 0.5f : 1.0f), glm::vec3(1, 0, 0));
        if (f.kind == Fix::Torso) {
            p.rot[SPINE] = glm::normalize(r * p.rot[SPINE]);
            p.rot[CHEST] = glm::normalize(r * p.rot[CHEST]);
        } else {
            p.rot[HEAD] = glm::normalize(r * p.rot[HEAD]);
        }
    }
    for (const Fix& f : fixes) {
        float w = f.weight(t);
        if (w <= 0.0f || f.kind != Fix::Hand) continue;
        glm::mat4 G[BONE_COUNT];
        computeGlobals(sk, p, G);
        const int s = f.right ? 1 : 0, h = f.right ? HAND_R : HAND_L;
        const glm::vec3 mid = (glm::vec3(G[UARM_L][3]) + glm::vec3(G[UARM_R][3])) * 0.5f;
        // lado do polegar a partir dos dedos e da palma (mesma convenção das costas da mão do esqueleto)
        const glm::vec3 ax = glm::normalize(f.fingers);
        const float sign = s == 0 ? 1.0f : -1.0f;
        glm::vec3 lat = -sign * glm::cross(ax, glm::normalize(f.palm));
        if (glm::length(lat) < 1e-3f) lat = glm::vec3(0, 0, 1);
        glm::mat3 neutral = basis(sk.tail[h] - sk.joint[h], sk.bindRot[h] * sk.handLat[s]);
        glm::mat4 hand = glm::translate(glm::mat4(1.0f), mid + f.pos) * glm::mat4(basis(ax, lat) * glm::transpose(neutral));
        applyHandTargetPole(sk, p, f.right, hand, f.elbow, w);
        Pose fing = p;
        setFingerCurl(sk, fing, s, f.curl, f.thumb);
        for (int b : {f.right ? FING1_R : FING1_L, f.right ? FING2_R : FING2_L, f.right ? FING3_R : FING3_L, f.right ? THUMB_R : THUMB_L})
            p.rot[b] = glm::normalize(glm::slerp(p.rot[b], glm::dot(p.rot[b], fing.rot[b]) < 0 ? -fing.rot[b] : fing.rot[b], w));
    }
    (void)id;
}
} // namespace

bool bakeVideoMocap(const Skeleton& sk, const std::string& in, const std::string& outAnim, bool seated, std::string& msg) {
    if (std::getenv("GA_SKDBG"))
        for (int b : {HIPS, CHEST, NECK, HEAD, UARM_L, FARM_L, HAND_L, UARM_R, FARM_R, HAND_R})
            std::printf("[sk] %s %.3f %.3f %.3f tail %.3f %.3f %.3f\n", sk.name[b], sk.joint[b].x, sk.joint[b].y, sk.joint[b].z,
                        sk.tail[b].x, sk.tail[b].y, sk.tail[b].z);
    std::vector<VFrame> frames;
    if (!readFrames(in, frames)) { msg = "não li " + in; return false; }
    std::string fixPath = outAnim;
    if (fixPath.size() > 5 && fixPath.substr(fixPath.size() - 5) == ".anim") fixPath.replace(fixPath.size() - 5, 5, ".ajustes");
    const std::vector<Fix> fixes = readFixes(fixPath);
    AnimClip clip;
    clip.linear = true;
    clip.loop = true;
    for (const VFrame& f : frames) {
        Pose p = solveFrame(sk, f, seated);
        applyFixes(sk, p, fixes, f.t);
        // quaternions vizinhos no mesmo hemisfério (o slerp entre chaves não dá a volta longa)
        for (int b = 0; b < BONE_COUNT; ++b) {
            if (!clip.bones[b].empty() && glm::dot(clip.bones[b].back().q, p.rot[b]) < 0.0f) p.rot[b] = -p.rot[b];
            clip.bones[b].push_back({f.t, p.rot[b]});
        }
        clip.root.push_back({f.t, p.rootOffset});
    }
    clip.length = std::max(0.1f, frames.back().t);
    if (!clip.save(outAnim)) { msg = "não gravei " + outAnim; return false; }
    // altura do assento: o quadril sentado (para pôr a cadeira certa na cena)
    glm::mat4 G[BONE_COUNT];
    computeGlobals(sk, clip.sample(0.0f), G);
    char buf[200];
    std::snprintf(buf, sizeof(buf), "%zu quadros, %.2f s, %zu ajustes -> %s (quadril a %.2f m do chão)", frames.size(),
                  clip.length, fixes.size(), outAnim.c_str(), G[HIPS][3].y);
    msg = buf;
    return true;
}
