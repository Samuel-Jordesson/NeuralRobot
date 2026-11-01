#pragma once
#include "mocap_live.h"
#include "panel_layout.h"
#include "skeleton.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

struct GLFWwindow;
struct GunDef;

// Animação feita no editor: chaves de rotação por osso, posição do quadril e um objeto (arma) que
// pode estar na mão direita ou solto com chaves próprias. Salva em anims/<nome>.anim.
struct AnimClip {
    std::string name = "nova_animacao";
    float length = 2.0f; // s
    bool loop = true;
    struct BoneKey { float t; glm::quat q; };
    struct RootKey { float t; glm::vec3 p; };
    // objeto: na mão (segue a mão direita) ou solto (posição/rotação no espaço do personagem)
    struct PropKey { float t; bool hand; glm::vec3 p; glm::quat q; };
    std::vector<BoneKey> bones[BONE_COUNT];
    std::vector<RootKey> root;
    int prop = -1; // arma do jogo (índice), -1 = nenhuma
    std::vector<PropKey> propKeys;
    // captura de movimento: chaves densas (sem entrar/sair devagar) e o giroscópio do celular gravado
    bool linear = false;
    struct DevKey { float t; glm::vec3 ori; }; // alpha, beta, gamma (graus)
    std::vector<DevKey> device;
    // câmera de cena (filmagem): posição e giro no espaço do palco, lente (mm) e foco (m)
    struct CamKey { float t; glm::vec3 p; glm::quat q; float focal, focus; };
    std::vector<CamKey> camKeys;

    Pose sample(float t) const;
    bool hasAnyKey() const;
    bool save(const std::string& path) const;
    bool load(const std::string& path);
};

// Editor de animação (botão "Criar animação"): o personagem num palco, ossos clicáveis na vista,
// linha do tempo com keyframes embaixo, biblioteca de animações à esquerda e detalhes à direita.
class AnimEditor {
public:
    bool active() const { return active_; }
    void begin(const std::string& root, const glm::vec3& stagePos, float stageYaw);
    void end() { active_ = false; }

    // Câmera, seleção e arraste de ossos (antes do desenho). Monta view/proj e a pose atual.
    void update(GLFWwindow* win, float dt, const Skeleton& sk, glm::mat4& view, glm::mat4& proj, glm::vec3& eye);
    // Painéis (dentro do quadro do ImGui). Retorna true quando pediu para voltar ao editor de cenário.
    bool drawUi(const Skeleton& sk, const std::vector<std::string>& gunNames);

    const Pose& pose() const { return pose_; }

    // Câmera de cena: a prévia embaixo mostra o que ela vê (o jogo renderiza numa textura e entrega).
    bool previewWanted() const { return active_ && preview_; }
    bool pilotView() const { return active_ && pilot_; } // a vista principal é a própria câmera
    glm::mat4 cineView() const;
    glm::mat4 cineProj(float aspect) const;
    glm::vec3 cineEye() const;
    bool dofOn() const { return dof_; }
    float focusDist() const { return focus_; }
    float focalMm() const { return focal_; }
    float fstop() const { return fstop_; }
    void setPreviewTexture(unsigned tex) { previewTex_ = tex; }
    glm::mat4 stageMatrix() const;
    // Objeto da cena (arma) neste quadro: índice (-1 nenhum) e matriz no mundo (espaço canônico da arma).
    int propGun() const { return clip_.prop; }
    glm::mat4 propMatrix(const glm::mat4 globals[BONE_COUNT], const GunDef& def) const;

private:
    struct Rect { float x0, y0, x1, y1; };
    Rect viewRect() const;
    bool project(const glm::vec3& p, float& sx, float& sy) const;
    void scanLibrary();
    void setBoneKey(int b, float t, const glm::quat& q);
    void setRootKey(float t, const glm::vec3& p);
    void keyAll(const Skeleton& sk);
    void deleteKeysAt(float t, bool onlySelected);
    void timelineUi(float x0, float y0, float w, float h);

    bool active_ = false;
    std::string root_;
    glm::vec3 stagePos_{0.0f};
    float stageYaw_ = 0.0f;
    // câmera em volta do personagem
    float camYaw_ = 3.14159f * 0.8f, camPitch_ = 0.25f, camDist_ = 3.2f;
    glm::vec3 target_{0.0f, 1.0f, 0.0f}; // relativo ao palco
    glm::mat4 view_{1.0f}, proj_{1.0f};
    float winW_ = 1.0f, winH_ = 1.0f;
    double lastMx_ = 0.0, lastMy_ = 0.0;
    bool rmbWas_ = false, lmbWas_ = false, mmbWas_ = false;

    AnimClip clip_;
    std::string file_;      // arquivo aberto (vazio = ainda não salvo)
    bool dirty_ = false;
    float time_ = 0.0f;
    bool playing_ = false;
    bool autoKey_ = true;
    Pose pose_;             // pose mostrada (chaves + mexida ainda não gravada)
    bool poseEdited_ = false;
    int selBone_ = HIPS;
    bool dragging_ = false, draggingRoot_ = false;
    glm::vec3 jointScreen_[BONE_COUNT];
    glm::mat4 globals_[BONE_COUNT];
    std::vector<std::string> library_;
    std::string status_;
    char nameBuf_[64] = {};
    bool scrubbing_ = false;

    // captura de movimento pela câmera
    void mocapUi();
    void bakeRecording();
    MocapLive mocap_;
    PanelLayout layout_; // tamanho dos painéis (arrastando as divisórias)

    // câmera de cena
    void cameraUi();
    void setCamKey();
    glm::quat stageRot() const;
    glm::vec3 camPos_{0.0f, 1.5f, 4.0f}; // no espaço do palco (o personagem olha para +Z)
    glm::quat camRot_{1, 0, 0, 0};       // olha para -Z (de frente para o personagem)
    float focal_ = 35.0f, focus_ = 4.0f, fstop_ = 2.8f;
    bool dof_ = true, preview_ = true, pilot_ = false;
    bool camEdited_ = false, camGrab_ = false;
    glm::vec3 camScreen_{0.0f};
    unsigned previewTex_ = 0;
    // giroscópio do celular move a câmera (AR)
    bool arCam_ = false, arInit_ = false;
    glm::quat arRef_{1, 0, 0, 0}, arBase_{1, 0, 0, 0};
    std::vector<AnimClip::CamKey> recCam_; // câmera durante a gravação
    bool mocapFollow_ = true; // boneco copia a câmera ao vivo
    bool showQr_ = false;
    bool serverTried_ = false, serverOk_ = false;
};
