#pragma once
#include "animation.h"
#include "character_model.h"
#include "guns.h"
#include "physics.h"
#include "ragdoll.h"
#include "renderer.h"
#include "weapon.h"

#include <memory>
#include <string>
#include <vector>

// Modelo do soldado (russian_soldier.glb) com o esqueleto encaixado no rig dele e as mesmas
// animações do jogador (andar/correr do Mixamo), retargetadas para as proporções dele.
struct SoldierAssets {
    Skeleton skeleton;
    CharacterModel model;
    MocapClip walk, run;
    bool load(const std::string& root);
};

struct Soldier {
    explicit Soldier(const Skeleton& sk) : anim(sk) {}
    CharacterBody body;
    float yaw = 0.0f;
    Animator anim;
    glm::mat4 model{1.0f}, gun{1.0f};
    glm::mat4 globals[BONE_COUNT], skin[BONE_COUNT];

    float health = 100.0f;
    bool alive = true;
    float deathT = 0.0f;
    Ragdoll rag; // corpo mole depois de morrer
    glm::vec3 home{0.0f};
    float homeYaw = 0.0f;

    // combate
    bool alert = false;
    float seenT = 0.0f, lostT = 0.0f; // tempo vendo / sem ver o jogador
    glm::vec3 lastSeen{0.0f};
    float aimW = 0.0f, recoil = 0.0f, hurt = 0.0f;
    int burst = 0;
    float shotT = 0.0f, pauseT = 1.0f;
    float strafe = 1.0f, strafeT = 0.0f;

    // cobertura: corre até um lugar escondido, agacha e alterna entre esconder e levantar atirando
    int cover = 0;                // 0 = nenhuma, 1 = indo, 2 = nela
    glm::vec3 coverSpot{0.0f};
    float coverT = 0.0f, searchT = 0.0f, crouch = 0.0f;
    bool popped = false;          // levantado atirando
};

// Os soldados da arena: enxergam o jogador, chegam perto, andam de lado e atiram em rajadas.
class Squad {
public:
    struct Player {
        glm::vec3 pos{0.0f}, vel{0.0f};
        bool alive = true;
        float chestH = 1.3f; // altura do peito (agachado na cobertura fica mais baixo)
    };
    struct Hit {
        int soldier = -1;
        float t = 0.0f;
        bool head = false;
    };

    void init(const SoldierAssets* assets, const GunDef* gun, const std::vector<glm::vec3>& spawns,
              const std::vector<float>& yaws);
    void reset(); // todos vivos de novo nos lugares iniciais
    // Retorna o dano que o jogador levou neste quadro. Os tiros saem em 'fx'.
    float update(World& w, const Player& pl, Weapon& fx, float dt);
    // Bala do jogador: primeiro soldado vivo atingido pelo raio (dir normalizada) antes de maxT.
    Hit raycast(const glm::vec3& o, const glm::vec3& dir, float maxT) const;
    // Retorna true se esse tiro matou.
    bool damage(int i, float amount, const glm::vec3& shotDir, const glm::vec3& shooterPos, const glm::vec3& hitPoint);
    int aliveCount() const;
    std::vector<glm::vec3> alivePositions() const;
    std::vector<CharacterBody*> aliveBodies();
    void addToScene(RenderScene& s, const CharacterModel& gunMesh) const;

    std::vector<std::unique_ptr<Soldier>> soldiers;

private:
    void pose(Soldier& s, const glm::vec3& aimWorld);
    float rnd();
    const SoldierAssets* assets_ = nullptr;
    const GunDef* gun_ = nullptr;
    uint32_t rng_ = 0x2545F491u;
};
