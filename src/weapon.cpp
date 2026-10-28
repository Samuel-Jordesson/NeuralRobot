#include "weapon.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace {
constexpr float kTracerSpeed = 320.0f;
constexpr size_t kMaxDecals = 120, kMaxShells = 40;
constexpr float kBulletImpulse = 9.0f; // N·s aplicado nas bolas
} // namespace

float Weapon::rnd() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return (rng_ & 0xFFFFFF) / float(0xFFFFFF);
}

void Weapon::fire(World& w, const GunDef& def, const glm::mat4& gunWorld, const glm::vec3& target, bool hitsBody) {
    const glm::vec3 muzzle = glm::vec3(gunWorld * glm::vec4(def.muzzle, 1.0f));
    glm::vec3 dir = target - muzzle;
    if (glm::length(dir) < 1e-3f) dir = glm::vec3(gunWorld * glm::vec4(-1, 0, 0, 0));
    dir = glm::normalize(dir);

    World::RayHit h = w.raycastHit(muzzle, dir, 300.0f);
    const float bodyDist = glm::length(target - muzzle);
    const bool body = hitsBody && (!h.hit || h.t > bodyDist - 0.05f);
    float dist = body ? bodyDist : h.hit ? h.t : 300.0f;
    tracers.push_back({muzzle, dir, dist, 0.0f});

    cooldown = def.fireInterval;
    recoil = 1.0f;
    flashTime = 0.05f;
    flashPos = muzzle;
    flashAngle = rnd() * 6.283f;

    // fumacinha saindo do cano
    for (int i = 0; i < 2; ++i)
        particles.push_back({muzzle + dir * 0.03f, dir * (0.6f + rnd()) + glm::vec3(0, 0.25f, 0), glm::vec3(0.75f),
                             0.0f, 0.5f + 0.3f * rnd(), 0.035f, true});

    // cápsula ejetada para a direita do atirador
    glm::vec3 right = glm::normalize(glm::vec3(gunWorld * glm::vec4(0, 0, -1, 0)));
    glm::vec3 up = glm::normalize(glm::vec3(gunWorld * glm::vec4(0, 1, 0, 0)));
    Shell s;
    s.pos = glm::vec3(gunWorld * glm::vec4(def.ejection, 1.0f));
    s.vel = right * (1.6f + 0.8f * rnd()) + up * (1.6f + 0.8f * rnd()) - dir * 0.4f;
    s.angVel = glm::vec3(rnd() - 0.5f, rnd() - 0.5f, rnd() - 0.5f) * 40.0f;
    s.rot = glm::quat_cast(glm::mat3(gunWorld));
    s.age = 0.0f;
    s.scale = def.shellScale;
    shells.push_back(s);
    if (shells.size() > kMaxShells) shells.erase(shells.begin());

    if (body) {
        // acertou alguém: borrifo de sangue saindo pelos dois lados
        for (int i = 0; i < 10; ++i) {
            glm::vec3 r(rnd() - 0.5f, rnd() - 0.5f, rnd() - 0.5f);
            glm::vec3 v = (i % 3 == 0 ? -dir : dir) * (0.6f + 1.4f * rnd()) + r * 1.2f;
            particles.push_back({target, v, glm::vec3(0.32f, 0.02f, 0.02f), 0.0f, 0.25f + 0.35f * rnd(),
                                 0.025f + 0.03f * rnd(), true});
        }
        return;
    }
    if (!h.hit) return;
    glm::vec3 p = muzzle + dir * h.t;
    glm::vec3 n = h.normal;

    if (h.ball >= 0) {
        Ball& b = w.balls[h.ball];
        b.vel += dir * (kBulletImpulse / b.mass);
    } else {
        // marca do tiro (decal) alinhada à superfície
        glm::vec3 t = glm::cross(n, std::fabs(n.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0));
        float a = rnd() * 6.283f;
        t = glm::normalize(t * std::cos(a) + glm::cross(n, t) * std::sin(a));
        decals.push_back({p, n, t, 0.06f + 0.02f * rnd()});
        if (decals.size() > kMaxDecals) decals.erase(decals.begin());
    }

    // faíscas ricocheteando
    glm::vec3 refl = glm::reflect(dir, n);
    for (int i = 0; i < 12; ++i) {
        glm::vec3 r(rnd() - 0.5f, rnd() - 0.5f, rnd() - 0.5f);
        glm::vec3 v = glm::normalize(refl + n * 0.5f + r * 1.4f) * (2.5f + 4.5f * rnd());
        particles.push_back({p + n * 0.01f, v, glm::vec3(8.0f, 4.5f, 1.6f), 0.0f, 0.12f + 0.2f * rnd(), 0.012f, false});
    }
    // poeira do impacto
    glm::vec3 dust = h.ball >= 0 ? w.balls[h.ball].color * 0.6f + 0.2f : glm::vec3(0.55f, 0.53f, 0.5f);
    for (int i = 0; i < 4; ++i) {
        glm::vec3 r(rnd() - 0.5f, rnd() - 0.5f, rnd() - 0.5f);
        particles.push_back({p + n * 0.04f, n * (0.4f + 0.6f * rnd()) + r * 0.4f, dust, 0.0f, 0.7f + 0.6f * rnd(),
                             0.06f + 0.04f * rnd(), true});
    }
}

void Weapon::update(const World& w, float dt) {
    cooldown = std::max(0.0f, cooldown - dt);
    recoil = std::max(0.0f, recoil - dt * 7.0f);
    flashTime -= dt;

    for (Tracer& t : tracers) t.traveled += kTracerSpeed * dt;
    tracers.erase(std::remove_if(tracers.begin(), tracers.end(),
                                 [](const Tracer& t) { return t.traveled - 2.0f > t.dist; }),
                  tracers.end());

    for (Particle& p : particles) {
        p.age += dt;
        if (p.smoke) {
            p.vel *= std::max(0.0f, 1.0f - 2.5f * dt);
            p.vel.y += 0.3f * dt;
            p.size += dt * 0.12f;
        } else {
            p.vel.y -= 9.8f * dt;
        }
        p.pos += p.vel * dt;
    }
    particles.erase(std::remove_if(particles.begin(), particles.end(), [](const Particle& p) { return p.age >= p.life; }),
                    particles.end());

    // cápsulas: gravidade, quique e rolamento até parar
    for (Shell& s : shells) {
        s.age += dt;
        s.vel.y -= 9.8f * dt;
        s.pos += s.vel * dt;
        float ground = w.groundHeightAt(s.pos.x, s.pos.z, s.pos.y + 0.05f) + 0.005f;
        if (s.pos.y < ground) {
            s.pos.y = ground;
            if (s.vel.y < 0.0f) s.vel.y = s.vel.y < -0.6f ? -s.vel.y * 0.35f : 0.0f;
            s.vel.x *= 0.7f;
            s.vel.z *= 0.7f;
            s.angVel *= 0.6f;
        }
        float ang = glm::length(s.angVel);
        if (ang > 1e-4f) s.rot = glm::normalize(glm::angleAxis(ang * dt, s.angVel / ang) * s.rot);
    }
    shells.erase(std::remove_if(shells.begin(), shells.end(), [](const Shell& s) { return s.age > 8.0f; }), shells.end());
}
