#pragma once
#include "gl_util.h"
#include "skeleton.h"

#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <vector>

// Malha com skinning na GPU (personagem) ou rígida (arma). Como o GLB do personagem
// não vem com rig, cada peça do boneco é presa ao osso mais próximo no carregamento.
class CharacterModel {
public:
    struct LoadOptions {
        bool groundAtZero = true;    // desloca para o ponto mais baixo ficar em y = 0
        bool fixSkinnedAxes = false; // corrige malhas com skin giradas (export do Sketchfab)
        int maxTextureSize = 0;      // > 0: reduz texturas maiores que isso (economiza memória)
    };
    // sk == nullptr: objeto rígido (ex.: a arma), desenhado com uBones[0] = identidade.
    bool load(const std::string& path, const Skeleton* sk, const LoadOptions& opt);
    void draw() const;
    void drawPart(size_t i) const;

    struct Material {
        GLuint texBase = 0, texMR = 0, texNormal = 0, texEmissive = 0;
        float metallicFactor = 1.0f, roughnessFactor = 1.0f;
        glm::vec3 emissiveFactor{0.0f};
        bool useOcclusion = false;
    };
    // Um trecho do índice por material.
    struct Part {
        GLsizei first, count;
        int material;
    };
    std::vector<Material> materials;
    std::vector<Part> parts;

private:
    GLuint vao_ = 0, vbo_ = 0, ebo_ = 0;
    GLsizei indexCount_ = 0;
};
