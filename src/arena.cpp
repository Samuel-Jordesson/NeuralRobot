#include "arena.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace {
constexpr float kOpenAngle = 1.65f; // ~95°
constexpr float kWallH = 4.0f, kWallT = 0.4f;
} // namespace

void Door::update(World& w, const std::vector<CharacterBody*>& bodies, float dt) {
    if (dt <= 0.0f) return;
    // movimento livre: inércia, atrito da dobradiça e um amortecedor que fecha devagar
    vel += (-angle * 1.2f - vel * 2.2f) * dt;
    angle += vel * dt;

    // quem encosta na folha empurra: a porta gira o necessário para sair da frente do corpo,
    // e ganha a velocidade de quem empurrou (correndo, ela bate mais forte e vai mais longe)
    bool touched = false;
    for (int iter = 0; iter < 2; ++iter)
        for (CharacterBody* b : bodies) {
            if (b->pos.y > height || b->pos.y + b->height < 0.0f) continue;
            glm::vec2 u(std::cos(angle), -std::sin(angle)); // da dobradiça para a ponta
            glm::vec2 n(std::sin(angle), std::cos(angle));  // normal da folha (lado +)
            glm::vec2 p(b->pos.x - hinge.x, b->pos.z - hinge.z);
            float along = glm::dot(p, u);
            if (along < -b->radius || along > width + b->radius) continue;
            float ac = std::clamp(along, 0.0f, width);
            float side = glm::dot(p, n) >= 0.0f ? 1.0f : -1.0f;
            float dist = glm::length(p - u * ac);
            float minD = b->radius + thick * 0.5f;
            if (dist >= minD) continue;
            touched = true;
            float pen = minD - dist;
            float lever = std::max(ac, 0.25f);
            // perto da dobradiça a porta não gira: o corpo é que é empurrado
            float doorShare = ac < 0.2f ? 0.0f : 0.8f;
            float before = angle;
            // aumentar o ângulo leva a folha para -n: corpo do lado + empurra o ângulo para cima
            angle = std::clamp(angle + side * pen * doorShare / lever, -kOpenAngle, kOpenAngle);
            float turned = (angle - before) * side * lever; // quanto a folha andou de verdade
            glm::vec2 out = glm::normalize(p - u * ac + n * side * 1e-4f);
            float rest = pen - turned;
            if (rest > 0.0f) { b->pos.x += out.x * rest; b->pos.z += out.y * rest; }
            // velocidade: a porta acompanha quem empurra; quem empurra perde um pouco de velocidade
            float vn = -glm::dot(glm::vec2(b->vel.x, b->vel.z), n * side); // indo contra a folha
            if (vn > 0.0f && doorShare > 0.0f) {
                float push = side * vn / lever;
                if (side * vel < side * push) vel = push;
                b->vel.x += n.x * side * vn * 0.25f;
                b->vel.z += n.y * side * vn * 0.25f;
            }
        }
    // batente: no fim do curso para e volta um pouco
    if (std::fabs(angle) >= kOpenAngle - 1e-4f && angle * vel > 0.0f) vel = -vel * 0.25f;
    if (!touched && std::fabs(angle) < 0.004f && std::fabs(vel) < 0.03f) angle = vel = 0.0f;

    // fechada (ou quase): o vão barra tiros, visão e câmera (o corpo já é barrado pela folha)
    if (collider >= 0) {
        Box& b = w.boxes[collider];
        if (std::fabs(angle) < 0.06f) {
            b.min = hinge + glm::vec3(0.0f, 0.0f, -thick * 0.5f);
            b.max = hinge + glm::vec3(width, height, thick * 0.5f);
        } else {
            b.min = b.max = glm::vec3(0.0f, -100.0f, 0.0f);
        }
    }
}

Door::Touch Door::touch(const CharacterBody& b) const {
    Touch t;
    glm::vec2 u(std::cos(angle), -std::sin(angle)), n(std::sin(angle), std::cos(angle));
    glm::vec2 p(b.pos.x - hinge.x, b.pos.z - hinge.z);
    float along = glm::dot(p, u);
    if (along < 0.1f || along > width + 0.15f) return t;
    float off = glm::dot(p, n);
    float side = off >= 0.0f ? 1.0f : -1.0f;
    float dist = std::fabs(off);
    float toward = -glm::dot(glm::vec2(b.vel.x, b.vel.z), n * side); // velocidade indo contra a folha
    auto ss = [](float a, float c, float x) { float k = std::clamp((x - a) / (c - a), 0.0f, 1.0f); return k * k * (3 - 2 * k); };
    t.weight = ss(1.0f, 0.45f, dist) * ss(0.15f, 0.6f, toward);
    glm::vec2 q = u * std::clamp(along, 0.25f, width - 0.12f) + n * side * (thick * 0.5f);
    t.point = glm::vec3(hinge.x + q.x, b.pos.y + 1.22f, hinge.z + q.y);
    t.normal = glm::vec3(n.x * side, 0.0f, n.y * side);
    return t;
}

