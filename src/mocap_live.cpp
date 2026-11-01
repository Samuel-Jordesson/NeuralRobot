#include "mocap_live.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <glm/gtc/matrix_transform.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace {
constexpr int kGamePort = 47811, kHttpsPort = 47843;

double nowSec() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

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

// Orientação com X = lado esquerdo e Y = para cima (Z = frente), a partir de dois eixos aproximados.
glm::quat frame(glm::vec3 x, const glm::vec3& up) {
    x = glm::normalize(x);
    glm::vec3 y = glm::normalize(up - x * glm::dot(up, x));
    glm::vec3 z = glm::cross(x, y);
    return glm::normalize(glm::quat_cast(glm::mat3(x, y, z)));
}

// IP da rede local (wi-fi/cabo), para o celular achar o computador.
std::string lanIp() {
    ifaddrs* list = nullptr;
    std::string best;
    if (getifaddrs(&list) != 0) return "127.0.0.1";
    for (ifaddrs* a = list; a; a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
        char buf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &((sockaddr_in*)a->ifa_addr)->sin_addr, buf, sizeof(buf));
        std::string ip = buf;
        if (ip.rfind("127.", 0) == 0 || ip.rfind("172.17.", 0) == 0) continue; // loopback e docker
        if (best.empty() || ip.rfind("192.168.", 0) == 0) best = ip;
    }
    freeifaddrs(list);
    return best.empty() ? "127.0.0.1" : best;
}
} // namespace

MocapLive::~MocapLive() {
    if (pid_ > 0) kill(pid_, SIGTERM);
    if (sock_ >= 0) close(sock_);
}

