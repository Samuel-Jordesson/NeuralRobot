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
    void draw() const;
    void release();
    bool save(const std::string& path) const;
    bool load(const std::string& path);

private:
    GLuint vao_ = 0, vbo_ = 0, ebo_ = 0;
    GLsizei count_ = 0;
};

// Pintura do chão: até 4 camadas de textura (cor + normal map, repetidas a cada 'tile' m) misturadas
// por um mapa de pesos em (x, z). Vale para toda superfície virada para cima dentro da área.
struct GroundPaint {
    static constexpr int kMaxLayers = 4;
    static constexpr int kLayerRes = 1024; // texturas das camadas são reamostradas para isso
    struct Layer {
        std::string albedo, normal; // caminhos relativos à raiz ("" = sem normal map)
        float tile = 2.0f;
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
    // Carrega as imagens das camadas na GPU (depois de mudar layers).
    void uploadLayers(const std::string& root);
    void uploadWeights();
    void release();
    bool saveWeights(const std::string& path) const;
    bool loadWeights(const std::string& path);

    GLuint weightTex = 0, albedoArr = 0, normalArr = 0;
};
