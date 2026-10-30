#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>

#include <string>
#include <vector>

// Relevo: grade de alturas (vértices a cada size/(n-1) m) desenhada como malha e usada como chão
// pela física. Esculpida com o pincel do editor.
struct Terrain {
    bool active = false;
    glm::vec2 origin{0.0f}; // canto (x, z) mínimo
    float size = 30.0f;     // lado (m)
    int n = 121;            // vértices por lado
    std::vector<float> h;   // n*n alturas (y no mundo)

    void create(const glm::vec2& center, float size, int n, float baseY);
    // Altura sob (x, z); false se fora do terreno.
    bool heightAt(float x, float z, float& y) const;
    // Raio contra o relevo (passos + refinamento). false se não acerta antes de maxT.
    bool raycast(const glm::vec3& o, const glm::vec3& d, float maxT, float& t) const;
    // Pincel: 0 subir, 1 descer, 2 suavizar, 3 nivelar (até 'level').
    void brush(const glm::vec2& c, float radius, float strength, float softness, int mode, float level, float dt);

    void upload(); // refaz a malha na GPU (depois de mexer em h)
    GLuint heightTex = 0; // alturas numa textura (R32F n x n): a grama 3D nasce na altura certa
    void draw() const;
    void release();
    bool save(const std::string& path) const;
    bool load(const std::string& path);

private:
    GLuint vao_ = 0, vbo_ = 0, ebo_ = 0;
    GLsizei count_ = 0;
};

// Pintura do chão: até 4 camadas de material (cor, normal, rugosidade, relevo e oclusão, repetidas a
// cada 'tile' m) misturadas por um mapa de pesos em (x, z) e pela altura de cada material (a grama
// entra pelas frestas da terra em vez de um degradê). Vale para toda superfície virada para cima.
struct GroundPaint {
    static constexpr int kMaxLayers = 4;
    static constexpr int kLayerRes = 2048; // texturas das camadas são reamostradas para isso
    struct Layer {
        // caminhos relativos à raiz ("" = não tem esse mapa)
        std::string albedo, normal, rough, height, ao;
        bool normalDX = false; // normal no padrão DirectX (verde invertido)
        float tile = 2.0f;
        int kind = 0;          // 0 material, 1 poça d'água (sem imagens: molha e alaga o que está embaixo)
        bool grass = false;    // folhas de grama 3D de verdade onde essa camada está pintada
        float depth = 0.0f;    // quanto o terreno afunda onde ela é pintada (m)
    };
    bool active = false;
    glm::vec2 origin{0.0f};
    float size = 64.0f;
    int res = 512;
    std::vector<Layer> layers;
    std::vector<unsigned char> weights; // res*res*4 (RGBA = camadas 1..4)

    void create(const glm::vec2& origin, float size, int res);
    // Pincel: aumenta a camada 'layer' (ou apaga tudo com erase) em volta de c.
    void brush(const glm::vec2& c, float radius, float strength, float softness, int layer, bool erase, float dt);
    float weightAt(const glm::vec2& p, int layer) const; // 0..1 (mais perto do texel)
    // Carrega as imagens das camadas na GPU (depois de mudar layers).
    void uploadLayers(const std::string& root);
    void uploadWeights();
    void release();
    bool saveWeights(const std::string& path) const;
    bool loadWeights(const std::string& path);

    GLuint weightTex = 0, albedoArr = 0, normalArr = 0;
    GLuint maskArr = 0; // R rugosidade, G oclusão, B altura (relevo)
};
