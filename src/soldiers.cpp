#include "soldiers.h"

#include "aim_ik.h"
#include "cover.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace {

float wrapAngle(float a) {
    while (a > (float)M_PI) a -= 2.0f * (float)M_PI;
    while (a < -(float)M_PI) a += 2.0f * (float)M_PI;
    return a;
}

// Osso do rig do soldado (nomes do Sketchfab/Valve Biped) -> nosso osso.
int soldierBone(const std::string& raw) {
    std::string n = raw;
    for (char& c : n) c = (char)std::tolower((unsigned char)c);
    auto has = [&](const char* k) { return n.find(k) != std::string::npos; };
    const bool right = has("right") || has("rght") || has("_r_") || has("_ri ");
    auto side = [&](Bone l, Bone r) { return (int)(right ? r : l); };
    size_t f = n.find("finger ");
    if (f != std::string::npos && f + 8 < n.size()) {
        char digit = n[f + 7], seg = n[f + 8];
        if (digit == '1') return side(THUMB_L, THUMB_R); // dedo 1 = polegar
        return seg == 'a' ? side(FING1_L, FING1_R) : seg == 'b' ? side(FING2_L, FING2_R) : side(FING3_L, FING3_R);
    }
    if (has("palm") || has("wrist") || has("_hand")) return side(HAND_L, HAND_R);
    if (has("elbow") || has("ulna") || has("forearm")) return side(FARM_L, FARM_R);
    if (has("shoulder 2") || has("upperarm")) return side(UARM_L, UARM_R);
    if (has("shoulder 1") || has("clavicle")) return side(CLAV_L, CLAV_R);
    if (has("thigh")) return side(THIGH_L, THIGH_R);
    if (has("knee") || has("calf")) return side(SHIN_L, SHIN_R);
    if (has("ankle") || has("foot") || has("toe")) return side(FOOT_L, FOOT_R);
    if (has("head part") || has("goggles") || has("head1") || has("helmet")) return HEAD;
    if (has("neck")) return NECK;
    if (has("spine 3") || has("spine 4") || has("spine4")) return CHEST;
    if (has("spine")) return SPINE;
    return HIPS;
}

// Distância do raio até o segmento [a, b]; t = onde o raio passa mais perto.
float rayToSegment(const glm::vec3& o, const glm::vec3& d, const glm::vec3& a, const glm::vec3& b, float& t) {
    glm::vec3 v = b - a, w0 = o - a;
    float bb = glm::dot(d, v), c = glm::dot(v, v), dd = glm::dot(d, w0), e = glm::dot(v, w0);
    float den = c - bb * bb;
    float s = den > 1e-6f ? (bb * e - c * dd) / den : 0.0f; // no raio
    float u = c > 1e-8f ? std::clamp((e + s * bb) / c, 0.0f, 1.0f) : 0.0f; // no segmento
    s = std::max(0.0f, glm::dot(a + v * u - o, d));
    t = s;
    return glm::length(o + d * s - (a + v * u));
}

glm::vec3 posOf(const glm::mat4& m) { return glm::vec3(m[3]); }

} // namespace

bool SoldierAssets::load(const std::string& root) {
    std::map<std::string, glm::vec3> joints;
    CharacterModel::LoadOptions opt;
    opt.maxTextureSize = 1024;
    opt.targetHeight = 1.80f;
    opt.boneMap = soldierBone;
    opt.joints = &joints;
    opt.skipMesh = "dummy"; // disquinho embaixo dos pés (base do Sketchfab)
    if (!model.load(root + "/russian_soldier.glb", nullptr, opt)) return false;

    bool ok = true;
    auto J = [&](const char* key) {
        for (auto& [name, p] : joints)
            if (name.find(key) != std::string::npos) return p;
        std::fprintf(stderr, "[soldado] junta '%s' nao encontrada\n", key);
        ok = false;
        return glm::vec3(0.0f);
    };
    RigJoints r;
    r.pelvis = J("unused root_03");
    r.spine = J("spine 1_");
    r.chest = J("spine 3_");
    r.neck = J("head neck lower");
    r.head = J("head part");
    r.headTop = r.head + glm::vec3(0.0f, 0.2f, 0.02f);
    const char* sideName[2] = {"left", "right"};
    for (int s = 0; s < 2; ++s) {
        auto key = [&](const char* part) { return std::string("arm ") + sideName[s] + " " + part; };
        r.clavicle[s] = J(key("shoulder 1").c_str());
        r.shoulder[s] = J(key("shoulder 2").c_str());
        r.elbow[s] = J(s == 0 ? "arm left elbow" : "arm rght elbow"); // o arquivo tem "rght"
        r.wrist[s] = J(key("wrist").c_str());
        r.knuckles[s] = J(key("finger 3a").c_str());
        auto leg = [&](const char* part) { return std::string("leg ") + sideName[s] + " " + part; };
        r.hip[s] = J(leg("thigh").c_str());
        r.knee[s] = J(leg("knee").c_str());
        r.ankle[s] = J(leg("ankle").c_str());
        r.toe[s] = J(leg("toe").c_str());
    }
    if (!ok) return false;
    skeleton = Skeleton::makeFromRig(r);
    loadMixamoClip(root + "/Walking.fbx", skeleton, walk);
    loadMixamoClip(root + "/Running.fbx", skeleton, run);
    return true;
}

