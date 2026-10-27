#include "table.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace {
constexpr float kModelHeight = 2.94f; // altura da malha no arquivo
constexpr float kTopThickness = 0.111f; // tampo + saia
// pernas (centro e meia largura no espaço da mesa, já em metros)
constexpr float kLegX = 0.388f, kLegZ = 0.763f, kLegHalf = 0.052f;
constexpr float kEdgeInset = 0.10f;

glm::vec3 rotY(const glm::vec3& v, int quarterTurns) {
    switch (((quarterTurns % 4) + 4) % 4) {
    case 1: return {v.z, v.y, -v.x};
    case 2: return {-v.x, v.y, -v.z};
    case 3: return {-v.z, v.y, v.x};
    default: return v;
    }
}
} // namespace

glm::mat4 Table::model() const {
    return glm::translate(glm::mat4(1.0f), pos) * glm::rotate(glm::mat4(1.0f), yaw(), glm::vec3(0, 1, 0)) *
           glm::scale(glm::mat4(1.0f), glm::vec3(kHeight / kModelHeight));
}

glm::vec3 Table::topPoint(float across, float along) const {
    return pos + rotY(glm::vec3(across * kHalfWidth, kHeight, along * kHalfLength), quarterTurns);
}

float Table::facingFromPlusX() const {
    glm::vec3 d = rotY(glm::vec3(-1, 0, 0), quarterTurns);
    return std::atan2(d.x, d.z);
}

void Table::addColliders(World& w) const {
    auto add = [&](glm::vec3 mn, glm::vec3 mx) {
        glm::vec3 a = pos + rotY(mn, quarterTurns), b = pos + rotY(mx, quarterTurns);
        Box box{glm::min(a, b), glm::max(a, b), glm::vec3(0.5f)};
        box.hidden = true; // a malha da mesa é desenhada no lugar
        w.boxes.push_back(box);
    };
    // o tampo colide um pouco para dentro nas bordas compridas: dá para encostar o quadril na
    // borda (a cápsula do personagem é mais larga que o corpo)
    add({-kHalfWidth + kEdgeInset, kHeight - kTopThickness, -kHalfLength}, {kHalfWidth - kEdgeInset, kHeight, kHalfLength});
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2)
            add({sx * kLegX - kLegHalf, 0.0f, sz * kLegZ - kLegHalf},
                {sx * kLegX + kLegHalf, kHeight - kTopThickness, sz * kLegZ + kLegHalf});
}
