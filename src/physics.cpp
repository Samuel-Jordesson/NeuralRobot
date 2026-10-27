#include "physics.h"

#include <algorithm>
#include <cmath>

namespace {

glm::vec3 moveTowards(const glm::vec3& cur, const glm::vec3& target, float maxDelta) {
    glm::vec3 d = target - cur;
    float len = glm::length(d);
    if (len <= maxDelta || len < 1e-6f) return target;
    return cur + d / len * maxDelta;
}

// Distância 2D (xz) de um ponto até um retângulo; 0 se dentro.
float rectDist(const Box& b, float x, float z) {
    float dx = std::max({b.min.x - x, 0.0f, x - b.max.x});
    float dz = std::max({b.min.z - z, 0.0f, z - b.max.z});
    return std::sqrt(dx * dx + dz * dz);
}

// Cápsula vertical (pés em 'feet') contra AABB. Retorna vetor para empurrar a cápsula para fora.
bool capsuleVsBox(const glm::vec3& feet, float r, float h, const Box& b, glm::vec3& push) {
    float y0 = feet.y + r, y1 = feet.y + h - r; // centros das esferas inferior/superior
    float sy;
    if (y1 < b.min.y) sy = y1;
    else if (y0 > b.max.y) sy = y0;
    else sy = std::clamp(0.5f * (std::max(y0, b.min.y) + std::min(y1, b.max.y)), y0, y1);
    glm::vec3 c(feet.x, sy, feet.z);
    glm::vec3 q = glm::clamp(c, b.min, b.max);
    glm::vec3 d = c - q;
    float d2 = glm::dot(d, d);
    if (d2 >= r * r) return false;
    if (d2 > 1e-10f) {
        float dist = std::sqrt(d2);
        push = d / dist * (r - dist);
        return true;
    }
    // Centro dentro da caixa: sai pelo menor eixo (horizontal ou por cima).
    float opts[5] = {c.x - b.min.x + r, b.max.x - c.x + r, c.z - b.min.z + r, b.max.z - c.z + r, b.max.y - feet.y};
    const glm::vec3 dirs[5] = {{-1, 0, 0}, {1, 0, 0}, {0, 0, -1}, {0, 0, 1}, {0, 1, 0}};
    int best = 0;
    for (int i = 1; i < 5; ++i) if (opts[i] < opts[best]) best = i;
    push = dirs[best] * opts[best];
    return true;
}

bool sphereVsBox(const glm::vec3& c, float r, const Box& b, glm::vec3& n, float& pen) {
    glm::vec3 q = glm::clamp(c, b.min, b.max);
    glm::vec3 d = c - q;
    float d2 = glm::dot(d, d);
    if (d2 >= r * r) return false;
    if (d2 > 1e-10f) {
        float dist = std::sqrt(d2);
        n = d / dist;
        pen = r - dist;
        return true;
    }
    float opts[6] = {c.x - b.min.x, b.max.x - c.x, c.y - b.min.y, b.max.y - c.y, c.z - b.min.z, b.max.z - c.z};
    const glm::vec3 dirs[6] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
    int best = 0;
    for (int i = 1; i < 6; ++i) if (opts[i] < opts[best]) best = i;
    n = dirs[best];
    pen = opts[best] + r;
    return true;
}

} // namespace