float Squad::rnd() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return (rng_ & 0xFFFFFF) / float(0xFFFFFF);
}

void Squad::init(const SoldierAssets* assets, const GunDef* gun, const std::vector<glm::vec3>& spawns,
                 const std::vector<float>& yaws) {
    assets_ = assets;
    gun_ = gun;
    soldiers.clear();
    for (size_t i = 0; i < spawns.size(); ++i) {
        auto s = std::make_unique<Soldier>(assets->skeleton);
        s->anim.setClips(&assets->walk, &assets->run);
        s->home = spawns[i];
        s->homeYaw = yaws[i];
        soldiers.push_back(std::move(s));
    }
    reset();
}

void Squad::reset() {
    for (auto& sp : soldiers) {
        Soldier& s = *sp;
        s.body = CharacterBody();
        s.body.pos = s.home;
        s.yaw = s.homeYaw;
        s.health = 100.0f;
        s.alive = true;
        s.deathT = 0.0f;
        s.rag.stop();
        s.alert = false;
        s.seenT = s.lostT = 0.0f;
        s.aimW = s.recoil = s.hurt = 0.0f;
        s.burst = 0;
        s.pauseT = 0.5f + rnd();
        s.cover = 0;
        s.crouch = 0.0f;
        s.searchT = rnd() * 0.5f;
        s.model = glm::translate(glm::mat4(1.0f), s.home) * glm::rotate(glm::mat4(1.0f), s.yaw, glm::vec3(0, 1, 0));
        pose(s, s.home + glm::vec3(std::sin(s.yaw), 1.5f, std::cos(s.yaw)) * 10.0f);
    }
}

void Squad::pose(Soldier& s, const glm::vec3& aimWorld) {
    const Skeleton& sk = assets_->skeleton;
    const GunDef& g = *gun_;
    Pose p = s.anim.pose();
    const glm::vec3 aim = glm::vec3(glm::inverse(s.model) * glm::vec4(aimWorld, 1.0f));
    // tranco ao levar um tiro: o tronco vai para trás e volta
    applyCrouch(sk, p, s.crouch, s.crouch * 0.8f);
    if (s.hurt > 0.0f) p.rot[CHEST] = p.rot[CHEST] * glm::angleAxis(-0.35f * s.hurt, glm::vec3(1, 0, 0));
    applyRifleTorso(sk, p, aim, s.alive ? s.aimW : 0.0f);
    applyRightHand(sk, p, handForGun(rifleHold(sk, p, g, aim, s.alive ? s.aimW : 0.0f, s.recoil), g), 1.0f);
    applySupportHand(sk, p, g, s.alive ? 1.0f : std::max(0.0f, 1.0f - s.deathT * 3.0f));
    setFingerCurl(sk, p, 1, 1.0f, 1.0f);
    setFingerCurl(sk, p, 0, 0.9f, 0.6f);
    computeGlobals(sk, p, s.globals);
    computeSkinMatrices(sk, s.globals, s.skin);
    s.gun = s.model * gunInHand(s.globals[HAND_R], g);
}

