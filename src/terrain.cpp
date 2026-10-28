#include "terrain.h"

#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>

namespace {
// Queda do pincel: 1 no miolo, cai suave até 0 na borda ('softness' = parte que cai).
float falloff(float d, float radius, float softness) {
    if (d >= radius) return 0.0f;
    float inner = radius * (1.0f - std::clamp(softness, 0.0f, 1.0f));
    if (d <= inner) return 1.0f;
    float k = (d - inner) / std::max(1e-4f, radius - inner);
    return 1.0f - k * k * (3.0f - 2.0f * k);
}

// Imagem RGBA reamostrada (bilinear) para s x s.
std::vector<unsigned char> loadResized(const std::string& path, int s, bool& ok) {
    int w, h, c;
    unsigned char* px = stbi_load(path.c_str(), &w, &h, &c, 4);
    std::vector<unsigned char> out((size_t)s * s * 4);
    ok = px != nullptr;
    if (!px) return out;
    for (int y = 0; y < s; ++y)
        for (int x = 0; x < s; ++x) {
            float fx = (x + 0.5f) * w / s - 0.5f, fy = (y + 0.5f) * h / s - 0.5f;
            int x0 = std::clamp((int)std::floor(fx), 0, w - 1), y0 = std::clamp((int)std::floor(fy), 0, h - 1);
            int x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
            float ax = std::clamp(fx - x0, 0.0f, 1.0f), ay = std::clamp(fy - y0, 0.0f, 1.0f);
            for (int k = 0; k < 4; ++k) {
                float a = px[(y0 * w + x0) * 4 + k] * (1 - ax) + px[(y0 * w + x1) * 4 + k] * ax;
                float b = px[(y1 * w + x0) * 4 + k] * (1 - ax) + px[(y1 * w + x1) * 4 + k] * ax;
                out[((size_t)y * s + x) * 4 + k] = (unsigned char)std::lround(a * (1 - ay) + b * ay);
            }
        }
    stbi_image_free(px);
    return out;
}
} // namespace

// ---------------------------------------------------------------- relevo
void Terrain::create(const glm::vec2& center, float sz, int count, float baseY) {
    size = sz;
    n = std::clamp(count, 3, 513);
    origin = center - glm::vec2(size * 0.5f);
    h.assign((size_t)n * n, baseY);
    active = true;
    upload();
}

bool Terrain::heightAt(float x, float z, float& y) const {
    if (!active) return false;
    const float cell = size / (n - 1);
    float fx = (x - origin.x) / cell, fz = (z - origin.y) / cell;
    if (fx < 0.0f || fz < 0.0f || fx > n - 1 || fz > n - 1) return false;
    int x0 = std::min((int)fx, n - 2), z0 = std::min((int)fz, n - 2);
    float ax = fx - x0, az = fz - z0;
    auto H = [&](int i, int j) { return h[(size_t)j * n + i]; };
    float a = H(x0, z0) * (1 - ax) + H(x0 + 1, z0) * ax;
    float b = H(x0, z0 + 1) * (1 - ax) + H(x0 + 1, z0 + 1) * ax;
    y = a * (1 - az) + b * az;
    return true;
}

bool Terrain::raycast(const glm::vec3& o, const glm::vec3& d, float maxT, float& t) const {
    if (!active) return false;
    const float step = std::max(0.05f, size / (n - 1) * 0.5f);
    float prev = 0.0f, y;
    bool prevAbove = !heightAt(o.x, o.z, y) || o.y >= y;
    for (float s = step; s <= maxT; s += step) {
        glm::vec3 p = o + d * s;
        if (!heightAt(p.x, p.z, y)) { prevAbove = true; prev = s; continue; }
        bool above = p.y >= y;
        if (prevAbove && !above) { // cruzou: refina
            float a = prev, b = s;
            for (int i = 0; i < 12; ++i) {
                float m = (a + b) * 0.5f;
                glm::vec3 q = o + d * m;
                float hy;
                if (heightAt(q.x, q.z, hy) && q.y < hy) b = m;
                else a = m;
            }
            t = b;
            return true;
        }
        prevAbove = above;
        prev = s;
    }
    return false;
}

