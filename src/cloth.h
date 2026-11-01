#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>

#include <string>
#include <vector>

struct World;
struct CharacterBody;

// Tecido posto no cenário (cortina, bandeira, toalha): onde fica, tamanho e a física.
struct ClothPlace {
    glm::vec3 pos{0.0f};     // meio da borda de cima (preso) ou centro (solto)
    float yaw = 0.0f;        // rad
    float width = 2.0f, height = 2.5f;
    int pin = 0;             // 0 borda de cima presa, 1 só os cantos de cima, 2 borda da esquerda, 3 solto (cai deitado)
    float stiffness = 0.85f; // 0..1: quanto o pano estica (1 = não estica)
    float bend = 0.3f;       // 0..1: dobra fácil (0) ou duro como lona (1)
    float mass = 0.3f;       // kg/m²: pano leve voa mais com o vento
    float damping = 0.03f;   // perde movimento (ar)
    float windResponse = 1.0f;
    bool physics = true;     // desligado: fica parado como foi posto
    std::string texture;     // imagem (relativa à raiz); vazio = tecido padrão
    float tile = 1.0f;       // repetição da textura (m)
};

// Vento do cenário (vem das Configurações).
struct Wind {
    bool on = false;
    float strength = 0.0f; // m/s
    float dir = 0.0f;      // rad, 0 = para +Z
    float gust = 0.5f;
};

// Pano simulado (partículas ligadas por molas, integração de Verlet).
class Cloth {
public:
    void build(const ClothPlace& p, const std::string& root);
    // Corpos que empurram o pano (personagem, soldados) e caixas do mundo; t = tempo total (rajadas).
    void step(float dt, const World& w, const std::vector<const CharacterBody*>& bodies,
              const std::vector<std::pair<glm::vec3, glm::vec3>>& movers, const Wind& wind, float t);
    void upload();
    void draw() const;
    void release();
    GLuint texture() const { return tex_; }
    glm::vec3 boundsMin() const { return lo_; }
    glm::vec3 boundsMax() const { return hi_; }

private:
    struct Spring { int a, b; float rest, k; };
    int nx_ = 0, ny_ = 0;
    std::vector<glm::vec3> p_, prev_, pin_;
    std::vector<float> invMass_;
    std::vector<Spring> springs_;
    ClothPlace place_;
    glm::vec3 lo_{0.0f}, hi_{0.0f};
    GLuint vao_ = 0, vbo_ = 0, ebo_ = 0, tex_ = 0;
    GLsizei count_ = 0;
    float acc_ = 0.0f;
};

// Texturas brancas/neutras para os outros mapas do material (rugosidade, normal, emissão).
struct ClothDefaults {
    static GLuint mr();
    static GLuint normal();
    static GLuint black();
};
