#pragma once
#include <cmath>
#include "physics.h"
#include "renderer.h"

#include <vector>

// Porta de dobradiça empurrada pelo corpo: quem encosta na folha faz ela girar o quanto
// empurrou (com inércia), e um amortecedor fecha devagar. Fechada, barra tiros e visão.
struct Door {
    glm::vec3 hinge{0.0f};   // pé da dobradiça (canto esquerdo do vão, olhando para +Z)
    float width = 1.5f, height = 2.4f, thick = 0.06f;
    float angle = 0.0f;      // rad; 0 = fechada; < 0 abre para +Z, > 0 abre para -Z
    float base = 0.0f;       // rad; giro da porta fechada (0 = folha ao longo de +X; -pi/2 = ao longo de +Z)
    float vel = 0.0f;        // rad/s
    int collider = -1;       // caixa em World::boxes que barra os raios com a porta fechada

    // 'bodies' = quem pode empurrar (jogador e soldados vivos); a folha também os empurra de volta.
    void update(World& w, const std::vector<CharacterBody*>& bodies, float dt);
    void addPanels(std::vector<RenderScene::Panel>& out) const;
    // Alguém indo contra a porta: onde encostar a mão (na altura do peito) e quanto (0..1).
    struct Touch {
        float weight = 0.0f;
        glm::vec3 point{0.0f}, normal{0.0f, 0.0f, 1.0f}; // normal aponta para quem empurra
    };
    Touch touch(const CharacterBody& b) const;
    glm::vec3 center() const { return hinge + glm::vec3(std::cos(base), 0.0f, -std::sin(base)) * (width * 0.5f); }
};

// Arena quadrada murada com uma porta no meio da parede sul (de frente para o início do mapa)
// e caixotes dentro para se proteger.
struct Arena {
    glm::vec3 min{0.0f}, max{0.0f}; // lado de dentro das paredes
    Door door;
    std::vector<Door> extraDoors;   // outras portas do cenário (só a principal aparece no editor)
    std::vector<glm::vec3> spawns;  // onde os soldados começam
    std::vector<float> spawnYaw;

    static Arena build(World& w);
    bool inside(const glm::vec3& p) const {
        return p.x > min.x && p.x < max.x && p.z > min.z && p.z < max.z;
    }
};
