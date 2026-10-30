#pragma once
#include "arena.h"
#include "character_model.h"
#include "physics.h"
#include "renderer.h"
#include "table.h"

#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Cenários do jogo: 0 = teste (mapa de blocos + arena), 1 = oficial (o do jogo final).
enum SceneId { SceneTest = 0, SceneOfficial = 1, SceneCount };
const char* sceneName(int id);
const char* sceneFile(int id); // levels/<nome>.txt (salvo pelo editor livre)
std::string scenePaintFile(int id);   // levels/<nome>_pintura.png
std::string sceneTerrainFile(int id); // levels/<nome>_relevo.bin

// Colisão gerada da malha: o que ocupa a altura do corpo (entre 'band0' e 'band1' acima do chão)
// vira caixa invisível, em células de 'cell' metros, juntando vizinhas de mesma altura.
// Chão e teto não bloqueiam (portas continuam passáveis).
void addMeshColliders(World& w, const std::vector<glm::vec3>& tris, const glm::mat4& model, float cell = 0.15f,
                      float band0 = 0.3f, float band1 = 1.9f);

// Tira a colisão gerada (caixas invisíveis) de dentro de uma região: as caixas que entram nela são
// cortadas e ficam só com os pedaços de fora (ex.: vão da porta, passagem livre no entulho).
void carveColliders(World& w, const glm::vec3& lo, const glm::vec3& hi);

// ---- layout editável (editor livre): onde fica cada coisa do cenário
struct PropPlace {
    std::string name;
    glm::vec3 pos{0.0f};
    float yaw = 0.0f; // rad
    float scale = 1.0f;
    glm::vec3 stretch{1.0f}; // largura, altura, profundidade (por cima da escala)
    std::map<int, std::string> textures; // material -> imagem (caminho relativo à raiz do projeto)
    std::string asset; // objeto posto pelo editor: modelo (relativo à raiz); vazio = objeto fixo do cenário
    glm::mat4 matrix(const glm::mat4& pivot = glm::mat4(1.0f)) const;
};

struct DoorPlace {
    glm::vec3 hinge{0.0f};
    float width = 0.9f, height = 2.1f;
    float yaw = 0.0f;    // rad, qualquer ângulo: 0 = folha ao longo de +X, -90° = ao longo de +Z
    bool mirror = false; // dobradiça no fim do vão (segunda folha de uma porta dupla)
    bool glass = false;  // porta de vidro (moldura de alumínio)
    glm::vec3 axis() const { return {std::cos(yaw), 0.0f, -std::sin(yaw)}; }
    glm::vec3 end() const { return hinge + axis() * width; }
    // Ao longo de X ou Z: o vão pode ser recortado na parede da malha.
    bool axisAligned() const { return std::fabs(std::sin(2.0f * yaw)) < 0.03f; }
};

// Luz posta pelo editor. Pontual (para todo lado), spot (cone) ou ambiente (clareia por igual
// tudo dentro do raio, sem direção: bom para salas escuras).
struct LightPlace {
    enum Type { Point = 0, Spot = 1, Ambient = 2 };
    int type = Point;
    glm::vec3 pos{0.0f};
    float yaw = 0.0f, pitch = -1.5707963f; // direção do spot (rad); -90° = para baixo
    glm::vec3 color{1.0f, 0.86f, 0.68f};
    float intensity = 6.0f; // brilho
    float range = 8.0f;     // alcance (m)
    float inner = 25.0f, outer = 40.0f; // spot: cone (graus, metade da abertura)
    glm::vec3 dir() const { return {std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw)}; }
};