bool MocapLive::startServer(const std::string& root) {
    if (sock_ < 0) {
        sock_ = socket(AF_INET, SOCK_DGRAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(kGamePort);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (sock_ < 0 || bind(sock_, (sockaddr*)&addr, sizeof(addr)) != 0) {
            std::fprintf(stderr, "[mocap] porta UDP %d ocupada\n", kGamePort);
            if (sock_ >= 0) close(sock_);
            sock_ = -1;
            return false;
        }
        fcntl(sock_, F_SETFL, O_NONBLOCK);
    }
    if (pid_ > 0 && waitpid(pid_, nullptr, WNOHANG) == 0) return true; // já está rodando
    const std::string script = root + "/tools/mocap/server.js";
    pid_ = fork();
    if (pid_ == 0) {
        execlp("node", "node", script.c_str(), (char*)nullptr);
        _exit(127);
    }
    if (pid_ < 0) return false;
    usleep(300000);
    return waitpid(pid_, nullptr, WNOHANG) == 0; // ainda vivo = subiu
}

void MocapLive::openPcCamera() {
    if (std::system("xdg-open 'http://localhost:47810/?src=pc' >/dev/null 2>&1 &")) {}
}

const std::string& MocapLive::phoneUrl() {
    if (url_.empty()) url_ = "https://" + lanIp() + ":" + std::to_string(kHttpsPort) + "/";
    return url_;
}

const QrCode& MocapLive::phoneQr() {
    if (!qrMade_) {
        qr_.encode(phoneUrl());
        qrMade_ = true;
    }
    return qr_;
}

void MocapLive::startRecording() {
    frames_.clear();
    recording_ = true;
    recStart_ = nowSec();
    calibrate();
}

void MocapLive::poll(const Skeleton& sk, float dt) {
    liveT_ += dt;
    oriT_ += dt;
    fpsAcc_ += dt;
    if (fpsAcc_ >= 1.0f) { fps_ = fpsCount_ / fpsAcc_; fpsCount_ = 0; fpsAcc_ = 0.0f; }
    if (sock_ < 0) return;
    char buf[16384];
    bool got = false;
    for (;;) {
        ssize_t n = recv(sock_, buf, sizeof(buf) - 1, 0);
        if (n <= 0) break;
        buf[n] = 0;
        std::istringstream in(buf);
        std::string tag, src;
        double t;
        int has, front;
        glm::vec3 o, im;
        if (!(in >> tag >> t >> src)) continue;
        if (tag == "O") { // só o giroscópio
            if (in >> o.x >> o.y >> o.z) { ori_ = o; hasOri_ = true; oriT_ = 0.0f; src_ = src == "cel" ? 1 : 0; }
            continue;
        }
        if (!(in >> has >> o.x >> o.y >> o.z >> im.x >> im.y >> im.z >> front) || tag != "F") continue;
        if (has) oriT_ = 0.0f;
        glm::vec3 p[33];
        float vis[33];
        bool ok = true;
        for (int i = 0; i < 33 && ok; ++i) ok = (bool)(in >> p[i].x >> p[i].y >> p[i].z >> vis[i]);
        if (!ok) continue;
        src_ = src == "cel" ? 1 : 0;
        hasOri_ = has != 0;
        if (hasOri_) {
            ori_ = o;
            if (!oriRef_) { ori0_ = o; oriRef_ = true; } // giro de referência: o primeiro que chegou
        }
        img_ = im;
        // MediaPipe: x para a direita da imagem (lado esquerdo da pessoa), y para baixo, z para longe
        // da câmera -> nosso espaço: +X esquerda, +Y cima, +Z frente (para a câmera)
        glm::quat fix(1, 0, 0, 0);
        if (useGyro && hasOri_) {
            // celular inclinado (beta = 90 em pé) e girado (alpha) desde o começo da gravação
            float down = front ? (o.y - 90.0f) : (90.0f - o.y); // câmera olhando para baixo (graus)
            fix = glm::angleAxis(glm::radians(o.x - ori0_.x), glm::vec3(0, 1, 0)) *
                  glm::angleAxis(glm::radians(-down), glm::vec3(1, 0, 0));
        }
        const float k = haveLm_ ? std::clamp(smoothing, 0.0f, 0.95f) : 0.0f;
        for (int i = 0; i < 33; ++i) {
            glm::vec3 q = fix * glm::vec3(p[i].x, -p[i].y, -p[i].z);
            if (haveLm_ && vis[i] < 0.4f) continue; // ponto escondido: fica onde estava
            lm_[i] = haveLm_ ? glm::mix(q, lm_[i], k) : q;
        }
        haveLm_ = true;
        got = true;
        liveT_ = 0.0f;
        ++fpsCount_;
    }
    if (!got) return;
    solve(sk);
    if (recording_) {
        Frame f;
        f.t = (float)(nowSec() - recStart_);
        f.pose = pose_;
        f.hasOri = hasOri_;
        f.ori = ori_;
        frames_.push_back(f);
    }
}

void MocapLive::solve(const Skeleton& sk) {
    const glm::vec3* L = lm_;
    const glm::vec3 midHip = (L[23] + L[24]) * 0.5f, midSh = (L[11] + L[12]) * 0.5f, earMid = (L[7] + L[8]) * 0.5f;
    glm::quat W[BONE_COUNT]; // giro de cada osso no espaço do personagem (neutro = identidade)
    for (int b = 0; b < BONE_COUNT; ++b) W[b] = sk.parent[b] >= 0 ? W[sk.parent[b]] : glm::quat(1, 0, 0, 0);
    W[HIPS] = frame(L[23] - L[24], midSh - midHip);
    W[CHEST] = frame(L[11] - L[12], midSh - midHip);
    W[SPINE] = glm::slerp(W[HIPS], W[CHEST], 0.5f);
    W[HEAD] = frame(L[7] - L[8], earMid - midSh);
    W[NECK] = glm::slerp(W[CHEST], W[HEAD], 0.5f);
    W[CLAV_L] = W[CLAV_R] = W[CHEST];
    // membros: do giro do pai, o menor giro que aponta o osso para o próximo ponto
    auto limb = [&](int b, const glm::vec3& from, const glm::vec3& to) {
        glm::vec3 d0 = sk.tail[b] - sk.joint[b];
        const glm::quat parent = W[sk.parent[b]];
        W[b] = rotBetween(parent * d0, to - from) * parent;
    };
    // ordem do pai para o filho
    limb(UARM_L, L[11], L[13]);
    limb(FARM_L, L[13], L[15]);
    limb(HAND_L, L[15], (L[17] + L[19]) * 0.5f);
    limb(UARM_R, L[12], L[14]);
    limb(FARM_R, L[14], L[16]);
    limb(HAND_R, L[16], (L[18] + L[20]) * 0.5f);
    limb(THIGH_L, L[23], L[25]);
    limb(SHIN_L, L[25], L[27]);
    limb(FOOT_L, L[27], L[31]);
    limb(THIGH_R, L[24], L[26]);
    limb(SHIN_R, L[26], L[28]);
    limb(FOOT_R, L[28], L[32]);
    for (int b = FING1_L; b < BONE_COUNT; ++b) W[b] = W[sk.parent[b]]; // dedos seguem a mão

    Pose p;
    for (int b = 0; b < BONE_COUNT; ++b)
        p.rot[b] = sk.parent[b] < 0 ? W[b] : glm::normalize(glm::inverse(W[sk.parent[b]]) * W[b]);
    // anda pelo palco: quadril na imagem em relação ao começo (o tronco medido dá a escala)
    if (moveRoot && img_.z > 0.01f && img0_.z > 0.01f) {
        const float torso = 0.52f;
        p.rootOffset.x = (img_.x - img0_.x) * torso / img_.z;
        p.rootOffset.z = torso * 1.1f * (1.0f / img0_.z - 1.0f / img_.z); // tronco maior = mais perto
    }
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
    pose_ = p;
}
