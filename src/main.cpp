#include <GL/glew.h>
#include <GLFW/glfw3.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <imgui.h>

#include "aim_ik.h"
#include "builder.h"
#include "gun_tuning.h"
#include "free_editor.h"
#include "level.h"
#if !GA_EDITOR
#include "level_baked.h"
#endif
#include "animation.h"
#include "arena.h"
#include "character_model.h"
#include "config.h"
#include "cover.h"
#include "foot_ik.h"
#include "graphics_settings.h"
#include "gun_handling.h"
#include "loadout.h"
#include "menu.h"
#include "physics.h"
#include "ragdoll.h"
#include "renderer.h"
#include "skeleton.h"
#include "soldiers.h"
#include "table.h"
#include "terrain.h"
#include "weapon.h"

namespace {

float g_scroll = 0.0f;
void scrollCallback(GLFWwindow* w, double x, double y) {
    g_scroll += (float)y;
    (void)w;
    if (ImGui::GetCurrentContext()) ImGui::GetIO().AddMouseWheelEvent((float)x, (float)y); // painéis e editor
}

float wrapAngle(float a) {
    while (a > (float)M_PI) a -= 2.0f * (float)M_PI;
    while (a < -(float)M_PI) a += 2.0f * (float)M_PI;
    return a;
}

float approach(float cur, float target, float rate, float dt) {
    return cur + (target - cur) * (1.0f - std::exp(-rate * dt));
}

// Câmera orbital em 3ª pessoa com ombro deslocado e colisão contra o cenário.
// Mirando (aimBlend = 1) ela chega perto do ombro e fecha o campo de visão.
struct ThirdPersonCamera {
    float yaw = 0.0f;     // 0 = olhando para +Z
    float pitch = 0.22f;  // positivo = olhando para baixo
    float dist = 4.2f, targetDist = 4.2f;
    float shoulder = 0.38f;
    float side = 1.0f;    // 1 = ombro direito, -1 = esquerdo (espiando pela quina esquerda da cobertura)
    float aimBlend = 0.0f;
    float kick = 0.0f;    // coice do tiro (sobe a câmera um pouco)
    float baseFov = 60.0f;
    glm::vec3 pivot{0.0f}, pos{0.0f}, forward{0, 0, 1};
    glm::mat4 view{1.0f};
    bool initialized = false;

    float fov() const { return glm::radians(baseFov * (1.0f - 0.27f * aimBlend)); }

    void update(const World& w, const glm::vec3& charPos, float dt) {
        glm::vec3 target = charPos + glm::vec3(0.0f, 1.45f + 0.1f * aimBlend, 0.0f);
        if (!initialized) { pivot = target; initialized = true; }
        float kh = 1.0f - std::exp(-20.0f * dt), kv = 1.0f - std::exp(-9.0f * dt);
        pivot.x += (target.x - pivot.x) * kh;
        pivot.z += (target.z - pivot.z) * kh;
        pivot.y += (target.y - pivot.y) * kv;
        dist += (targetDist - dist) * (1.0f - std::exp(-10.0f * dt));

        float p = pitch - kick;
        float d = dist + (1.9f - dist) * aimBlend;
        float sh = (shoulder + (0.62f - shoulder) * aimBlend) * side;
        forward = glm::vec3(std::cos(p) * std::sin(yaw), -std::sin(p), std::cos(p) * std::cos(yaw));
        glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
        glm::vec3 desired = pivot + right * sh - forward * d;
        glm::vec3 dv = desired - pivot;
        float len = glm::length(dv);
        glm::vec3 dir = dv / len;
        float hit = w.raycast(pivot, dir, len + 0.25f);
        float actual = std::max(0.3f, std::min(len, hit - 0.25f));
        pos = pivot + dir * actual;
        pos.y = std::max(pos.y, w.floorAt(pos.x, pos.z) + 0.15f);
        view = glm::lookAt(pos, pos + forward, glm::vec3(0, 1, 0));
    }

    // Ponto do mundo exatamente sob a mira (centro da tela).
    glm::vec3 aimPoint(const World& w) const {
        World::RayHit h = w.raycastHit(pos, forward, 250.0f);
        return pos + forward * (h.hit ? h.t : 250.0f);
    }
};

struct Args {
    const char* shot = nullptr;
    const char* anim = "idle";
    float phase = 0.0f, time = 1.0f;
    float camYaw = 180.0f, camPitch = 8.0f, camDist = 3.6f;
    glm::vec3 pos{0.0f};
    float yaw = 0.0f;
    bool debug = false, noIK = false;
    float simTime = 0.0f; // > 0: simula andando para frente por esse tempo antes da foto
    bool simRun = false;
    bool gun = false, aim = false; // segurando a arma / mirando
    int fire = 0;                  // tiros disparados antes da foto
    bool viewCam = false;          // foto de outra câmera (a mira continua vindo da câmera de jogo)
    float viewYaw = 0.0f, viewPitch = 0.0f, viewDist = 3.0f;
    bool viewAtSet = false;
    glm::vec3 viewAt{0.0f}; // ponto que a câmera de observação enquadra
    int width = 1600, height = 900;
    std::vector<std::pair<std::string, std::string>> gfx; // --gfx chave=valor
    int preset = -1;
    bool menu = false;  // desenha o menu de pausa na foto
    int scene = -1;     // --scene N: cenário (0 = teste/arena, 1 = oficial)
    bool freeEdit = false; // --freeedit: abre já na edição livre do cenário
    int bench = 0;      // renderiza N quadros e mede o tempo
    bool holster = false;       // todas as armas guardadas no corpo
    int weapon = 0;             // arma usada por --gun/--aim/--act: 0 pistola, 1 AK-47, 2 AK-12
    const char* act = nullptr;  // pickup | stow | draw: foto no meio da animação
    float actTime = 0.0f;
    float npcTime = 0.0f;       // simula os soldados por esse tempo antes da foto
    bool doorOpen = false;      // porta da arena aberta na foto
    bool vaultTest = false;     // imprime o que o parkour faria em alguns pontos do mapa
    int killNpc = 0;            // mata os N primeiros soldados (testar a queda)
    const char* coverClip = nullptr; // low | stand: pose do clipe de cobertura (--coverclip tipo lado fase)
    int coverClipSide = 0;
    float coverClipPhase = 0.0f;
    float crouch = 0.0f;        // agachado (m que o quadril desce), como atrás de uma cobertura baixa
};

Args parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        auto next = [&]() { return i + 1 < argc ? argv[++i] : ""; };
        if (!std::strcmp(argv[i], "--shot")) a.shot = next();
        else if (!std::strcmp(argv[i], "--anim")) a.anim = next();
        else if (!std::strcmp(argv[i], "--phase")) a.phase = (float)std::atof(next());
        else if (!std::strcmp(argv[i], "--time")) a.time = (float)std::atof(next());
        else if (!std::strcmp(argv[i], "--cam")) {
            a.camYaw = (float)std::atof(next());
            a.camPitch = (float)std::atof(next());
            a.camDist = (float)std::atof(next());
        } else if (!std::strcmp(argv[i], "--pos")) {
            a.pos.x = (float)std::atof(next());
            a.pos.y = (float)std::atof(next());
            a.pos.z = (float)std::atof(next());
        } else if (!std::strcmp(argv[i], "--yaw")) a.yaw = glm::radians((float)std::atof(next()));
        else if (!std::strcmp(argv[i], "--noik")) a.noIK = true;
        else if (!std::strcmp(argv[i], "--sim")) a.simTime = (float)std::atof(next());
        else if (!std::strcmp(argv[i], "--simrun")) a.simRun = true;
        else if (!std::strcmp(argv[i], "--gun")) a.gun = true;
        else if (!std::strcmp(argv[i], "--aim")) a.aim = a.gun = true;
        else if (!std::strcmp(argv[i], "--fire")) a.fire = std::atoi(next());
        else if (!std::strcmp(argv[i], "--viewat")) {
            a.viewAtSet = true;
            a.viewAt.x = (float)std::atof(next());
            a.viewAt.y = (float)std::atof(next());
            a.viewAt.z = (float)std::atof(next());
        } else if (!std::strcmp(argv[i], "--view")) {
            a.viewCam = true;
            a.viewYaw = (float)std::atof(next());
            a.viewPitch = (float)std::atof(next());
            a.viewDist = (float)std::atof(next());
        }
        else if (!std::strcmp(argv[i], "--debug")) a.debug = true;
        else if (!std::strcmp(argv[i], "--hour")) a.gfx.push_back({"timeOfDay", next()});
        else if (!std::strcmp(argv[i], "--gfx")) {
            std::string kv = next();
            size_t eq = kv.find('=');
            if (eq != std::string::npos) a.gfx.push_back({kv.substr(0, eq), kv.substr(eq + 1)});
        } else if (!std::strcmp(argv[i], "--preset")) a.preset = std::atoi(next());
        else if (!std::strcmp(argv[i], "--menu")) a.menu = true;
        else if (!std::strcmp(argv[i], "--scene")) a.scene = std::atoi(next());
        else if (!std::strcmp(argv[i], "--freeedit")) a.freeEdit = a.menu = true;
        else if (!std::strcmp(argv[i], "--bench")) a.bench = std::atoi(next());
        else if (!std::strcmp(argv[i], "--holster")) a.holster = true;
        else if (!std::strcmp(argv[i], "--weapon")) a.weapon = std::atoi(next());
        else if (!std::strcmp(argv[i], "--act")) {
            a.act = next();
            a.actTime = (float)std::atof(next());
        }
        else if (!std::strcmp(argv[i], "--npc")) a.npcTime = (float)std::atof(next());
        else if (!std::strcmp(argv[i], "--door")) a.doorOpen = true;
        else if (!std::strcmp(argv[i], "--vaulttest")) a.vaultTest = true;
        else if (!std::strcmp(argv[i], "--coverclip")) {
            a.coverClip = next();
            a.coverClipSide = std::atoi(next());
            a.coverClipPhase = (float)std::atof(next());
        } else if (!std::strcmp(argv[i], "--crouch")) a.crouch = (float)std::atof(next());
        else if (!std::strcmp(argv[i], "--kill")) a.killNpc = std::atoi(next());
        else if (!std::strcmp(argv[i], "--size")) std::sscanf(next(), "%dx%d", &a.width, &a.height);
    }
    return a;
}

void printControls() {
    std::printf(
        "\n=== Controles ===\n"
        "  W A S D      mover (relativo a camera)\n"
        "  Shift        correr\n"
        "  Espaco       pular\n"
        "  E            pegar a arma (da mesa)\n"
        "  1 / 2 / 3    pistola (cintura) / fuzil 1 / fuzil 2 (costas): saca, guarda ou troca\n"
        "  Botao dir.   mirar (zoom)\n"
        "  Botao esq.   atirar\n"
        "  G            soltar a arma\n"
        "  Q            cobertura (entra/sai); A/D anda colado; botao dir. atira dela\n"
        "  Mouse        girar camera | Scroll: zoom\n"
        "  F1           ver pesos do rig (debug)\n"
        "  F2           liga/desliga Foot IK\n"
        "  R            voltar ao inicio\n"
        "  Esc          menu (graficos) / pausa\n\n");
}

} // namespace

