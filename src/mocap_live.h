#pragma once
#include "qrcode.h"
#include "skeleton.h"

#include <glm/glm.hpp>
#include <string>
#include <vector>

// Captura de movimento pela câmera (do PC ou do celular): a página tools/mocap/index.html detecta o
// corpo (MediaPipe) e manda os 33 pontos + o giroscópio do celular; o servidor tools/mocap/server.js
// repassa por UDP; aqui viram a pose do nosso esqueleto, ao vivo, e podem ser gravados.
class MocapLive {
public:
    ~MocapLive();

    // Sobe o servidor (node) se ainda não está rodando. false se não conseguiu.
    bool startServer(const std::string& root);
    void openPcCamera();             // abre a página da câmera do PC no navegador
    const std::string& phoneUrl();   // endereço para o celular (https na rede local)
    const QrCode& phoneQr();

    // Lê os quadros que chegaram e atualiza a pose. Chame todo quadro.
    void poll(const Skeleton& sk, float dt);
    bool live() const { return liveT_ < 1.0f; } // chegou quadro há menos de 1 s
    const char* source() const { return src_ == 1 ? "celular" : "câmera do PC"; }
    float fps() const { return fps_; }
    const Pose& pose() const { return pose_; }
    // giroscópio do celular (alpha, beta, gamma em graus), se chegou há menos de 1 s
    bool oriLive() const { return oriT_ < 1.0f; }
    glm::vec3 orientation() const { return ori_; }

    // opções
    bool moveRoot = false;      // anda pelo palco pela posição na imagem (experimental)
    bool useGyro = true;        // compensa o giro do celular (ele pode girar para acompanhar)
    float smoothing = 0.5f;     // 0 = cru, 0.9 = bem suave

    // gravação
    struct Frame {
        float t;
        Pose pose;
        bool hasOri;
        glm::vec3 ori; // alpha, beta, gamma do celular (graus)
    };
    void startRecording();
    // Posição e giro do celular atuais viram a referência (de frente, no centro do palco).
    void calibrate() { img0_ = img_; ori0_ = ori_; oriRef_ = hasOri_; }
    void stopRecording() { recording_ = false; }
    bool recording() const { return recording_; }
    float recordedTime() const { return frames_.empty() ? 0.0f : frames_.back().t; }
    const std::vector<Frame>& frames() const { return frames_; }

private:
    void solve(const Skeleton& sk);
    int sock_ = -1;
    int pid_ = -1;
    std::string url_;
    QrCode qr_;
    bool qrMade_ = false;

    glm::vec3 lm_[33];     // pontos do corpo no espaço do personagem (suavizados)
    bool haveLm_ = false;
    glm::vec3 img_{0.0f};  // quadril na imagem (x, y) e tamanho do tronco (z)
    glm::vec3 img0_{0.0f}; // referência (início da gravação)
    glm::vec3 ori_{0.0f}, ori0_{0.0f};
    bool hasOri_ = false, oriRef_ = false;
    int src_ = 0;
    Pose pose_;
    float oriT_ = 10.0f;
    float liveT_ = 10.0f, fps_ = 0.0f, fpsAcc_ = 0.0f;
    int fpsCount_ = 0;
    bool recording_ = false;
    double recStart_ = 0.0;
    std::vector<Frame> frames_;
};
