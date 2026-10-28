#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

struct Box {
    glm::vec3 min, max;
    glm::vec3 color;
    bool hidden = false; // só colisão (a malha de um objeto é desenhada no lugar)
    bool solid = true;   // false: só barra raios (tiros, visão, câmera), não o corpo (ex.: porta)
};

struct Ball {
    glm::vec3 pos, vel{0.0f}, angVel{0.0f};
    glm::quat rot{1, 0, 0, 0};
    float radius;
    float mass;
    glm::vec3 color;
};

struct World {
    float groundY = 0.0f;
    std::vector<Box> boxes;
    std::vector<Ball> balls;

    static World makeTestMap();

    // Raio contra chão e caixas; retorna distância do primeiro impacto (ou maxDist).
    float raycast(const glm::vec3& origin, const glm::vec3& dir, float maxDist) const;
    struct RayHit {
        bool hit = false;
        float t = 0.0f;
        glm::vec3 normal{0, 1, 0};
        int ball = -1; // índice da bola atingida, -1 = chão/caixa
    };
    // Raio contra chão, caixas e bolas, com a normal da superfície atingida.
    RayHit raycastHit(const glm::vec3& origin, const glm::vec3& dir, float maxDist) const;

    // Altura da superfície mais alta sob (x, z) que não passe de maxY.
    float groundHeightAt(float x, float z, float maxY) const;
};

// Controlador cinemático em cápsula (pés em pos).
struct CharacterBody {
    glm::vec3 pos{0.0f};
    glm::vec3 vel{0.0f};
    float radius = 0.30f;
    float height = 1.72f;
    float mass = 80.0f;
    bool grounded = true;

    // eventos do último passo
    bool justLanded = false;
    float landImpact = 0.0f;
    bool justJumped = false;

    void step(World& w, const glm::vec3& wishDir, float wishSpeed, bool jumpHeld, float dt);

private:
    void resolve(World& w, bool wasGrounded);
    float coyote_ = 0.0f;
    float jumpBuffer_ = 0.0f;
    bool jumpWasHeld_ = false;
};

constexpr float kGravity = 22.0f;
constexpr float kJumpSpeed = 6.6f;
constexpr float kStepHeight = 0.36f;

void stepBalls(World& w, CharacterBody& ch, float dt);
