#pragma once
#include "physics.h"
#include "skeleton.h"

// Foot IK / posicionamento procedural dos pés: cada pé é ajustado à altura real do
// terreno embaixo dele (degraus, caixas), a pelve desce para o pé mais baixo alcançar
// e o joelho dobra com IK analítico de 2 ossos.
class FootIK {
public:
    // model: matriz do personagem no mundo (base = pés em y = 0 no espaço do modelo).
    // weight: 0 desliga (no ar), 1 liga totalmente.
    void apply(const Skeleton& sk, Pose& pose, const glm::mat4& model, const World& world, float weight, float dt);
    void reset() { off_[0] = off_[1] = pelvis_ = 0.0f; }

    bool enabled = true;

private:
    float off_[2] = {0.0f, 0.0f}; // altura do terreno sob cada pé, relativa à base
    float pelvis_ = 0.0f;
};
