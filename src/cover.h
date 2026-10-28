#pragma once
#include "physics.h"

#include <vector>

// Sistema de cobertura: faces das caixas do mapa onde dá para se proteger.
// Baixa (até 1,45 m): agacha atrás e levanta para atirar por cima.
// Alta: fica de pé colado e espia pela quina para atirar.
struct CoverFace {
    int box = -1;
    glm::vec3 origin{0.0f}; // centro da face, no chão
    glm::vec3 n{0.0f};      // normal da face (para fora da caixa)
    glm::vec3 t{0.0f};      // ao longo da face
    float s0 = 0.0f, s1 = 0.0f; // extensão ao longo de t
    float top = 0.0f;       // altura do topo
    bool low = false;

    float along(const glm::vec3& p) const { return glm::dot(p - origin, t); }
    float dist(const glm::vec3& p) const { return glm::dot(p - origin, n); }
    glm::vec3 pointAt(float s, float off) const { return origin + t * s + n * off; }
};

constexpr float kLowCoverMax = 1.45f;

// Face da caixa virada para 'viewer'.
CoverFace coverFace(const World& w, int box, const glm::vec3& viewer);
// Jogador: cobertura na frente (direção da câmera) a até ~1,6 m. false se não há.
bool findPlayerCover(const World& w, const glm::vec3& pos, const glm::vec3& camForward, CoverFace& out);
// Soldado: lugar agachado atrás de uma cobertura baixa que o esconda de 'threat' (peito do inimigo).
// 'taken' = lugares já ocupados por outros. Retorna false se não acha perto.
bool findHidingSpot(const World& w, const glm::vec3& threat, const glm::vec3& from, float maxDist,
                    const std::vector<glm::vec3>& taken, CoverFace& face, glm::vec3& spot);
// O lugar ainda esconde quem está agachado lá?
bool spotHidden(const World& w, const glm::vec3& threat, const glm::vec3& spot);
