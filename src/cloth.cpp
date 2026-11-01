#include "cloth.h"
#include "physics.h"

#include <stb_image.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace {
// Mesmo formato de vértice dos modelos (o tecido é desenhado pelo shader deles).
struct Vertex {
    float pos[3];
    float nrm[3];
    float uv[2];
    float tan[4];
    uint8_t bones[4];
    float weights[4];
};

GLuint makeTex(const std::vector<unsigned char>& px, int w, int h, bool srgb) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    return t;
}

GLuint solid(unsigned char r, unsigned char g, unsigned char b, bool srgb) {
    std::vector<unsigned char> px = {r, g, b, 255};
    return makeTex(px, 1, 1, srgb);
}

// Tecido padrão: trama de linhas cruzadas em vinho com listras claras (parece cortina de pano).
GLuint defaultFabric() {
    const int S = 256;
    std::vector<unsigned char> px((size_t)S * S * 4);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x) {
            float weave = ((x / 2 + y / 2) % 2 == 0) ? 1.0f : 0.86f;            // trama fina
            float thread = 0.92f + 0.08f * std::sin(x * 0.9f) * std::sin(y * 1.1f); // fios
            bool stripe = (x % 64) < 6;                                        // listra clara
            float r = stripe ? 0.85f : 0.48f, g = stripe ? 0.78f : 0.10f, b = stripe ? 0.62f : 0.12f;
            float k = weave * thread;
            unsigned char* p = &px[((size_t)y * S + x) * 4];
            p[0] = (unsigned char)std::clamp(r * k * 255.0f, 0.0f, 255.0f);
            p[1] = (unsigned char)std::clamp(g * k * 255.0f, 0.0f, 255.0f);
            p[2] = (unsigned char)std::clamp(b * k * 255.0f, 0.0f, 255.0f);
            p[3] = 255;
        }
    return makeTex(px, S, S, true);
}
} // namespace

GLuint ClothDefaults::mr() { static GLuint t = solid(255, 225, 0, false); return t; }      // rugoso, sem metal
GLuint ClothDefaults::normal() { static GLuint t = solid(128, 128, 255, false); return t; } // normal reta
GLuint ClothDefaults::black() { static GLuint t = solid(0, 0, 0, true); return t; }

void Cloth::build(const ClothPlace& pl, const std::string& root) {
    release();
    place_ = pl;
    // ~8 cm entre pontos, limitado (desempenho)
    nx_ = std::clamp((int)std::round(pl.width / 0.08f) + 1, 4, 48);
    ny_ = std::clamp((int)std::round(pl.height / 0.08f) + 1, 4, 56);
    const int n = nx_ * ny_;
    p_.resize(n);
    prev_.resize(n);
    pin_.resize(n);
    invMass_.assign(n, 1.0f);
    const glm::mat4 M = glm::translate(glm::mat4(1.0f), pl.pos) * glm::rotate(glm::mat4(1.0f), pl.yaw, glm::vec3(0, 1, 0));
    for (int j = 0; j < ny_; ++j)
        for (int i = 0; i < nx_; ++i) {
            float u = (float)i / (nx_ - 1), v = (float)j / (ny_ - 1);
            glm::vec3 local = pl.pin == 3 ? glm::vec3((u - 0.5f) * pl.width, 0.0f, (v - 0.5f) * pl.height) // deitado
                                          : glm::vec3((u - 0.5f) * pl.width, -v * pl.height, 0.0f);      // pendurado
            glm::vec3 w = glm::vec3(M * glm::vec4(local, 1.0f));
            int k = j * nx_ + i;
            p_[k] = prev_[k] = pin_[k] = w;
            bool pinned = (pl.pin == 0 && j == 0) || (pl.pin == 1 && j == 0 && (i == 0 || i == nx_ - 1)) ||
                          (pl.pin == 2 && i == 0);
            if (pinned || !pl.physics) invMass_[k] = 0.0f;
        }
    // molas: estrutura (vizinhos), cisalhamento (diagonais) e dobra (pula um)
    auto add = [&](int a, int b, float k) { springs_.push_back({a, b, glm::length(p_[a] - p_[b]), k}); };
    const float ks = 0.3f + 0.7f * std::clamp(pl.stiffness, 0.0f, 1.0f), kb = 0.02f + 0.6f * std::clamp(pl.bend, 0.0f, 1.0f);
    for (int j = 0; j < ny_; ++j)
        for (int i = 0; i < nx_; ++i) {
            int k = j * nx_ + i;
            if (i + 1 < nx_) add(k, k + 1, ks);
            if (j + 1 < ny_) add(k, k + nx_, ks);
            if (i + 1 < nx_ && j + 1 < ny_) { add(k, k + nx_ + 1, ks * 0.7f); add(k + 1, k + nx_, ks * 0.7f); }
            if (i + 2 < nx_) add(k, k + 2, kb);
            if (j + 2 < ny_) add(k, k + 2 * nx_, kb);
        }
    // textura
    if (!pl.texture.empty()) {
        int w, h, c;
        unsigned char* px = stbi_load((root + "/" + pl.texture).c_str(), &w, &h, &c, 4);
        if (px) {
            std::vector<unsigned char> v(px, px + (size_t)w * h * 4);
            stbi_image_free(px);
            tex_ = makeTex(v, w, h, true);
        }
    }
    if (!tex_) tex_ = defaultFabric();
    // malha
    std::vector<unsigned> idx;
    for (int j = 0; j + 1 < ny_; ++j)
        for (int i = 0; i + 1 < nx_; ++i) {
            unsigned a = j * nx_ + i, b = a + 1, c = a + nx_, d = c + 1;
            idx.insert(idx.end(), {a, c, b, b, c, d});
        }
    count_ = (GLsizei)idx.size();
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned), idx.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, (size_t)n * sizeof(Vertex), nullptr, GL_DYNAMIC_DRAW);
    const GLsizei st = sizeof(Vertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, st, (void*)offsetof(Vertex, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, st, (void*)offsetof(Vertex, nrm));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, st, (void*)offsetof(Vertex, uv));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, st, (void*)offsetof(Vertex, tan));
    glEnableVertexAttribArray(4);
    glVertexAttribIPointer(4, 4, GL_UNSIGNED_BYTE, st, (void*)offsetof(Vertex, bones));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, st, (void*)offsetof(Vertex, weights));
    glBindVertexArray(0);
    upload();
}

