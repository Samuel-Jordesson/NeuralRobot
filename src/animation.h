#pragma once
#include "mocap.h"
#include "skeleton.h"

struct AnimInput {
    float dt = 0.0f;
    float speed = 0.0f;      // velocidade horizontal real (m/s)
    float verticalVel = 0.0f;
    float turnRate = 0.0f;   // rad/s, positivo = virando para a esquerda
    bool grounded = true;
    bool landed = false;     // tocou o chão neste frame
    float landImpact = 0.0f; // velocidade vertical no impacto (m/s, positiva)
    // Mirando: o tronco fica virado para a mira e as pernas andam na direção do movimento.
    float legYaw = 0.0f;     // rad, direção das pernas relativa ao corpo
    bool backwards = false;  // andando de costas (passada tocada ao contrário)
};

// Velocidades dos ciclos procedurais (usadas só se os clipes de captura não carregarem).
constexpr float kWalkSpeed = 1.7f;
constexpr float kSprintSpeed = 6.6f;

class Animator {
public:
    explicit Animator(const Skeleton& sk) : sk_(sk) {}

    // Clipes de captura de movimento para andar/correr (podem ser nulos).
    void setClips(const MocapClip* walk, const MocapClip* run);
    // Velocidade em que cada passada não desliza; o controlador usa estas.
    float walkSpeed() const;
    float runSpeed() const;

    void update(const AnimInput& in);
    const Pose& pose() const { return pose_; }
    Pose& pose() { return pose_; }
    float groundedWeight() const { return 1.0f - airW_; }
    const char* stateName() const { return state_; }

    // Força uma pose específica (modo screenshot / depuração).
    void forceClip(const char* clip, float phase, float time);

private:
    void groundFeet(Pose& p, float weight) const;
    Pose walkPose(float phase) const;
    Pose runPose(float phase) const;

    const Skeleton& sk_;
    const MocapClip* walkClip_ = nullptr;
    const MocapClip* runClip_ = nullptr;
    Pose pose_;
    float phase_ = 0.0f;   // fase do ciclo de passada [0,1)
    float time_ = 0.0f;
    float moveW_ = 0.0f;   // peso parado -> movimento
    float airW_ = 0.0f;    // peso no ar
    float landW_ = 0.0f;   // peso da aterrissagem (decai)
    float landAmp_ = 0.0f;
    float lean_ = 0.0f;
    float legYaw_ = 0.0f;
    float speedSm_ = 0.0f;
    const char* state_ = "parado";
};
