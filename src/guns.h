#pragma once
#include <glm/glm.hpp>
#include <string>

// Definição de cada arma. Todas usam o mesmo "espaço canônico" em metros: cano para -X,
// topo para +Y, lado direito para -Z. meshFix leva a malha do arquivo para esse espaço.
struct GunDef {
    std::string name, file;
    glm::mat4 meshFix{1.0f};
    bool rifle = false;          // duas mãos, vai nas costas
    glm::vec3 grip{0.0f};        // centro da empunhadura (palma da mão direita)
    glm::vec3 fingers{-1, 0, 0}; // direção dos dedos da mão direita em volta da empunhadura
    glm::vec3 support{0.0f};     // onde a mão esquerda segura (guarda-mão)
    glm::vec3 muzzle{0.0f};      // ponta do cano
    glm::vec3 ejection{0.0f};    // janela de ejeção
    glm::vec3 eye{0.0f};         // fuzil: onde fica o olho na linha de mira (rosto encostado na coronha)
    float sideHalfWidth = 0.02f; // do centro até o lado esquerdo (deitada na mesa)
    float fireInterval = 0.16f;  // s entre tiros
    bool automatic = false;
    float recoilKick = 0.022f;   // coice da câmera por tiro
    float shellScale = 1.0f;

    // Converte um ponto medido no arquivo para o espaço canônico.
    glm::vec3 fromFile(const glm::vec3& p) const { return glm::vec3(meshFix * glm::vec4(p, 1.0f)); }
};

const GunDef& pistolDef();
const GunDef& ak47Def();
const GunDef& ak12Def();

// Onde a arma está agora.
enum class GunPlace { World, Hand, Body };

// Lugares no corpo: 0 = cintura (pistola), 1 = costas baixo, 2 = costas alto (fuzis).
constexpr int kSlotCount = 3;

// Uma arma do mapa: deitada no mundo (mesa/chão), na mão ou guardada no corpo.
struct GunItem {
    const GunDef* def = nullptr;
    GunPlace place = GunPlace::World;
    int slot = -1; // lugar no corpo enquanto equipada

    glm::vec3 groundPos{0.0f}; // centro da empunhadura, na superfície onde está deitada
    float groundYaw = 0.0f;
    // Em cima de uma mesa: o personagem vai até 'standPos', vira para 'standYaw' e pega com a mão.
    bool onTable = false;
    glm::vec3 standPos{0.0f};
    float standYaw = 0.0f;

    glm::mat4 groundMatrix() const; // espaço canônico -> mundo
    bool canPickUp(const glm::vec3& charPos) const;
    void drop(const glm::vec3& pos, float yaw);
    // Deita a arma num ponto do tampo; 'facing' = direção em que quem pega fica virado,
    // 'turn' = quanto o cano gira para a esquerda em relação a essa direção (rad).
    void placeOnTable(const glm::vec3& top, float facing, float turn, float floorY);
};
