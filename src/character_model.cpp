#include "character_model.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/GltfMaterial.h>

#include <glm/gtc/matrix_transform.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <string>
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

bool decodeEmbedded(const aiScene* sc, const aiMaterial* m, std::initializer_list<aiTextureType> types, int maxSize,
                    Image& out) {
    for (aiTextureType type : types) {
        aiString path;
        if (m->GetTexture(type, 0, &path) != AI_SUCCESS) continue;
        const aiTexture* tex = sc->GetEmbeddedTexture(path.C_Str());
        if (!tex) continue;
        int w, h, c;
        if (tex->mHeight == 0) {
            unsigned char* px = stbi_load_from_memory((const stbi_uc*)tex->pcData, (int)tex->mWidth, &w, &h, &c, 4);
            if (!px) continue;
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
        out.name = path.C_Str();
        downscale(out, maxSize);
        return true;
    }
    return false;
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

// Junta todas as malhas aplicando as transformações da hierarquia de nós.
void collectMeshes(const aiScene* sc, const aiNode* node, const glm::mat4& parentXf, bool fixSkinnedAxes,
                   std::vector<Vertex>& verts, std::vector<unsigned>& idx, std::vector<int>& triMat) {
    const glm::mat4 nodeXf = parentXf * toGlm(node->mTransformation);
    for (unsigned k = 0; k < node->mNumMeshes; ++k) {
        const aiMesh* m = sc->mMeshes[node->mMeshes[k]];
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
        for (unsigned f = 0; f < m->mNumFaces; ++f)
            if (m->mFaces[f].mNumIndices == 3) {
                for (int e = 0; e < 3; ++e) idx.push_back(base + m->mFaces[f].mIndices[e]);
                triMat.push_back((int)m->mMaterialIndex);
            }
    }
    for (unsigned c = 0; c < node->mNumChildren; ++c)
        collectMeshes(sc, node->mChildren[c], nodeXf, fixSkinnedAxes, verts, idx, triMat);
}

unsigned char linearToSrgb8(float v) {
    v = std::clamp(v, 0.0f, 1.0f);
    float s = v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
    return (unsigned char)std::lround(s * 255.0f);
}

CharacterModel::Material loadMaterial(const aiScene* sc, const aiMaterial* mat, int maxTex) {
    CharacterModel::Material m;
    m.texBase = loadEmbedded(sc, mat, {aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE}, true, maxTex);
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
    if (!m.texEmissive) m.texEmissive = makeSolidTexture(0, 0, 0, 255, true);
    aiColor3D em(0, 0, 0);
    if (mat->Get(AI_MATKEY_COLOR_EMISSIVE, em) == AI_SUCCESS) m.emissiveFactor = glm::vec3(em.r, em.g, em.b);
    std::printf("[modelo] material %s: metal %.2f rugosidade %.2f\n", mat->GetName().C_Str(), m.metallicFactor,
                m.roughnessFactor);
    return m;
}

} // namespace

bool CharacterModel::load(const std::string& path, const Skeleton* sk, const LoadOptions& opt) {
    Assimp::Importer imp;
    // FlipUVs desfaz a inversão que o importador glTF do Assimp aplica -> UVs no padrão glTF.
    const aiScene* sc = imp.ReadFile(path, aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
                                               aiProcess_GenSmoothNormals | aiProcess_FlipUVs);
    if (!sc || !sc->mNumMeshes) {
        std::fprintf(stderr, "[modelo] falha ao carregar %s: %s\n", path.c_str(), imp.GetErrorString());
        return false;
    }

    std::vector<Vertex> verts;
    std::vector<unsigned> idx;
    std::vector<int> triMat; // material de cada triângulo
    collectMeshes(sc, sc->mRootNode, glm::mat4(1.0f), opt.fixSkinnedAxes, verts, idx, triMat);

    if (opt.groundAtZero) { // apoia os pés em y = 0
        float minY = 1e9f;
        for (const Vertex& v : verts) minY = std::min(minY, v.pos[1]);
        for (Vertex& v : verts) v.pos[1] -= minY;
    }
    std::printf("[modelo] %zu vertices, %zu triangulos\n", verts.size(), idx.size() / 3);

    computeTangents(verts, idx);
    if (sk) {
        rigidRig(*sk, verts, idx);
    } else { // objeto rígido: tudo no "osso" 0
        for (Vertex& v : verts) {
            v.bones[0] = v.bones[1] = v.bones[2] = v.bones[3] = 0;
            v.weights[0] = 1.0f;
            v.weights[1] = v.weights[2] = v.weights[3] = 0.0f;
        }
    }

    // agrupa os triângulos por material: uma parte desenhada por material
    {
        std::vector<int> order(triMat.size());
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return triMat[a] < triMat[b]; });
        std::vector<unsigned> sorted;
        sorted.reserve(idx.size());
        std::vector<int> matIds;
        for (int t : order) {
            if (parts.empty() || matIds.back() != triMat[t]) {
                parts.push_back({(GLsizei)sorted.size(), 0, (int)matIds.size()});
                matIds.push_back(triMat[t]);
            }
            for (int e = 0; e < 3; ++e) sorted.push_back(idx[t * 3 + e]);
            parts.back().count += 3;
        }
        idx.swap(sorted);
        for (int id : matIds) materials.push_back(loadMaterial(sc, sc->mMaterials[id], opt.maxTextureSize));
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

void CharacterModel::draw() const {
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
}

void CharacterModel::drawPart(size_t i) const {
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, parts[i].count, GL_UNSIGNED_INT, (void*)(sizeof(unsigned) * parts[i].first));
}
