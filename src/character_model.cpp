#include "character_model.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/GltfMaterial.h>

#include <glm/gtc/matrix_transform.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <numeric>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

struct Vertex {
    float pos[3];
    float nrm[3];
    float uv[2];
    float tan[4];
    uint8_t bones[4];
    float weights[4];
};

float distToSegment(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b) {
    glm::vec3 ab = b - a;
    float t = glm::clamp(glm::dot(p - a, ab) / glm::dot(ab, ab), 0.0f, 1.0f);
    return glm::length(p - (a + ab * t));
}

int findRoot(std::vector<int>& par, int x) {
    while (par[x] != x) x = par[x] = par[par[x]];
    return x;
}

// Peças da mão: em vez do osso mais próximo, decide pela posição ao longo da mão
// (palma / falanges 1-2-3) e pelo lado do polegar.
int handPieceBone(const Skeleton& sk, int nearest, const glm::vec3& c) {
    const int hand[2][5] = {{HAND_L, FING1_L, FING2_L, FING3_L, THUMB_L}, {HAND_R, FING1_R, FING2_R, FING3_R, THUMB_R}};
    for (int k = 0; k < 2; ++k) {
        if (std::find(hand[k], hand[k] + 5, nearest) == hand[k] + 5) continue;
        glm::vec3 r = c - sk.bindJoint[hand[k][0]];
        float along = glm::dot(r, sk.handAxis[k]), lat = glm::dot(r, sk.handLat[k]), back = glm::dot(r, sk.handBack[k]);
        if (lat > 0.025f && back < 0.015f && along < 0.14f) return hand[k][4];
        if (along < 0.105f) return hand[k][0];
        if (along < 0.142f) return hand[k][1];
        if (along < 0.173f) return hand[k][2];
        return hand[k][3];
    }
    return nearest;
}

// Rig rígido: o boneco é feito de peças separadas. Cada peça (componente conexo
// da malha) segue inteira o osso mais próximo, como um boneco mecânico.
void rigidRig(const Skeleton& sk, std::vector<Vertex>& verts, const std::vector<unsigned>& idx) {
    auto t0 = std::chrono::steady_clock::now();

    // solda vértices com a mesma posição (costuras de UV) para achar as peças
    std::unordered_map<uint64_t, int> map;
    map.reserve(verts.size() * 2);
    std::vector<int> weld(verts.size());
    int n = 0;
    for (size_t i = 0; i < verts.size(); ++i) {
        auto q = [](float v) { return (uint64_t)((int64_t)std::llround(v * 1e4) + (1 << 20)) & 0x1FFFFF; };
        uint64_t key = (q(verts[i].pos[0]) << 42) | (q(verts[i].pos[1]) << 21) | q(verts[i].pos[2]);
        auto it = map.try_emplace(key, n);
        if (it.second) n++;
        weld[i] = it.first->second;
    }

    std::vector<int> par(n);
    std::iota(par.begin(), par.end(), 0);
    for (size_t t = 0; t + 2 < idx.size(); t += 3)
        for (int e = 1; e < 3; ++e) {
            int a = findRoot(par, weld[idx[t]]), b = findRoot(par, weld[idx[t + e]]);
            if (a != b) par[a] = b;
        }

    // distância média de cada peça até cada osso (na pose do arquivo)
    std::unordered_map<int, std::vector<double>> sum;
    std::unordered_map<int, int> count;
    std::unordered_map<int, glm::dvec3> center;
    for (size_t i = 0; i < verts.size(); ++i) {
        int c = findRoot(par, weld[i]);
        auto& s = sum[c];
        if (s.empty()) s.assign(BONE_COUNT, 0.0);
        glm::vec3 p(verts[i].pos[0], verts[i].pos[1], verts[i].pos[2]);
        for (int b = 0; b < BONE_COUNT; ++b) s[b] += distToSegment(p, sk.bindJoint[b], sk.bindTail[b]);
        center[c] += glm::dvec3(p);
        count[c]++;
    }
    std::unordered_map<int, int> boneOf;
    int perBone[BONE_COUNT] = {};
    for (auto& [c, s] : sum) {
        int best = (int)(std::min_element(s.begin(), s.end()) - s.begin());
        best = handPieceBone(sk, best, glm::vec3(center[c] / (double)count[c]));
        boneOf[c] = best;
        perBone[best]++;
    }
    for (size_t i = 0; i < verts.size(); ++i) {
        Vertex& v = verts[i];
        v.bones[0] = (uint8_t)boneOf[findRoot(par, weld[i])];
        v.bones[1] = v.bones[2] = v.bones[3] = 0;
        v.weights[0] = 1.0f;
        v.weights[1] = v.weights[2] = v.weights[3] = 0.0f;
    }

    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("[rig] %zu pecas rigidas em %.0f ms |", sum.size(), ms);
    for (int b = 0; b < BONE_COUNT; ++b) std::printf(" %s:%d", sk.name[b], perBone[b]);
    std::printf("\n");
}