World World::makeTestMap() {
    World w;
    const glm::vec3 orange(0.85f, 0.42f, 0.12f), grey(0.55f, 0.56f, 0.58f), blue(0.20f, 0.42f, 0.75f),
        green(0.30f, 0.62f, 0.32f);
    auto box = [&](glm::vec3 mn, glm::vec3 mx, glm::vec3 col) { w.boxes.push_back({mn, mx, col}); };

    // Escada (degraus de 0.25 m) subindo até uma plataforma
    for (int i = 0; i < 6; ++i)
        box({5.0f, 0.0f, 6.0f + 0.5f * i}, {8.0f, 0.25f * (i + 1), 6.5f + 0.5f * i}, grey);
    box({5.0f, 0.0f, 9.0f}, {8.0f, 1.5f, 14.0f}, grey);
    // Caixotes empilhados
    box({-5.0f, 0.0f, -4.5f}, {-4.0f, 1.0f, -3.5f}, orange);
    box({-6.2f, 0.0f, -4.5f}, {-5.2f, 1.0f, -3.5f}, orange);
    box({-5.6f, 1.0f, -4.5f}, {-4.6f, 2.0f, -3.5f}, orange);
    // Muretas para pular
    box({-7.0f, 0.0f, 5.0f}, {-2.0f, 0.5f, 5.4f}, blue);
    box({-7.0f, 0.0f, 8.0f}, {-2.0f, 0.8f, 8.4f}, blue);
    // Pilares
    for (int i = 0; i < 4; ++i) box({10.0f + i * 3.0f, 0.0f, -8.0f}, {11.0f + i * 3.0f, 4.0f, -7.0f}, grey);
    // Bloco grande e uma parede longa
    box({-16.0f, 0.0f, 10.0f}, {-11.0f, 2.2f, 15.0f}, green);
    box({-20.0f, 0.0f, -18.0f}, {20.0f, 3.0f, -17.0f}, grey);
    // Plataformas em altura crescente (testar pulo)
    for (int i = 0; i < 4; ++i)
        box({12.0f + i * 2.2f, 0.0f, 4.0f}, {14.0f + i * 2.2f, 0.4f + 0.3f * i, 6.0f}, blue);

    const glm::vec3 cols[6] = {{0.9f, 0.15f, 0.15f}, {0.95f, 0.8f, 0.1f}, {0.15f, 0.6f, 0.95f},
                               {0.95f, 0.95f, 0.95f}, {0.3f, 0.85f, 0.3f}, {0.8f, 0.3f, 0.9f}};
    for (int i = 0; i < 8; ++i) {
        Ball b;
        b.radius = 0.25f + 0.08f * (i % 4);
        b.pos = glm::vec3(2.5f + 1.3f * (i % 4), b.radius + 0.5f * (i / 4), -3.0f - 1.4f * (i / 4));
        b.mass = 150.0f * b.radius * b.radius * b.radius;
        b.color = cols[i % 6];
        w.balls.push_back(b);
    }
    return w;
}

float World::raycast(const glm::vec3& o, const glm::vec3& d, float maxDist) const {
    float best = maxDist;
    if (d.y < -1e-6f) {
        float t = (groundY - o.y) / d.y;
        if (t >= 0.0f && t < best) best = t;
    }
    for (const Box& b : boxes) {
        float tmin = 0.0f, tmax = best;
        bool hit = true;
        for (int a = 0; a < 3; ++a) {
            if (std::fabs(d[a]) < 1e-8f) {
                if (o[a] < b.min[a] || o[a] > b.max[a]) { hit = false; break; }
                continue;
            }
            float inv = 1.0f / d[a];
            float t0 = (b.min[a] - o[a]) * inv, t1 = (b.max[a] - o[a]) * inv;
            if (t0 > t1) std::swap(t0, t1);
            tmin = std::max(tmin, t0);
            tmax = std::min(tmax, t1);
            if (tmin > tmax) { hit = false; break; }
        }
        if (hit && tmin < best) best = tmin;
    }
    return best;
}

World::RayHit World::raycastHit(const glm::vec3& o, const glm::vec3& d, float maxDist) const {
    RayHit best;
    best.t = maxDist;
    if (d.y < -1e-6f) {
        float t = (groundY - o.y) / d.y;
        if (t >= 0.0f && t < best.t) { best = {true, t, {0, 1, 0}, -1}; }
    }
    for (const Box& b : boxes) {
        float tmin = 0.0f, tmax = best.t;
        int axis = -1;
        float sign = 0.0f;
        bool hit = true;
        for (int a = 0; a < 3; ++a) {
            if (std::fabs(d[a]) < 1e-8f) {
                if (o[a] < b.min[a] || o[a] > b.max[a]) { hit = false; break; }
                continue;
            }
            float inv = 1.0f / d[a];
            float t0 = (b.min[a] - o[a]) * inv, t1 = (b.max[a] - o[a]) * inv;
            float s = -1.0f; // entrou pela face "min"
            if (t0 > t1) { std::swap(t0, t1); s = 1.0f; }
            if (t0 > tmin) { tmin = t0; axis = a; sign = s; }
            tmax = std::min(tmax, t1);
            if (tmin > tmax) { hit = false; break; }
        }
        if (hit && axis >= 0 && tmin < best.t) {
            glm::vec3 n(0.0f);
            n[axis] = sign;
            best = {true, tmin, n, -1};
        }
    }
    for (size_t i = 0; i < balls.size(); ++i) {
        glm::vec3 oc = o - balls[i].pos;
        float b = glm::dot(oc, d), c = glm::dot(oc, oc) - balls[i].radius * balls[i].radius;
        float disc = b * b - c;
        if (disc < 0.0f) continue;
        float t = -b - std::sqrt(disc);
        if (t >= 0.0f && t < best.t) best = {true, t, glm::normalize(oc + d * t), (int)i};
    }
    return best;
}

