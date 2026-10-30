#pragma once
#include "character_model.h"
#include "gl_util.h"
#include "graphics_settings.h"
#include "physics.h"
#include "skeleton.h"
#include "weapon.h"

#include <map>
#include <string>
#include <vector>

struct RenderScene {
    const World* world = nullptr;
    const CharacterModel* character = nullptr;
    glm::mat4 characterModel{1.0f};
    const glm::mat4* bones = nullptr; // BONE_COUNT matrizes de skinning
    glm::vec3 characterPos{0.0f};
    glm::mat4 view{1.0f}, proj{1.0f};
    glm::vec3 camPos{0.0f};
    bool debugWeights = false;
    float time = 0.0f; // segundos de jogo (nuvens, granulação)

    // objetos do cenário com malha própria (mesas)
    struct Prop {
        const CharacterModel* mesh;
        glm::mat4 model;
    };
    std::vector<Prop> props;
    const struct Terrain* terrain = nullptr;    // relevo esculpido no editor
    const struct GroundPaint* paint = nullptr;  // pintura do chão

    // armas (na mão, no corpo e no mundo) e efeitos de tiro (do jogador e dos inimigos)
    std::vector<Prop> guns;
    const Weapon* weapon = nullptr;
    const Weapon* enemyWeapon = nullptr;

    // outros personagens animados (soldados)
    struct Actor {
        const CharacterModel* mesh;
        glm::mat4 model;
        const glm::mat4* bones; // BONE_COUNT matrizes de skinning
    };
    std::vector<Actor> actors;

    // peças de cenário que giram (porta): cubo unitário centrado -> mundo
    struct Panel {
        glm::mat4 model;
        glm::vec3 color;
        int material; // 3 = metal liso, 4 = madeira, 6 = vidro, 7 = imagem (image)
        std::string image; // material 7: caminho completo da imagem
    };
    std::vector<Panel> panels; // material 6 = vidro (desenhado transparente depois da cena)

    // luzes postas no editor (0 pontual, 1 spot, 2 ambiente)
    struct Light {
        int type = 0;
        glm::vec3 pos{0.0f}, dir{0.0f, -1.0f, 0.0f}, color{1.0f}; // cor já com o brilho
        float range = 8.0f, cosInner = 0.9f, cosOuter = 0.75f;
    };
    std::vector<Light> lights;

    // pintura nos modelos (editor): carimbos que escurecem/tingem a superfície
    struct Stain {
        glm::vec3 pos, normal;
        float size, angle, strength, soft;
        std::string image; // caminho completo da imagem; vazio = sujeira automática
    };
    std::vector<Stain> stains;
    // ondas na água das poças: (x, z, quando começou em 'time', força)
    std::vector<glm::vec4> ripples;

    // HUD
    float crosshairAlpha = 0.0f;
    float promptAlpha = 0.0f;
    glm::vec3 promptWorld{0.0f}; // onde a tecla "E" aparece
};

class Renderer {
public:
    bool init(const std::string& shaderDir, int width, int height);
    void applySettings(const GraphicsSettings& gs);
    void resize(int width, int height);
    // targetFbo = 0 desenha na janela. dt alimenta a exposição automática.
    void render(const RenderScene& s, float dt, GLuint targetFbo = 0);
    void resetExposure() { exposureReset_ = true; }

    glm::vec3 sunDir() const { return sunDir_; }

private:
    struct FxVertex {
        float pos[3];
        float uv[2];
        float color[4];
        float kind;
    };
    enum class Pass { Shadow, Prepass, Color };

    void updateSun();
    void updateEnvironment(float time);
    void computeCascades(const RenderScene& s);
    void setLightingUniforms(Shader& sh, const RenderScene& s);
    void drawWorld(Shader& sh, const RenderScene& s, Pass pass);
    void drawSkinned(Shader& sh, const CharacterModel& m, const glm::mat4& model, const glm::mat4* bones, bool material);
    void drawScene(Shader& worldSh, Shader& skinSh, const RenderScene& s, Pass pass, const glm::mat4& vp);
    void drawEffects(const RenderScene& s);
    void drawWeaponFx(const RenderScene& s, const Weapon& w);
    void drawHud(const RenderScene& s);
    void drawFx(const std::vector<FxVertex>& v);
    void fullscreen();
    void createTargets();
    void destroyTargets();
    void createShadowMap();