void computeTangents(std::vector<Vertex>& v, const std::vector<unsigned>& idx) {
    std::vector<glm::vec3> T(v.size(), glm::vec3(0)), B(v.size(), glm::vec3(0));
    for (size_t t = 0; t + 2 < idx.size(); t += 3) {
        const Vertex &a = v[idx[t]], &b = v[idx[t + 1]], &c = v[idx[t + 2]];
        glm::vec3 p0(a.pos[0], a.pos[1], a.pos[2]), p1(b.pos[0], b.pos[1], b.pos[2]), p2(c.pos[0], c.pos[1], c.pos[2]);
        glm::vec2 u0(a.uv[0], a.uv[1]), u1(b.uv[0], b.uv[1]), u2(c.uv[0], c.uv[1]);
        glm::vec3 e1 = p1 - p0, e2 = p2 - p0;
        glm::vec2 d1 = u1 - u0, d2 = u2 - u0;
        float r = d1.x * d2.y - d2.x * d1.y;
        if (std::fabs(r) < 1e-12f) continue;
        float f = 1.0f / r;
        glm::vec3 tt = (e1 * d2.y - e2 * d1.y) * f;
        glm::vec3 bb = (e2 * d1.x - e1 * d2.x) * f;
        for (int k = 0; k < 3; ++k) { T[idx[t + k]] += tt; B[idx[t + k]] += bb; }
    }
    for (size_t i = 0; i < v.size(); ++i) {
        glm::vec3 n(v[i].nrm[0], v[i].nrm[1], v[i].nrm[2]);
        glm::vec3 t = T[i] - n * glm::dot(n, T[i]);
        if (glm::dot(t, t) < 1e-20f) t = glm::cross(n, std::fabs(n.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0));
        t = glm::normalize(t);
        // UVs no padrão glTF (v cresce para baixo na imagem); o verde do normal map aponta para cima.
        float w = glm::dot(glm::cross(n, t), B[i]) < 0.0f ? 1.0f : -1.0f;
        v[i].tan[0] = t.x; v[i].tan[1] = t.y; v[i].tan[2] = t.z; v[i].tan[3] = w;
    }
}

struct Image {
    std::vector<unsigned char> px; // RGBA8
    int w = 0, h = 0;
    std::string name;
};

// Reduz pela metade (média 2x2) até caber em maxSize.
void downscale(Image& im, int maxSize) {
    while (maxSize > 0 && std::max(im.w, im.h) > maxSize && im.w > 1 && im.h > 1) {
        int w = im.w / 2, h = im.h / 2;
        std::vector<unsigned char> out((size_t)w * h * 4);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                for (int c = 0; c < 4; ++c) {
                    auto at = [&](int xx, int yy) { return (int)im.px[((size_t)yy * im.w + xx) * 4 + c]; };
                    out[((size_t)y * w + x) * 4 + c] =
                        (unsigned char)((at(2 * x, 2 * y) + at(2 * x + 1, 2 * y) + at(2 * x, 2 * y + 1) + at(2 * x + 1, 2 * y + 1) + 2) / 4);
                }
        im.px.swap(out);
        im.w = w;
        im.h = h;
    }
}

bool decodeTexture(const aiTexture* tex, const char* name, int maxSize, Image& out) {
    int w, h, c;
    if (tex->mHeight == 0) {
        unsigned char* px = stbi_load_from_memory((const stbi_uc*)tex->pcData, (int)tex->mWidth, &w, &h, &c, 4);
        if (!px) return false;
        out.px.assign(px, px + (size_t)w * h * 4);
        stbi_image_free(px);
    } else {
        w = (int)tex->mWidth;
        h = (int)tex->mHeight;
        out.px.resize((size_t)w * h * 4);
        for (int i = 0; i < w * h; ++i) {
            out.px[i * 4 + 0] = tex->pcData[i].r;
            out.px[i * 4 + 1] = tex->pcData[i].g;
            out.px[i * 4 + 2] = tex->pcData[i].b;
            out.px[i * 4 + 3] = tex->pcData[i].a;
        }
    }
    out.w = w;
    out.h = h;
    out.name = name;
    downscale(out, maxSize);
    return true;
}

// Imagens já decodificadas em paralelo durante o load (textura embutida -> imagem reduzida).
std::map<const aiTexture*, Image>* g_decoded = nullptr;

const aiTextureType kAllTextureTypes[] = {aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE,   aiTextureType_METALNESS,
                                          aiTextureType_DIFFUSE_ROUGHNESS, aiTextureType_UNKNOWN, aiTextureType_NORMALS,
                                          aiTextureType_EMISSIVE,   aiTextureType_SPECULAR,  aiTextureType_LIGHTMAP,
                                          aiTextureType_AMBIENT_OCCLUSION};

// Decodifica de uma vez, em várias threads, todas as texturas embutidas que esses materiais usam.
void predecode(const aiScene* sc, const std::vector<int>& matIds, int maxSize, std::map<const aiTexture*, Image>& out) {
    std::vector<std::pair<const aiTexture*, std::string>> todo;
    for (int id : matIds)
        for (aiTextureType type : kAllTextureTypes) {
            aiString path;
            if (sc->mMaterials[id]->GetTexture(type, 0, &path) != AI_SUCCESS) continue;
            const aiTexture* tex = sc->GetEmbeddedTexture(path.C_Str());
            if (tex && !out.count(tex)) {
                out[tex];
                todo.push_back({tex, path.C_Str()});
            }
        }
    if (todo.size() < 2) { // uma textura só: decodifica na hora, sem threads
        for (auto& t : todo) out.erase(t.first);
        return;
    }
    std::vector<Image> imgs(todo.size());
    std::vector<char> ok(todo.size(), 0);
    std::atomic<size_t> next{0};
    auto work = [&]() {
        for (size_t i; (i = next++) < todo.size();) ok[i] = decodeTexture(todo[i].first, todo[i].second.c_str(), maxSize, imgs[i]);
    };
    const unsigned n = std::max(1u, std::min<unsigned>(std::thread::hardware_concurrency(), (unsigned)todo.size()));
    std::vector<std::thread> th;
    for (unsigned k = 1; k < n; ++k) th.emplace_back(work);
    work();
    for (std::thread& t : th) t.join();
    for (size_t i = 0; i < todo.size(); ++i) {
        if (ok[i]) out[todo[i].first] = std::move(imgs[i]);
        else out.erase(todo[i].first);
    }
}