void Terrain::brush(const glm::vec2& c, float radius, float strength, float softness, int mode, float level, float dt) {
    if (!active) return;
    const float cell = size / (n - 1);
    int i0 = std::max(0, (int)((c.x - radius - origin.x) / cell)), i1 = std::min(n - 1, (int)((c.x + radius - origin.x) / cell) + 1);
    int j0 = std::max(0, (int)((c.y - radius - origin.y) / cell)), j1 = std::min(n - 1, (int)((c.y + radius - origin.y) / cell) + 1);
    std::vector<float> src = h;
    for (int j = j0; j <= j1; ++j)
        for (int i = i0; i <= i1; ++i) {
            glm::vec2 p = origin + glm::vec2(i, j) * cell;
            float f = falloff(glm::length(p - c), radius, softness);
            if (f <= 0.0f) continue;
            float& y = h[(size_t)j * n + i];
            float k = f * strength * dt;
            if (mode == 0) y += k;
            else if (mode == 1) y -= k;
            else if (mode == 2) { // média dos vizinhos
                float s = 0.0f;
                int cnt = 0;
                for (int dj = -1; dj <= 1; ++dj)
                    for (int di = -1; di <= 1; ++di) {
                        int a = std::clamp(i + di, 0, n - 1), b = std::clamp(j + dj, 0, n - 1);
                        s += src[(size_t)b * n + a];
                        ++cnt;
                    }
                y += (s / cnt - y) * std::min(1.0f, k * 4.0f);
            } else y += (level - y) * std::min(1.0f, k * 2.0f);
        }
    upload();
}

void Terrain::upload() {
    if (!active) return;
    struct V { float p[3], nrm[3]; };
    std::vector<V> v((size_t)n * n);
    const float cell = size / (n - 1);
    auto H = [&](int i, int j) { return h[(size_t)std::clamp(j, 0, n - 1) * n + std::clamp(i, 0, n - 1)]; };
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            V& q = v[(size_t)j * n + i];
            q.p[0] = origin.x + i * cell;
            q.p[1] = H(i, j);
            q.p[2] = origin.y + j * cell;
            glm::vec3 nn = glm::normalize(glm::vec3(H(i - 1, j) - H(i + 1, j), 2.0f * cell, H(i, j - 1) - H(i, j + 1)));
            q.nrm[0] = nn.x; q.nrm[1] = nn.y; q.nrm[2] = nn.z;
        }
    if (!vao_) {
        std::vector<unsigned> idx;
        idx.reserve((size_t)(n - 1) * (n - 1) * 6);
        for (int j = 0; j + 1 < n; ++j)
            for (int i = 0; i + 1 < n; ++i) {
                unsigned a = j * n + i, b = a + 1, c = a + n, d = c + 1;
                idx.insert(idx.end(), {a, c, b, b, c, d});
            }
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glGenBuffers(1, &ebo_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned), idx.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(V), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(V), (void*)(3 * sizeof(float)));
        count_ = (GLsizei)idx.size();
    }
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(V), v.data(), GL_DYNAMIC_DRAW);
    glBindVertexArray(0);
}

void Terrain::draw() const {
    if (!active || !vao_) return;
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, count_, GL_UNSIGNED_INT, nullptr);
}

void Terrain::release() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (ebo_) glDeleteBuffers(1, &ebo_);
    vao_ = vbo_ = ebo_ = 0;
    active = false;
    h.clear();
}

bool Terrain::save(const std::string& path) const {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write((const char*)h.data(), h.size() * sizeof(float));
    return (bool)f;
}

bool Terrain::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<float> v((size_t)n * n);
    if (!f.read((char*)v.data(), v.size() * sizeof(float))) return false;
    h = v;
    active = true;
    upload();
    return true;
}

// ---------------------------------------------------------------- pintura
void GroundPaint::create(const glm::vec2& o, float sz, int r) {
    origin = o;
    size = sz;
    res = std::clamp(r, 64, 2048);
    weights.assign((size_t)res * res * 4, 0);
    active = true;
    uploadWeights();
}

