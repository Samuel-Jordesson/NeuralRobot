#include "guns.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace {

// Fuzis do Sketchfab: cano para +Z e lado direito para -X no arquivo -> gira -90° em Y.
glm::mat4 rifleFix(float scale) {
    return glm::scale(glm::mat4(1.0f), glm::vec3(scale)) *
           glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0, 1, 0));
}

GunDef makePistol() {
    GunDef d;
    d.name = "Classic";
    d.file = "valorant_weapon_classic_pistol.glb";
    d.meshFix = glm::scale(glm::mat4(1.0f), glm::vec3(0.8f));
    d.grip = d.fromFile({0.080f, 0.115f, 0.0f});
    d.fingers = glm::normalize(glm::vec3(-1.0f, -0.35f, 0.0f));
    d.support = d.fromFile({0.070f, 0.080f, 0.030f});
    d.muzzle = d.fromFile({-0.165f, 0.187f, 0.0f});
    d.ejection = d.fromFile({0.028f, 0.196f, -0.014f});
    d.sideHalfWidth = 0.026f * 0.8f;
    return d;
}

GunDef makeAK47() {
    GunDef d;
    d.name = "AK-47";
    d.file = "armas/ak_47_assault_rifle.glb";
    d.meshFix = rifleFix(0.93f);
    d.rifle = true;
    d.grip = d.fromFile({0.0f, 0.099f, -0.197f});
    d.fingers = glm::normalize(glm::vec3(-0.94f, -0.34f, 0.0f));
    d.support = d.fromFile({0.0f, 0.175f, 0.150f});
    d.muzzle = d.fromFile({0.0f, 0.187f, 0.481f});
    d.ejection = d.fromFile({-0.02f, 0.203f, -0.013f});
    d.eye = d.fromFile({0.0f, 0.240f, -0.300f});
    d.sideHalfWidth = 0.0194f * 0.93f;
    d.fireInterval = 0.1f; // 600 tiros/min
    d.automatic = true;
    d.recoilKick = 0.010f;
    d.shellScale = 1.5f;
    return d;
}

GunDef makeAK12() {
    GunDef d;
    d.name = "AK-12";
    d.file = "armas/ak12_-_3d_model_assault_rifle.glb";
    d.meshFix = rifleFix(0.95f);
    d.rifle = true;
    d.grip = d.fromFile({0.0f, -0.025f, -0.005f});
    d.fingers = glm::normalize(glm::vec3(-0.978f, -0.206f, 0.0f));
    d.support = d.fromFile({0.0f, 0.078f, 0.340f});
    d.muzzle = d.fromFile({0.0f, 0.077f, 0.719f});
    d.ejection = d.fromFile({-0.02f, 0.090f, 0.110f});
    d.eye = d.fromFile({0.0f, 0.150f, -0.100f});
    d.sideHalfWidth = 0.0299f * 0.95f;
    d.fireInterval = 0.087f; // ~700 tiros/min
    d.automatic = true;
    d.recoilKick = 0.009f;
    d.shellScale = 1.4f;
    return d;
}

} // namespace

const GunDef& pistolDef() { static const GunDef d = makePistol(); return d; }
const GunDef& ak47Def() { static const GunDef d = makeAK47(); return d; }
const GunDef& ak12Def() { static const GunDef d = makeAK12(); return d; }

glm::mat4 GunItem::groundMatrix() const {
    // deitada de lado, com o lado direito para cima (a mão pega por cima, palma para baixo)
    return glm::translate(glm::mat4(1.0f), groundPos + glm::vec3(0.0f, def->sideHalfWidth + 0.002f, 0.0f)) *
           glm::rotate(glm::mat4(1.0f), groundYaw, glm::vec3(0, 1, 0)) *
           glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1, 0, 0)) *
           glm::translate(glm::mat4(1.0f), -def->grip);
}

bool GunItem::canPickUp(const glm::vec3& charPos) const {
    if (place != GunPlace::World) return false;
    glm::vec3 p = onTable ? standPos : groundPos;
    glm::vec2 d(charPos.x - p.x, charPos.z - p.z);
    return glm::length(d) < (onTable ? 0.9f : 1.4f) && std::fabs(charPos.y - p.y) < (onTable ? 0.5f : 1.0f);
}

void GunItem::drop(const glm::vec3& pos, float yaw) {
    place = GunPlace::World;
    slot = -1;
    groundPos = pos;
    groundYaw = yaw;
    onTable = false;
}

void GunItem::placeOnTable(const glm::vec3& top, float facing, float turn, float floorY) {
    place = GunPlace::World;
    onTable = true;
    groundPos = top;
    // cano apontando para longe de quem pega, girado 'turn' para a esquerda
    groundYaw = facing + glm::radians(90.0f) + turn;
    glm::vec3 fwd(std::sin(facing), 0.0f, std::cos(facing)), right(-std::cos(facing), 0.0f, std::sin(facing));
    // a empunhadura fica na frente do ombro direito
    standPos = glm::vec3(top.x, floorY, top.z) - fwd * 0.42f - right * 0.15f;
    standYaw = facing;
}