bool decodeEmbedded(const aiScene* sc, const aiMaterial* m, std::initializer_list<aiTextureType> types, int maxSize,
                    Image& out) {
    for (aiTextureType type : types) {
        aiString path;
        if (m->GetTexture(type, 0, &path) != AI_SUCCESS) continue;
        const aiTexture* tex = sc->GetEmbeddedTexture(path.C_Str());
        if (!tex) continue;
        if (g_decoded) {
            auto it = g_decoded->find(tex);
            if (it != g_decoded->end()) {
                out = it->second;
                return true;
            }
        }
        if (decodeTexture(tex, path.C_Str(), maxSize, out)) return true;
    }
    return false;
}

// Folhagem: se a imagem já tem alpha, recorta por ele; se não tem e o material é de folhas, o fundo
// branco vira transparente (e ganha a cor média das folhas, para o filtro não deixar borda branca).
bool cutoutAlpha(Image& im, std::string name) {
    const size_t n = (size_t)im.w * im.h;
    size_t holes = 0;
    for (size_t i = 0; i < n; ++i) holes += im.px[i * 4 + 3] < 128;
    if (holes > n / 50) return true;
    for (char& ch : name) ch = (char)std::tolower((unsigned char)ch);
    if (name.find("leaf") == std::string::npos && name.find("leaves") == std::string::npos && name.find("folh") == std::string::npos && name.find("foliage") == std::string::npos)
        return false;
    double sum[3] = {0, 0, 0};
    size_t kept = 0;
    std::vector<bool> bg(n);
    for (size_t i = 0; i < n; ++i) {
        const unsigned char* q = &im.px[i * 4];
        int mn = std::min({q[0], q[1], q[2]});
        bg[i] = mn > 200 && std::max({q[0], q[1], q[2]}) - mn < 40; // quase branco e sem cor
        if (!bg[i]) { for (int k = 0; k < 3; ++k) sum[k] += q[k]; ++kept; }
    }
    if (!kept) return false;
    for (size_t i = 0; i < n; ++i)
        if (bg[i]) {
            for (int k = 0; k < 3; ++k) im.px[i * 4 + k] = (unsigned char)(sum[k] / kept);
            im.px[i * 4 + 3] = 0;
        }
    return true;
}

GLuint upload(const Image& im, bool srgb) {
    std::printf("[modelo] textura %s %dx%d\n", im.name.c_str(), im.w, im.h);
    return makeTexture2D(im.px.data(), im.w, im.h, srgb);
}

GLuint loadEmbedded(const aiScene* sc, const aiMaterial* m, std::initializer_list<aiTextureType> types, bool srgb,
                    int maxSize) {
    Image im;
    return decodeEmbedded(sc, m, types, maxSize, im) ? upload(im, srgb) : 0;
}

// Material specular/glossiness (KHR_materials_pbrSpecularGlossiness): monta a textura no
// formato do shader (R = oclusão, G = rugosidade = 1 - brilho, B = metal = 0).
GLuint specGlossToMR(const aiScene* sc, const aiMaterial* m, int maxSize, float glossFactor, bool& hasAO) {
    Image sg, ao;
    bool hasSG = decodeEmbedded(sc, m, {aiTextureType_SPECULAR}, maxSize, sg);
    hasAO = decodeEmbedded(sc, m, {aiTextureType_LIGHTMAP, aiTextureType_AMBIENT_OCCLUSION}, maxSize, ao);
    if (!hasSG && !hasAO) return 0;
    Image out = hasSG ? sg : ao;
    out.name += hasSG && hasAO ? " (brilho + oclusao -> rugosidade)" : hasSG ? " (brilho -> rugosidade)" : " (oclusao)";
    for (int y = 0; y < out.h; ++y)
        for (int x = 0; x < out.w; ++x) {
            unsigned char* p = &out.px[((size_t)y * out.w + x) * 4];
            float gloss = hasSG ? sg.px[((size_t)y * sg.w + x) * 4 + 3] / 255.0f * glossFactor : 0.5f;
            int aoV = 255;
            if (hasAO) aoV = ao.px[(((size_t)y * ao.h / out.h) * ao.w + (size_t)x * ao.w / out.w) * 4];
            p[0] = (unsigned char)aoV;
            p[1] = (unsigned char)std::lround(std::clamp(1.0f - gloss, 0.0f, 1.0f) * 255.0f);
            p[2] = 0;
            p[3] = 255;
        }
    return upload(out, false);
}

glm::mat4 toGlm(const aiMatrix4x4& m) {
    // aiMatrix4x4 é row-major; glm é column-major.
    return glm::transpose(glm::make_mat4(&m.a1));
}

// Interpola dois vértices (posição, normal, UV) para recortar triângulos.
Vertex lerpVertex(const Vertex& a, const Vertex& b, float t) {
    Vertex v = a;
    for (int k = 0; k < 3; ++k) {
        v.pos[k] = a.pos[k] + (b.pos[k] - a.pos[k]) * t;
        v.nrm[k] = a.nrm[k] + (b.nrm[k] - a.nrm[k]) * t;
    }
    for (int k = 0; k < 2; ++k) v.uv[k] = a.uv[k] + (b.uv[k] - a.uv[k]) * t;
    return v;
}

// Corta o polígono (convexo) pelo plano eixo = valor e fica com o lado 'less' (ou o outro).
std::vector<Vertex> clipPoly(const std::vector<Vertex>& in, int axis, float value, bool less) {
    std::vector<Vertex> out;
    auto inside = [&](const Vertex& v) { return less ? v.pos[axis] <= value : v.pos[axis] >= value; };
    for (size_t i = 0; i < in.size(); ++i) {
        const Vertex &a = in[i], &b = in[(i + 1) % in.size()];
        bool ia = inside(a), ib = inside(b);
        if (ia) out.push_back(a);
        if (ia != ib) out.push_back(lerpVertex(a, b, (value - a.pos[axis]) / (b.pos[axis] - a.pos[axis])));
    }
    return out;
}

