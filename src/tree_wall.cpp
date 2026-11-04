#include "tree_wall.h"
#include "gl_util.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace {
// Mesmo formato de vértice dos modelos (desenhado pelo shader deles, com recorte por alpha).
struct Vertex {
    float pos[3], nrm[3], uv[2], tan[4];
    uint8_t bones[4];
    float weights[4];
};

float hash2(int x, int y, unsigned s) {
    unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u + s * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return ((h ^ (h >> 16)) & 0xFFFFFF) / float(0xFFFFFF);
}
float noise2(float x, float y, unsigned s) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float fx = x - xi, fy = y - yi;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    float a = hash2(xi, yi, s), b = hash2(xi + 1, yi, s), c = hash2(xi, yi + 1, s), d = hash2(xi + 1, yi + 1, s);
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}

// Atlas 2x2 de árvores (256 px cada): tronco + copa de vários "tufos" com folhas em ruído,
// iluminados de cima (claro em cima, escuro embaixo) para parecer volume de longe.
GLuint makeTreeAtlas() {
    const int S = 256, W = S * 2;
    std::vector<unsigned char> px((size_t)W * W * 4, 0);
    std::mt19937 rng(11);
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    for (int t = 0; t < 4; ++t) {
        const int ox = (t % 2) * S, oy = (t / 2) * S;
        struct Blob { float x, y, r; };
        std::vector<Blob> blobs;
        const bool tall = t == 3; // uma variante alta e estreita (tipo eucalipto)
        const int nb = 16 + (int)(U(rng) * 8);
        for (int i = 0; i < nb; ++i) {
            float y = (tall ? 0.10f : 0.12f) + U(rng) * (tall ? 0.55f : 0.45f);
            float spread = (tall ? 0.16f : 0.30f) * (0.6f + 0.4f * std::sin(y * 3.14f));
            blobs.push_back({0.5f + (U(rng) * 2 - 1) * spread, y, (tall ? 0.09f : 0.12f) + U(rng) * 0.07f});
        }
        for (int y = 0; y < S; ++y)
            for (int x = 0; x < S; ++x) {
                const float u = (x + 0.5f) / S, v = (y + 0.5f) / S; // v = 0 em cima
                float leaf = 0.0f, shade = 0.0f;
                for (const Blob& b : blobs) {
                    float d = std::hypot(u - b.x, (v - b.y) * 1.05f) / b.r;
                    float edge = 1.0f + 0.35f * (noise2(u * 40, v * 40, t * 7 + 1) - 0.5f) + 0.2f * (noise2(u * 90, v * 90, t * 7 + 2) - 0.5f);
                    if (d < edge) {
                        leaf = 1.0f;
                        shade = std::max(shade, 1.0f - (v - b.y + b.r) / (2.0f * b.r)); // claro em cima de cada tufo
                    }
                }
                // tronco
                const float trunkW = tall ? 0.018f : 0.028f;
                const bool trunk = !leaf && v > 0.45f && v < 0.98f && std::fabs(u - 0.5f - 0.02f * std::sin(v * 9)) < trunkW * (1.3f - v * 0.4f);
                unsigned char* p = &px[((size_t)(oy + y) * W + ox + x) * 4];
                if (leaf) {
                    float n = noise2(u * 60, v * 60, t * 13 + 3);
                    float k = 0.35f + 0.55f * shade * (0.7f + 0.3f * n) * (1.0f - 0.4f * v);
                    const float base[3] = {0.09f + 0.03f * (t == 2), 0.14f + 0.02f * t, 0.06f}; // verde de mata, pouco saturado
                    for (int c = 0; c < 3; ++c) p[c] = (unsigned char)std::clamp(base[c] * k * 255.0f * 1.5f, 0.0f, 255.0f);
                    p[3] = 255;
                } else if (trunk) {
                    p[0] = 62; p[1] = 48; p[2] = 36; p[3] = 255;
                } else {
                    p[0] = 28; p[1] = 48; p[2] = 16; p[3] = 0; // fundo transparente com cor de folha (sem halo branco)
                }
            }
    }
    return makeTexture2D(px.data(), W, W, true);
}
} // namespace

void TreeWall::build(float r0, float r1, unsigned seed, GLuint atlas, float aspect) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    std::vector<Vertex> v;
    std::vector<unsigned> idx;
    auto quad = [&](glm::vec3 c, glm::vec3 side, float w, float h, int variant) {
        const float u0 = (variant % 2) * 0.5f, v0 = (variant / 2) * 0.5f;
        const glm::vec3 n(0.0f, 1.0f, 0.0f); // luz de copa, igual nos dois planos (nenhum fica escuro de costas)
        const glm::vec3 corners[4] = {c - side * (w * 0.5f), c + side * (w * 0.5f), c + side * (w * 0.5f) + glm::vec3(0, h, 0),
                                      c - side * (w * 0.5f) + glm::vec3(0, h, 0)};
        // foto feita pela placa: linha 0 da textura é a de baixo (tronco embaixo, copa em cima)
        const float uvs[4][2] = {{u0, v0}, {u0 + 0.5f, v0}, {u0 + 0.5f, v0 + 0.5f}, {u0, v0 + 0.5f}};
        const unsigned base = (unsigned)v.size();
        for (int k = 0; k < 4; ++k) {
            Vertex q{};
            q.pos[0] = corners[k].x; q.pos[1] = corners[k].y; q.pos[2] = corners[k].z;
            q.nrm[0] = n.x; q.nrm[1] = n.y; q.nrm[2] = n.z;
            q.uv[0] = uvs[k][0]; q.uv[1] = uvs[k][1];
            q.tan[0] = side.x; q.tan[1] = side.y; q.tan[2] = side.z; q.tan[3] = 1.0f;
            q.weights[0] = 1.0f;
            v.push_back(q);
        }
        idx.insert(idx.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    };
    // densidade cai com a distância (perto: mata fechada; longe: o relevo já tem cor de mata)
    for (int i = 0; i < 6000; ++i) {
        float t = U(rng);
        float r = r0 + (r1 - r0) * t * t;
        float a = U(rng) * 6.2831853f;
        glm::vec3 p(std::cos(a) * r, 0.0f, std::sin(a) * r);
        p.y = std::max(0.0f, vistaHeightAt(p.x, p.z, 125.0f, 960.0f, 7u)) - 0.3f;
        const int variant = (int)(U(rng) * 4) % 4;
        // mais baixas que as do mapa (6 a 8,5 m), largura da foto do modelo
        const float h = 6.0f + 2.5f * U(rng), w = h * aspect * 2.0f;
        const float rot = U(rng) * 3.14159f;
        glm::vec3 s1(std::cos(rot), 0, std::sin(rot)), s2(-s1.z, 0, s1.x);
        quad(p, s1, w, h, variant);
        quad(p, s2, w, h, variant);
    }
    tex_ = atlas ? atlas : makeTreeAtlas();
    count_ = (GLsizei)idx.size();
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(Vertex), v.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned), idx.data(), GL_STATIC_DRAW);
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
}

void TreeWall::draw() const {
    if (!vao_) return;
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, count_, GL_UNSIGNED_INT, nullptr);
}