float World::groundHeightAt(float x, float z, float maxY) const {
    float h = groundY;
    for (const Box& b : boxes)
        if (x >= b.min.x && x <= b.max.x && z >= b.min.z && z <= b.max.z && b.max.y <= maxY) h = std::max(h, b.max.y);
    return h;
}

void CharacterBody::step(World& w, const glm::vec3& wishDir, float wishSpeed, bool jumpHeld, float dt) {
    justLanded = false;
    justJumped = false;

    coyote_ = grounded ? 0.12f : coyote_ - dt;
    bool jumpPressed = jumpHeld && !jumpWasHeld_;
    jumpWasHeld_ = jumpHeld;
    jumpBuffer_ = jumpPressed ? 0.12f : jumpBuffer_ - dt;

    glm::vec3 hv(vel.x, 0.0f, vel.z);
    glm::vec3 target = wishDir * wishSpeed;
    float accel;
    if (grounded) accel = glm::length(target) > 0.01f ? 24.0f : 30.0f; // acelera / freia
    else accel = 6.0f;                                               // controle no ar
    hv = moveTowards(hv, target, accel * dt);
    vel.x = hv.x;
    vel.z = hv.z;

    if (jumpBuffer_ > 0.0f && coyote_ > 0.0f) {
        vel.y = kJumpSpeed;
        grounded = false;
        coyote_ = 0.0f;
        jumpBuffer_ = 0.0f;
        justJumped = true;
    }

    vel.y -= kGravity * dt;
    float preVy = vel.y;
    pos += vel * dt;

    bool wasGrounded = grounded && !justJumped;
    resolve(w, wasGrounded);
    if (grounded && !wasGrounded && !justJumped) {
        justLanded = true;
        landImpact = -preVy;
    }
}

void CharacterBody::resolve(World& w, bool wasGrounded) {
    grounded = false;

    // Subir degraus: se encostar numa caixa baixa, sobe direto para o topo.
    if (wasGrounded && vel.y <= 0.5f) {
        for (const Box& b : w.boxes) {
            float rise = b.max.y - pos.y;
            if (rise <= 0.001f || rise > kStepHeight) continue;
            float rd = rectDist(b, pos.x, pos.z);
            glm::vec2 toBox(std::clamp(pos.x, b.min.x, b.max.x) - pos.x, std::clamp(pos.z, b.min.z, b.max.z) - pos.z);
            bool movingInto = rd < 1e-4f || glm::dot(toBox, glm::vec2(vel.x, vel.z)) > 0.0f;
            if (rd < radius + 0.03f && movingInto) {
                // só sobe se houver espaço livre acima do degrau
                bool blocked = false;
                glm::vec3 test(pos.x, b.max.y + 0.01f, pos.z), dummy;
                for (const Box& o : w.boxes)
                    if (&o != &b && capsuleVsBox(test, radius * 0.9f, height, o, dummy)) { blocked = true; break; }
                if (!blocked) {
                    pos.y = b.max.y;
                    vel.y = std::max(vel.y, 0.0f);
                    grounded = true;
                }
            }
        }
    }

    for (int iter = 0; iter < 4; ++iter) {
        bool any = false;
        for (const Box& b : w.boxes) {
            glm::vec3 push;
            if (!capsuleVsBox(pos, radius, height, b, push)) continue;
            any = true;
            pos += push;
            glm::vec3 n = glm::normalize(push);
            float vn = glm::dot(vel, n);
            if (vn < 0.0f) vel -= n * vn;
            if (n.y > 0.65f) grounded = true;
        }
        if (!any) break;
    }

    if (pos.y <= w.groundY) {
        pos.y = w.groundY;
        if (vel.y < 0.0f) vel.y = 0.0f;
        grounded = true;
    }

    // Grudar no chão ao descer degraus (evita "voar" do topo de cada degrau).
    if (!grounded && wasGrounded && vel.y <= 0.0f) {
        float support = w.groundY;
        for (const Box& b : w.boxes)
            if (b.max.y <= pos.y + 0.01f && rectDist(b, pos.x, pos.z) < radius * 0.7f) support = std::max(support, b.max.y);
        if (pos.y - support <= kStepHeight + 0.05f) {
            pos.y = support;
            vel.y = 0.0f;
            grounded = true;
        }
    }
    if (grounded && vel.y < 0.0f) vel.y = 0.0f;
}