// Abre os vãos: triângulos da parede (normal no eixo mais fino da caixa) que cruzam a caixa
// ficam só com os pedaços de fora do retângulo do vão.
void cutHoles(std::vector<Vertex>& verts, std::vector<unsigned>& idx, std::vector<int>& triMat,
              const std::vector<std::pair<glm::vec3, glm::vec3>>& cuts) {
    for (const auto& [lo, hi] : cuts) {
        glm::vec3 ext = hi - lo;
        int thin = ext.x < ext.y ? (ext.x < ext.z ? 0 : 2) : (ext.y < ext.z ? 1 : 2);
        int u = (thin + 1) % 3, v = (thin + 2) % 3;
        std::vector<unsigned> outIdx;
        std::vector<int> outMat;
        int cutCount = 0;
        for (size_t t = 0; t + 2 < idx.size(); t += 3) {
            const Vertex* tv[3] = {&verts[idx[t]], &verts[idx[t + 1]], &verts[idx[t + 2]]};
            glm::vec3 p[3];
            for (int k = 0; k < 3; ++k) p[k] = glm::vec3(tv[k]->pos[0], tv[k]->pos[1], tv[k]->pos[2]);
            glm::vec3 bmin = glm::min(p[0], glm::min(p[1], p[2])), bmax = glm::max(p[0], glm::max(p[1], p[2]));
            glm::vec3 n = glm::cross(p[1] - p[0], p[2] - p[0]);
            bool wall = std::fabs(n[thin]) > 0.7f * glm::length(n);
            bool overlap = glm::all(glm::lessThanEqual(bmin, hi)) && glm::all(glm::greaterThanEqual(bmax, lo));
            if (!wall || !overlap) {
                for (int k = 0; k < 3; ++k) outIdx.push_back(idx[t + k]);
                outMat.push_back(triMat[t / 3]);
                continue;
            }
            ++cutCount;
            // pedaços fora do retângulo: antes de lo[u], depois de hi[u], e entre eles acima/abaixo em v
            std::vector<Vertex> poly = {*tv[0], *tv[1], *tv[2]};
            std::vector<std::vector<Vertex>> pieces;
            pieces.push_back(clipPoly(poly, u, lo[u], true));
            std::vector<Vertex> rest = clipPoly(poly, u, lo[u], false);
            pieces.push_back(clipPoly(rest, u, hi[u], false));
            rest = clipPoly(rest, u, hi[u], true);
            pieces.push_back(clipPoly(rest, v, lo[v], true));
            rest = clipPoly(rest, v, lo[v], false);
            pieces.push_back(clipPoly(rest, v, hi[v], false));
            for (const auto& pc : pieces) {
                if (pc.size() < 3) continue;
                unsigned base = (unsigned)verts.size();
                for (const Vertex& vx : pc) verts.push_back(vx);
                for (size_t k = 1; k + 1 < pc.size(); ++k) { // leque (mesma ordem de giro do original)
                    outIdx.push_back(base);
                    outIdx.push_back(base + (unsigned)k);
                    outIdx.push_back(base + (unsigned)k + 1);
                    outMat.push_back(triMat[t / 3]);
                }
            }
        }
        idx.swap(outIdx);
        triMat.swap(outMat);
        std::printf("[modelo] vao aberto: %d triangulos recortados\n", cutCount);
    }
}

// Pesos do arquivo já levados para os nossos ossos (um mapa por vértice).
using BoneWeights = std::vector<std::map<int, float>>;

// Junta todas as malhas aplicando as transformações da hierarquia de nós.
// Arquivo aberto por último para carregar partes (cada parte é um modelo, o arquivo é lido uma vez).
struct OpenFile {
    std::string path;
    int maxTex = -1;
    std::unique_ptr<Assimp::Importer> imp;
    std::map<const aiTexture*, Image> decoded;
};
OpenFile& openFile() {
    static OpenFile f;
    return f;
}

bool hasMeshes(const aiNode* n) {
    if (n->mNumMeshes) return true;
    for (unsigned c = 0; c < n->mNumChildren; ++c)
        if (hasMeshes(n->mChildren[c])) return true;
    return false;
}

// Desce pelos nós sem malha com um filho só (embrulhos do exportador); 'xf' acumula a transformação
// até o nó onde o arquivo se divide (sem a dele: ela entra no collectMeshes de cada filho).
const aiNode* splitNode(const aiNode* n, glm::mat4& xf) {
    xf = glm::mat4(1.0f);
    for (;;) {
        int withMesh = 0;
        const aiNode* only = nullptr;
        for (unsigned c = 0; c < n->mNumChildren; ++c)
            if (hasMeshes(n->mChildren[c])) { ++withMesh; only = n->mChildren[c]; }
        if (n->mNumMeshes || withMesh != 1) break;
        xf = xf * toGlm(n->mTransformation);
        n = only;
    }
    xf = xf * toGlm(n->mTransformation);
    return n;
}

