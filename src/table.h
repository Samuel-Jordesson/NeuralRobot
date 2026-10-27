#pragma once
#include "physics.h"

#include <glm/glm.hpp>

// Mesa de madeira (wooden_table.glb) colocada no mapa. Futuramente várias mesas, cada uma
// com armas em cima: os pontos do tampo saem de topPoint().
struct Table {
    glm::vec3 pos{0.0f};  // centro da mesa, no chão
    int quarterTurns = 0; // giro em múltiplos de 90° (a colisão usa caixas alinhadas aos eixos)

    // medidas reais (o modelo vem em outra escala)
    static constexpr float kHeight = 0.76f;
    static constexpr float kHalfWidth = 0.506f;  // eixo X local
    static constexpr float kHalfLength = 0.904f; // eixo Z local

    float yaw() const { return quarterTurns * 1.5707963f; }
    glm::mat4 model() const; // matriz da malha (o GLB carregado com a base em y = 0)
    // Ponto no tampo: across/along de -1 a 1 nos eixos X/Z locais da mesa.
    glm::vec3 topPoint(float across, float along) const;
    // Direção em que fica virado quem está na borda +X local olhando para a mesa.
    float facingFromPlusX() const;
    void addColliders(World& w) const; // tampo + 4 pernas (o vão embaixo continua livre para tiros)
};