void GroundPaint::brush(const glm::vec2& c, float radius, float strength, float softness, int layer, bool erase, float dt) {
    if (!active || layer < 0 || layer >= kMaxLayers) return;
    const float texel = size / res;
    int i0 = std::max(0, (int)((c.x - radius - origin.x) / texel)), i1 = std::min(res - 1, (int)((c.x + radius - origin.x) / texel) + 1);
    int j0 = std::max(0, (int)((c.y - radius - origin.y) / texel)), j1 = std::min(res - 1, (int)((c.y + radius - origin.y) / texel) + 1);
    for (int j = j0; j <= j1; ++j)
        for (int i = i0; i <= i1; ++i) {
            glm::vec2 p = origin + (glm::vec2(i, j) + 0.5f) * texel;
            float a = falloff(glm::length(p - c), radius, softness) * std::clamp(strength * dt, 0.0f, 1.0f);
            if (a <= 0.0f) continue;
            unsigned char* w = &weights[((size_t)j * res + i) * 4];
            // mistura suave: a camada pintada cresce e as outras diminuem na mesma proporção
            for (int k = 0; k < 4; ++k) {
                float target = !erase && k == layer ? 255.0f : 0.0f;
                w[k] = (unsigned char)std::lround(w[k] + (target - w[k]) * a);
            }
        }
    uploadWeights();
}

void GroundPaint::uploadWeights() {
    if (!active) return;
    if (!weightTex) {
        glGenTextures(1, &weightTex);
        glBindTexture(GL_TEXTURE_2D, weightTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, weightTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, res, res, 0, GL_RGBA, GL_UNSIGNED_BYTE, weights.data());
}

void GroundPaint::uploadLayers(const std::string& root) {
    const int s = kLayerRes;
    auto makeArray = [&](GLuint& tex, bool srgb) {
        if (!tex) glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D_ARRAY, tex);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, s, s, kMaxLayers, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameterf(GL_TEXTURE_2D_ARRAY, 0x84FE /*GL_TEXTURE_MAX_ANISOTROPY*/, 8.0f);
    };
    makeArray(albedoArr, true);
    makeArray(normalArr, false);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (int i = 0; i < kMaxLayers; ++i) {
        bool ok = false;
        std::vector<unsigned char> a, nm;
        if (i < (int)layers.size() && !layers[i].albedo.empty()) a = loadResized(root + "/" + layers[i].albedo, s, ok);
        if (!ok) a.assign((size_t)s * s * 4, 160); // cinza enquanto não escolhe a imagem
        ok = false;
        if (i < (int)layers.size() && !layers[i].normal.empty()) nm = loadResized(root + "/" + layers[i].normal, s, ok);
        if (!ok) { // normal "reta"
            nm.resize((size_t)s * s * 4);
            for (size_t k = 0; k < nm.size(); k += 4) { nm[k] = 128; nm[k + 1] = 128; nm[k + 2] = 255; nm[k + 3] = 255; }
        }
        glBindTexture(GL_TEXTURE_2D_ARRAY, albedoArr);
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, i, s, s, 1, GL_RGBA, GL_UNSIGNED_BYTE, a.data());
        glBindTexture(GL_TEXTURE_2D_ARRAY, normalArr);
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, i, s, s, 1, GL_RGBA, GL_UNSIGNED_BYTE, nm.data());
    }
    glBindTexture(GL_TEXTURE_2D_ARRAY, albedoArr);
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    glBindTexture(GL_TEXTURE_2D_ARRAY, normalArr);
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
}

void GroundPaint::release() {
    GLuint t[3] = {weightTex, albedoArr, normalArr};
    glDeleteTextures(3, t);
    weightTex = albedoArr = normalArr = 0;
    active = false;
    weights.clear();
    layers.clear();
}

bool GroundPaint::saveWeights(const std::string& path) const {
    stbi_flip_vertically_on_write(0);
    return stbi_write_png(path.c_str(), res, res, 4, weights.data(), res * 4) != 0;
}

bool GroundPaint::loadWeights(const std::string& path) {
    int w, h, c;
    unsigned char* px = stbi_load(path.c_str(), &w, &h, &c, 4);
    if (!px) return false;
    if (w == res && h == res) weights.assign(px, px + (size_t)w * h * 4);
    stbi_image_free(px);
    if (w != res || h != res) return false;
    uploadWeights();
    return true;
}