void collectMeshes(const aiScene* sc, const aiNode* node, const glm::mat4& parentXf, bool fixSkinnedAxes,
                   std::vector<Vertex>& verts, std::vector<unsigned>& idx, std::vector<int>& triMat,
                   const CharacterModel::LoadOptions& opt, BoneWeights& weights) {
    const glm::mat4 nodeXf = parentXf * toGlm(node->mTransformation);
    for (unsigned k = 0; k < node->mNumMeshes; ++k) {
        const aiMesh* m = sc->mMeshes[node->mMeshes[k]];
        if (!opt.skipMesh.empty() && std::string(m->mName.C_Str()).find(opt.skipMesh) != std::string::npos) continue;
        // Alguns exports do Sketchfab deixam a malha com skin girada 90° no eixo X
        // em relação ao resto do modelo.
        glm::mat4 xf = nodeXf;
        if (fixSkinnedAxes && m->mNumBones > 0)
            xf = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1, 0, 0)) * xf;
        glm::mat3 nxf = glm::transpose(glm::inverse(glm::mat3(xf)));
        unsigned base = (unsigned)verts.size();
        for (unsigned i = 0; i < m->mNumVertices; ++i) {
            Vertex v{};
            glm::vec3 p = glm::vec3(xf * glm::vec4(m->mVertices[i].x, m->mVertices[i].y, m->mVertices[i].z, 1.0f));
            glm::vec3 n = glm::normalize(nxf * glm::vec3(m->mNormals[i].x, m->mNormals[i].y, m->mNormals[i].z));
            v.pos[0] = p.x; v.pos[1] = p.y; v.pos[2] = p.z;
            v.nrm[0] = n.x; v.nrm[1] = n.y; v.nrm[2] = n.z;
            if (m->HasTextureCoords(0)) {
                v.uv[0] = m->mTextureCoords[0][i].x;
                v.uv[1] = m->mTextureCoords[0][i].y;
            }
            verts.push_back(v);
        }
        if (opt.boneMap) {
            weights.resize(verts.size());
            for (unsigned b = 0; b < m->mNumBones; ++b) {
                const aiBone* bone = m->mBones[b];
                int ours = opt.boneMap(bone->mName.C_Str());
                if (opt.joints) // junta = origem do osso na pose do arquivo
                    (*opt.joints)[bone->mName.C_Str()] = glm::vec3(xf * glm::inverse(toGlm(bone->mOffsetMatrix)) * glm::vec4(0, 0, 0, 1));
                for (unsigned w = 0; w < bone->mNumWeights; ++w)
                    weights[base + bone->mWeights[w].mVertexId][ours] += bone->mWeights[w].mWeight;
            }
        }
        for (unsigned f = 0; f < m->mNumFaces; ++f)
            if (m->mFaces[f].mNumIndices == 3) {
                for (int e = 0; e < 3; ++e) idx.push_back(base + m->mFaces[f].mIndices[e]);
                triMat.push_back((int)m->mMaterialIndex);
            }
    }
    for (unsigned c = 0; c < node->mNumChildren; ++c)
        collectMeshes(sc, node->mChildren[c], nodeXf, fixSkinnedAxes, verts, idx, triMat, opt, weights);
}

unsigned char linearToSrgb8(float v) {
    v = std::clamp(v, 0.0f, 1.0f);
    float s = v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
    return (unsigned char)std::lround(s * 255.0f);
}

CharacterModel::Material loadMaterial(const aiScene* sc, const aiMaterial* mat, int maxTex) {
    CharacterModel::Material m;
    {
        Image im;
        if (decodeEmbedded(sc, mat, {aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE}, maxTex, im)) {
            m.alphaCut = cutoutAlpha(im, mat->GetName().C_Str());
            m.texBase = upload(im, true);
        }
    }
    m.texMR = loadEmbedded(sc, mat, {aiTextureType_METALNESS, aiTextureType_DIFFUSE_ROUGHNESS, aiTextureType_UNKNOWN}, false, maxTex);
    m.texNormal = loadEmbedded(sc, mat, {aiTextureType_NORMALS}, false, maxTex);
    m.texEmissive = loadEmbedded(sc, mat, {aiTextureType_EMISSIVE}, true, maxTex);
    // oclusão do glTF fica no canal R (aqui na mesma imagem do metal/rugosidade)
    m.useOcclusion = mat->GetTextureCount(aiTextureType_LIGHTMAP) > 0;
    mat->Get(AI_MATKEY_METALLIC_FACTOR, m.metallicFactor);
    mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, m.roughnessFactor);
    float gloss = 1.0f;
    if (!m.texMR && mat->Get(AI_MATKEY_GLOSSINESS_FACTOR, gloss) == AI_SUCCESS) {
        m.texMR = specGlossToMR(sc, mat, maxTex, gloss, m.useOcclusion);
        m.metallicFactor = m.roughnessFactor = 1.0f;
    }
    if (!m.texBase) { // material só com cor (ex.: AK-12): textura de 1 pixel com a cor base
        aiColor4D c(0.8f, 0.8f, 0.8f, 1.0f);
        if (mat->Get(AI_MATKEY_BASE_COLOR, c) != AI_SUCCESS) mat->Get(AI_MATKEY_COLOR_DIFFUSE, c);
        m.texBase = makeSolidTexture(linearToSrgb8(c.r), linearToSrgb8(c.g), linearToSrgb8(c.b), 255, true);
    }
    // sem textura de metal/rugosidade valem só os fatores do material
    if (!m.texMR) m.texMR = makeSolidTexture(255, 255, 255, 255, false);
    if (!m.texNormal) m.texNormal = makeSolidTexture(128, 128, 255, 255, false);
    aiColor3D em(0, 0, 0);
    if (mat->Get(AI_MATKEY_COLOR_EMISSIVE, em) == AI_SUCCESS) m.emissiveFactor = glm::vec3(em.r, em.g, em.b);
    float emStrength = 1.0f; // KHR_materials_emissive_strength (lâmpadas)
    if (mat->Get(AI_MATKEY_EMISSIVE_INTENSITY, emStrength) == AI_SUCCESS) m.emissiveFactor *= emStrength;
    // brilho só por cor (lâmpada sem textura): textura branca para o fator valer
    if (!m.texEmissive) {
        bool glows = m.emissiveFactor.x + m.emissiveFactor.y + m.emissiveFactor.z > 0.0f;
        m.texEmissive = glows ? makeSolidTexture(255, 255, 255, 255, true) : makeSolidTexture(0, 0, 0, 255, true);
    }
    std::printf("[modelo] material %s: metal %.2f rugosidade %.2f\n", mat->GetName().C_Str(), m.metallicFactor,
                m.roughnessFactor);
    return m;
}