    GraphicsSettings gs_;
    glm::mat4 cullVP_{1.0f}; // visão do passo atual (corta os blocos do mapa fora dela)
    void drawGlass(const RenderScene& s);
    void drawStains(const RenderScene& s);
    void drawGrass(const RenderScene& s);
    Shader grass_, glass_;
    GLuint stainTexture(const std::string& path); // carrega uma vez (0 se não abriu)
    std::map<std::string, GLuint> stainTex_;
    Shader stain_;
    void updateRoof(const World& w);
    GLuint roofTex_ = 0;   // mapa de teto (lado de dentro): R altura, G cobertura suavizada
    int roofVersion_ = -1;
    glm::vec4 roofRect_{0.0f};
    int w_ = 0, h_ = 0;   // janela
    int rw_ = 0, rh_ = 0; // resolução interna
    Shader skinned_, skinnedDepth_, skinnedPre_, world_, worldDepth_, worldPre_;
    Shader sky_, skyAtmo_, prefilter_, irradiance_;
    Shader ssao_, ssaoBlur_, volumetric_, fogComp_, bloomDown_, bloomUp_, lum_, adapt_, post_, fxaa_;
    Shader fx_, decal_, hud_;
    Mesh cube_, sphere_, plane_;
    GLuint emptyVao_ = 0;
    GLuint noiseTex_ = 0;
    GLuint fxVao_ = 0, fxVbo_ = 0;
    glm::mat4 identityBones_[BONE_COUNT];

    // sombras em cascata
    static constexpr int kMaxCascades = 4;
    GLuint shadowFbo_ = 0, shadowTex_ = 0;
    int shadowRes_ = 0, shadowLayers_ = 0;
    glm::mat4 cascadeVP_[kMaxCascades];
    float cascadeSplit_[kMaxCascades] = {}, cascadeTexel_[kMaxCascades] = {};

    // céu e iluminação do ambiente
    static constexpr int kSkyRes = 128, kEnvRes = 64, kEnvMips = 6, kIrrRes = 16;
    GLuint captureFbo_ = 0, skyCube_ = 0, envRaw_ = 0, envPref_ = 0, irrCube_ = 0;
    glm::vec3 skyCubeSun_{0.0f};
    float envTime_ = -1e9f;
    bool envDirty_ = true;
    glm::vec3 skyAmbient_{0.3f};

    // alvos de renderização
    GLuint preFbo_ = 0, preNormal_ = 0, preDepth_ = 0;
    GLuint aoFbo_ = 0, aoTex_ = 0, aoBlurFbo_ = 0, aoBlurTex_ = 0;
    int samples_ = 0;
    GLuint msFbo_ = 0, msColor_ = 0, msDepth_ = 0;
    GLuint hdrFbo_ = 0, hdrTex_ = 0;
    GLuint volFbo_ = 0, volTex_ = 0;
    GLuint fogFbo_ = 0, fogTex_ = 0;
    static constexpr int kBloomMips = 6;
    GLuint bloomFbo_[kBloomMips] = {}, bloomTex_[kBloomMips] = {};
    int bloomW_[kBloomMips] = {}, bloomH_[kBloomMips] = {};
    static constexpr int kLumRes = 128;
    GLuint lumFbo_ = 0, lumTex_ = 0, adaptFbo_[2] = {}, adaptTex_[2] = {};
    int adaptCur_ = 0;
    bool exposureReset_ = true;
    GLuint ldrFbo_ = 0, ldrTex_ = 0;

    glm::vec3 sunDir_ = glm::normalize(glm::vec3(0.45f, 0.78f, 0.32f));
    glm::vec3 sunColor_{3.3f};
    glm::vec3 cloudSunColor_{3.3f};
    float skyIntensity_ = 8.0f;
};
