#pragma once
#include "anim_editor.h"
#include "mocap.h"
#include "skeleton.h"

#include <glm/glm.hpp>

// Cena de abertura (cinematográfica) do cenário oficial, no último cômodo da casa:
// um rato sai da lixeira, corre até o robô deitado, sobe na cabeça dele e o acorda; o robô senta,
// olha para as mãos sem entender e anda um pouco para a frente; aí o jogo começa.
// Linguagem de cinema: plano de estabelecimento, plano detalhe (insert), travelling baixo seguindo o rato,
// close, plano médio, plano por cima do ombro e plano geral final; cortes secos entre os planos
// (regra dos 180°: a câmera fica sempre do mesmo lado do eixo robô-lixeira), lente de 50 mm com
// fundo desfocado, barras de cinema 2,39:1 e entrada/saída em fade.
class Cinematic {
public:
    // anchor = chão onde fica o quadril do robô deitado; yaw = para onde ele anda no final.
    void start(const glm::vec3& anchor, float yaw);
    bool active() const { return active_; }
    void skip() { t_ = kLength; }
    // Avança o tempo; false quando acabou (o jogo assume daí).
    bool update(float dt);
    float time() const { return t_; }
    void setTime(float t) { t_ = t; } // depuração

    // Saídas para desenhar este quadro.
    Pose robotPose(const Skeleton& sk, const MocapClip& walk) const; // pose (rootOffset no espaço do robô)
    glm::mat4 robotModel() const;           // posição/giro do robô no mundo
    glm::mat4 mouseModel(float scale) const; // rato no mundo (escala do arquivo)
    bool mouseVisible() const;
    glm::mat4 canModel() const;             // lixeira (fica no cenário depois)
    void camera(float aspect, glm::mat4& view, glm::mat4& proj, glm::vec3& eye, float& focus) const;
    float fade() const;    // 0 = imagem, 1 = preto
    float bars() const;    // 0..1 barras de cinema
    glm::vec3 endPos() const; // onde o robô termina (mundo)
    float endYaw() const { return yaw_; }

    static constexpr float kLength = 20.5f;

    // Animação editável (editor de animação > Biblioteca > "abertura"): poses do robô e chaves de câmera.
    // Se o arquivo existe, a cena toca por ele; se não, é gerado a partir da cena feita por código.
    bool useClip(const std::string& path, const Skeleton& sk, const MocapClip& walk);

private:
    glm::vec3 toWorld(const glm::vec3& local) const;
    glm::vec3 mousePos(float& heading) const; // local
    AnimClip clip_;
    bool hasClip_ = false;
    void codeCamera(float aspect, glm::mat4& view, glm::mat4& proj, glm::vec3& eye, float& focus) const;
    Pose codePose(const Skeleton& sk, const MocapClip& walk) const;
    mutable glm::vec3 head_{0.0f, 0.24f, -0.68f}; // cabeça deitada (espaço do robô), medida na pose
    bool active_ = false;
    float t_ = 0.0f;
    glm::vec3 anchor_{0.0f};
    float yaw_ = 0.0f;
};