// Ordena os triângulos por material ('matIds' na ordem das partes) e, com 'chunk' > 0, por bloco
// de chunk x chunk metros no plano xz. Monta as partes, os blocos (com caixa) e o índice ordenado.
std::vector<unsigned> organize(const std::vector<Vertex>& v, const std::vector<unsigned>& idx, const std::vector<int>& triMat,
                               const std::vector<int>& matIds, float chunk, std::vector<CharacterModel::Part>& parts,
                               std::vector<CharacterModel::Chunk>& chunks) {
    const size_t nt = triMat.size();
    std::vector<int> slot(nt, -1);
    std::map<int, int> slotOf;
    for (size_t i = 0; i < matIds.size(); ++i) slotOf[matIds[i]] = (int)i;
    std::vector<long long> cell(nt, 0);
    for (size_t t = 0; t < nt; ++t) {
        slot[t] = slotOf.at(triMat[t]);
        if (chunk > 0.0f) {
            float cx = 0.0f, cz = 0.0f;
            for (int e = 0; e < 3; ++e) { cx += v[idx[t * 3 + e]].pos[0]; cz += v[idx[t * 3 + e]].pos[2]; }
            long long ix = (long long)std::floor(cx / 3.0f / chunk), iz = (long long)std::floor(cz / 3.0f / chunk);
            cell[t] = (ix + (1 << 20)) * (1LL << 22) + (iz + (1 << 20));
        }
    }
    std::vector<int> order(nt);
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        return slot[a] != slot[b] ? slot[a] < slot[b] : cell[a] < cell[b];
    });
    parts.assign(matIds.size(), CharacterModel::Part{0, 0, 0});
    for (size_t i = 0; i < parts.size(); ++i) parts[i].material = (int)i;
    chunks.clear();
    std::vector<unsigned> out;
    out.reserve(idx.size());
    size_t k = 0;
    for (size_t pi = 0; pi < parts.size(); ++pi) {
        CharacterModel::Part& p = parts[pi];
        p.first = (GLsizei)out.size();
        p.chunk0 = (int)chunks.size();
        while (k < nt && slot[order[k]] == (int)pi) {
            long long c = cell[order[k]];
            CharacterModel::Chunk ch{(GLsizei)out.size(), 0, glm::vec3(1e30f), glm::vec3(-1e30f)};
            while (k < nt && slot[order[k]] == (int)pi && cell[order[k]] == c) {
                for (int e = 0; e < 3; ++e) {
                    unsigned vi = idx[order[k] * 3 + e];
                    out.push_back(vi);
                    glm::vec3 q(v[vi].pos[0], v[vi].pos[1], v[vi].pos[2]);
                    ch.lo = glm::min(ch.lo, q);
                    ch.hi = glm::max(ch.hi, q);
                }
                ch.count += 3;
                ++k;
            }
            if (chunk > 0.0f) chunks.push_back(ch);
        }
        p.count = (GLsizei)out.size() - p.first;
        p.chunk1 = (int)chunks.size();
    }
    return out;
}

// Caixa (espaço do modelo) toda fora do frustum de 'mvp'? (os 8 cantos do mesmo lado de um plano)
bool outsideFrustum(const glm::mat4& mvp, const glm::vec3& lo, const glm::vec3& hi) {
    int out[6] = {0, 0, 0, 0, 0, 0};
    for (int i = 0; i < 8; ++i) {
        glm::vec4 c = mvp * glm::vec4((i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z, 1.0f);
        out[0] += c.x < -c.w;
        out[1] += c.x > c.w;
        out[2] += c.y < -c.w;
        out[3] += c.y > c.w;
        out[4] += c.z < -c.w;
        out[5] += c.z > c.w;
    }
    for (int o : out)
        if (o == 8) return true;
    return false;
}

} // namespace