int main(int argc, char** argv) {
    Args args = parseArgs(argc, argv);
#if GA_EDITOR
    if (argc == 1) args.freeEdit = args.menu = true; // versão de desenvolvimento abre no editor
#endif
    const bool shotMode = args.shot != nullptr;

    // No Wayland o GLFW carrega o libdecor-gtk, que aborta no GNOME novo; e o GLEW
    // do Ubuntu só funciona com GLX. Usa X11 (XWayland) quando disponível.
#ifdef GLFW_PLATFORM_X11
    if (glfwPlatformSupported(GLFW_PLATFORM_X11))
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif
    if (!glfwInit()) {
        std::fprintf(stderr, "Falha ao iniciar GLFW\n");
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    if (shotMode) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* win = glfwCreateWindow(args.width, args.height, "GA - teste do personagem", nullptr, nullptr);
    if (!win) {
        std::fprintf(stderr, "Falha ao criar janela OpenGL 3.3\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::fprintf(stderr, "Falha ao iniciar GLEW\n");
        return 1;
    }
    std::printf("[gl] %s | %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));

    std::string root = GA_ROOT;
#if !GA_EDITOR
    // jogo copiado pelo Compilar: os arquivos ficam junto do executável
    {
        std::error_code ec;
        std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
        if (!ec && std::filesystem::exists(exe.parent_path() / "shaders", ec)) root = exe.parent_path().string();
    }
#endif
    std::printf("[jogo] arquivos em %s\n", root.c_str());
    Skeleton skeleton = Skeleton::makeCrashDummy();
    CharacterModel character;
    if (!character.load(root + "/crash_dummy.glb", &skeleton, {})) return 1;
    // armas: pistola, AK-47 e AK-12 (cada uma com sua definição em guns.cpp)
    const GunDef* defs[3] = {&pistolDef(), &ak47Def(), &ak12Def()};
    CharacterModel gunMeshes[3];
    for (int i = 0; i < 3; ++i) {
        CharacterModel::LoadOptions gunOpt;
        gunOpt.groundAtZero = false;
        gunOpt.fixSkinnedAxes = true;
        gunOpt.maxTextureSize = 2048;
        if (!gunMeshes[i].load(root + "/" + defs[i]->file, nullptr, gunOpt)) return 1;
    }
    // ajustes das armas feitos no editor (weapons.ini)
    const std::string weaponsPath = root + "/weapons.ini";
    loadGunTuning(weaponsPath);
    CharacterModel tableMesh;
    CharacterModel::LoadOptions tableOpt;
    tableOpt.maxTextureSize = 2048;
    if (!tableMesh.load(root + "/wooden_table.glb", nullptr, tableOpt)) return 1;

    int fbw, fbh;
    glfwGetFramebufferSize(win, &fbw, &fbh);
    if (shotMode) { fbw = args.width; fbh = args.height; }
    ThirdPersonCamera cam;
    Renderer renderer;
    if (!renderer.init(root + "/shaders", fbw, fbh)) return 1;

    // opções gráficas: salvas em settings.ini (a captura usa os padrões + --preset/--gfx)
    const std::string settingsPath = root + "/settings.ini";
    GraphicsSettings gfx;
    if (!shotMode) gfx.load(settingsPath);
    if (args.preset >= 0) gfx.applyPreset(args.preset);
    for (auto& kv : args.gfx)
        if (!gfx.setValue(kv.first, kv.second)) std::fprintf(stderr, "[gfx] opcao desconhecida: %s\n", kv.first.c_str());
    renderer.applySettings(gfx);
    glfwSwapInterval(gfx.vsync ? 1 : 0);
    cam.baseFov = gfx.fov;

    // cenário: no editor o escolhido no menu (ou --scene); a versão final é sempre a oficial
#if GA_EDITOR
    const int startScene = std::clamp(args.scene >= 0 ? args.scene : gfx.scene, 0, SceneCount - 1);
#else
    const int startScene = SceneOfficial;
#endif
    World world;
    Arena arena;              // área de combate: porta, inimigos (no teste é a arena murada)
    std::vector<Table> tables; // só no cenário de teste
    Terrain terrain;          // relevo e pintura do chão do cenário atual (editor)
    GroundPaint paint;
    int paintScene = -1;      // cenário de onde vieram (só recarrega ao trocar de cenário)
    AssetLibrary assets;      // modelos postos no cenário pelo editor
    assets.root = root;
    assets.loadConfig();
    OfficialLevel official;   // carregado quando o cenário oficial é usado
    int sceneId = -1;
    SoldierAssets soldierAssets;
    if (!soldierAssets.load(root)) return 1;
    Squad squad;
    Weapon enemyFx; // tiros dos soldados
    CharacterBody body;
    // Captura de movimento (Mixamo) convertida para o esqueleto do boneco
    MocapClip walkClip, runClip;
    loadMixamoClip(root + "/Walking.fbx", skeleton, walkClip);
    loadMixamoClip(root + "/Running.fbx", skeleton, runClip);
    // cobertura (de costas para a parede): de pé na alta, agachado na baixa; [0] = esquerda, [1] = direita
    MocapClip coverStand[2], coverLow[2];
    loadMixamoClip(root + "/animation-teste/Left Cover Sneak.fbx", skeleton, coverStand[0]);
    loadMixamoClip(root + "/animation-teste/Right Cover Sneak.fbx", skeleton, coverStand[1]);
    loadMixamoClip(root + "/animation-teste/Crouched Sneaking Left.fbx", skeleton, coverLow[0]);
    loadMixamoClip(root + "/animation-teste/Crouched Sneaking Right.fbx", skeleton, coverLow[1]);
    // parkour: pulo correndo (Running Jump) e salto por cima de obstáculos (Jumping)
    MocapClip runJumpClip, vaultClip;
    loadMixamoClip(root + "/animation-teste/parkur/Running Jump.fbx", skeleton, runJumpClip);
    loadMixamoClip(root + "/animation-teste/parkur/Jumping.fbx", skeleton, vaultClip);
    // pulo correndo: quem sobe é a física, então o clipe fica com o pé mais baixo sempre na altura
    // do chão do modelo (mantém o agachar da saída e da chegada, tira a subida)
    if (runJumpClip.valid()) {
        const float ankle = skeleton.joint[FOOT_L].y;
        for (Pose& p : runJumpClip.frames) {
            glm::mat4 g[BONE_COUNT];
            computeGlobals(skeleton, p, g);
            p.rootOffset.y -= std::max(0.0f, std::min(g[FOOT_L][3].y, g[FOOT_R][3].y) - ankle);
        }
    }
    // quadros do clipe: pé sai do chão / volta ao chão (medidos no arquivo)
    const float kRunJumpTakeoff = 3.0f, kRunJumpLand = 17.5f; // Running Jump (27 quadros)
    const float kVaultTakeoff = 10.5f;                         // Jumping (35 quadros)
    // entrar/sair da cobertura (tocam uma vez): [0] = em pé, [1] = agachado
    MocapClip coverIn[2], coverOut[2];
    {
        const std::string dir = root + "/animation-teste/cover saindo e entrando/";
        loadMixamoClip(dir + "entrando no cover em pe.fbx", skeleton, coverIn[0]);
        loadMixamoClip(dir + "entrando no cover abaixado.fbx", skeleton, coverIn[1]);
        loadMixamoClip(dir + "saindo do cover em pe.fbx", skeleton, coverOut[0]);
        loadMixamoClip(dir + "saindo do cover abaixado.fbx", skeleton, coverOut[1]);
        // a pose de costas para a parede fica olhando para +Z (como no jogo): último quadro do
        // "entrando" e primeiro do "saindo"
        for (MocapClip* c : {&coverIn[0], &coverIn[1]})
            if (c->valid()) rotateClip(*c, -poseYaw(c->frames.back()));
        for (MocapClip* c : {&coverOut[0], &coverOut[1]})
            if (c->valid()) rotateClip(*c, -poseYaw(c->frames.front()));
    }
    // costas retas na parede (o Mixamo vem com o peito um pouco girado) e as fases em que dá para
    // parar: os dois pés no chão com a abertura de quem está parado
    std::vector<float> coverIdle[4];
    {
        MocapClip* all[4] = {&coverStand[0], &coverStand[1], &coverLow[0], &coverLow[1]};
        for (int k = 0; k < 4; ++k) {
            MocapClip& c = *all[k];
            if (!c.valid()) continue;
            removeClipYaw(c);
            const int n = (int)c.frames.size();
            std::vector<float> score(n);
            for (int f = 0; f < n; ++f) {
                glm::mat4 g[BONE_COUNT];
                computeGlobals(skeleton, c.frames[f], g);
                glm::vec3 l(g[FOOT_L][3]), r(g[FOOT_R][3]);
                float width = glm::length(glm::vec2(l.x - r.x, l.z - r.z));
                score[f] = std::fabs(width - 0.3f) + 3.0f * std::fabs(l.y - r.y);
            }
            // os dois melhores quadros, separados por pelo menos 1/3 do ciclo
            int a = (int)(std::min_element(score.begin(), score.end()) - score.begin()), b = -1;
            for (int f = 0; f < n; ++f) {
                int d = std::abs(f - a);
                if (std::min(d, n - d) < n / 3) continue;
                if (b < 0 || score[f] < score[b]) b = f;
            }
            // fase = quadro/n descontado o deslocamento de fase do clipe
            for (int f : {a, b})
                if (f >= 0) coverIdle[k].push_back(std::fmod((float)f / n - c.phaseOffset + 1.0f, 1.0f));
        }
    }
    Animator animator(skeleton);
    animator.setClips(&walkClip, &runClip);
    FootIK footIK;
    footIK.enabled = !args.noIK;
    Weapon weapon; // tiros e efeitos
    // as armas deitadas perto da borda virada para o início; os fuzis com o cano para a esquerda,
    // a coronha correndo junto da borda (tudo em cima do tampo)
    Loadout loadout;
    GunHandling handling;
    float charYaw = 0.0f;
    float visualY = 0.0f; // altura do corpo na tela (suaviza os degraus da cápsula)
    float aimW = 0.0f;    // 0 = arma baixa, 1 = mirando
    bool debugWeights = args.debug;

    glm::mat4 globals[BONE_COUNT], skin[BONE_COUNT];
    RenderScene scene;
    scene.world = &world;
    scene.character = &character;
    scene.bones = skin;
    scene.weapon = &weapon;
    scene.enemyWeapon = &enemyFx;

    // Layout de cada cenário (editor livre): de fábrica + o salvo em levels/; a versão final usa o
    // layout oficial embutido no executável.
    LevelLayout layouts[SceneCount];
    defaultTestLayout(layouts[SceneTest]);
    defaultOfficialLayout(layouts[SceneOfficial]);
#if GA_EDITOR
    for (int i = 0; i < SceneCount; ++i)
        if (layouts[i].load(root + "/" + sceneFile(i))) std::printf("[cenario] layout carregado de %s\n", sceneFile(i));
#else
    layouts[SceneOfficial].parse(kBakedOfficialLevel);
#endif

    // Monta um cenário: mundo (colisões), objetos, área de combate, armas e o jogador no começo.
    auto setupScene = [&](int id) {
        if (id == SceneOfficial && !official.load(root, layouts[SceneOfficial])) {
            std::fprintf(stderr, "[cenario] falha ao carregar o cenario oficial; usando o de teste\n");
            id = SceneTest;
        }
        sceneId = id;
        LevelLayout& L = layouts[id];
        scene.props.clear();
        if (id == SceneTest) {
            // mapa de blocos, arena com porta e soldados (russian_soldier.glb) com AK-47, mesas com armas
            arena = buildTestScene(world, L, tables);
            for (const Table& t : tables) scene.props.push_back({&tableMesh, t.model()});
        } else {
            tables.clear();
            arena = official.build(world, scene.props, L);
            for (auto& [i, b] : L.boxes) // caixas visíveis movidas no editor livre (batente da porta)
                if (i >= 0 && i < (int)world.boxes.size() && !world.boxes[i].hidden) { world.boxes[i].min = b.first; world.boxes[i].max = b.second; }
        }
        assets.build(world, scene.props, L); // objetos arrastados do navegador de conteúdo
        if (paintScene != id) { // relevo e pintura salvos do cenário
            paintScene = id;
            terrain.release();
            paint.release();
            if (L.terrainOn) {
                terrain.origin = L.terrainOrigin;
                terrain.size = L.terrainSize;
                terrain.n = L.terrainN;
                if (!terrain.load(root + "/" + sceneTerrainFile(id))) terrain.create(L.terrainOrigin + glm::vec2(L.terrainSize * 0.5f), L.terrainSize, L.terrainN, 0.0f);
            }
            if (L.paintOn) {
                paint.create(L.paintOrigin, L.paintSize, L.paintRes);
                for (const LevelLayout::PaintLayer& pl : L.paintLayers) paint.layers.push_back({pl.albedo, pl.normal, pl.tile});
                paint.uploadLayers(root);
                paint.loadWeights(root + "/" + scenePaintFile(id));
            }
        }
        world.terrain = &terrain;
        scene.terrain = &terrain;
        scene.paint = &paint;
        const glm::vec3 start = L.start;
        const float startYaw = L.startYaw;
        // armas: no teste em cima das mesas, perto da borda virada para o início (os fuzis com o cano
        // para a esquerda); no oficial no chão, perto de onde o personagem acorda
        loadout = Loadout();
        const struct { int table; float across, along, turn; } spots[3] = {
            {0, 0.80f, 0.45f, glm::radians(20.0f)},  // pistola
            {0, 0.60f, -0.72f, glm::radians(75.0f)}, // AK-47
            {1, 0.60f, -0.65f, glm::radians(75.0f)}, // AK-12
        };
        for (int i = 0; i < 3; ++i) {
            GunItem it;
            it.def = defs[i];
            if (auto g = L.guns.find(i); g != L.guns.end()) {
                it.drop(g->second.first, g->second.second); // posta no editor
            } else if (id == SceneTest && (int)tables.size() > spots[i].table) {
                const Table& t = tables[spots[i].table];
                it.placeOnTable(t.topPoint(spots[i].across, spots[i].along), t.facingFromPlusX(), spots[i].turn, t.pos.y);
            } else {
                glm::vec3 fwd(std::sin(startYaw), 0.0f, std::cos(startYaw)), right(-fwd.z, 0.0f, fwd.x);
                it.drop(start + fwd * 1.2f + right * (0.6f * (i - 1)), startYaw + 1.3f);
            }
            loadout.items.push_back(it);
        }
        squad.init(&soldierAssets, defs[1], arena.spawns, arena.spawnYaw);
        handling = GunHandling();
        weapon = Weapon();
        enemyFx = Weapon();
        body = CharacterBody();
        body.pos = start;
        charYaw = startYaw;
        cam.yaw = startYaw; // câmera atrás do personagem, olhando para onde ele começa virado
        visualY = start.y;
        footIK.reset();
        renderer.resetExposure();
        std::printf("[cenario] %s\n", sceneName(id));
    };
    setupScene(startScene);

    // onde cada arma está agora: na mão, no corpo ou deitada (mesa/chão). Espaço canônico -> mundo.
    auto gunMatrix = [&](int i, const glm::mat4& charModel) {
        const GunItem& it = loadout.items[i];
        switch (it.place) {
        case GunPlace::Hand: return charModel * gunInHand(globals[HAND_R], *it.def);
        case GunPlace::Body: return charModel * slotMatrix(skeleton, globals, it.slot, *it.def);
        default: return it.groundMatrix();
        }
    };
    // eventos das animações: a arma muda de lugar. Retorna a arma que mudou (-1 nenhuma).
    auto applyEvents = [&](const GunHandling::Events& ev) {
        if (ev.grabbed) loadout.toHand(handling.item());
        if (ev.stowed) loadout.toBody(handling.item());
        return ev.grabbed || ev.stowed ? handling.item() : -1;
    };
    // Braços com a arma (segurar/mirar/pegar/guardar/sacar) por cima da animação do corpo.
    // Deixa 'globals' e 'skin' prontos.
    auto poseArms = [&](Pose& pose, const glm::mat4& charModel, const glm::vec3& aimWorld, float aimW, float recoil) {
        const glm::vec3 aimLocal = glm::vec3(glm::inverse(charModel) * glm::vec4(aimWorld, 1.0f));
        const GunDef* hd = loadout.hand >= 0 ? loadout.items[loadout.hand].def : nullptr;
        const bool rifle = hd && hd->rifle;
        if (rifle) {
            applyRifleTorso(skeleton, pose, aimLocal, aimW);
            applyRightHand(skeleton, pose, handForGun(rifleHold(skeleton, pose, *hd, aimLocal, aimW, recoil), *hd), 1.0f);
        } else if (hd) {
            applyCarryGrip(skeleton, pose, *hd, 1.0f - aimW);
            applyAimPose(skeleton, pose, *hd, aimLocal, aimW, recoil);
        }
        float support = rifle ? 1.0f : 0.0f; // mão esquerda no guarda-mão
        if (handling.busy()) {
            const GunItem& it = loadout.items[handling.item()];
            computeGlobals(skeleton, pose, globals);
            HandReach hr = handling.reach(skeleton, globals, *it.def, glm::inverse(charModel) * it.groundMatrix());
            applyHandReach(skeleton, pose, hr);
            if (rifle) support = hr.support;
        }
        if (rifle) applySupportHand(skeleton, pose, *hd, support);
        setFingerCurl(skeleton, pose, 1, handling.fingerCurl(hd != nullptr), hd ? 1.0f : 0.2f);
        float left = rifle ? support : aimW;
        setFingerCurl(skeleton, pose, 0, 0.3f + 0.6f * left, 0.2f + 0.4f * left);
        computeGlobals(skeleton, pose, globals);
        computeSkinMatrices(skeleton, globals, skin);
    };
    auto fillGuns = [&](const glm::mat4& charModel, std::vector<glm::mat4>& mats) {
        mats.resize(loadout.items.size());
        for (size_t i = 0; i < loadout.items.size(); ++i) mats[i] = gunMatrix((int)i, charModel);
    };
    auto sceneGuns = [&](const std::vector<glm::mat4>& mats) {
        scene.guns.clear();
        for (size_t i = 0; i < loadout.items.size(); ++i) {
            int k = (int)(std::find(defs, defs + 3, loadout.items[i].def) - defs);
            scene.guns.push_back({&gunMeshes[k], mats[i] * loadout.items[i].def->meshFix});
        }
    };
    // onde a tecla E aparece (acima da empunhadura da arma)
    auto promptFor = [&](int i) {
        const GunItem& it = loadout.items[i];
        return glm::vec3(it.groundMatrix() * glm::vec4(it.def->grip, 1.0f)) + glm::vec3(0.0f, 0.3f, 0.0f);
    };

    // ---------- modo screenshot (usado para validar rig/animações/tiro) ----------
    if (shotMode) {
        GLuint fbo, tex;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, fbw, fbh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);

        // armas no corpo / na mão conforme os argumentos
        const int wi = std::clamp(args.weapon, 0, 2);
        if (args.holster)
            for (int i = 0; i < 3; ++i) loadout.equipDirect(i, false);
        if (args.gun) loadout.equipDirect(wi, true);
        if (args.act) {
            if (!std::strcmp(args.act, "pickup")) {
                args.pos = loadout.items[wi].standPos;
                args.yaw = loadout.items[wi].standYaw;
                handling.startPickUp(wi);
                handling.begin();
            } else if (!std::strcmp(args.act, "stow")) {
                loadout.equipDirect(wi, true);
                handling.startStow(wi, loadout.items[wi].slot);
            } else if (!std::strcmp(args.act, "draw")) {
                loadout.equipDirect(wi, false);
                handling.startDraw(wi, loadout.items[wi].slot);
            }
            for (float t = 0.0f; t < args.actTime; t += 1.0f / 120.0f) applyEvents(handling.update(1.0f / 120.0f));
            std::printf("[act] %s t=%.2f arma %s na mao %d\n", args.act, args.actTime, defs[wi]->name.c_str(), loadout.hand);
        }
        glm::mat4 model = glm::translate(glm::mat4(1.0f), args.pos) * glm::rotate(glm::mat4(1.0f), args.yaw, glm::vec3(0, 1, 0));
        Pose pose;
        if (args.simTime > 0.0f) {
            // mesmo pipeline do jogo, com entrada fixa "para frente"
            body.pos = args.pos;
            visualY = args.pos.y;
            glm::vec3 dir(std::sin(args.yaw), 0.0f, std::cos(args.yaw));
            const float sdt = 1.0f / 120.0f;
            for (float t = 0.0f; t < args.simTime; t += sdt) {
                body.step(world, dir, args.simRun ? animator.runSpeed() : animator.walkSpeed(), false, sdt);
                arena.door.update(world, {&body}, sdt);
                AnimInput ai;
                ai.dt = sdt;
                ai.speed = glm::length(glm::vec2(body.vel.x, body.vel.z));
                ai.verticalVel = body.vel.y;
                ai.grounded = body.grounded;
                ai.landed = body.justLanded;
                ai.landImpact = body.landImpact;
                animator.update(ai);
                visualY = body.grounded ? std::clamp(visualY + (body.pos.y - visualY) * (1.0f - std::exp(-12.0f * sdt)),
                                                     body.pos.y - 0.45f, body.pos.y + 0.45f)
                                        : body.pos.y;
                model = glm::translate(glm::mat4(1.0f), glm::vec3(body.pos.x, visualY, body.pos.z)) *
                        glm::rotate(glm::mat4(1.0f), args.yaw, glm::vec3(0, 1, 0));
                pose = animator.pose();
                footIK.apply(skeleton, pose, model, world, animator.groundedWeight(), sdt);
            }
            args.pos = glm::vec3(body.pos.x, visualY, body.pos.z);
            std::printf("[sim] pos %.2f %.2f %.2f visualY %.2f estado %s\n", body.pos.x, body.pos.y, body.pos.z, visualY,
                        animator.stateName());
        } else {
            animator.forceClip(args.anim, args.phase, args.time);
            for (int i = 0; i < 8; ++i) { pose = animator.pose(); footIK.apply(skeleton, pose, model, world, 1.0f, 1.0f); }
        }

        float fovy = glm::radians(55.0f);
        if (args.aim) {
            // câmera de mira de verdade, atrás do ombro
            cam.yaw = args.yaw;
            cam.pitch = glm::radians(args.camPitch);
            cam.aimBlend = 1.0f;
            fovy = cam.fov();
        } else {
            cam.yaw = glm::radians(args.camYaw);
            cam.pitch = glm::radians(args.camPitch);
            cam.dist = cam.targetDist = args.camDist;
            cam.shoulder = 0.0f;
        }
        cam.update(world, args.pos, 1.0f);
        glm::vec3 aimPoint = cam.aimPoint(world);

        if (args.aim && loadout.hand < 0) loadout.equipDirect(wi, true);
        std::vector<glm::mat4> gunMats;
        // na porta (simulando andar até ela): mão espalmada ou ombro
        Door::Touch shotTouch = args.simTime > 0.0f ? arena.door.touch(body) : Door::Touch();
        auto poseWithGun = [&](float recoil) {
            Pose p = pose;
            if (args.coverClip) {
                std::string k = args.coverClip;
                if (k == "low" || k == "stand") {
                    const MocapClip& c = (k == "low" ? coverLow : coverStand)[args.coverClipSide & 1];
                    if (c.valid()) p = c.sample(args.coverClipPhase);
                } else if (k == "runjump" || k == "vault") { // parkour (fase 0..1)
                    const MocapClip& c = k == "vault" ? vaultClip : runJumpClip;
                    if (c.valid()) p = c.frames[(size_t)(std::clamp(args.coverClipPhase, 0.0f, 1.0f) * (c.frames.size() - 1))];
                } else { // in / out / inlow / outlow: quadro da transição (fase 0..1)
                    const MocapClip& c = (k.rfind("in", 0) == 0 ? coverIn : coverOut)[k.find("low") != std::string::npos];
                    if (c.valid()) p = c.frames[(size_t)std::clamp(args.coverClipPhase, 0.0f, 1.0f) * (c.frames.size() - 1)];
                }
            }
            applyCrouch(skeleton, p, args.crouch, args.crouch * 0.8f);
            if (loadout.hand >= 0) applyShoulderPush(p, shotTouch.weight);
            poseArms(p, model, aimPoint, args.aim ? 1.0f : 0.0f, recoil);
            if (loadout.hand < 0 && shotTouch.weight > 0.001f) {
                glm::mat4 inv = glm::inverse(model);
                glm::vec3 pl = glm::vec3(inv * glm::vec4(shotTouch.point, 1.0f));
                applyPalmOnSurface(skeleton, p, pl.x < 0.15f, pl, glm::vec3(inv * glm::vec4(shotTouch.normal, 0.0f)), shotTouch.weight);
                computeGlobals(skeleton, p, globals);
                computeSkinMatrices(skeleton, globals, skin);
            }
            fillGuns(model, gunMats);
        };
        poseWithGun(0.0f);
        for (int i = 0; i < args.fire && loadout.hand >= 0; ++i) {
            const GunDef& d = *loadout.items[loadout.hand].def;
            weapon.cooldown = 0.0f;
            weapon.fire(world, d, gunMats[loadout.hand], aimPoint + glm::vec3(0.12f * (i % 3 - 1), 0.1f * (i % 2), 0.0f));
            weapon.update(world, i + 1 < args.fire ? 0.3f : 0.012f);
            poseWithGun(weapon.recoil);
        }

        if (args.viewCam) {
            ThirdPersonCamera view;
            view.yaw = glm::radians(args.viewYaw);
            view.pitch = glm::radians(args.viewPitch);
            view.dist = view.targetDist = args.viewDist;
            view.shoulder = 0.0f;
            view.update(world, args.viewAtSet ? args.viewAt - glm::vec3(0.0f, 1.45f, 0.0f) : args.pos, 1.0f);
            cam = view;
            fovy = glm::radians(50.0f);
        }
        scene.characterModel = model;
        scene.characterPos = args.pos;
        sceneGuns(gunMats);
        // --npc T: simula os soldados por T segundos com o jogador parado em --pos
        for (float t = 0.0f; t < args.npcTime; t += 1.0f / 60.0f) {
            arena.door.update(world, squad.aliveBodies(), 1.0f / 60.0f);
            for (Door& d : arena.extraDoors) d.update(world, squad.aliveBodies(), 1.0f / 60.0f);
            squad.update(world, {args.pos, glm::vec3(0.0f), true}, enemyFx, 1.0f / 60.0f);
            enemyFx.update(world, t + 1.0f / 60.0f < args.npcTime ? 1.0f / 60.0f : 0.0f);
        }
        if (args.doorOpen) {
            arena.door.angle = -1.2f;
            arena.door.update(world, {}, 1e-4f);
        }
        for (int k = 0; k < args.killNpc && k < (int)squad.soldiers.size(); ++k) {
            squad.damage(k, 1000.0f, glm::vec3(0, 0, -1), args.pos, squad.soldiers[k]->body.pos + glm::vec3(0, 1.4f, 0));
            for (int f = 0; f < 150; ++f) squad.update(world, {args.pos, glm::vec3(0.0f), false}, enemyFx, 1.0f / 60.0f);
        }
        scene.actors.clear();
        scene.panels.clear();
        squad.addToScene(scene, gunMeshes[1]);
        arena.door.addPanels(scene.panels);
        for (const Door& d : arena.extraDoors) d.addPanels(scene.panels);
        scene.view = cam.view;
        scene.proj = glm::perspective(fovy, (float)fbw / fbh, 0.05f, 1000.0f);
        scene.camPos = cam.pos;
        scene.debugWeights = debugWeights;
        scene.crosshairAlpha = args.aim && !args.viewCam ? 1.0f : 0.0f;
        int pick = handling.busy() ? -1 : loadout.pickable(args.pos);
        scene.promptAlpha = pick >= 0 ? 1.0f : 0.0f;
        if (pick >= 0) scene.promptWorld = promptFor(pick);
        scene.time = 30.0f;
        if (args.menu) menu::init(win, false);
        const int frames = std::max(1, args.bench);
        glFinish();
        double t0 = glfwGetTime();
        for (int i = 0; i < frames; ++i) {
            renderer.render(scene, 1.0f / 60.0f, fbo);
            if (args.menu) {
                glBindFramebuffer(GL_FRAMEBUFFER, fbo);
                bool changed;
                menu::beginFrame();
                menu::drawPauseMenu(gfx, changed);
                menu::endFrame();
            }
        }
        glFinish();
        if (args.bench) std::printf("[bench] %.2f ms por quadro (%dx%d)\n", (glfwGetTime() - t0) * 1000.0 / frames, fbw, fbh);
        std::vector<unsigned char> px((size_t)fbw * fbh * 4);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glReadPixels(0, 0, fbw, fbh, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        stbi_flip_vertically_on_write(1);
        stbi_write_png(args.shot, fbw, fbh, 4, px.data(), fbw * 4);
        std::printf("[shot] salvo %s\n", args.shot);
        glfwTerminate();
        return 0;
    }

    // ---------- jogo ----------
    printControls();
    glfwSetScrollCallback(win, scrollCallback);
    if (!menu::init(win, true)) {
        std::fprintf(stderr, "Falha ao iniciar o menu (ImGui)\n");
        return 1;
    }
    glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    if (glfwRawMouseMotionSupported()) glfwSetInputMode(win, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    bool mouseCaptured = !args.menu;
    if (args.menu) glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    double lastMx, lastMy;
    glfwGetCursorPos(win, &lastMx, &lastMy);

    const float fixedDt = 1.0f / 120.0f;
    float accumulator = 0.0f;
    double lastTime = glfwGetTime(), titleTimer = 0.0;
    int frames = 0;
    bool f1Was = false, f2Was = false, escWas = false, eWas = false, gWas = false, lmbWas = false;
    bool numWas[kSlotCount] = {};
    bool fireQueued = false;
    bool rmbWas = false;
    bool menuOpen = args.menu; // --menu: já começa com o menu aberto
    bool vsync = gfx.vsync;
    float gameTime = 0.0f, fpsShown = 0.0f, msShown = 0.0f;
    float hipFireTimer = 0.0f; // atirar sem segurar o botão direito levanta a arma por um instante
    float promptAlpha = 0.0f;
    glm::vec3 promptPos(0.0f);
    std::vector<glm::mat4> gunMats, lastGuns;
    fillGuns(glm::mat4(1.0f), lastGuns);
    int transferItem = -1;  // arma trocando de lugar (mesa/corpo <-> mão): suaviza o encaixe
    glm::mat4 transferFrom(1.0f);
    float transferT = 1.0f;
    // próximo gesto depois de guardar a arma da mão (trocar de arma / pegar outra da mesa)
    enum class Next { None, Draw, PickUp } next = Next::None;
    int nextItem = -1;
    float hitch = 0.0f; // pausa curta entre guardar e o próximo gesto

    // combate na arena: vida do jogador, morte/renascimento e ondas de soldados
    float health = 100.0f, sinceHurt = 10.0f, hurtFlash = 0.0f, hitMarker = 0.0f, deadT = 0.0f, waveT = 0.0f;
    bool playerAlive = true, hitKill = false;
    int kills = 0, wave = 1;
    float doorW = 0.0f; // empurrando a porta (mão ou ombro)
    Ragdoll playerRag;  // corpo mole quando o jogador morre
    // cobertura (Q): colado numa caixa/parede; agacha atrás das baixas, espia pela quina nas altas
    bool coverOn = false, coverEntering = false, qWas = false, coverHint = false;
    // animação de entrar/sair da cobertura tocando: 1 = entrando, -1 = saindo
    int coverTrans = 0;
    float transT = 0.0f, transYaw = 0.0f, transYawFrom = 0.0f;
    float transClipYaw = 0.0f; // giro do clipe neste quadro (vai para a direção do personagem, não para a pose)
    // parkour: pulo correndo (clipe sincronizado com o voo) e salto por cima de obstáculos
    bool runJumpOn = false, vaultOn = false;
    float runJumpRate = 1.0f;     // velocidade do clipe no ar (o trecho no ar dura o mesmo que o voo)
    float runJumpT = 0.0f, vaultT = 0.0f, vaultT0 = 0.0f, vaultSpeed = 0.0f, vaultYaw = 0.0f;
    glm::vec3 vaultDir(0.0f, 0.0f, 1.0f);
    glm::vec3 vaultPlant(0.0f); // onde a mão esquerda apoia no topo do obstáculo (com arma na mão)
    auto frameTime = [](const MocapClip& c, float f) { return f / (float)c.frames.size() * c.duration; };
    // começa o Running Jump: o trecho no ar do clipe é esticado para durar o voo inteiro (2 vy / g)
    auto startRunJump = [&](float vy) {
        if (!runJumpClip.valid()) return;
        float clipAir = frameTime(runJumpClip, kRunJumpLand) - frameTime(runJumpClip, kRunJumpTakeoff);
        runJumpRate = std::clamp(clipAir / (2.0f * vy / kGravity), 0.4f, 1.5f);
        runJumpOn = true;
        runJumpT = frameTime(runJumpClip, kRunJumpTakeoff - 1.5f); // um pouco antes do impulso (entra suave)
    };
    // pose do clipe no tempo t, tocando uma vez (sem voltar ao começo)
    auto sampleOnce = [](const MocapClip& c, float t) {
        const int n = (int)c.frames.size();
        float f = std::clamp(t / c.duration, 0.0f, 1.0f) * (n - 1);
        int i0 = std::min((int)f, n - 1), i1 = std::min(i0 + 1, n - 1);
        return blendPose(c.frames[i0], c.frames[i1], f - i0);
    };
    // Obstáculo na frente, medido pelo perfil de altura do chão ao longo do caminho (de 10 em 10 cm),
    // no centro e nos dois lados do corpo. 0 = nada, 1 = salto de parkour (65 cm a 1,15 m),
    // 2 = baixo (só passa a perna por cima com um pulinho). Recusa escada (continua subindo),
    // coisas compridas (mesa de lado), a ponta do obstáculo e o que não tem chão livre do outro lado.
    auto findVault = [&](const glm::vec3& pos, const glm::vec3& dir, float& faceDist, float& height) {
        const glm::vec3 side(-dir.z, 0.0f, dir.x);
        const float step = 0.1f, range = 3.2f;
        float face = 1e9f, end = -1.0f, top = 0.0f;
        for (float off : {0.0f, -0.25f, 0.25f}) {
            float f = -1.0f, e = -1.0f, h = 0.0f;
            bool after = false; // já passou do obstáculo
            for (float d = step; d <= range; d += step) {
                glm::vec3 p = pos + dir * d + side * off;
                float g = world.groundHeightAt(p.x, p.z, pos.y + 3.0f) - pos.y;
                bool solid = g > kStepHeight - 0.02f;
                if (solid && after) return 0; // subiu de novo depois: escada / mais obstáculos
                if (solid) {
                    if (f < 0.0f) f = d;
                    e = d;
                    h = std::max(h, g);
                } else if (f >= 0.0f) {
                    after = true;
                    if (d - e > 1.1f) break; // chão livre o bastante para cair
                }
            }
            if (f < 0.0f || !after) return 0;             // não tem obstáculo (ou não acaba): nada
            if (off != 0.0f && std::fabs(f - face) > 0.25f) return 0; // ponta / quina: não é o mesmo obstáculo
            face = std::min(face, f);
            end = std::max(end, e);
            top = std::max(top, h);
        }
        float depth = end - face + step;
        if (depth > 1.3f || top > 1.15f) return 0;
        // espaço livre por cima para o corpo passar
        glm::vec3 over = pos + glm::vec3(0.0f, top + 0.45f, 0.0f);
        if (world.raycast(over, dir, end + 1.0f) < end + 1.0f) return 0;
        faceDist = face - step * 0.5f;
        height = top;
        return top >= 0.65f ? 1 : 2;
    };
    if (args.vaultTest) { // depuração do parkour: o que ele faria em cada ponto
        const struct { const char* name; glm::vec3 pos, dir; } tests[] = {
            {"escada", {6.5f, 0, 4.0f}, {0, 0, 1}},          {"azul baixa 0.5", {-4.5f, 0, 4.0f}, {0, 0, 1}},
            {"azul 0.8", {-4.5f, 0, 7.0f}, {0, 0, 1}},       {"mureta arena", {-7.0f, 0, 30.0f}, {0, 0, 1}},
            {"ponta da mureta", {-8.45f, 0, 30.0f}, {0, 0, 1}}, {"mesa (lado comprido)", {2.2f, 0, 1.6f}, {0, 0, 1}},
            {"mesa (pela ponta)", {0.3f, 0, 3.0f}, {1, 0, 0}},
        };
        if (getenv("GA_BOXES")) // depuração: caixas de colisão numa região (x0 z0 x1 z1)
            {
                float r[4];
                std::sscanf(getenv("GA_BOXES"), "%f %f %f %f", &r[0], &r[1], &r[2], &r[3]);
                for (const Box& b : world.boxes)
                    if (b.max.x > r[0] && b.min.x < r[2] && b.max.z > r[1] && b.min.z < r[3] && b.max.y > -10.0f)
                        std::printf("[caixa] x %.2f..%.2f y %.2f..%.2f z %.2f..%.2f %s\n", b.min.x, b.max.x, b.min.y,
                                    b.max.y, b.min.z, b.max.z, b.solid ? "" : "(so raios)");
            }
        for (auto& t : tests) {
            float face = 0, h = 0;
            int k = findVault(t.pos, glm::normalize(t.dir), face, h);
            std::printf("[parkour] %-22s -> %s (frente %.2f m, altura %.2f m)\n", t.name,
                        k == 1 ? "SALTO" : k == 2 ? "pulinho" : "nada", face, h);
        }
        for (int i = 0; i < SceneCount; ++i) { // layout: salvar e ler de volta tem que dar o mesmo texto
            LevelLayout copy;
            copy.parse(layouts[i].serialize());
            std::printf("[layout] %s: ida e volta %s\n", sceneName(i), copy.serialize() == layouts[i].serialize() ? "ok" : "DIFERENTE");
        }
        glfwTerminate();
        return 0;
    }
    const MocapClip* transClip = nullptr;
    glm::vec3 transFrom(0.0f), transTo(0.0f);
    CoverFace coverF;
    float coverCrouch = 0.0f, coverPeek = 0.0f, peekSide = 1.0f, peekEdge = 0.0f;
    float coverAnimW = 0.0f, coverPhase = 0.0f; // peso e fase do clipe de cobertura
    int coverSide = 0;                           // lado pedido: 0 = indo para a esquerda, 1 = direita
    float coverSideW = 0.0f;                     // mistura entre os clipes (0 = esquerda, 1 = direita), suave
    Door::Touch doorTouch;

    // editor de armas: o jogo fica pausado; o personagem mostra a arma escolhida no modo escolhido
    bool editorOpen = false;
    menu::GunEditor ed;
    float edPanel = 0.0f;                      // fração da tela ocupada pelo painel
    float edYaw = 0.0f, edPitch = 0.15f, edDist = 2.2f; // câmera em volta do personagem (yaw relativo a ele)
    int edLastMode = -1;
    bool edDragging = false;
    Loadout savedLoadout;
    GunHandling savedHandling;
    Animator previewAnim(skeleton);
    previewAnim.setClips(&walkClip, &runClip);
    const char* gunNames[3];
    bool gunIsRifle[3];
    for (int i = 0; i < 3; ++i) { gunNames[i] = defs[i]->name.c_str(); gunIsRifle[i] = defs[i]->rifle; }
    auto setEditor = [&](bool open) {
        if (open == editorOpen) return;
        editorOpen = open;
        if (open) {
            savedLoadout = loadout;
            savedHandling = handling;
            handling.cancel();
            ed.dirty = false;
            ed.status.clear();
            edLastMode = -1;
        } else {
            loadout = savedLoadout;
            handling = savedHandling;
            if (ed.dirty) loadGunTuning(weaponsPath); // fechou sem aplicar: volta ao que estava salvo
            ed.dirty = false;
            fillGuns(glm::mat4(1.0f), lastGuns);
        }
    };

    // editor do jogo (abre direto na versão de desenvolvimento; menu > Edição livre)
    FreeEditor freeEdit;
    bool playFromEditor = false;  // jogando a partir do editor: Esc para e volta
    bool gunFromEditor = false;   // editor da arma aberto pelo editor: ao fechar, volta para ele
    bool editorSettings = false;  // menu de configurações aberto por cima do editor
    auto freeEditContext = [&]() {
        LevelLayout& L = layouts[sceneId];
        FreeEditor::Context c{L, world, scene.props, {}, L.hasDoor ? &arena.door : nullptr, root, sceneId};
        for (const PropPlace& p : L.props)
            c.propMeshes.push_back(!p.asset.empty() ? assets.mesh(p.asset)
                                   : sceneId == SceneTest ? &tableMesh : official.mesh(p.name));
        c.assets = &assets;
        c.terrain = &terrain;
        c.paint = &paint;
        for (int i = 0; i < 3; ++i) {
            c.gunNames.push_back(defs[i]->name);
            c.gunFiles.push_back(defs[i]->file);
            const GunItem& it = loadout.items[i];
            c.gunPos.push_back(it.groundPos);
            c.gunYaw.push_back(it.groundYaw);
        }
        return c;
    };
    // sai da edição: o cenário fica como foi editado e o jogo volta (do início do cenário)
    auto exitFreeEdit = [&]() {
        freeEdit.end();
        setupScene(sceneId);
        coverOn = coverEntering = runJumpOn = vaultOn = false;
        coverTrans = 0;
        transferT = 1.0f;
        fillGuns(glm::mat4(1.0f), lastGuns);
        // fecha o menu e volta a capturar o mouse
        menuOpen = false;
        mouseCaptured = true;
        glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        glfwGetCursorPos(win, &lastMx, &lastMy);
    };

#if GA_EDITOR
    if (args.freeEdit) {
        glm::vec3 back(std::sin(charYaw), 0.0f, std::cos(charYaw));
        glm::vec3 p = body.pos - back * 4.0f + glm::vec3(0.0f, 2.5f, 0.0f);
        float y = charYaw, pt = 0.35f;
        if (const char* c = getenv("GA_CAM")) std::sscanf(c, "%f %f %f %f %f", &p.x, &p.y, &p.z, &y, &pt); // depuração
        freeEdit.begin(p, y, pt, root);
    }
#endif
    while (!glfwWindowShouldClose(win)) {
        double now = glfwGetTime();
        float realDt = (float)std::min(now - lastTime, 0.1);
        lastTime = now;
        glfwPollEvents();

        // --- entrada: Esc abre/fecha o menu (o jogo pausa enquanto ele está aberto)
        bool esc = glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS;
        auto setMenu = [&](bool open) {
            menuOpen = open;
            mouseCaptured = !open;
            glfwSetInputMode(win, GLFW_CURSOR, open ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
            glfwGetCursorPos(win, &lastMx, &lastMy);
            if (!open) gfx.save(settingsPath);
        };
        if (esc && !escWas && !freeEdit.active()) { // na edição livre o Esc é do editor
            if (editorOpen) setEditor(false); // Esc no editor volta ao menu
            else if (playFromEditor) { // Parar: volta ao editor com o cenário do começo
                playFromEditor = false;
                setupScene(sceneId);
                playerRag.stop();
                playerAlive = true;
                health = 100.0f;
                freeEdit.resume();
                menuOpen = true;
                mouseCaptured = false;
                glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            } else setMenu(!menuOpen);
        }
        escWas = esc;
        const float dt = menuOpen ? 0.0f : realDt; // tempo de jogo
        gameTime += dt;
        if (gfx.dayCycle && dt > 0.0f) {
            gfx.timeOfDay += dt * gfx.dayCycleSpeed / 60.0f;
            if (gfx.timeOfDay > 19.0f) gfx.timeOfDay = 5.5f;
        }
        bool lmb = !menuOpen && glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        bool rmb = mouseCaptured && glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        bool lmbPressed = lmb && !lmbWas;
        lmbWas = lmb;
        if (!mouseCaptured && !menuOpen && lmbPressed) {
            glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            mouseCaptured = true;
            glfwGetCursorPos(win, &lastMx, &lastMy);
            lmbPressed = false; // esse clique só recaptura o mouse
        }
        double mx, my;
        glfwGetCursorPos(win, &mx, &my);
        if (mouseCaptured) {
            float sens = 0.0025f + (0.0014f - 0.0025f) * cam.aimBlend;
            cam.yaw -= (float)(mx - lastMx) * sens;
            cam.pitch = std::clamp(cam.pitch + (float)(my - lastMy) * sens, -0.9f, 1.2f);
        }
        const double prevMx = lastMx, prevMy = lastMy;
        lastMx = mx;
        lastMy = my;
        if (editorOpen) {
            // câmera do editor: arrastar com o botão esquerdo fora do painel gira; roda aproxima
            bool lmbDown = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            int ww, wh;
            glfwGetWindowSize(win, &ww, &wh);
            if (lmbDown && !edDragging && mx < ww * (1.0f - edPanel)) edDragging = true;
            if (!lmbDown) edDragging = false;
            if (edDragging) {
                edYaw -= (float)(mx - prevMx) * 0.008f;
                edPitch = std::clamp(edPitch + (float)(my - prevMy) * 0.006f, -1.2f, 1.3f);
            }
            if (mx < ww * (1.0f - edPanel)) edDist = std::clamp(edDist - g_scroll * 0.2f, 0.6f, 6.0f);
        }
        if (!menuOpen) cam.targetDist = std::clamp(cam.targetDist - g_scroll * 0.4f, 1.6f, 9.0f);
        g_scroll = 0.0f;

        auto key = [&](int k) { return !menuOpen && playerAlive && glfwGetKey(win, k) == GLFW_PRESS ? 1.0f : 0.0f; };
        bool f1 = key(GLFW_KEY_F1) > 0;
        if (f1 && !f1Was) debugWeights = !debugWeights;
        f1Was = f1;
        bool f2 = key(GLFW_KEY_F2) > 0;
        if (f2 && !f2Was) footIK.enabled = !footIK.enabled;
        f2Was = f2;
        if (key(GLFW_KEY_R) > 0) {
            if (handling.locksMovement()) handling.cancel();
            body.pos = glm::vec3(0.0f);
            body.vel = glm::vec3(0.0f);
            visualY = 0.0f;
            footIK.reset();
        }

        if (editorOpen) {
            // só a arma escolhida, no lugar do modo escolhido
            const GunDef* d = defs[ed.gun];
            int slot = d->rifle ? (ed.mode == 2 ? ed.slot : 1) : 0;
            GunItem it;
            it.def = d;
            it.slot = slot;
            it.place = ed.mode == 2 ? GunPlace::Body : GunPlace::Hand;
            loadout = Loadout();
            loadout.items.push_back(it);
            loadout.slots[slot] = 0;
            loadout.hand = ed.mode == 2 ? -1 : 0;
        }

        // --- armas: pegar (E), sacar/guardar/trocar (1, 2, 3), soltar (G), mirar (botão direito),
        //     atirar (botão esquerdo; fuzis atiram enquanto segura)
        bool eKey = key(GLFW_KEY_E) > 0, gKey = key(GLFW_KEY_G) > 0;
        const bool idle = !handling.busy() && next == Next::None;
        auto stowHand = [&]() { handling.startStow(loadout.hand, loadout.items[loadout.hand].slot); };
        auto startPick = [&](int i) {
            if (loadout.items[i].onTable) handling.startPickUp(i); // anda até a mesa e pega com a mão
            else loadout.toHand(i);                               // do chão: pega direto
        };
        int pick = idle ? loadout.pickable(body.pos) : -1;
        if (eKey && !eWas && pick >= 0) {
            if (loadout.hand >= 0) { stowHand(); next = Next::PickUp; nextItem = pick; }
            else startPick(pick);
        }
        for (int sl = 0; sl < kSlotCount; ++sl) {
            bool k = key(GLFW_KEY_1 + sl) > 0;
            int it = loadout.slots[sl];
            if (k && !numWas[sl] && idle && it >= 0) {
                if (loadout.hand == it) stowHand();                                     // mesma arma: guarda
                else if (loadout.hand >= 0) { stowHand(); next = Next::Draw; nextItem = it; } // outra: troca
                else handling.startDraw(it, sl);                                        // mão livre: saca
            }
            numWas[sl] = k;
        }
        if (gKey && !gWas && idle && loadout.hand >= 0) {
            glm::vec3 p = body.pos + glm::vec3(std::sin(charYaw), 0.0f, std::cos(charYaw)) * 0.7f;
            p.y = world.groundHeightAt(p.x, p.z, body.pos.y + 0.5f);
            loadout.drop(loadout.hand, p, charYaw + 1.3f);
            aimW = 0.0f;
        }
        eWas = eKey;
        gWas = gKey;
        // depois de guardar, continua a troca
        if (next != Next::None && !handling.busy()) {
            hitch += dt;
            if (hitch > 0.06f) {
                if (next == Next::Draw) handling.startDraw(nextItem, loadout.items[nextItem].slot);
                else startPick(nextItem);
                next = Next::None;
                hitch = 0.0f;
            }
        }
        const GunDef* held = loadout.hand >= 0 ? loadout.items[loadout.hand].def : nullptr;
        bool armed = held && !handling.busy() && playerAlive;
        if (armed && mouseCaptured && (lmbPressed || (held->automatic && lmb))) {
            fireQueued = true;
            hipFireTimer = 0.8f;
        }
        if (!armed) fireQueued = false;
        hipFireTimer -= dt;
        if (!rmb && rmbWas) hipFireTimer = 0.0f; // soltou a mira: volta na hora (mesmo depois de atirar)
        rmbWas = rmb;
        bool aiming = armed && (rmb || hipFireTimer > 0.0f);
        cam.kick = approach(cam.kick, 0.0f, 9.0f, dt);

        glm::vec3 camF(std::sin(cam.yaw), 0.0f, std::cos(cam.yaw));
        glm::vec3 camR(-std::cos(cam.yaw), 0.0f, std::sin(cam.yaw));
        glm::vec3 move = camF * (key(GLFW_KEY_W) - key(GLFW_KEY_S)) + camR * (key(GLFW_KEY_D) - key(GLFW_KEY_A));
        if (glm::length(move) > 1e-3f) move = glm::normalize(move);
        else move = glm::vec3(0.0f);
        // velocidades iguais às da captura -> passada sem deslizar; mirando só anda
        bool run = !aiming && (key(GLFW_KEY_LEFT_SHIFT) > 0 || key(GLFW_KEY_RIGHT_SHIFT) > 0);
        float wishSpeed = run ? animator.runSpeed() : animator.walkSpeed();
        bool jump = key(GLFW_KEY_SPACE) > 0;
        float standDist = 0.0f;
        if (handling.locksMovement()) {
            // pegando da mesa: caminha sozinho até a borda e fica parado durante o gesto
            jump = false;
            move = glm::vec3(0.0f);
            const GunItem& target = loadout.items[handling.item()];
            glm::vec3 d = target.standPos - body.pos;
            d.y = 0.0f;
            standDist = glm::length(d);
            if (handling.approaching()) {
                if (standDist < 0.04f || handling.approachTime > 1.5f) handling.begin();
                else {
                    move = d / standDist;
                    wishSpeed = std::min(animator.walkSpeed(), 0.25f + standDist * 3.0f);
                }
            }
        }

        // --- cobertura: Q entra/sai; dentro anda de lado colado (A/D); puxar para trás sai
        const float coverOff = body.radius + 0.06f;
        {
            bool qKey = key(GLFW_KEY_Q) > 0;
            CoverFace cand;
            bool free = !coverOn && !coverEntering && coverTrans == 0 && playerAlive && !handling.locksMovement() &&
                        body.grounded;
            coverHint = free && findPlayerCover(world, body.pos, cam.forward, cand);
            // começa uma transição: o clipe de entrar/sair toca uma vez
            auto startTrans = [&](int dir) {
                coverTrans = dir;
                transT = 0.0f;
                transClip = &(dir > 0 ? coverIn : coverOut)[coverF.low ? 1 : 0];
                if (!transClip->valid()) { coverTrans = 0; transClip = nullptr; }
            };
            if (qKey && !qWas && coverTrans == 0) {
                if (coverOn) {
                    startTrans(-1);
                    coverOn = false;
                    transYaw = charYaw;
                } else if (coverHint) {
                    // entra: vira de costas para a parede pela animação enquanto desliza até ela
                    coverF = cand;
                    transFrom = body.pos;
                    transTo = coverF.pointAt(std::clamp(coverF.along(body.pos), coverF.s0 + 0.25f, coverF.s1 - 0.25f), coverOff);
                    transTo.y = body.pos.y;
                    transYaw = std::atan2(coverF.n.x, coverF.n.z);
                    coverEntering = true;
                    startTrans(1);
                    // o primeiro quadro do clipe fica virado para onde o personagem já está (não pula)
                    if (coverTrans) transYawFrom = wrapAngle(charYaw - poseYaw(transClip->frames.front()));
                    if (!coverTrans) { body.pos = transTo; coverEntering = false; coverOn = true; } // sem clipe: gruda direto
                }
            }
            qWas = qKey;
            if (handling.locksMovement() || !playerAlive) { coverOn = coverEntering = false; coverTrans = 0; }
        }
        float edgeDist = 1e9f, edgeSide = 1.0f;
        if (coverTrans != 0) { // durante a animação de entrar/sair não anda nem pula
            move = glm::vec3(0.0f);
            jump = false;
            aiming = false;
            fireQueued = false;
        }
        if (coverOn) {
            jump = false;
            if (glm::dot(move, coverF.n) > 0.75f) {
                // puxou para longe da parede: sai com a animação
                coverOn = false;
                coverTrans = -1;
                transT = 0.0f;
                transYaw = charYaw;
                transClip = &coverOut[coverF.low ? 1 : 0];
                if (!transClip->valid()) coverTrans = 0;
            } else {
                float sNow = coverF.along(body.pos);
                float ms = glm::dot(move, coverF.t);
                if ((ms > 0.0f && sNow >= coverF.s1 - 0.25f) || (ms < 0.0f && sNow <= coverF.s0 + 0.25f)) ms = 0.0f;
                move = std::fabs(ms) > 0.3f ? coverF.t * (ms > 0.0f ? 1.0f : -1.0f) : glm::vec3(0.0f);
                const MocapClip& c = (coverF.low ? coverLow : coverStand)[coverSide];
                wishSpeed = c.valid() ? c.naturalSpeed() : 1.3f; // passada do clipe, sem deslizar
                edgeDist = std::min(sNow - coverF.s0, coverF.s1 - sNow);
                edgeSide = coverF.s1 - sNow < sNow - coverF.s0 ? 1.0f : -1.0f;
            }
        }
        // cobertura alta: só dá para mirar perto da quina (espia para fora); baixa: levanta e mira por cima
        if (coverOn && !coverF.low && edgeDist > 0.8f) { aiming = false; fireQueued = false; }
        aimW = approach(aimW, aiming ? 1.0f : 0.0f, aiming ? 16.0f : 8.0f, dt);
        cam.aimBlend = approach(cam.aimBlend, armed && rmb && aiming ? 1.0f : 0.0f, 11.0f, dt);

        // --- parkour: correndo de frente para um obstáculo baixo, salta por cima sozinho
        const float hSpeed = glm::length(glm::vec2(body.vel.x, body.vel.z));
        if (!vaultOn && vaultClip.valid() && run && body.grounded && hSpeed > 3.0f && glm::length(move) > 0.5f &&
            !coverOn && coverTrans == 0 && !handling.busy() && playerAlive) {
            glm::vec3 dir = glm::normalize(glm::vec3(body.vel.x, 0.0f, body.vel.z));
            float face, height;
            int kind = glm::dot(dir, move) > 0.8f ? findVault(body.pos, dir, face, height) : 0;
            if (kind == 2 && face > 0.35f && face < 0.9f && body.grounded) {
                // baixo: pulinho só para passar a perna por cima (altura do obstáculo + folga)
                body.vel.y = std::sqrt(2.0f * kGravity * (height + 0.2f));
                body.grounded = false;
                startRunJump(body.vel.y);
            }
            if (kind == 1 && face > 0.45f && face < 1.4f) {
                // começa o clipe de modo que o pé saia do chão uns 30 cm antes do obstáculo
                float perFrame = glm::length(vaultClip.travel) / (float)vaultClip.frames.size();
                float startFrame = std::max(0.0f, kVaultTakeoff - (face - 0.3f) / perFrame);
                vaultOn = true;
                vaultT = vaultT0 = frameTime(vaultClip, startFrame);
                vaultDir = dir;
                // apoio da mão esquerda: logo depois da borda, um pouco à esquerda do corpo
                glm::vec3 left(dir.z, 0.0f, -dir.x); // esquerda de quem olha para 'dir'
                vaultPlant = body.pos + dir * (face + 0.15f) + left * 0.2f;
                vaultPlant.y = body.pos.y + height + 0.01f;
                vaultYaw = std::atan2(dir.x, dir.z);
                vaultSpeed = glm::length(vaultClip.travel) / vaultClip.duration;
                runJumpOn = false;
            }
        }
        // correndo, o pulo é mais baixo e longo: o voo dura o mesmo que no clipe Running Jump
        body.jumpSpeed = run && hSpeed > 3.0f && runJumpClip.valid() ? 5.9f : kJumpSpeed; // ~0,8 m de altura

        // --- física em passo fixo
        bool landed = false, jumped = false;
        float impact = 0.0f;
        accumulator += dt;
        if (vaultOn) { // saltando o obstáculo: o corpo segue o clipe (passa por cima, sem colidir)
            accumulator = 0.0f;
            body.pos += vaultDir * vaultSpeed * dt;
            body.vel = vaultDir * vaultSpeed;
            vaultT += dt;
            if (vaultT >= vaultClip.duration - 0.05f) {
                vaultOn = false;
                body.vel = vaultDir * animator.runSpeed();
            }
        }
        while (accumulator >= fixedDt) {
            body.step(world, move, wishSpeed, jump, fixedDt);
            stepBalls(world, body, fixedDt);
            if (body.justLanded) { landed = true; impact = std::max(impact, body.landImpact); }
            if (body.justJumped) jumped = true;
            accumulator -= fixedDt;
        }
        if (jumped && body.jumpSpeed < kJumpSpeed) startRunJump(body.jumpSpeed); // pulou correndo: Running Jump
        if (runJumpOn) {
            // no ar o clipe anda no ritmo do voo; se o voo demorar mais (caindo de algum lugar), segura
            // a pose de queda; tocou o chão: segue da aterrissagem em velocidade normal
            float landT = frameTime(runJumpClip, kRunJumpLand);
            if (!body.grounded) runJumpT = std::min(runJumpT + dt * runJumpRate, landT - 0.04f);
            else runJumpT = std::max(runJumpT, landT - 0.04f) + dt;
            if (runJumpT >= runJumpClip.duration || coverOn || vaultOn) runJumpOn = false;
        }
        if (coverOn) { // continua colado na parede
            float d = coverF.dist(body.pos);
            body.pos += coverF.n * (coverOff - d);
        }
        // transição: o corpo acompanha o deslocamento do clipe
        float transW = 0.0f;
        Pose transPose;
        if (coverTrans != 0 && transClip) {
            float prevT = transT;
            transT = std::min(transT + dt, transClip->duration);
            float u0 = prevT / transClip->duration, u1 = transT / transClip->duration;
            if (coverTrans > 0) { // entrando: desliza até o ponto da parede
                glm::vec3 p = glm::mix(transFrom, transTo, glm::smoothstep(0.0f, 0.85f, u1));
                body.pos.x = p.x;
                body.pos.z = p.z;
            } else {              // saindo: afasta da parede como no clipe
                glm::vec3 step = glm::angleAxis(transYaw, glm::vec3(0, 1, 0)) * transClip->travel * (u1 - u0);
                body.pos.x += step.x;
                body.pos.z += step.z;
            }
            body.vel.x = body.vel.z = 0.0f;
            // pose do clipe (uma vez, sem voltar ao começo), entrando e saindo misturada
            const int n = (int)transClip->frames.size();
            float f = u1 * (n - 1);
            int i0 = std::min((int)f, n - 1), i1 = std::min(i0 + 1, n - 1);
            transPose = blendPose(transClip->frames[i0], transClip->frames[i1], f - i0);
            // o giro do clipe sai da pose e entra na direção do personagem: misturando com as outras
            // animações (todas viradas para a frente) o corpo não dá meia-volta
            transClipYaw = poseYaw(transPose);
            {
                glm::quat q = glm::angleAxis(-transClipYaw, glm::vec3(0, 1, 0));
                transPose.rot[HIPS] = glm::normalize(q * transPose.rot[HIPS]);
                transPose.rootOffset = q * transPose.rootOffset;
            }
            float left = transClip->duration - transT;
            float in = glm::smoothstep(0.0f, 0.2f, transT), out = glm::smoothstep(0.0f, 0.3f, left);
            transW = in * out;
            if (transT >= transClip->duration) {
                if (coverTrans > 0) { coverEntering = false; coverOn = true; }
                else charYaw = wrapAngle(transYaw + poseYaw(transClip->frames.back())); // continua virado como o clipe terminou
                coverTrans = 0;
                transW = 0.0f;
            }
        }

        // --- direção do corpo: mirando, fica de frente para a mira; senão, vira para o movimento
        float prevYaw = charYaw;
        bool moving = glm::length(move) > 0.1f;
        if (vaultOn) {
            charYaw = vaultYaw;
        } else if (coverTrans > 0 && transClip) {
            // entrando: do jeito que estava até de costas para a parede no fim (o resto do giro vem do clipe)
            float u = glm::smoothstep(0.0f, 1.0f, transT / transClip->duration);
            charYaw = wrapAngle(transYawFrom + wrapAngle(transYaw - transYawFrom) * u + transClipYaw);
        } else if (coverTrans != 0) {
            charYaw = wrapAngle(transYaw + transClipYaw); // saindo: o corpo gira como no clipe
        } else if (aiming) {
            charYaw = wrapAngle(charYaw + wrapAngle(cam.yaw - charYaw) * (1.0f - std::exp(-18.0f * dt)));
        } else if (coverOn) { // de costas para a cobertura
            float faceYaw = std::atan2(coverF.n.x, coverF.n.z);
            charYaw = wrapAngle(charYaw + wrapAngle(faceYaw - charYaw) * (1.0f - std::exp(-12.0f * dt)));
        } else if (handling.locksMovement() && (!moving || standDist < 0.35f)) {
            // de frente para a mesa
            float faceYaw = loadout.items[handling.item()].standYaw;
            charYaw = wrapAngle(charYaw + wrapAngle(faceYaw - charYaw) * (1.0f - std::exp(-10.0f * dt)));
        } else if (moving) {
            float target = std::atan2(move.x, move.z);
            float rate = body.grounded ? 11.0f : 4.0f;
            charYaw = wrapAngle(charYaw + wrapAngle(target - charYaw) * (1.0f - std::exp(-rate * dt)));
        }
        float turnRate = dt > 0.0f ? wrapAngle(charYaw - prevYaw) / dt : 0.0f;

        AnimInput ai;
        ai.dt = dt;
        ai.speed = glm::length(glm::vec2(body.vel.x, body.vel.z));
        ai.verticalVel = body.vel.y;
        ai.turnRate = aiming || coverOn ? 0.0f : turnRate;
        ai.grounded = body.grounded;
        ai.landed = landed;
        ai.landImpact = impact;
        if (aiming && moving) {
            // andando de lado/de costas mirando: pernas na direção do movimento
            float rel = wrapAngle(std::atan2(move.x, move.z) - charYaw);
            ai.backwards = std::fabs(rel) > 1.75f;
            ai.legYaw = std::clamp(ai.backwards ? wrapAngle(rel - (float)M_PI) : rel, -1.7f, 1.7f);
        }
        animator.update(ai);

        // A cápsula sobe degraus "de uma vez"; o corpo visível sobe suave como numa rampa.
        if (body.grounded) {
            visualY += (body.pos.y - visualY) * (1.0f - std::exp(-12.0f * dt));
            visualY = std::clamp(visualY, body.pos.y - 0.45f, body.pos.y + 0.45f);
        } else {
            visualY = body.pos.y;
        }
        glm::vec3 visualPos(body.pos.x, visualY, body.pos.z);
        // agachado atrás da baixa (levanta ao mirar); espiando pela quina da alta
        coverCrouch = approach(coverCrouch, coverOn && coverF.low && !aiming ? 0.5f : 0.0f, 10.0f, dt);
        bool peeking = coverOn && !coverF.low && aiming;
        if (peeking) { peekSide = edgeSide; peekEdge = std::min(edgeDist, 0.8f); }
        coverPeek = approach(coverPeek, peeking ? 1.0f : 0.0f, 10.0f, dt);
        // costas encostadas: a cápsula fica afastada (raio), o corpo visível encosta na parede
        if (coverAnimW > 0.001f) visualPos -= coverF.n * (coverF.low ? 0.14f : 0.17f) * coverAnimW;
        // espiando: o corpo fica atrás da quina; só ombro, cabeça e arma passam para fora
        // espiando para a esquerda do personagem a arma (destra) fica do lado da parede: sai um pouco mais
        bool peekLeft = glm::dot(coverF.t * peekSide, glm::vec3(-std::cos(charYaw), 0.0f, std::sin(charYaw))) < 0.0f;
        if (coverPeek > 0.001f)
            visualPos += (coverF.t * peekSide * (std::max(0.0f, peekEdge - 0.12f) + (peekLeft ? 0.22f : 0.0f)) +
                          coverF.n * (peekLeft ? 0.14f : 0.08f)) * coverPeek;
        // quina à esquerda da câmera: câmera vai para o ombro esquerdo (senão mira de dentro da parede)
        {
            glm::vec3 camRight(-std::cos(cam.yaw), 0.0f, std::sin(cam.yaw));
            bool leftEdge = glm::dot(coverF.t * peekSide, camRight) < 0.0f;
            cam.side = approach(cam.side, coverPeek > 0.3f && leftEdge ? -1.0f : 1.0f, 8.0f, dt);
        }
        glm::mat4 charModel = glm::translate(glm::mat4(1.0f), visualPos) *
                              glm::rotate(glm::mat4(1.0f), charYaw, glm::vec3(0, 1, 0));
        Pose& pose = animator.pose();
        // na cobertura: clipe de andar de lado de costas para a parede (agachado na baixa);
        // parado segura a pose; mirando volta para a pose normal de mira
        coverAnimW = approach(coverAnimW, (coverOn || coverTrans > 0) && !aiming ? 1.0f : 0.0f, 8.0f, dt);
        if (coverAnimW > 0.001f) {
            const MocapClip* clips = coverF.low ? coverLow : coverStand;
            glm::vec3 charLeft(std::cos(charYaw), 0.0f, -std::sin(charYaw)); // esquerda do personagem = +X
            float side = glm::dot(glm::vec3(body.vel.x, 0.0f, body.vel.z), charLeft);
            if (std::fabs(side) > 0.15f) coverSide = side > 0.0f ? 0 : 1;
            // troca de lado sem tranco: os dois clipes tocam na mesma fase e a mistura vai de um para o outro
            coverSideW = approach(coverSideW, (float)coverSide, 7.0f, dt);
            const MocapClip &cl = clips[0], &cr = clips[1];
            if (cl.valid() && cr.valid()) {
                float cycle = glm::mix(cl.cycleLength, cr.cycleLength, coverSideW);
                float advance = std::fabs(side) / cycle * dt;
                if (glm::length(move) < 0.1f) {
                    // soltou a tecla: vai rápido (para frente ou para trás) até a pose de parado mais
                    // próxima, com os pés no chão, e para ali
                    const std::vector<float>& idle = coverIdle[(coverF.low ? 2 : 0) + coverSide];
                    float best = 0.0f, bestAbs = 1.0f;
                    for (float ph : idle) {
                        float d = std::fmod(ph - coverPhase + 2.5f, 1.0f) - 0.5f; // -0.5..0.5
                        if (std::fabs(d) < bestAbs) { bestAbs = std::fabs(d); best = d; }
                    }
                    float maxStep = 2.5f * dt; // ~0,2 s para acertar os pés
                    advance = std::clamp(best, -maxStep, maxStep);
                }
                coverPhase = std::fmod(coverPhase + advance + 1.0f, 1.0f);
                Pose cp = blendPose(cl.sample(coverPhase), cr.sample(coverPhase), coverSideW);
                pose = blendPose(pose, cp, coverAnimW);
            }
        }
        if (transW > 0.001f) pose = blendPose(pose, transPose, transW); // entrando/saindo da cobertura
        // parkour: pose dos clipes por cima da animação (entra e sai misturando)
        if (runJumpOn) {
            // entra em ~0,15 s a partir do começo e sai nos últimos 0,3 s (aterrissagem -> corrida)
            float t0 = frameTime(runJumpClip, kRunJumpTakeoff - 1.5f);
            float w = glm::smoothstep(0.0f, 0.3f, runJumpClip.duration - runJumpT) *
                      glm::smoothstep(t0, t0 + 0.15f * runJumpRate, runJumpT);
            pose = blendPose(pose, sampleOnce(runJumpClip, runJumpT), w);
        }
        if (vaultOn) {
            float w = glm::smoothstep(0.0f, 0.2f, vaultClip.duration - vaultT) * glm::smoothstep(0.0f, 0.12f, vaultT - vaultT0);
            pose = blendPose(pose, sampleOnce(vaultClip, vaultT), w);
        }
        footIK.apply(skeleton, pose, charModel, world, animator.groundedWeight(), dt);
        if (coverPeek > 0.001f) {
            // tronco inclinado para o lado da quina (expõe o mínimo)
            glm::vec3 charRight(-std::cos(charYaw), 0.0f, std::sin(charYaw));
            bool right = glm::dot(coverF.t * peekSide, charRight) > 0.0f;
            float lean = (right ? 0.22f : -0.38f) * coverPeek; // para a esquerda inclina mais (tira a arma da parede)
            pose.rot[SPINE] = pose.rot[SPINE] * glm::angleAxis(lean * 0.4f, glm::vec3(0, 0, 1));
            pose.rot[CHEST] = pose.rot[CHEST] * glm::angleAxis(lean * 0.6f, glm::vec3(0, 0, 1));
        }

        // --- câmera, mira e pose de tiro
        cam.update(world, visualPos - glm::vec3(0.0f, coverCrouch * 0.6f, 0.0f), dt);
        glm::vec3 aimPoint = cam.aimPoint(world);
        {   // soldado na frente da mira: o ponto mirado é nele
            Squad::Hit h = squad.raycast(cam.pos, cam.forward, glm::length(aimPoint - cam.pos));
            if (h.soldier >= 0) aimPoint = cam.pos + cam.forward * h.t;
        }
        int moved = applyEvents(handling.update(dt));
        if (moved >= 0) {
            transferItem = moved;
            transferFrom = lastGuns[moved];
            transferT = 0.0f;
        }
        // indo contra a porta: com a mão livre espalma a mão nela; armado, empurra com o ombro
        {
            Door::Touch t = arena.door.touch(body);
            for (const Door& d : arena.extraDoors) {
                Door::Touch e = d.touch(body);
                if (e.weight > t.weight) t = e;
            }
            if (t.weight > 0.01f) doorTouch = t;
            doorW = approach(doorW, playerAlive && !handling.busy() ? t.weight : 0.0f, 9.0f, dt);
        }
        const bool doorArmed = loadout.hand >= 0;
        if (doorArmed) applyShoulderPush(pose, doorW * (1.0f - aimW));
        poseArms(pose, charModel, aimPoint, aimW, weapon.recoil);
        if (!doorArmed && doorW > 0.001f) {
            glm::mat4 inv = glm::inverse(charModel);
            glm::vec3 pl = glm::vec3(inv * glm::vec4(doorTouch.point, 1.0f));
            glm::vec3 nl = glm::vec3(inv * glm::vec4(doorTouch.normal, 0.0f));
            applyPalmOnSurface(skeleton, pose, pl.x < 0.15f, pl, nl, doorW); // mão do lado da porta (esquerda = +X)
            computeGlobals(skeleton, pose, globals);
            computeSkinMatrices(skeleton, globals, skin);
        }
        // parkour com arma: a mão esquerda solta a arma e apoia no topo do obstáculo enquanto passa por cima
        if (vaultOn && loadout.hand >= 0) {
            float f = vaultT / vaultClip.duration * (float)vaultClip.frames.size(); // quadro do clipe
            float w = glm::smoothstep(kVaultTakeoff - 3.0f, kVaultTakeoff, f) * (1.0f - glm::smoothstep(18.0f, 23.0f, f));
            if (w > 0.001f) {
                // mão espalmada no topo: palma para baixo, dedos para a frente do salto
                glm::vec3 x(0.0f, 1.0f, 0.0f), y = -vaultDir; // eixos da mão esquerda: palma = -X, dedos = -Y
                glm::quat rot = glm::normalize(glm::quat_cast(glm::mat3(x, y, glm::cross(x, y))));
                glm::vec3 wrist = vaultPlant - rot * glm::vec3(-0.03f, -0.07f, 0.0f) + glm::vec3(0.0f, 0.02f, 0.0f);
                glm::mat4 inv = glm::inverse(charModel);
                glm::mat4 hand = inv * (glm::translate(glm::mat4(1.0f), wrist) * glm::mat4_cast(rot));
                applyHandTarget(skeleton, pose, false, hand, w);
                setFingerCurl(skeleton, pose, 0, 0.1f * w + 0.3f * (1.0f - w), 0.2f);
                computeGlobals(skeleton, pose, globals);
                computeSkinMatrices(skeleton, globals, skin);
            }
        }
        // na cobertura sem arma: a mão que passa da face da parede (a de trás no passo de lado)
        // fica espalmada nela em vez de atravessar
        if (loadout.hand < 0 && (coverOn || coverTrans != 0) && coverAnimW > 0.05f) {
            glm::mat4 inv = glm::inverse(charModel);
            bool changed = false;
            for (int side = 0; side < 2; ++side) {
                Bone h = side == 0 ? HAND_L : HAND_R;
                glm::vec3 tip = glm::vec3(charModel * globals[h] * glm::vec4(skeleton.tail[h] - skeleton.joint[h], 1.0f));
                glm::vec3 wrist = glm::vec3(charModel * globals[h][3]);
                float d = std::min(coverF.dist(tip), coverF.dist(wrist));
                float pen = 0.04f - d; // quanto passou da parede (com folga de 4 cm)
                if (pen <= 0.0f) continue;
                // mesma mão do clipe (mesma rotação), só empurrada para fora da parede
                glm::mat4 hand = globals[h];
                glm::vec3 push = glm::vec3(inv * glm::vec4(coverF.n * pen, 0.0f));
                hand[3] += glm::vec4(push, 0.0f);
                applyHandTarget(skeleton, pose, side == 1, hand, coverAnimW);
                changed = true;
            }
            if (changed) {
                computeGlobals(skeleton, pose, globals);
                computeSkinMatrices(skeleton, globals, skin);
            }
        }
        if (playerRag.active()) { // morto: o ragdoll manda no corpo (a arma continua na mão)
            playerRag.step(world, dt);
            playerRag.apply(skeleton, pose, charModel);
            computeGlobals(skeleton, pose, globals);
            computeSkinMatrices(skeleton, globals, skin);
        }
        fillGuns(charModel, gunMats);
        if (transferT < 1.0f) {
            transferT = std::min(1.0f, transferT + dt / 0.12f);
            gunMats[transferItem] = blendRigid(transferFrom, gunMats[transferItem], glm::smoothstep(0.0f, 1.0f, transferT));
        }
        if (editorOpen) {
            // animação de prévia (no lugar) e pose do modo escolhido
            AnimInput pa;
            pa.dt = realDt;
            pa.speed = ed.anim == 0 ? 0.0f : ed.anim == 1 ? animator.walkSpeed() : animator.runSpeed();
            pa.grounded = true;
            previewAnim.update(pa);
            Pose pp = previewAnim.pose();
            glm::vec3 ahead = glm::vec3(charModel * glm::vec4(0.0f, 1.5f, 12.0f, 1.0f));
            poseArms(pp, charModel, ahead, ed.mode == 0 ? 1.0f : 0.0f, 0.0f);
            fillGuns(charModel, gunMats);
            transferT = 1.0f;
        } else {
            lastGuns = gunMats;
        }

        held = loadout.hand >= 0 ? loadout.items[loadout.hand].def : nullptr;
        if (fireQueued && held && aimW > 0.85f && weapon.ready()) {
            // bala da boca do cano até a mira: acerta o primeiro soldado no caminho
            const glm::mat4& gm = gunMats[loadout.hand];
            glm::vec3 muzzle = glm::vec3(gm * glm::vec4(held->muzzle, 1.0f));
            glm::vec3 dir = glm::normalize(aimPoint - muzzle);
            Squad::Hit h = squad.raycast(muzzle, dir, world.raycast(muzzle, dir, 300.0f));
            if (h.soldier >= 0) {
                float dmg = (held->rifle ? 28.0f : 36.0f) * (h.head ? 3.0f : 1.0f);
                hitKill = squad.damage(h.soldier, dmg, dir, body.pos, muzzle + dir * h.t);
                kills += hitKill ? 1 : 0;
                hitMarker = 1.0f;
                weapon.fire(world, *held, gm, muzzle + dir * h.t, true);
            } else {
                weapon.fire(world, *held, gm, aimPoint);
            }
            cam.kick += held->recoilKick;
            fireQueued = false;
        }
        weapon.update(world, dt);

        // --- arena: porta, soldados, dano no jogador, morte e novas ondas
        {
            std::vector<CharacterBody*> pushers = squad.aliveBodies();
            pushers.push_back(&body);
            arena.door.update(world, pushers, dt);
            for (Door& d : arena.extraDoors) d.update(world, pushers, dt);
            assets.stepDynamics(world, scene.props, pushers, dt); // objetos com física
            float hurt = squad.update(world, {visualPos, body.vel, playerAlive, 1.3f - coverCrouch * 0.95f}, enemyFx, dt);
            enemyFx.update(world, dt);
#if GA_EDITOR
            const bool immortal = gfx.immortal;
#else
            const bool immortal = false;
#endif
            if (hurt > 0.0f && playerAlive) {
                if (!immortal) health -= hurt;
                sinceHurt = 0.0f;
                hurtFlash = 1.0f;
                if (health <= 0.0f) {
                    health = 0.0f;
                    playerAlive = false;
                    deadT = 0.0f;
                    // cai para longe de onde veio o tiro (o soldado mais perto)
                    glm::vec3 from = body.pos + glm::vec3(std::sin(charYaw), 0.0f, std::cos(charYaw));
                    float best = 1e9f;
                    for (const glm::vec3& p : squad.alivePositions())
                        if (glm::length(p - body.pos) < best) { best = glm::length(p - body.pos); from = p; }
                    glm::vec3 push = body.pos - from;
                    push.y = 0.0f;
                    push = glm::length(push) > 1e-3f ? glm::normalize(push) * 3.0f : glm::vec3(0.0f);
                    playerRag.start(skeleton, globals, charModel, body.vel, push, visualPos + glm::vec3(0.0f, 1.3f, 0.0f));
                    coverOn = coverEntering = false;
                    coverTrans = 0;
                    fireQueued = false;
                    if (handling.locksMovement()) handling.cancel();
                }
            }
            sinceHurt += dt;
            hurtFlash = std::max(0.0f, hurtFlash - dt * 2.5f);
            hitMarker = std::max(0.0f, hitMarker - dt * 4.0f);
            if (playerAlive && sinceHurt > 5.0f) health = std::min(100.0f, health + 12.0f * dt); // recupera fora de combate
            if (!playerAlive) {
                deadT += dt;
                if (deadT > 4.0f) { // renasce no início e a arena recomeça
                    playerAlive = true;
                    playerRag.stop();
                    health = 100.0f;
                    body.pos = body.vel = glm::vec3(0.0f);
                    visualY = 0.0f;
                    charYaw = 0.0f;
                    footIK.reset();
                    squad.reset();
                    wave = 1;
                    waveT = 0.0f;
                }
            }
            if (squad.aliveCount() == 0) {
                waveT += dt;
                if (waveT > 8.0f) { // arena limpa: nova onda
                    squad.reset();
                    wave++;
                    waveT = 0.0f;
                }
            }
        }
        promptAlpha = approach(promptAlpha, pick >= 0 ? 1.0f : 0.0f, 14.0f, dt);
        if (pick >= 0) promptPos = promptFor(pick);

        // --- render
        glfwGetFramebufferSize(win, &fbw, &fbh);
        if (fbw > 0 && fbh > 0) {
            renderer.resize(fbw, fbh);
            scene.characterModel = charModel;
            scene.characterPos = visualPos;
            sceneGuns(gunMats);
            scene.actors.clear();
            scene.panels.clear();
            squad.addToScene(scene, gunMeshes[1]);
            arena.door.addPanels(scene.panels);
            for (const Door& d : arena.extraDoors) d.addPanels(scene.panels);
            scene.view = cam.view;
            scene.proj = glm::perspective(cam.fov(), (float)fbw / fbh, 0.05f, 1000.0f);
            scene.camPos = cam.pos;
            scene.debugWeights = debugWeights;
            scene.crosshairAlpha = armed ? std::clamp((aimW - 0.3f) / 0.7f, 0.0f, 1.0f) : 0.0f;
            scene.promptAlpha = promptAlpha;
            if (editorOpen) {
                // ao trocar de modo, a câmera vai para um ângulo bom: mira por trás do ombro,
                // segurando pela frente, guardada pelas costas (ou pelo lado da cintura)
                if (ed.mode != edLastMode) {
                    edLastMode = ed.mode;
                    bool rifle = defs[ed.gun]->rifle;
                    edYaw = glm::radians(ed.mode == 0 ? -140.0f : ed.mode == 1 ? -40.0f : rifle ? 180.0f : -100.0f);
                    edPitch = 0.15f;
                    edDist = 2.2f;
                }
                float a = charYaw + edYaw;
                glm::vec3 pivot = visualPos + glm::vec3(0.0f, ed.mode == 2 && !defs[ed.gun]->rifle ? 1.0f : 1.3f, 0.0f);
                glm::vec3 eye = pivot + glm::vec3(std::sin(a) * std::cos(edPitch), std::sin(edPitch),
                                                  std::cos(a) * std::cos(edPitch)) * edDist;
                scene.view = glm::lookAt(eye, pivot, glm::vec3(0, 1, 0));
                // personagem centrado na parte da tela livre do painel
                scene.proj = glm::translate(glm::mat4(1.0f), glm::vec3(-edPanel, 0.0f, 0.0f)) *
                             glm::perspective(glm::radians(45.0f), (float)fbw / fbh, 0.05f, 1000.0f);
                scene.camPos = eye;
                scene.crosshairAlpha = 0.0f;
                scene.promptAlpha = 0.0f;
            }
            scene.promptWorld = promptPos;
            scene.time = gameTime;
            FreeEditor::Context feCtx = freeEditContext();
            if (freeEdit.active()) {
                // edição livre: câmera voando, seleção e gestos; confirmar remonta o cenário
                glm::vec3 eye;
                glm::mat4 v, p;
                FreeEditor::Result r = freeEdit.update(win, realDt, feCtx, fbw, fbh, v, p, eye);
                if (r == FreeEditor::Result::Rebuild) setupScene(sceneId);
                scene.view = v;
                scene.proj = p;
                scene.camPos = eye;
                scene.crosshairAlpha = 0.0f;
                scene.promptAlpha = 0.0f;
            }
            if (playFromEditor) { // jogando pelo editor: a imagem fica na vista, entre os painéis
                int ww, wh;
                glfwGetWindowSize(win, &ww, &wh);
                scene.proj = freeEdit.viewportFix((float)ww, (float)wh, (float)fbw / std::max(1, fbh)) * scene.proj;
            }
            renderer.render(scene, realDt);

            menu::beginFrame();
            if (freeEdit.active()) {
                FreeEditor::Result r = freeEdit.drawUi(feCtx, scene.proj * scene.view, fbw, fbh);
                using R = FreeEditor::Result;
                if (r == R::Rebuild) setupScene(sceneId);
                else if (r == R::Play) {
                    editorSettings = false;
                    playFromEditor = true;
                    playerRag.stop();
                    playerAlive = true;
                    health = 100.0f;
                    exitFreeEdit();
                } else if (r == R::Compile) {
                    saveGunTuning(weaponsPath);
                    builder::start(root);
                } else if (r == R::Settings) editorSettings = !editorSettings;
                else if (r == R::EditGun) {
                    freeEdit.end();
                    editorSettings = false;
                    gunFromEditor = true;
                    ed.gun = freeEdit.gunToEdit();
                    setEditor(true);
                } else if (r == R::ChangeScene) {
                    gfx.scene = freeEdit.sceneTarget();
                    setupScene(gfx.scene);
                    gfx.save(settingsPath);
                }
                if (editorSettings && freeEdit.active()) {
                    bool changed = false;
                    menu::Action act = menu::drawPauseMenu(gfx, changed);
                    if (act == menu::Action::Resume || act == menu::Action::FreeEdit) editorSettings = false;
                    else if (act == menu::Action::Quit) glfwSetWindowShouldClose(win, 1);
                    else if (act == menu::Action::Compile) { saveGunTuning(weaponsPath); builder::start(root); editorSettings = false; }
                    if (changed) gfx.save(settingsPath);
                }
            } else if (editorOpen) {
                menu::EditorAction ea = menu::drawGunEditor(ed, gunNames, gunIsRifle, 3, edPanel);
                auto apply = [&]() {
                    bool ok = saveGunTuning(weaponsPath);
                    ed.dirty = !ok;
                    ed.status = ok ? "Aplicado e salvo em weapons.ini" : "Erro ao salvar weapons.ini";
                    return ok;
                };
                if (ea == menu::EditorAction::Apply) apply();
                else if (ea == menu::EditorAction::Revert) { loadGunTuning(weaponsPath); ed.dirty = false; ed.status.clear(); }
                else if (ea == menu::EditorAction::Close) setEditor(false);
                else if (ea == menu::EditorAction::Compile && apply()) builder::start(root);
            } else if (menuOpen) {
                bool changed = false;
                menu::Action act = menu::drawPauseMenu(gfx, changed);
                if (act == menu::Action::Resume) setMenu(false);
                else if (act == menu::Action::Quit) glfwSetWindowShouldClose(win, 1);
                else if (act == menu::Action::EditGuns) setEditor(true);
                else if (act == menu::Action::FreeEdit) freeEdit.begin(cam.pos, cam.yaw, cam.pitch, root);
                else if (act == menu::Action::ChangeScene) {
                    // troca de cenário na hora: zera tudo que é do jogador e do combate
                    setupScene(gfx.scene);
                    coverOn = coverEntering = runJumpOn = vaultOn = false;
                    coverTrans = 0;
                    playerRag.stop();
                    playerAlive = true;
                    health = 100.0f;
                    wave = 1;
                    waveT = 0.0f;
                    next = Next::None;
                    transferT = 1.0f;
                    fillGuns(glm::mat4(1.0f), lastGuns);
                    gfx.save(settingsPath);
                }
                else if (act == menu::Action::Compile) {
                    saveGunTuning(weaponsPath);
                    builder::start(root);
                }
            }
            if (gunFromEditor && !editorOpen) { // fechou o editor da arma: volta ao editor
                gunFromEditor = false;
                freeEdit.resume();
            }
            if (playFromEditor) freeEdit.drawPlaying(feCtx); // painéis do editor em volta do jogo
            menu::drawBuildStatus();
            if (!editorOpen && !freeEdit.active()) {
                menu::CombatHud hud;
                hud.health = health;
                hud.hurtFlash = hurtFlash;
                hud.hitMarker = hitMarker;
                hud.hitKill = hitKill;
                hud.alive = squad.aliveCount();
                hud.total = (int)squad.soldiers.size();
                hud.kills = kills;
                hud.wave = wave;
                hud.inArena = arena.inside(body.pos) || squad.aliveCount() < hud.total;
                hud.dead = !playerAlive;
                hud.coverHint = coverHint;
                hud.inCover = coverOn;
                hud.nextWave = hud.alive == 0 ? std::max(0.0f, 8.0f - waveT) : -1.0f;
                menu::drawCombatHud(hud);
            }
            if (gfx.showFps) menu::drawFps(fpsShown, msShown);
            menu::endFrame();
            renderer.applySettings(gfx);
            cam.baseFov = gfx.fov;
            if (gfx.vsync != vsync) {
                vsync = gfx.vsync;
                glfwSwapInterval(vsync ? 1 : 0);
            }
        }
        // depuração: GA_GRAB="quadro arquivo.png" salva a tela do jogo nesse quadro e fecha
        if (const char* grab = getenv("GA_GRAB")) {
            static int frameNo = 0;
            int at = 0;
            char path[512] = {};
            if (std::sscanf(grab, "%d %511s", &at, path) == 2 && ++frameNo == at) {
                std::vector<unsigned char> px((size_t)fbw * fbh * 4);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glReadPixels(0, 0, fbw, fbh, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
                stbi_flip_vertically_on_write(1);
                stbi_write_png(path, fbw, fbh, 4, px.data(), fbw * 4);
                glfwSetWindowShouldClose(win, 1);
            }
        }
        glfwSwapBuffers(win);

        frames++;
        titleTimer += realDt;
        if (titleTimer >= 0.5) {
            fpsShown = (float)(frames / titleTimer);
            msShown = (float)(titleTimer * 1000.0 / frames);
            char title[160];
            std::snprintf(title, sizeof(title), "GA - teste | %.0f FPS | %s | %.1f m/s | Foot IK %s%s%s",
                          frames / titleTimer, animator.stateName(), ai.speed, footIK.enabled ? "on" : "off",
                          held ? (" | " + held->name).c_str() : "", debugWeights ? " | DEBUG RIG" : "");
            glfwSetWindowTitle(win, title);
            frames = 0;
            titleTimer = 0.0;
        }
    }
    gfx.save(settingsPath);
    menu::shutdown();
    glfwTerminate();
    return 0;
}
