#include "gun_tuning.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>

#if !GA_EDITOR
#include "weapons_baked.h"
#endif

namespace {

std::map<std::string, GunTuning>& table() {
    static std::map<std::string, GunTuning> t;
    return t;
}

const char* kKeys[] = {"aim", "carry", "slot0", "slot1", "slot2"};

GunOffset* field(GunTuning& t, const std::string& key) {
    if (key == "aim") return &t.aim;
    if (key == "carry") return &t.carry;
    for (int s = 0; s < kSlotCount; ++s)
        if (key == kKeys[2 + s]) return &t.body[s];
    return nullptr;
}

void parse(std::istream& in) {
    table().clear();
    std::string line, section;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line[0] == '[') {
            section = line.substr(1, line.find(']') - 1);
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos || section.empty()) continue;
        std::string key = line.substr(0, eq);
        while (!key.empty() && key.back() == ' ') key.pop_back();
        GunOffset* o = field(table()[section], key);
        if (!o) continue;
        std::istringstream v(line.substr(eq + 1));
        v >> o->move.x >> o->move.y >> o->move.z >> o->turn.x >> o->turn.y >> o->turn.z;
    }
}

} // namespace

GunTuning& gunTuning(const GunDef& d) { return table()[d.name]; }

glm::mat4 applyGunOffset(const glm::mat4& gun, const GunDef& d, const GunOffset& o) {
    if (o.isZero()) return gun;
    // frente = -X (cano), direita = -Z no espaço canônico
    glm::vec3 t(-o.move.x, o.move.y, -o.move.z);
    glm::mat4 m = glm::translate(glm::mat4(1.0f), t * 0.01f) *
                  glm::rotate(glm::mat4(1.0f), glm::radians(o.turn.y), glm::vec3(0, 1, 0)) *
                  glm::rotate(glm::mat4(1.0f), glm::radians(o.turn.z), glm::vec3(0, 0, -1)) *
                  glm::rotate(glm::mat4(1.0f), glm::radians(o.turn.x), glm::vec3(-1, 0, 0));
    return gun * glm::translate(glm::mat4(1.0f), d.grip) * m * glm::translate(glm::mat4(1.0f), -d.grip);
}

void loadGunTuning(const std::string& path) {
#if GA_EDITOR
    std::ifstream f(path);
    if (f) parse(f);
#else
    (void)path;
    std::istringstream s(kBakedWeapons);
    parse(s);
#endif
}

bool saveGunTuning(const std::string& path) {
    std::ofstream f(path);
    if (!f) return false;
    f << "# Ajustes das armas (editor: menu > Editar arma)\n"
         "# chave = frente cima direita (cm)  rolar virar inclinar (graus)\n";
    for (auto& [name, t] : table()) {
        f << "\n[" << name << "]\n";
        for (const char* k : kKeys) {
            const GunOffset& o = *field(t, k);
            char buf[160];
            std::snprintf(buf, sizeof(buf), "%s = %.2f %.2f %.2f %.1f %.1f %.1f\n", k, o.move.x, o.move.y, o.move.z,
                          o.turn.x, o.turn.y, o.turn.z);
            f << buf;
        }
    }
    return (bool)f;
}
