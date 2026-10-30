#pragma once
#include "gl_util.h"
#include "skeleton.h"

#include <glm/gtc/type_ptr.hpp>
#include <functional>
#include <map>
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
        float targetHeight = 0.0f;   // > 0: escala o modelo para ter essa altura (m)
        // Modelo com rig próprio: usa os pesos do arquivo, levando cada osso dele para um dos
        // nossos (retorna o Bone). Em 'joints' ficam as posições das juntas do arquivo, já na
        // escala e posição finais do modelo.
        std::function<int(const std::string&)> boneMap;
        std::map<std::string, glm::vec3>* joints = nullptr;
        std::string skipMesh; // malhas cujo nome contém isso ficam de fora (ex.: base de exibição)
        std::vector<glm::vec3>* triangles = nullptr; // saída: 3 vértices por triângulo (gerar colisão do cenário)
        // Vãos abertos nas paredes (ex.: porta): caixas na posição final do modelo; o lado mais fino
        // da caixa é a espessura da parede. Os triângulos dessa parede perdem o pedaço de dentro.
        std::vector<std::pair<glm::vec3, glm::vec3>> cuts;
        // Regiões esvaziadas: triângulos inteiros dentro delas (fora o chão) são tirados (ex.: entulho
        // na frente de uma porta).
        std::vector<std::pair<glm::vec3, glm::vec3>> clear;
        // Guarda a malha antes dos recortes, para recortar de novo sem ler o arquivo (recut).
        bool keepSource = false;
        // > 0: divide a malha em blocos de chunkSize metros (xz) para desenhar só o que aparece
        // (mapas grandes). Cada bloco tem sua caixa.
        float chunkSize = 0.0f;
    };
    // sk == nullptr: objeto rígido (ex.: a arma), desenhado com uBones[0] = identidade,
    // ou modelo com rig próprio (opt.boneMap).
    bool load(const std::string& path, const Skeleton* sk, const LoadOptions& opt);
    // mvp != nullptr: só os blocos dentro desse frustum (modelo dividido com chunkSize).
    void draw(const glm::mat4* mvp = nullptr) const;
    void drawPart(size_t i, const glm::mat4* mvp = nullptr) const;
    // Solta a malha e as texturas (para carregar de novo, ex.: recortar a porta em outro lugar).
    void reset();
    // Troca a textura de cor de um material por uma imagem (PNG/JPG). false se não abriu.
    bool setBaseTexture(int material, const std::string& imagePath);
    std::vector<std::string> materialNames;

    glm::vec3 boundsMin{0.0f}, boundsMax{0.0f}; // caixa da malha (espaço do modelo)

    struct Material {
        GLuint texBase = 0, texMR = 0, texNormal = 0, texEmissive = 0;
        float metallicFactor = 1.0f, roughnessFactor = 1.0f;
        glm::vec3 emissiveFactor{0.0f};
        bool useOcclusion = false;
        bool alphaCut = false; // recorte por alpha (folhas): pixels com alpha < 0,5 somem
    };
    // Um trecho do índice por material (com chunkSize: subdividido em blocos chunk0..chunk1).
    struct Part {
        GLsizei first, count;
        int material;
        int chunk0 = 0, chunk1 = 0;
    };
    struct Chunk {
        GLsizei first, count;
        glm::vec3 lo, hi; // caixa no espaço do modelo
    };
    std::vector<Material> materials;
    std::vector<Part> parts;
    std::vector<Chunk> chunks;
    bool hasAlphaCut = false; // algum material com recorte (os passos de sombra/profundidade usam a textura)

    // Refaz os vãos (opt.cuts) a partir da malha guardada no load (keepSource), sem ler o arquivo.
    // Só objetos rígidos. false se não há malha guardada.
    bool recut(const std::vector<std::pair<glm::vec3, glm::vec3>>& cuts, std::vector<glm::vec3>* triangles);

private:
    GLuint vao_ = 0, vbo_ = 0, ebo_ = 0;
    GLsizei indexCount_ = 0;
    // malha original (antes dos recortes), para recut
    std::vector<unsigned char> srcVerts_;
    std::vector<unsigned> srcIdx_;
    std::vector<int> srcMat_;   // material (do arquivo) de cada triângulo
    std::vector<int> matIds_;   // material do arquivo de cada entrada de 'materials'
    float chunkSize_ = 0.0f;
};
