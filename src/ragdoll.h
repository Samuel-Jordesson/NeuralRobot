#pragma once
#include "physics.h"
#include "skeleton.h"

#include <vector>

// Ragdoll: na morte o corpo vira partículas nas juntas presas por varetas (Verlet), com
// gravidade e colisão com o chão e as caixas. Depois a pose é reconstruída das partículas,
// então serve para qualquer esqueleto do jogo (boneco ou soldado).
class Ragdoll {
public:
    bool active() const { return active_; }
    void stop() { active_ = false; }
    // globals = pose atual (espaço do modelo), model = modelo -> mundo. vel = velocidade do corpo;
    // impulse (m/s) é aplicado nas partículas perto de 'hitPoint' (o tiro empurra).
    void start(const Skeleton& sk, const glm::mat4 globals[BONE_COUNT], const glm::mat4& model, const glm::vec3& vel,
               const glm::vec3& impulse, const glm::vec3& hitPoint);
    void step(const World& w, float dt);
    // Escreve as rotações do corpo em 'pose' (dedos ficam como estão) e a matriz do modelo.
    void apply(const Skeleton& sk, Pose& pose, glm::mat4& model) const;

private:
    struct Particle { glm::vec3 p, prev; float r; };
    struct Stick { int a, b; float len; bool minOnly; float scale; };
    void addStick(int a, int b, bool minOnly = false, float scale = 1.0f);

    bool active_ = false;
    std::vector<Particle> parts_;
    std::vector<Stick> sticks_;
    std::vector<glm::vec3> rest_; // posições na pose neutra (espaço do modelo)
    glm::mat4 model_{1.0f}, invModel_{1.0f};
    float time_ = 0.0f;
};