void Cloth::step(float dt, const World& w, const std::vector<const CharacterBody*>& bodies,
                 const std::vector<std::pair<glm::vec3, glm::vec3>>& movers, const Wind& wind, float t) {
    if (dt <= 0.0f || p_.empty() || !place_.physics) return;
    // passo fixo de 1/120 s (estável com qualquer FPS)
    acc_ = std::min(acc_ + dt, 0.05f);
    const float h = 1.0f / 120.0f;
    // caixas sólidas perto do pano (as outras nem são testadas)
    std::vector<const Box*> near;
    const glm::vec3 lo = lo_ - glm::vec3(1.0f), hi = hi_ + glm::vec3(1.0f);
    for (const Box& b : w.boxes)
        if (b.solid && b.max.x > lo.x && b.min.x < hi.x && b.max.y > lo.y && b.min.y < hi.y && b.max.z > lo.z && b.min.z < hi.z)
            near.push_back(&b);
    const float massPt = std::max(0.02f, place_.mass) * place_.width * place_.height / (nx_ * ny_);
    while (acc_ >= h) {
        acc_ -= h;
        t += h;
        // vento: só a parte que bate de frente no pano empurra (por triângulo), com rajadas
        std::vector<glm::vec3> force(p_.size(), glm::vec3(0.0f, -9.81f * massPt, 0.0f));
        if (wind.on && wind.strength > 0.0f) {
            const glm::vec3 wd(std::sin(wind.dir), 0.0f, std::cos(wind.dir));
            for (int j = 0; j + 1 < ny_; ++j)
                for (int i = 0; i + 1 < nx_; ++i) {
                    int a = j * nx_ + i, b = a + 1, c = a + nx_;
                    glm::vec3 n = glm::cross(p_[b] - p_[a], p_[c] - p_[a]);
                    float area = glm::length(n);
                    if (area < 1e-8f) continue;
                    n /= area;
                    glm::vec3 mid = (p_[a] + p_[b] + p_[c]) / 3.0f;
                    float gust = 1.0f + wind.gust * (0.6f * std::sin(t * 1.7f + mid.x * 0.4f) + 0.4f * std::sin(t * 4.3f + mid.z * 0.9f + mid.y));
                    glm::vec3 v = wd * wind.strength * std::max(0.0f, gust);
                    glm::vec3 rel = v - (p_[a] - prev_[a]) / h; // vento em relação ao pano
                    // arrasto: ½·ρ·v²·Cd·área (ρ do ar ≈ 1,2; Cd ≈ 1 no pano de frente)
                    const float dn = glm::dot(n, rel);
                    glm::vec3 f = n * (dn * std::fabs(dn)) * 0.6f * (area * 0.5f) * place_.windResponse;
                    force[a] += f / 3.0f;
                    force[b] += f / 3.0f;
                    force[c] += f / 3.0f;
                }
        }
        // Verlet
        const float keep = 1.0f - std::clamp(place_.damping, 0.0f, 0.5f);
        for (size_t k = 0; k < p_.size(); ++k) {
            if (invMass_[k] == 0.0f) { p_[k] = prev_[k] = pin_[k]; continue; }
            glm::vec3 cur = p_[k];
            p_[k] += (p_[k] - prev_[k]) * keep + force[k] / massPt * h * h;
            prev_[k] = cur;
        }
        // molas (algumas voltas)
        for (int it = 0; it < 6; ++it)
            for (const Spring& s : springs_) {
                float wa = invMass_[s.a], wb = invMass_[s.b];
                if (wa + wb == 0.0f) continue;
                glm::vec3 d = p_[s.b] - p_[s.a];
                float len = glm::length(d);
                if (len < 1e-6f) continue;
                glm::vec3 corr = d * ((len - s.rest) / len) * s.k / (wa + wb);
                p_[s.a] += corr * wa;
                p_[s.b] -= corr * wb;
            }
        // colisões: chão, caixas, corpos (cápsulas) e objetos com física (caixas que se mexem)
        const float th = 0.03f; // espessura do pano
        for (size_t k = 0; k < p_.size(); ++k) {
            if (invMass_[k] == 0.0f) continue;
            glm::vec3& q = p_[k];
            float fy = w.floorAt(q.x, q.z) + th;
            if (q.y < fy) { q.y = fy; prev_[k] = glm::mix(prev_[k], q, 0.6f); } // atrito no chão
            auto boxOut = [&](const glm::vec3& bmin, const glm::vec3& bmax) {
                glm::vec3 a = bmin - glm::vec3(th), b = bmax + glm::vec3(th);
                if (q.x <= a.x || q.x >= b.x || q.y <= a.y || q.y >= b.y || q.z <= a.z || q.z >= b.z) return;
                float dx0 = q.x - a.x, dx1 = b.x - q.x, dy0 = q.y - a.y, dy1 = b.y - q.y, dz0 = q.z - a.z, dz1 = b.z - q.z;
                float m = std::min({dx0, dx1, dy0, dy1, dz0, dz1});
                if (m == dy1) q.y = b.y;
                else if (m == dy0) q.y = a.y;
                else if (m == dx0) q.x = a.x;
                else if (m == dx1) q.x = b.x;
                else if (m == dz0) q.z = a.z;
                else q.z = b.z;
                prev_[k] = glm::mix(prev_[k], q, 0.5f); // atrito
            };
            for (const Box* b : near) boxOut(b->min, b->max);
            for (const auto& [mn, mx] : movers) boxOut(mn, mx);
            for (const CharacterBody* c : bodies) {
                const float r = c->radius + 0.06f;
                glm::vec3 a = c->pos + glm::vec3(0.0f, r, 0.0f), b = c->pos + glm::vec3(0.0f, c->height - r * 0.5f, 0.0f);
                glm::vec3 ab = b - a;
                float s = glm::clamp(glm::dot(q - a, ab) / glm::dot(ab, ab), 0.0f, 1.0f);
                glm::vec3 cp = a + ab * s, d = q - cp;
                float dist = glm::length(d);
                if (dist < r && dist > 1e-5f) {
                    q = cp + d / dist * r;
                    prev_[k] = glm::mix(prev_[k], q - c->vel * h, 0.5f); // o corpo arrasta o pano junto
                }
            }
        }
    }
}