struct LevelLayout {
    std::vector<PropPlace> props;                             // objetos com malha, na ordem de montagem
    std::map<int, std::pair<glm::vec3, glm::vec3>> boxes;     // caixas visíveis movidas (índice em World::boxes)
    bool hasDoor = false;
    glm::vec3 doorHinge{0.0f};
    float doorWidth = 0.85f, doorHeight = 2.08f;
    std::vector<DoorPlace> extraDoors;                        // outras portas (só no arquivo/painel)
    std::vector<DoorPlace> allDoors() const;                  // a principal (se houver) + as outras
    std::vector<LightPlace> lights;                           // luzes postas pelo editor
    std::vector<glm::vec3> spawns;                            // inimigos
    std::vector<float> spawnYaw;
    std::map<int, std::pair<glm::vec3, float>> guns;          // arma i posta no chão pelo editor (posição, giro)
    // pintura do chão e relevo (editor): os pesos e as alturas ficam em levels/<cenário>_pintura.png
    // e levels/<cenário>_relevo.bin
    struct PaintLayer {
        std::string albedo, normal;
        float tile = 2.0f;
    };
    bool paintOn = false;
    glm::vec2 paintOrigin{0.0f};
    float paintSize = 64.0f;
    int paintRes = 512;
    std::vector<PaintLayer> paintLayers;
    bool terrainOn = false;
    glm::vec2 terrainOrigin{0.0f};
    float terrainSize = 30.0f;
    int terrainN = 121;
    glm::vec3 start{0.0f};                                    // onde o personagem começa
    float startYaw = 0.0f;

    PropPlace* prop(const std::string& name);
    std::string serialize() const;
    bool parse(const std::string& text);
    bool load(const std::string& path);
    bool save(const std::string& path) const;
};

// Modelos postos no cenário pelo editor (PropPlace::asset): cada arquivo é carregado uma vez.
struct AssetLibrary {
    struct Entry {
        CharacterModel model;
        std::vector<glm::vec3> tris;
        bool ok = false;
    };
    std::string root;
    Entry* get(const std::string& rel); // nullptr se não abriu

    // Configuração de cada modelo (assets.ini, na raiz): como ele colide no cenário.
    enum Collision { None = 0, Solid = 1, Physics = 2 }; // atravessa / parado e sólido / cai e é empurrado
    std::map<std::string, int> collision;
    int collisionOf(const std::string& rel) const {
        auto it = collision.find(rel);
        return it == collision.end() ? Solid : it->second;
    }
    void loadConfig();
    bool saveConfig() const;

    // Objetos com física (Collision::Physics) do último build: caem e são empurrados.
    struct Dynamic {
        int prop;             // índice em props
        glm::mat4 base;       // matriz do objeto como foi posto
        glm::vec3 offset{0.0f}, vel{0.0f}; // deslocamento desde que foi posto
        glm::vec3 lo, hi;     // caixa no mundo como foi posto
    };
    std::vector<Dynamic> dynamics;
    // Passo da física simples: gravidade, chão/caixas e empurrão de quem encosta.
    void stepDynamics(World& w, std::vector<RenderScene::Prop>& props, const std::vector<CharacterBody*>& pushers, float dt);
    CharacterModel* mesh(const std::string& rel) {
        Entry* e = get(rel);
        return e ? &e->model : nullptr;
    }
    // Desenho e colisão dos objetos postos (na ordem de L.props, depois dos fixos).
    void build(World& w, std::vector<RenderScene::Prop>& props, const LevelLayout& L);

private:
    std::map<std::string, std::unique_ptr<Entry>> cache_;
};

// Portas extras (L.extraDoors) com batente, para cenários sem recorte na malha (teste).
void addExtraDoors(World& w, Arena& a, const LevelLayout& L);

// Valores de fábrica (usados quando ainda não há arquivo salvo).
void defaultTestLayout(LevelLayout& L);
void defaultOfficialLayout(LevelLayout& L);

// Cenário de teste: mapa de blocos, arena, mesas (props "mesa1"/"mesa2").
Arena buildTestScene(World& w, LevelLayout& L, std::vector<Table>& tables);

// Cenário oficial (Novomapa.glb, montado no Blender): o personagem começa no corredor e as portas
// levam às salas; os inimigos ficam na sala do fundo.
struct OfficialLevel {
    CharacterModel map; // Novomapa.glb
    std::vector<glm::vec3> mapTris;
    bool loaded = false;

    // Carrega o mapa; com uma porta mudada, recarrega com os vãos novos.
    bool load(const std::string& root, const LevelLayout& L);
    // Monta o mundo (colisões), os objetos desenhados (na ordem de L.props), a porta e os inimigos.
    Arena build(World& w, std::vector<RenderScene::Prop>& props, const LevelLayout& L) const;
    CharacterModel* mesh(const std::string& propName);
    void applyTextures(const std::string& root, const LevelLayout& L);

private:
    std::vector<std::pair<glm::vec3, glm::vec3>> cuts_; // vãos recortados na última carga
};
