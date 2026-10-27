#include "loadout.h"

#include <glm/glm.hpp>

int Loadout::freeSlotFor(const GunDef& d) const {
    if (!d.rifle) return slots[0] < 0 ? 0 : -1;
    for (int s = 1; s < kSlotCount; ++s)
        if (slots[s] < 0) return s;
    return -1;
}

int Loadout::pickable(const glm::vec3& charPos) const {
    int best = -1;
    float bestD = 1e9f;
    for (size_t i = 0; i < items.size(); ++i) {
        const GunItem& it = items[i];
        if (!it.canPickUp(charPos) || freeSlotFor(*it.def) < 0) continue;
        glm::vec3 p = it.onTable ? it.standPos : it.groundPos;
        float d = glm::length(glm::vec2(p.x - charPos.x, p.z - charPos.z));
        if (d < bestD) { bestD = d; best = (int)i; }
    }
    return best;
}

void Loadout::toHand(int item) {
    GunItem& it = items[item];
    if (it.slot < 0) {
        it.slot = freeSlotFor(*it.def);
        if (it.slot >= 0) slots[it.slot] = item;
    }
    it.place = GunPlace::Hand;
    it.onTable = false;
    hand = item;
}

void Loadout::toBody(int item) {
    items[item].place = GunPlace::Body;
    if (hand == item) hand = -1;
}

void Loadout::drop(int item, const glm::vec3& pos, float yaw) {
    GunItem& it = items[item];
    if (it.slot >= 0) slots[it.slot] = -1;
    if (hand == item) hand = -1;
    it.drop(pos, yaw);
}

void Loadout::equipDirect(int item, bool inHand) {
    toHand(item);
    if (!inHand) toBody(item);
}