void Cloth::upload() {
    if (!vbo_) return;
    std::vector<Vertex> v(p_.size());
    lo_ = glm::vec3(1e9f);
    hi_ = glm::vec3(-1e9f);
    for (int j = 0; j < ny_; ++j)
        for (int i = 0; i < nx_; ++i) {
            int k = j * nx_ + i;
            const glm::vec3& q = p_[k];
            lo_ = glm::min(lo_, q);
            hi_ = glm::max(hi_, q);
            glm::vec3 tx = p_[j * nx_ + std::min(i + 1, nx_ - 1)] - p_[j * nx_ + std::max(i - 1, 0)];
            glm::vec3 ty = p_[std::min(j + 1, ny_ - 1) * nx_ + i] - p_[std::max(j - 1, 0) * nx_ + i];
            glm::vec3 n = glm::cross(ty, tx);
            n = glm::length(n) > 1e-8f ? glm::normalize(n) : glm::vec3(0, 0, 1);
            glm::vec3 t = glm::length(tx) > 1e-8f ? glm::normalize(tx) : glm::vec3(1, 0, 0);
            Vertex& o = v[k];
            o.pos[0] = q.x; o.pos[1] = q.y; o.pos[2] = q.z;
            o.nrm[0] = n.x; o.nrm[1] = n.y; o.nrm[2] = n.z;
            const float tile = std::max(0.05f, place_.tile);
            o.uv[0] = (float)i / (nx_ - 1) * place_.width / tile;
            o.uv[1] = (float)j / (ny_ - 1) * place_.height / tile;
            o.tan[0] = t.x; o.tan[1] = t.y; o.tan[2] = t.z; o.tan[3] = 1.0f;
            o.bones[0] = o.bones[1] = o.bones[2] = o.bones[3] = 0;
            o.weights[0] = 1.0f; o.weights[1] = o.weights[2] = o.weights[3] = 0.0f;
        }
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, v.size() * sizeof(Vertex), v.data());
}

void Cloth::draw() const {
    if (!vao_) return;
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, count_, GL_UNSIGNED_INT, nullptr);
}

void Cloth::release() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (ebo_) glDeleteBuffers(1, &ebo_);
    if (tex_) glDeleteTextures(1, &tex_);
    vao_ = vbo_ = ebo_ = tex_ = 0;
    p_.clear();
    prev_.clear();
    pin_.clear();
    invMass_.clear();
    springs_.clear();
    acc_ = 0.0f;
}
