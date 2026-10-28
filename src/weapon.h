#pragma once
#include "guns.h"
#include "physics.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstdint>
#include <vector>

struct Decal {
    glm::vec3 pos, normal, tangent;
    float size;
};

struct Particle {
    glm::vec3 pos, vel, color;
    float age, life, size;
    bool smoke; // false = faísca (aditiva), true = fumaça/poeira (alpha)
};

struct Tracer {
    glm::vec3 from, dir;
    float dist;      // até o impacto
    float traveled;  // posição da cabeça do rastro
};

struct Shell {
    glm::vec3 pos, vel, angVel;
    glm::quat rot;
    float age;
    float scale;
};

// Tiros e efeitos de todas as armas (clarão, rastro, faíscas, fumaça, cápsulas e marcas).
class Weapon {
public:
    bool ready() const { return cooldown <= 0.0f; }
    // Tiro hitscan da boca do cano até 'target' (ponto sob a mira). gunWorld = espaço canônico -> mundo.
    // hitsBody: 'target' está num personagem -> a bala para lá (sangue em vez de faísca).
    void fire(World& w, const GunDef& def, const glm::mat4& gunWorld, const glm::vec3& target, bool hitsBody = false);
    void update(const World& w, float dt);

    float cooldown = 0.0f;
    float recoil = 0.0f;    // 1 no disparo, decai rápido
    float flashTime = 0.0f; // clarão visível enquanto > 0
    glm::vec3 flashPos{0.0f};
    float flashAngle = 0.0f;

    std::vector<Decal> decals;
    std::vector<Particle> particles;
    std::vector<Tracer> tracers;
    std::vector<Shell> shells;

private:
    float rnd();
    uint32_t rng_ = 0x9E3779B9u;
};
