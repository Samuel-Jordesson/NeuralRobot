#pragma once
#include "skeleton.h"

#include <string>
#include <vector>

// Clipe de captura de movimento (Mixamo FBX) já convertido para o nosso esqueleto.
struct MocapClip {
    std::vector<Pose> frames; // um ciclo completo, em loop
    float duration = 1.0f;    // segundos por ciclo
    float cycleLength = 1.0f; // metros percorridos por ciclo (root motion)
    float phaseOffset = 0.0f; // fase 0 = calcanhar esquerdo tocando o chão

    bool valid() const { return !frames.empty(); }
    float naturalSpeed() const { return cycleLength / duration; }
    // phase em [0,1): mesma convenção dos ciclos procedurais (0 = pé esquerdo à frente)
    Pose sample(float phase) const;
};

bool loadMixamoClip(const std::string& path, const Skeleton& sk, MocapClip& out);
