#include "gun_handling.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace {

// tempos (s) de cada gesto
constexpr float kPickReach = 0.55f, kPickGrab = 0.55f, kPickLift = 0.78f, kPickEnd = 1.35f;
// pistola na cintura
constexpr float kHolsterAbove = 0.30f, kHolsterIn = 0.44f, kHolsterEnd = 0.78f;
constexpr float kDrawGrab = 0.26f, kDrawPull = 0.40f, kDrawEnd = 0.74f;
// fuzil nas costas
constexpr float kBackDrawGrab = 0.34f, kBackDrawFront = 0.68f, kBackDrawEnd = 1.02f;
constexpr float kBackStowFront = 0.30f, kBackStowIn = 0.64f, kBackStowEnd = 0.94f;

float ss(float a, float b, float t) { return glm::smoothstep(a, b, t); }

glm::mat4 lifted(const glm::mat4& m, float up) {
    glm::mat4 r = m;
    r[3].y += up;
    return r;
}

// Caminho da arma entre as costas e a frente do ombro, passando por fora do corpo.
glm::mat4 backPath(const glm::mat4& back, const glm::mat4& front, float s) {
    glm::mat4 m = blendRigid(back, front, s);
    m[3].x -= 0.10f * std::sin(s * 3.14159265f);
    m[3].y += 0.06f * std::sin(s * 3.14159265f);
    return m;
}

// Cotovelo ao buscar nas costas: para fora e para trás (empunhadura embaixo) ou para cima (em cima).
glm::vec3 backPole(int slot) { return slot == 2 ? glm::vec3(-0.35f, 0.55f, 0.35f) : glm::vec3(-0.6f, -0.25f, 0.15f); }

} // namespace

void GunHandling::start(Action a, int item, int slot) {
    action_ = a;
    item_ = item;
    slot_ = slot;
    started_ = a != Action::PickUp;
    t_ = 0.0f;
    approachTime = 0.0f;
}

GunHandling::Events GunHandling::update(float dt) {
    Events ev;
    if (action_ == Action::None) return ev;
    if (!started_) {
        approachTime += dt;
        return ev;
    }
    float prev = t_;
    t_ += dt;
    auto crossed = [&](float k) { return prev < k && t_ >= k; };
    const bool back = slot_ >= 1;
    switch (action_) {
    case Action::PickUp:
        if (crossed(kPickGrab)) ev.grabbed = true;
        if (t_ >= kPickEnd) action_ = Action::None;
        break;
    case Action::Stow:
        if (crossed(back ? kBackStowIn : kHolsterIn)) ev.stowed = true;
        if (t_ >= (back ? kBackStowEnd : kHolsterEnd)) action_ = Action::None;
        break;
    case Action::Draw:
        if (crossed(back ? kBackDrawGrab : kDrawGrab)) ev.grabbed = true;
        if (t_ >= (back ? kBackDrawEnd : kDrawEnd)) action_ = Action::None;
        break;
    default: break;
    }
    return ev;
}

HandReach GunHandling::reach(const Skeleton& sk, const glm::mat4 globals[BONE_COUNT], const GunDef& def,
                             const glm::mat4& tableGun) const {
    HandReach r;
    if (action_ == Action::None || !started_) return r;
    const float t = t_;
    switch (action_) {
    case Action::PickUp: {
        // corpo: inclina e dobra os joelhos enquanto a mão desce; volta ao trazer a arma
        float body = ss(0.0f, 0.45f, t) * (1.0f - ss(kPickLift - 0.1f, kPickEnd - 0.1f, t));
        r.lean = 0.70f * body;
        r.crouch = 0.10f * body;
        r.look = body;
        r.lookAt = glm::vec3(tableGun * glm::vec4(def.grip, 1.0f));
        // a mão vem por cima, desce até a empunhadura, fecha, levanta a arma da mesa
        // e passa para a pose de segurar
        glm::mat4 grip = handForGun(tableGun, def);
        if (t < kPickGrab) r.hand = lifted(grip, 0.14f * (1.0f - ss(0.2f, kPickReach, t)));
        else r.hand = lifted(grip, 0.16f * ss(kPickGrab, kPickLift, t));
        r.arm = ss(0.0f, 0.4f, t) * (1.0f - ss(kPickLift, kPickEnd, t));
        r.support = def.rifle ? ss(kPickLift + 0.1f, kPickEnd, t) : 0.0f;
        break;
    }
    case Action::Stow:
        if (slot_ == 0) {
            glm::mat4 grip = handForGun(slotMatrix(sk, globals, 0, def), def);
            r.hand = lifted(grip, 0.09f * (1.0f - ss(kHolsterAbove, kHolsterIn, t)));
            r.arm = ss(0.0f, kHolsterAbove, t) * (1.0f - ss(kHolsterIn + 0.02f, kHolsterEnd, t));
        } else {
            // leva o fuzil para a frente do ombro, passa por fora e encaixa nas costas
            glm::mat4 front = rifleShoulderMatrix(sk, globals, slot_, def);
            glm::mat4 back = slotMatrix(sk, globals, slot_, def);
            r.hand = handForGun(backPath(front, back, ss(kBackStowFront, kBackStowIn, t)), def);
            r.arm = ss(0.0f, kBackStowFront, t) * (1.0f - ss(kBackStowIn + 0.02f, kBackStowEnd, t));
            float k = ss(kBackStowFront, kBackStowIn, t);
            r.pole = glm::mix(r.pole, backPole(slot_), k);
            r.support = def.rifle ? 1.0f - ss(0.0f, 0.2f, t) : 0.0f;
        }
        break;
    case Action::Draw:
        if (slot_ == 0) {
            glm::mat4 grip = handForGun(slotMatrix(sk, globals, 0, def), def);
            r.hand = lifted(grip, 0.13f * ss(kDrawGrab, kDrawPull, t));
            r.arm = ss(0.0f, kDrawGrab, t) * (1.0f - ss(kDrawPull, kDrawEnd, t));
        } else {
            // mão vai nas costas, puxa o fuzil por fora do ombro até a frente e a esquerda pega
            glm::mat4 front = rifleShoulderMatrix(sk, globals, slot_, def);
            glm::mat4 back = slotMatrix(sk, globals, slot_, def);
            r.hand = handForGun(backPath(back, front, ss(kBackDrawGrab, kBackDrawFront, t)), def);
            r.arm = ss(0.0f, kBackDrawGrab - 0.04f, t) * (1.0f - ss(kBackDrawFront, kBackDrawEnd, t));
            r.pole = glm::mix(backPole(slot_), r.pole, ss(kBackDrawGrab, kBackDrawFront, t));
            r.support = ss(kBackDrawFront - 0.05f, kBackDrawEnd, t);
        }
        break;
    default: break;
    }
    return r;
}

float GunHandling::fingerCurl(bool gunInHand) const {
    if (gunInHand) return 1.0f;
    if (action_ == Action::None || !started_) return 0.3f;
    // abre a mão chegando na arma; depois de soltar no corpo relaxa de novo
    float t = t_;
    const bool back = slot_ >= 1;
    switch (action_) {
    case Action::PickUp: return glm::mix(0.3f, 0.05f, ss(0.15f, 0.45f, t));
    case Action::Draw: return glm::mix(0.3f, 0.1f, ss(0.05f, back ? 0.28f : 0.2f, t));
    case Action::Stow: {
        float in = back ? kBackStowIn : kHolsterIn, end = back ? kBackStowEnd : kHolsterEnd;
        return glm::mix(0.15f, 0.3f, ss(in, end, t));
    }
    default: return 0.3f;
    }
}