bool CharacterModel::load(const std::string& path, const Skeleton* sk, const LoadOptions& opt) {
    // FlipUVs desfaz a inversão que o importador glTF do Assimp aplica -> UVs no padrão glTF.
    const unsigned flags = aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_GenSmoothNormals | aiProcess_FlipUVs;
    Assimp::Importer local;
    Assimp::Importer* imp = &local;
    OpenFile& of = openFile();
    if (opt.part >= 0) { // partes do mesmo arquivo: lê uma vez só e guarda as texturas já abertas
        if (of.path != path || of.maxTex != opt.maxTextureSize || !of.imp) {
            of = OpenFile();
            of.imp = std::make_unique<Assimp::Importer>();
            if (!of.imp->ReadFile(path, flags)) {
                std::fprintf(stderr, "[modelo] falha ao carregar %s: %s\n", path.c_str(), of.imp->GetErrorString());
                of = OpenFile();
                return false;
            }
            of.path = path;
            of.maxTex = opt.maxTextureSize;
        }
        imp = of.imp.get();
    } else
        local.ReadFile(path, flags);
    const aiScene* sc = imp->GetScene();
    if (!sc || !sc->mNumMeshes) {
        std::fprintf(stderr, "[modelo] falha ao carregar %s: %s\n", path.c_str(), imp->GetErrorString());
        return false;
    }

    std::vector<Vertex> verts;
    std::vector<unsigned> idx;
    std::vector<int> triMat; // material de cada triângulo
    BoneWeights weights;
    if (opt.part >= 0) { // só uma parte do arquivo, com a transformação dos nós de cima
        glm::mat4 xf(1.0f);
        const aiNode* split = splitNode(sc->mRootNode, xf);
        if (opt.part >= (int)split->mNumChildren) {
            std::fprintf(stderr, "[modelo] %s não tem a parte %d\n", path.c_str(), opt.part);
            return false;
        }
        collectMeshes(sc, split->mChildren[opt.part], xf, opt.fixSkinnedAxes, verts, idx, triMat, opt, weights);
        if (verts.empty()) return false;
    } else
        collectMeshes(sc, sc->mRootNode, glm::mat4(1.0f), opt.fixSkinnedAxes, verts, idx, triMat, opt, weights);

    float minY = 1e9f, maxY = -1e9f;
    for (const Vertex& v : verts) { minY = std::min(minY, v.pos[1]); maxY = std::max(maxY, v.pos[1]); }
    const float scale = opt.targetHeight > 0.0f && maxY > minY ? opt.targetHeight / (maxY - minY) : 1.0f;
    const float shift = opt.groundAtZero ? minY : 0.0f; // apoia os pés em y = 0
    auto place = [&](glm::vec3 p) { p.y -= shift; return p * scale; };
    for (Vertex& v : verts) {
        glm::vec3 p = place(glm::vec3(v.pos[0], v.pos[1], v.pos[2]));
        v.pos[0] = p.x; v.pos[1] = p.y; v.pos[2] = p.z;
    }
    if (opt.joints)
        for (auto& [name, p] : *opt.joints) p = place(p);
    std::printf("[modelo] %zu vertices, %zu triangulos\n", verts.size(), idx.size() / 3);

    if (opt.keepSource) {
        srcVerts_.assign((const unsigned char*)verts.data(), (const unsigned char*)(verts.data() + verts.size()));
        srcIdx_ = idx;
        srcMat_ = triMat;
    }
    if (!opt.cuts.empty()) cutHoles(verts, idx, triMat, opt.cuts);
    for (const auto& [lo, hi] : opt.clear) {
        std::vector<unsigned> keepIdx;
        std::vector<int> keepMat;
        int removed = 0;
        for (size_t t = 0; t + 2 < idx.size(); t += 3) {
            bool in = true;
            float top = -1e9f;
            for (int k = 0; k < 3; ++k) {
                glm::vec3 p(verts[idx[t + k]].pos[0], verts[idx[t + k]].pos[1], verts[idx[t + k]].pos[2]);
                in = in && glm::all(glm::greaterThanEqual(p, lo)) && glm::all(glm::lessThanEqual(p, hi));
                top = std::max(top, p.y);
            }
            if (in && top > lo.y + 0.03f) { ++removed; continue; } // o chão (bem rente a lo.y) fica
            for (int k = 0; k < 3; ++k) keepIdx.push_back(idx[t + k]);
            keepMat.push_back(triMat[t / 3]);
        }
        idx.swap(keepIdx);
        triMat.swap(keepMat);
        std::printf("[modelo] regiao esvaziada: %d triangulos tirados\n", removed);
    }
    if (opt.triangles) {
        opt.triangles->reserve(idx.size());
        for (unsigned i : idx) opt.triangles->push_back(glm::vec3(verts[i].pos[0], verts[i].pos[1], verts[i].pos[2]));
    }
    boundsMin = glm::vec3(1e9f);
    boundsMax = glm::vec3(-1e9f);
    for (const Vertex& v : verts) {
        boundsMin = glm::min(boundsMin, glm::vec3(v.pos[0], v.pos[1], v.pos[2]));
        boundsMax = glm::max(boundsMax, glm::vec3(v.pos[0], v.pos[1], v.pos[2]));
    }
    computeTangents(verts, idx);
    if (sk) {
        rigidRig(*sk, verts, idx);
    } else if (opt.boneMap) { // os 4 maiores pesos de cada vértice
        weights.resize(verts.size());
        for (size_t i = 0; i < verts.size(); ++i) {
            std::vector<std::pair<float, int>> w;
            for (auto& [b, x] : weights[i]) w.push_back({x, b});
            std::sort(w.rbegin(), w.rend());
            float sum = 0.0f;
            for (size_t k = 0; k < 4 && k < w.size(); ++k) sum += w[k].first;
            for (int k = 0; k < 4; ++k) {
                bool has = k < (int)w.size() && sum > 0.0f;
                verts[i].bones[k] = has ? (uint8_t)w[k].second : 0;
                verts[i].weights[k] = has ? w[k].first / sum : (k == 0 && sum <= 0.0f ? 1.0f : 0.0f);
            }
        }
    } else { // objeto rígido: tudo no "osso" 0
        for (Vertex& v : verts) {
            v.bones[0] = v.bones[1] = v.bones[2] = v.bones[3] = 0;
            v.weights[0] = 1.0f;
            v.weights[1] = v.weights[2] = v.weights[3] = 0.0f;
        }
    }

    // agrupa os triângulos por material (uma parte por material) e, se pedido, em blocos do mapa
    {
        std::vector<int> matIds(triMat.begin(), triMat.end());
        std::sort(matIds.begin(), matIds.end());
        matIds.erase(std::unique(matIds.begin(), matIds.end()), matIds.end());
        chunkSize_ = opt.chunkSize;
        idx = organize(verts, idx, triMat, matIds, chunkSize_, parts, chunks);
        if (!chunks.empty()) std::printf("[modelo] %zu blocos de %.0f m\n", chunks.size(), chunkSize_);
        matIds_ = matIds;
        std::map<const aiTexture*, Image> decodedLocal;
        std::map<const aiTexture*, Image>& decoded = opt.part >= 0 ? of.decoded : decodedLocal;
        predecode(sc, matIds, opt.maxTextureSize, decoded);
        g_decoded = &decoded;
        for (int id : matIds) {
            materials.push_back(loadMaterial(sc, sc->mMaterials[id], opt.maxTextureSize));
            hasAlphaCut |= materials.back().alphaCut;
            materialNames.push_back(sc->mMaterials[id]->GetName().C_Str());
        }
        g_decoded = nullptr;
    }

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vertex), verts.data(), GL_STATIC_DRAW);
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
    indexCount_ = (GLsizei)idx.size();

    return true;
}

