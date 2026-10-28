#pragma once
#include "arena.h"
#include "character_model.h"
#include "level.h"
#include "physics.h"
#include "renderer.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;

// Editor do jogo (estilo Unreal): barra de ferramentas em cima (Salvar, Importar, Compilar, Jogar),
// detalhes do que está selecionado à esquerda, objetos do cenário à direita e o navegador de
// conteúdo embaixo (arrastar um modelo para a vista põe ele no cenário).
// Na vista: botão direito segurado + mouse/WASD/QE voa; clique seleciona; G move, R gira, S escala
// (X/Y/Z travam o eixo; clique confirma, botão direito/Esc cancela); Delete remove.
// Tudo que se mexe vai para o LevelLayout do cenário, salvo em levels/<cenário>.txt.
class FreeEditor {
public:
    struct Context {
        LevelLayout& layout;
        World& world;
        std::vector<RenderScene::Prop>& props;     // mesma ordem de layout.props
        std::vector<CharacterModel*> propMeshes;   // malha de cada prop (para trocar textura)
        Door* door;                                // porta do cenário (nullptr = sem porta)
        std::string root;                          // raiz do projeto (texturas, levels/)
        int sceneId;
        AssetLibrary* assets = nullptr;
        struct Terrain* terrain = nullptr;
        struct GroundPaint* paint = nullptr;
        std::vector<std::string> gunNames, gunFiles; // armas do jogo (arquivo relativo à raiz)
        std::vector<glm::vec3> gunPos;               // onde cada arma está agora
        std::vector<float> gunYaw;
    };
    enum class Result { None, Rebuild, Play, Compile, Settings, EditGun, ChangeScene };

    bool active() const { return active_; }
    void begin(const glm::vec3& camPos, float yaw, float pitch, const std::string& root);
    void resume() { active_ = true; } // volta do jogo com a câmera de antes
    void end() { active_ = false; }
    int gunToEdit() const { return gunToEdit_; }
    int sceneTarget() const { return sceneTarget_; }
    void setStatus(const std::string& s) { status_ = s; }

    // Câmera e gestos (antes do desenho). Monta view/proj da câmera livre (a imagem fica no
    // retângulo da vista, entre os painéis).
    Result update(GLFWwindow* win, float dt, Context& ctx, int fbw, int fbh, glm::mat4& view, glm::mat4& proj,
                  glm::vec3& camPos);
    // Painéis e marcações na tela (dentro do quadro do ImGui).
    Result drawUi(Context& ctx, const glm::mat4& viewProj, int fbw, int fbh);
    // Jogando pelo editor: a imagem do jogo vai para a vista e os painéis ficam em volta.
    glm::mat4 viewportFix(float W, float H, float fullAspect) const;
    void drawPlaying(Context& ctx);

private:
    enum Kind { None, Prop, Box, DoorSel, Spawn, Start, Gun, ExtraDoor };
    struct Ray { glm::vec3 o, d; };
    struct Asset {
        std::string rel, name;
        int category; // 1 cenário, 2 armas, 3 personagens, 4 animações, 5 importados
        int gun = -1; // arma do jogo (índice), -1 = não é arma
    };
    struct Rect { float x0, y0, x1, y1; };
    Rect viewRect(float W, float H) const;
    Ray mouseRay(GLFWwindow* win, int fbw, int fbh) const;
    Ray rayAt(float sx, float sy) const;
    bool groundHit(const Ray& r, Context& c, glm::vec3& out) const;
    void pick(const Ray& r, Context& c);
    void startGesture(int mode, GLFWwindow* win, Context& ctx, int fbw, int fbh);
    void applyGesture(GLFWwindow* win, Context& ctx, int fbw, int fbh);
    void restore(Context& ctx);
    void livePropModel(Context& ctx);
    glm::vec3 selPos(Context& ctx) const;
    std::string selName(const Context& ctx) const;
    bool removeSelected(Context& c);
    void scanImages();
    void scanAssets(const Context& c);
    bool placeAsset(const Asset& a, const glm::vec3& pos, Context& c);
    void startImport(bool image = false);
    void drawBrush(Context& c, Result& res, const Rect& area);
    void applyBrush(Context& c, float dt);
    void ensurePaint(Context& c);
    void saveGround(Context& c);

    // painéis
    void drawToolbar(Context& c, Result& res, float W, float h);
    void drawDetails(Context& c, Result& res, const Rect& area);
    void drawOutliner(Context& c, const Rect& area);
    void drawContent(Context& c, Result& res, const Rect& area);

    bool active_ = false;
    glm::vec3 cam_{0.0f};
    float yaw_ = 0.0f, pitch_ = 0.0f, speed_ = 4.0f;
    glm::mat4 view_{1.0f}, proj_{1.0f};
    float winW_ = 1.0f, winH_ = 1.0f;
    double lastMx_ = 0.0, lastMy_ = 0.0;
    bool prevKey_[512] = {};
    bool lmbWas_ = false, rmbWas_ = false;
    bool looking_ = false;     // botão direito segurado na vista: câmera voando
    bool viewHovered_ = false; // mouse em cima da vista (e não de um painel)
    bool rebuildPending_ = false;
    bool playing_ = false;

    // pincel (botão Pintar): 0 textura, 1 relevo, 2 folhagem
    bool paintMode_ = false;
    int paintTab_ = 0;
    float brushRadius_ = 1.5f, brushStrength_ = 1.5f, brushSoft_ = 0.6f;
    int activeLayer_ = 0;
    int terrainTool_ = 0;
    float terrainStrength_ = 0.8f, levelY_ = 0.0f, newTerrainSize_ = 30.0f;
    int foliageAsset_ = -1;
    float foliageDensity_ = 2.0f, foliageSpacing_ = 0.4f, foliageScale_[2] = {0.8f, 1.25f};
    bool brushing_ = false, brushHasHit_ = false, invert_ = false, strokeProps_ = false;
    glm::vec3 brushHit_{0.0f};
    float foliageAcc_ = 0.0f;

    Kind kind_ = None;
    int idx_ = -1;
    int mode_ = 0; // 0 nada, 1 mover (G), 2 girar (R), 3 escala (S)
    int axis_ = -1; // -1 livre, 0 X, 1 Y (cima), 2 Z
    // estado de quando o gesto começou (para cancelar e calcular o movimento)
    double mx0_ = 0.0, my0_ = 0.0;
    glm::vec3 hit0_{0.0f}, pos0_{0.0f}, bmin0_{0.0f}, bmax0_{0.0f};
    float yaw0_ = 0.0f, scale0_ = 1.0f;
    glm::mat4 pivot0_{1.0f};

    std::string root_;
    std::vector<std::string> images_; // texturas disponíveis (relativas à raiz)
    std::vector<Asset> assetList_;
    bool assetsScanned_ = false;
    int category_ = 0, selAsset_ = -1;
    char filter_[64] = {};
    std::string status_;
    bool dirty_ = false; // mudou algo que ainda não foi salvo
    int gunToEdit_ = 0, sceneTarget_ = 0;

    // importação (janela de escolher arquivo roda em outra thread)
    struct Import {
        std::atomic<bool> done{false};
        std::string path;
        bool image = false; // textura (vai para textures/) em vez de modelo (assets/)
    };
    std::shared_ptr<Import> import_;
};