float Squad::update(World& w, const Player& pl, Weapon& fx, float dt) {
    float damageToPlayer = 0.0f;
    if (dt <= 0.0f) return 0.0f;
    const glm::vec3 chest = pl.pos + glm::vec3(0.0f, pl.chestH, 0.0f);
    const float playerSpeed = glm::length(glm::vec2(pl.vel.x, pl.vel.z));

    for (size_t i = 0; i < soldiers.size(); ++i) {
        Soldier& s = *soldiers[i];
        s.hurt = std::max(0.0f, s.hurt - dt * 4.0f);
        s.recoil = std::max(0.0f, s.recoil - dt * 7.0f);

        if (!s.alive) { // ragdoll: cai de verdade, batendo no chão e nas caixas
            s.deathT += dt;
            s.rag.step(w, dt);
            Pose p = s.anim.pose();
            s.rag.apply(assets_->skeleton, p, s.model);
            computeGlobals(assets_->skeleton, p, s.globals);
            computeSkinMatrices(assets_->skeleton, s.globals, s.skin);
            s.gun = s.model * gunInHand(s.globals[HAND_R], *gun_);
            continue;
        }

        // --- enxerga o jogador? (linha de visão livre; parado de guarda só vê o que está na frente)
        const glm::vec3 eye = s.body.pos + glm::vec3(0.0f, 1.62f - s.crouch, 0.0f);
        glm::vec3 toP = chest - eye;
        const float dist = glm::length(toP);
        glm::vec3 fwd(std::sin(s.yaw), 0.0f, std::cos(s.yaw));
        bool inView = s.alert || dist < 5.0f || glm::dot(glm::normalize(glm::vec3(toP.x, 0.0f, toP.z)), fwd) > 0.2f;
        bool visible = pl.alive && dist < 45.0f && inView && w.raycast(eye, toP / dist, dist) >= dist - 0.4f;
        if (visible) {
            if (!s.alert) s.seenT = 0.0f;
            s.alert = true;
            s.lastSeen = pl.pos;
            s.lostT = 0.0f;
            s.seenT += dt;
        } else {
            s.lostT += dt;
            s.seenT = 0.0f;
            if (s.alert && (s.lostT > 9.0f || !pl.alive)) s.alert = false; // desiste e volta ao posto
        }

        // --- cobertura: procura um lugar que o esconda; se o jogador der a volta e ele ficar
        //     exposto, larga e procura outro
        s.searchT -= dt;
        if (s.alert) {
            glm::vec3 threat = visible ? chest : s.lastSeen + glm::vec3(0.0f, 1.3f, 0.0f);
            if (s.cover && !spotHidden(w, threat, s.coverSpot)) { s.cover = 0; s.searchT = 0.3f; }
            if (!s.cover && s.searchT <= 0.0f) {
                s.searchT = 1.2f + rnd();
                std::vector<glm::vec3> taken;
                for (auto& o : soldiers)
                    if (o.get() != &s && o->alive && o->cover) taken.push_back(o->coverSpot);
                CoverFace f;
                glm::vec3 spot;
                if (findHidingSpot(w, threat, s.body.pos, 10.0f, taken, f, spot)) {
                    s.cover = 1;
                    s.coverSpot = spot;
                }
            }
        } else {
            s.cover = 0;
        }
        if (s.cover == 2) { // alterna: escondido <-> levantado atirando
            s.coverT -= dt;
            if (s.coverT <= 0.0f) {
                s.popped = !s.popped;
                s.coverT = s.popped ? 1.8f + 1.4f * rnd() : 1.2f + 1.8f * rnd();
                if (s.popped) { s.pauseT = 0.15f; s.burst = 0; }
            }
        } else {
            s.popped = false;
        }
        float crouchTarget = s.cover == 2 && !s.popped ? 0.55f : 0.0f;
        s.crouch += (crouchTarget - s.crouch) * (1.0f - std::exp(-9.0f * dt));

        // --- para onde anda
        glm::vec3 move(0.0f);
        float speed = s.anim.walkSpeed();
        glm::vec3 flatTo(pl.pos.x - s.body.pos.x, 0.0f, pl.pos.z - s.body.pos.z);
        float flatDist = glm::length(flatTo);
        if (s.cover == 1) { // correndo para a cobertura
            glm::vec3 to = s.coverSpot - s.body.pos;
            to.y = 0.0f;
            if (glm::length(to) < 0.2f) {
                s.cover = 2;
                s.popped = false;
                s.coverT = 0.8f + rnd();
            } else {
                move = glm::normalize(to) * std::min(1.0f, glm::length(to) * 2.0f);
                speed = s.anim.runSpeed() * 0.75f;
            }
        } else if (s.cover == 2) {
            glm::vec3 to = s.coverSpot - s.body.pos; // fica no lugar
            to.y = 0.0f;
            if (glm::length(to) > 0.15f) move = to * 2.0f;
        } else if (s.alert && visible) {
            glm::vec3 dir = flatDist > 0.01f ? flatTo / flatDist : fwd;
            glm::vec3 side(dir.z, 0.0f, -dir.x);
            s.strafeT -= dt;
            if (s.strafeT <= 0.0f) {
                s.strafe = rnd() < 0.5f ? -1.0f : 1.0f;
                s.strafeT = 1.2f + 2.0f * rnd();
            }
            if (flatDist > 13.0f) move = dir;                       // chega mais perto
            else if (flatDist < 5.0f) move = -dir * 0.7f + side * s.strafe * 0.5f; // recua
            else move = side * s.strafe * 0.8f;                     // anda de lado atirando
        } else if (s.alert) {
            glm::vec3 to = s.lastSeen - s.body.pos;
            to.y = 0.0f;
            if (glm::length(to) > 1.2f) move = glm::normalize(to); // vai até onde viu por último
            speed = s.anim.runSpeed() * 0.6f;
        } else {
            glm::vec3 to = s.home - s.body.pos;
            to.y = 0.0f;
            if (glm::length(to) > 0.4f) move = glm::normalize(to);
        }
        if (glm::length(move) > 1.0f) move = glm::normalize(move);
        // não se amontoam: empurra para longe dos outros
        for (size_t j = 0; j < soldiers.size(); ++j) {
            if (j == i || !soldiers[j]->alive) continue;
            glm::vec3 d = s.body.pos - soldiers[j]->body.pos;
            d.y = 0.0f;
            float l = glm::length(d);
            if (l < 1.2f && l > 1e-3f) move += d / l * (1.2f - l);
        }

        float moveLen = glm::length(move);
        for (float t = 0.0f; t < dt - 1e-5f; t += 1.0f / 60.0f) {
            float h = std::min(1.0f / 60.0f, dt - t);
            s.body.step(w, moveLen > 0.05f ? move / moveLen : glm::vec3(0.0f), speed * std::min(1.0f, moveLen), false, h);
        }
        // e não atravessam o jogador
        glm::vec3 d = s.body.pos - pl.pos;
        d.y = 0.0f;
        if (glm::length(d) < 0.65f && glm::length(d) > 1e-3f) s.body.pos += d / glm::length(d) * (0.65f - glm::length(d));

        // --- direção do corpo: de frente para o jogador quando o vê, senão para onde anda
        float targetYaw = s.yaw;
        if ((s.alert && visible) || s.cover == 2) targetYaw = std::atan2(flatTo.x, flatTo.z);
        else if (moveLen > 0.1f) targetYaw = std::atan2(move.x, move.z);
        else if (!s.alert) targetYaw = s.homeYaw;
        s.yaw = wrapAngle(s.yaw + wrapAngle(targetYaw - s.yaw) * (1.0f - std::exp(-8.0f * dt)));

        AnimInput ai;
        ai.dt = dt;
        ai.speed = glm::length(glm::vec2(s.body.vel.x, s.body.vel.z));
        ai.grounded = s.body.grounded;
        ai.verticalVel = s.body.vel.y;
        if (ai.speed > 0.2f) { // pernas na direção do movimento, tronco virado para a mira
            float rel = wrapAngle(std::atan2(s.body.vel.x, s.body.vel.z) - s.yaw);
            ai.backwards = std::fabs(rel) > 1.75f;
            ai.legYaw = std::clamp(ai.backwards ? wrapAngle(rel - (float)M_PI) : rel, -1.7f, 1.7f);
        }
        s.anim.update(ai);

        s.aimW += ((s.alert && visible ? 1.0f : 0.0f) - s.aimW) * (1.0f - std::exp(-6.0f * dt));
        s.model = glm::translate(glm::mat4(1.0f), s.body.pos) * glm::rotate(glm::mat4(1.0f), s.yaw, glm::vec3(0, 1, 0));
        glm::vec3 aimAt = s.alert ? chest : s.body.pos + glm::vec3(std::sin(s.yaw), 1.5f, std::cos(s.yaw)) * 10.0f;
        pose(s, aimAt);

        // --- rajadas: espera um pouco ao avistar, atira 3 a 6 tiros e pausa
        if (!(visible && s.aimW > 0.85f && s.seenT > 0.7f)) continue;
        if (s.burst <= 0) {
            s.pauseT -= dt;
            if (s.pauseT <= 0.0f) {
                s.burst = 3 + (int)(rnd() * 4.0f);
                s.shotT = 0.0f;
            }
            continue;
        }
        s.shotT -= dt;
        if (s.shotT > 0.0f) continue;
        s.shotT = gun_->fireInterval * 1.3f;
        if (--s.burst <= 0) s.pauseT = 0.7f + rnd() * 1.1f;
        // chance de acertar: cai com a distância e com o jogador se mexendo
        float chance = std::clamp(0.62f - dist * 0.014f - playerSpeed * 0.05f, 0.12f, 0.7f);
        bool hit = rnd() < chance;
        glm::vec3 target = chest + glm::vec3(rnd() - 0.5f, rnd() - 0.5f, rnd() - 0.5f) * 0.3f;
        if (!hit) { // erra por pouco: passa do lado ou por cima
            glm::vec3 side = glm::normalize(glm::cross(toP, glm::vec3(0, 1, 0)));
            target = chest + side * ((rnd() < 0.5f ? -1.0f : 1.0f) * (0.45f + 0.8f * rnd())) +
                     glm::vec3(0.0f, (rnd() - 0.3f) * 0.9f, 0.0f);
            target = eye + glm::normalize(target - eye) * 60.0f;
        }
        fx.fire(w, *gun_, s.gun, target, hit);
        s.recoil = 1.0f;
        if (hit) damageToPlayer += 8.0f + 4.0f * rnd();
    }
    return damageToPlayer;
}

