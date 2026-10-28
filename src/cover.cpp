#include "cover.h"

#include <algorithm>
#include <cmath>

namespace {

bool usable(const Box& b) {
    float h = b.max.y - b.min.y;
    return b.solid && h > 0.75f && b.min.y < 0.3f; // apoiada no chão e alta o bastante para esconder
}

} // namespace

CoverFace coverFace(const World& w, int box, const glm::vec3& viewer) {
    const Box& b = w.boxes[box];
    CoverFace f;
    f.box = box;
    glm::vec3 c = (b.min + b.max) * 0.5f, h = (b.max - b.min) * 0.5f;
    glm::vec3 d = viewer - c;
    // eixo em que o ponto está mais "para fora" (relativo ao tamanho da caixa)
    bool xAxis = std::fabs(d.x) - h.x > std::fabs(d.z) - h.z;
    float sign = (xAxis ? d.x : d.z) >= 0.0f ? 1.0f : -1.0f;
    f.n = xAxis ? glm::vec3(sign, 0, 0) : glm::vec3(0, 0, sign);
    f.t = glm::vec3(-f.n.z, 0.0f, f.n.x);
    f.origin = glm::vec3(c.x, b.min.y, c.z) + f.n * (xAxis ? h.x : h.z);
    float half = xAxis ? h.z : h.x;
    f.s0 = -half;
    f.s1 = half;
    f.top = b.max.y;
    f.low = b.max.y - b.min.y <= kLowCoverMax;
    return f;
}

bool findPlayerCover(const World& w, const glm::vec3& pos, const glm::vec3& camForward, CoverFace& out) {
    glm::vec3 fwd(camForward.x, 0.0f, camForward.z);
    if (glm::length(fwd) < 1e-3f) return false;
    fwd = glm::normalize(fwd);
    float best = 1e9f;
    for (int i = 0; i < (int)w.boxes.size(); ++i) {
        const Box& b = w.boxes[i];
        if (!usable(b) || b.max.y < pos.y + 0.75f || b.min.y > pos.y + 0.3f) continue;
        CoverFace f = coverFace(w, i, pos);
        if (f.s1 - f.s0 < 0.5f) continue;
        float dist = f.dist(pos), s = f.along(pos);
        if (dist < 0.0f || dist > 1.6f || s < f.s0 - 0.2f || s > f.s1 + 0.2f) continue;
        if (glm::dot(fwd, -f.n) < 0.3f) continue; // a parede tem que estar na frente da câmera
        float score = dist - 0.5f * glm::dot(fwd, -f.n);
        if (score < best) {
            best = score;
            out = f;
        }
    }
    return best < 1e8f;
}

bool spotHidden(const World& w, const glm::vec3& threat, const glm::vec3& spot) {
    glm::vec3 head = spot + glm::vec3(0.0f, 1.05f, 0.0f); // cabeça de quem está agachado
    glm::vec3 d = head - threat;
    float len = glm::length(d);
    return w.raycast(threat, d / len, len) < len - 0.3f;
}

bool findHidingSpot(const World& w, const glm::vec3& threat, const glm::vec3& from, float maxDist,
                    const std::vector<glm::vec3>& taken, CoverFace& face, glm::vec3& spot) {
    float best = 1e9f;
    for (int i = 0; i < (int)w.boxes.size(); ++i) {
        const Box& b = w.boxes[i];
        if (!usable(b) || b.max.y - b.min.y > kLowCoverMax) continue;
        glm::vec3 c = (b.min + b.max) * 0.5f;
        if (glm::length(glm::vec2(c.x - from.x, c.z - from.z)) > maxDist + 2.0f) continue;
        // face do lado oposto ao inimigo
        CoverFace f = coverFace(w, i, c + (c - threat));
        if (f.s1 - f.s0 < 0.6f) continue;
        // na linha do inimigo atrás da caixa, ou o mais perto possível de quem procura
        for (float s : {std::clamp(f.along(threat + (c - threat) * 2.0f), f.s0 + 0.3f, f.s1 - 0.3f),
                        std::clamp(f.along(from), f.s0 + 0.3f, f.s1 - 0.3f)}) {
            glm::vec3 p = f.pointAt(s, 0.5f);
            float d = glm::length(glm::vec2(p.x - from.x, p.z - from.z));
            if (d > maxDist || d >= best) continue;
            bool free = true;
            for (const glm::vec3& o : taken)
                if (glm::length(glm::vec2(o.x - p.x, o.z - p.z)) < 1.0f) { free = false; break; }
            if (!free || !spotHidden(w, threat, p)) continue;
            best = d;
            face = f;
            spot = p;
        }
    }
    return best < 1e8f;
}