void Door::addPanels(std::vector<RenderScene::Panel>& out) const {
    glm::mat4 leaf = glm::translate(glm::mat4(1.0f), hinge) * glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0, 1, 0));
    auto part = [&](glm::vec3 c, glm::vec3 size, glm::vec3 col, int mat) {
        out.push_back({leaf * glm::translate(glm::mat4(1.0f), c) * glm::scale(glm::mat4(1.0f), size), col, mat});
    };
    const glm::vec3 wood(0.42f, 0.24f, 0.12f), metal(0.75f, 0.72f, 0.66f);
    part({width * 0.5f, height * 0.5f, 0.0f}, {width - 0.01f, height - 0.01f, thick}, wood, 4);
    // almofadas (painéis em relevo) dos dois lados
    for (float y : {0.62f, 1.68f})
        for (float z : {-1.0f, 1.0f})
            part({width * 0.5f, y, z * (thick * 0.5f + 0.006f)}, {width * 0.7f, 0.8f, 0.012f}, wood * 0.85f, 4);
    // maçanetas dos dois lados e dobradiças
    for (float z : {-1.0f, 1.0f}) {
        part({width - 0.12f, 1.0f, z * (thick * 0.5f + 0.035f)}, {0.03f, 0.03f, 0.07f}, metal, 3);
        part({width - 0.17f, 1.0f, z * (thick * 0.5f + 0.07f)}, {0.13f, 0.025f, 0.025f}, metal, 3);
    }
    for (float y : {0.3f, height - 0.3f}) part({0.0f, y, 0.0f}, {0.03f, 0.12f, thick + 0.02f}, metal * 0.5f, 3);
}

Arena Arena::build(World& w) {
    Arena a;
    // 26 x 26 m de frente para o início (o jogador começa olhando para +Z)
    a.min = glm::vec3(-13.0f, 0.0f, 22.0f);
    a.max = glm::vec3(13.0f, 0.0f, 48.0f);
    const glm::vec3 wallCol(0.60f, 0.55f, 0.47f), frameCol(0.30f, 0.30f, 0.32f), crate(0.45f, 0.40f, 0.25f),
        cover(0.38f, 0.42f, 0.36f);
    auto box = [&](glm::vec3 mn, glm::vec3 mx, glm::vec3 col) { w.boxes.push_back({mn, mx, col}); };

    const float x0 = a.min.x, x1 = a.max.x, z0 = a.min.z, z1 = a.max.z, t = kWallT;
    // paredes norte, leste e oeste
    box({x0 - t, 0.0f, z1}, {x1 + t, kWallH, z1 + t}, wallCol);
    box({x0 - t, 0.0f, z0 - t}, {x0, kWallH, z1}, wallCol);
    box({x1, 0.0f, z0 - t}, {x1 + t, kWallH, z1}, wallCol);
    // parede sul com o vão da porta no meio
    Door& d = a.door;
    const float gap0 = -d.width * 0.5f - 0.02f, gap1 = d.width * 0.5f + 0.02f, zc = z0 - t * 0.5f;
    box({x0 - t, 0.0f, z0 - t}, {gap0, kWallH, z0}, wallCol);
    box({gap1, 0.0f, z0 - t}, {x1 + t, kWallH, z0}, wallCol);
    box({gap0, d.height + 0.02f, z0 - t}, {gap1, kWallH, z0}, wallCol); // em cima da porta
    // batente
    box({gap0 - 0.08f, 0.0f, z0 - t - 0.03f}, {gap0, d.height + 0.1f, z0 + 0.03f}, frameCol);
    box({gap1, 0.0f, z0 - t - 0.03f}, {gap1 + 0.08f, d.height + 0.1f, z0 + 0.03f}, frameCol);
    box({gap0 - 0.08f, d.height + 0.02f, z0 - t - 0.03f}, {gap1 + 0.08f, d.height + 0.1f, z0 + 0.03f}, frameCol);
    d.hinge = glm::vec3(-d.width * 0.5f, 0.0f, zc);
    w.boxes.push_back({glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(0.0f), true, false}); // barra raios na porta fechada
    d.collider = (int)w.boxes.size() - 1;

    // proteção lá dentro: caixotes e muretas espalhados
    box({-3.0f, 0.0f, 27.0f}, {-1.8f, 1.2f, 28.2f}, crate);
    box({2.2f, 0.0f, 28.0f}, {3.4f, 1.2f, 29.2f}, crate);
    box({-8.5f, 0.0f, 31.0f}, {-5.5f, 1.1f, 31.5f}, cover);
    box({5.5f, 0.0f, 32.5f}, {8.5f, 1.1f, 33.0f}, cover);
    box({-1.5f, 0.0f, 35.0f}, {1.5f, 1.1f, 35.5f}, cover);
    box({-9.0f, 0.0f, 39.0f}, {-7.8f, 1.2f, 40.2f}, crate);
    box({-9.0f, 1.2f, 39.2f}, {-8.0f, 2.2f, 40.2f}, crate);
    box({8.0f, 0.0f, 40.0f}, {9.2f, 1.2f, 41.2f}, crate);
    box({-4.0f, 0.0f, 42.0f}, {-1.0f, 1.1f, 42.5f}, cover);
    box({2.5f, 0.0f, 43.0f}, {5.5f, 1.1f, 43.5f}, cover);

    // soldados espalhados no fundo, virados para a porta
    a.spawns = {{-6.0f, 0.0f, 41.5f}, {6.5f, 0.0f, 44.5f}, {0.0f, 0.0f, 45.5f}, {-10.5f, 0.0f, 35.0f}, {10.5f, 0.0f, 37.0f}};
    for (const glm::vec3& p : a.spawns) {
        glm::vec3 to = d.center() - p;
        a.spawnYaw.push_back(std::atan2(to.x, to.z));
    }
    return a;
}