Squad::Hit Squad::raycast(const glm::vec3& o, const glm::vec3& dir, float maxT) const {
    Hit best;
    best.t = maxT;
    for (size_t i = 0; i < soldiers.size(); ++i) {
        const Soldier& s = *soldiers[i];
        if (!s.alive) continue;
        if (glm::length(glm::cross(s.body.pos + glm::vec3(0, 1, 0) - o, dir)) > 1.6f) continue; // longe do raio
        auto P = [&](Bone b) { return glm::vec3(s.model * glm::vec4(posOf(s.globals[b]), 1.0f)); };
        glm::vec3 headC = glm::vec3(s.model * s.globals[HEAD] * glm::vec4(0.0f, 0.1f, 0.02f, 1.0f));
        struct Cap { glm::vec3 a, b; float r; bool head; };
        const Cap caps[] = {
            {headC, headC, 0.13f, true},
            {P(HIPS), P(CHEST), 0.17f, false},
            {P(CHEST), P(NECK), 0.2f, false},
            {P(UARM_L), P(FARM_L), 0.065f, false}, {P(FARM_L), P(HAND_L), 0.055f, false},
            {P(UARM_R), P(FARM_R), 0.065f, false}, {P(FARM_R), P(HAND_R), 0.055f, false},
            {P(THIGH_L), P(SHIN_L), 0.09f, false}, {P(SHIN_L), P(FOOT_L), 0.07f, false},
            {P(THIGH_R), P(SHIN_R), 0.09f, false}, {P(SHIN_R), P(FOOT_R), 0.07f, false},
        };
        for (const Cap& c : caps) {
            float t;
            float d = rayToSegment(o, dir, c.a, c.b, t);
            if (d >= c.r) continue;
            t -= std::sqrt(c.r * c.r - d * d);
            if (t > 0.0f && t < best.t) {
                best.t = t;
                best.soldier = (int)i;
                best.head = c.head;
            }
        }
    }
    return best;
}