void stepBalls(World& w, CharacterBody& ch, float dt) {
    const glm::vec3 up(0, 1, 0);
    for (Ball& b : w.balls) {
        b.vel.y -= kGravity * dt;
        b.pos += b.vel * dt;
        bool onGround = false;

        if (b.pos.y - b.radius < w.groundY) {
            b.pos.y = w.groundY + b.radius;
            if (b.vel.y < 0.0f) b.vel.y = b.vel.y < -1.0f ? -b.vel.y * 0.45f : 0.0f;
            onGround = true;
        }
        for (const Box& box : w.boxes) {
            glm::vec3 n;
            float pen;
            if (!sphereVsBox(b.pos, b.radius, box, n, pen)) continue;
            b.pos += n * pen;
            float vn = glm::dot(b.vel, n);
            if (vn < 0.0f) b.vel -= n * vn * (vn < -1.0f ? 1.5f : 1.0f);
            if (n.y > 0.7f) onGround = true;
        }

        // Personagem empurra a bola (cápsula x esfera)
        glm::vec3 a = ch.pos + glm::vec3(0, ch.radius, 0), c = ch.pos + glm::vec3(0, ch.height - ch.radius, 0);
        float t = glm::clamp(b.pos.y, a.y, c.y);
        glm::vec3 cp(ch.pos.x, t, ch.pos.z);
        glm::vec3 d = b.pos - cp;
        float dist = glm::length(d), rr = ch.radius + b.radius;
        if (dist < rr && dist > 1e-5f) {
            glm::vec3 n = d / dist;
            float pen = rr - dist;
            float total = ch.mass + b.mass;
            b.pos += n * pen * (ch.mass / total);
            glm::vec3 horiz(n.x, 0.0f, n.z);
            if (glm::dot(horiz, horiz) > 1e-6f) ch.pos -= glm::normalize(horiz) * pen * (b.mass / total);
            float rel = glm::dot(b.vel - ch.vel, n);
            if (rel < 0.0f) b.vel -= n * rel * 1.35f * (ch.mass / total);
        }

        if (onGround) {
            // rolamento sem deslizar + atrito de rolamento
            glm::vec3 hv(b.vel.x, 0.0f, b.vel.z);
            hv *= std::max(0.0f, 1.0f - 0.8f * dt);
            b.vel.x = hv.x;
            b.vel.z = hv.z;
            b.angVel = glm::cross(up, hv) / b.radius;
        } else {
            b.angVel *= std::max(0.0f, 1.0f - 0.2f * dt);
        }
        float ang = glm::length(b.angVel);
        if (ang > 1e-5f) b.rot = glm::normalize(glm::angleAxis(ang * dt, b.angVel / ang) * b.rot);
    }

    // Bola x bola
    for (size_t i = 0; i < w.balls.size(); ++i)
        for (size_t j = i + 1; j < w.balls.size(); ++j) {
            Ball &p = w.balls[i], &q = w.balls[j];
            glm::vec3 d = q.pos - p.pos;
            float dist = glm::length(d), rr = p.radius + q.radius;
            if (dist >= rr || dist < 1e-6f) continue;
            glm::vec3 n = d / dist;
            float pen = rr - dist, total = p.mass + q.mass;
            p.pos -= n * pen * (q.mass / total);
            q.pos += n * pen * (p.mass / total);
            float rel = glm::dot(q.vel - p.vel, n);
            if (rel < 0.0f) {
                float j = -(1.0f + 0.6f) * rel / (1.0f / p.mass + 1.0f / q.mass);
                p.vel -= n * (j / p.mass);
                q.vel += n * (j / q.mass);
            }
        }
}