bool CharacterModel::recut(const std::vector<std::pair<glm::vec3, glm::vec3>>& cuts, std::vector<glm::vec3>* triangles) {
    if (srcVerts_.empty() || !vao_) return false;
    std::vector<Vertex> verts(srcVerts_.size() / sizeof(Vertex));
    std::memcpy(verts.data(), srcVerts_.data(), srcVerts_.size());
    std::vector<unsigned> idx = srcIdx_;
    std::vector<int> triMat = srcMat_;
    if (!cuts.empty()) cutHoles(verts, idx, triMat, cuts);
    if (triangles) {
        triangles->clear();
        triangles->reserve(idx.size());
        for (unsigned i : idx) triangles->push_back(glm::vec3(verts[i].pos[0], verts[i].pos[1], verts[i].pos[2]));
    }
    computeTangents(verts, idx);
    for (Vertex& v : verts) {
        v.bones[0] = v.bones[1] = v.bones[2] = v.bones[3] = 0;
        v.weights[0] = 1.0f;
        v.weights[1] = v.weights[2] = v.weights[3] = 0.0f;
    }
    // mesma divisão por material do load (os materiais já carregados continuam valendo)
    std::vector<unsigned> sorted = organize(verts, idx, triMat, matIds_, chunkSize_, parts, chunks);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vertex), verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sorted.size() * sizeof(unsigned), sorted.data(), GL_STATIC_DRAW);
    glBindVertexArray(0);
    indexCount_ = (GLsizei)sorted.size();
    return true;
}

void CharacterModel::draw(const glm::mat4* mvp) const {
    if (mvp && !chunks.empty()) {
        for (size_t i = 0; i < parts.size(); ++i) drawPart(i, mvp);
        return;
    }
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
}

void CharacterModel::drawPart(size_t i, const glm::mat4* mvp) const {
    glBindVertexArray(vao_);
    const Part& p = parts[i];
    if (!mvp || chunks.empty()) {
        glDrawElements(GL_TRIANGLES, p.count, GL_UNSIGNED_INT, (void*)(sizeof(unsigned) * p.first));
        return;
    }
    // blocos visíveis vizinhos no índice viram um desenho só
    GLsizei start = 0, count = 0;
    for (int c = p.chunk0; c < p.chunk1; ++c) {
        const Chunk& ch = chunks[c];
        if (outsideFrustum(*mvp, ch.lo, ch.hi)) continue;
        if (count && start + count == ch.first) count += ch.count;
        else {
            if (count) glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, (void*)(sizeof(unsigned) * start));
            start = ch.first;
            count = ch.count;
        }
    }
    if (count) glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, (void*)(sizeof(unsigned) * start));
}

void CharacterModel::reset() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (ebo_) glDeleteBuffers(1, &ebo_);
    for (const Material& m : materials) {
        GLuint t[4] = {m.texBase, m.texMR, m.texNormal, m.texEmissive};
        glDeleteTextures(4, t);
    }
    vao_ = vbo_ = ebo_ = 0;
    indexCount_ = 0;
    materials.clear();
    materialNames.clear();
    parts.clear();
    chunks.clear();
    srcVerts_.clear();
    srcIdx_.clear();
    srcMat_.clear();
    matIds_.clear();
}

bool CharacterModel::setBaseTexture(int material, const std::string& imagePath) {
    if (material < 0 || material >= (int)materials.size()) return false;
    int w, h, c;
    unsigned char* px = stbi_load(imagePath.c_str(), &w, &h, &c, 4);
    if (!px) {
        std::fprintf(stderr, "[modelo] nao abriu a imagem %s\n", imagePath.c_str());
        return false;
    }
    Image im;
    im.px.assign(px, px + (size_t)w * h * 4);
    im.w = w;
    im.h = h;
    im.name = imagePath;
    stbi_image_free(px);
    downscale(im, 2048);
    glDeleteTextures(1, &materials[material].texBase);
    materials[material].texBase = upload(im, true);
    return true;
}

std::vector<std::string> CharacterModel::listParts(const std::string& path) {
    Assimp::Importer imp;
    const aiScene* sc = imp.ReadFile(path, 0);
    std::vector<std::string> out;
    if (!sc || !sc->mRootNode) return out;
    glm::mat4 xf;
    const aiNode* split = splitNode(sc->mRootNode, xf);
    if (split->mNumMeshes) return out; // o próprio nó tem malha: um objeto só
    for (unsigned c = 0; c < split->mNumChildren; ++c) {
        const aiNode* ch = split->mChildren[c];
        std::string name = ch->mName.C_Str();
        if (name.empty()) name = "parte" + std::to_string(c);
        out.push_back(hasMeshes(ch) ? name : ""); // vazio = sem malha (câmera, luz)
    }
    int real = 0;
    for (const std::string& n : out) real += !n.empty();
    if (real <= 1) out.clear();
    return out;
}