bool Squad::damage(int i, float amount, const glm::vec3& shotDir, const glm::vec3& shooterPos, const glm::vec3& hitPoint) {
    Soldier& s = *soldiers[i];
    if (!s.alive) return false;
    s.health -= amount;
    s.hurt = 1.0f;
    s.alert = true; // levou tiro: sabe onde o jogador está
    s.lastSeen = shooterPos;
    s.lostT = 0.0f;
    if (s.health > 0.0f) return false;
    s.alive = false;
    s.deathT = 0.0f;
    s.rag.start(assets_->skeleton, s.globals, s.model, s.body.vel, shotDir * 4.0f, hitPoint);
    return true;
}

int Squad::aliveCount() const {
    int n = 0;
    for (auto& s : soldiers) n += s->alive ? 1 : 0;
    return n;
}

std::vector<glm::vec3> Squad::alivePositions() const {
    std::vector<glm::vec3> v;
    for (auto& s : soldiers)
        if (s->alive) v.push_back(s->body.pos);
    return v;
}

std::vector<CharacterBody*> Squad::aliveBodies() {
    std::vector<CharacterBody*> v;
    for (auto& s : soldiers)
        if (s->alive) v.push_back(&s->body);
    return v;
}

void Squad::addToScene(RenderScene& sc, const CharacterModel& gunMesh) const {
    for (auto& s : soldiers) {
        sc.actors.push_back({&assets_->model, s->model, s->skin});
        sc.guns.push_back({&gunMesh, s->gun * gun_->meshFix});
    }
}
