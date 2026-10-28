#include <GL/glew.h>
#include <GLFW/glfw3.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "aim_ik.h"
#include "builder.h"
#include "gun_tuning.h"
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
#include "weapon.h"

namespace {

float g_scroll = 0.0f;
void scrollCallback(GLFWwindow*, double, double y) { g_scroll += (float)y; }

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
        pos.y = std::max(pos.y, w.groundY + 0.15f);
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
    int bench = 0;      // renderiza N quadros e mede o tempo
    bool holster = false;       // todas as armas guardadas no corpo
    int weapon = 0;             // arma usada por --gun/--aim/--act: 0 pistola, 1 AK-47, 2 AK-12
    const char* act = nullptr;  // pickup | stow | draw: foto no meio da animação
    float actTime = 0.0f;
    float npcTime = 0.0f;       // simula os soldados por esse tempo antes da foto
    bool doorOpen = false;      // porta da arena aberta na foto
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
        else if (!std::strcmp(argv[i], "--bench")) a.bench = std::atoi(next());
        else if (!std::strcmp(argv[i], "--holster")) a.holster = true;
        else if (!std::strcmp(argv[i], "--weapon")) a.weapon = std::atoi(next());
        else if (!std::strcmp(argv[i], "--act")) {
            a.act = next();
            a.actTime = (float)std::atof(next());
        }
        else if (!std::strcmp(argv[i], "--npc")) a.npcTime = (float)std::atof(next());
        else if (!std::strcmp(argv[i], "--door")) a.doorOpen = true;
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

    const std::string root = GA_ROOT;
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

    World world = World::makeTestMap();
    // arena murada com porta e soldados (russian_soldier.glb) com AK-47
    Arena arena = Arena::build(world);
    SoldierAssets soldierAssets;
    if (!soldierAssets.load(root)) return 1;
    Squad squad;
    squad.init(&soldierAssets, defs[1], arena.spawns, arena.spawnYaw);
    Weapon enemyFx; // tiros dos soldados
    // mesas com as armas em cima (pistola e AK-47 na primeira, AK-12 na segunda)
    std::vector<Table> tables = {{glm::vec3(2.2f, 0.0f, 3.0f), 1}, {glm::vec3(4.3f, 0.0f, 3.0f), 1}};
    for (const Table& t : tables) t.addColliders(world);
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
    {
        const struct { int table; float across, along, turn; } spots[3] = {
            {0, 0.80f, 0.45f, glm::radians(20.0f)},  // pistola
            {0, 0.60f, -0.72f, glm::radians(75.0f)}, // AK-47
            {1, 0.60f, -0.65f, glm::radians(75.0f)}, // AK-12
        };
        for (int i = 0; i < 3; ++i) {
            const Table& t = tables[spots[i].table];
            GunItem it;
            it.def = defs[i];
            it.placeOnTable(t.topPoint(spots[i].across, spots[i].along), t.facingFromPlusX(), spots[i].turn, t.pos.y);
            loadout.items.push_back(it);
        }
    }
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
    for (const Table& t : tables) scene.props.push_back({&tableMesh, t.model()});

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
                const MocapClip& c = (!std::strcmp(args.coverClip, "low") ? coverLow : coverStand)[args.coverClipSide & 1];
                if (c.valid()) p = c.sample(args.coverClipPhase);
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
        if (esc && !escWas) {
            if (editorOpen) setEditor(false); // Esc no editor volta ao menu
            else setMenu(!menuOpen);
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
            bool free = !coverOn && !coverEntering && playerAlive && !handling.locksMovement() && body.grounded;
            coverHint = free && findPlayerCover(world, body.pos, cam.forward, cand);
            if (qKey && !qWas) {
                if (coverOn || coverEntering) coverOn = coverEntering = false;
                else if (coverHint) { coverF = cand; coverEntering = true; }
            }
            qWas = qKey;
            if (handling.locksMovement() || !playerAlive) coverOn = coverEntering = false;
        }
        float edgeDist = 1e9f, edgeSide = 1.0f;
        if (coverEntering) { // vai até a parede
            glm::vec3 target = coverF.pointAt(std::clamp(coverF.along(body.pos), coverF.s0 + 0.25f, coverF.s1 - 0.25f), coverOff);
            glm::vec3 d = target - body.pos;
            d.y = 0.0f;
            float l = glm::length(d);
            if (l < 0.07f) { coverEntering = false; coverOn = true; }
            else { move = d / l; wishSpeed = std::min(animator.runSpeed(), 1.0f + l * 6.0f); jump = false; }
        }
        if (coverOn) {
            jump = false;
            if (glm::dot(move, coverF.n) > 0.75f) {
                coverOn = false; // puxou para longe da parede
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

        // --- física em passo fixo
        bool landed = false;
        float impact = 0.0f;
        accumulator += dt;
        while (accumulator >= fixedDt) {
            body.step(world, move, wishSpeed, jump, fixedDt);
            stepBalls(world, body, fixedDt);
            if (body.justLanded) { landed = true; impact = std::max(impact, body.landImpact); }
            accumulator -= fixedDt;
        }
        if (coverOn) { // continua colado na parede
            float d = coverF.dist(body.pos);
            body.pos += coverF.n * (coverOff - d);
        }

        // --- direção do corpo: mirando, fica de frente para a mira; senão, vira para o movimento
        float prevYaw = charYaw;
        bool moving = glm::length(move) > 0.1f;
        if (aiming) {
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
        if (coverPeek > 0.001f) visualPos += (coverF.t * peekSide * std::max(0.0f, peekEdge - 0.12f) + coverF.n * 0.08f) * coverPeek;
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
        coverAnimW = approach(coverAnimW, coverOn && !aiming ? 1.0f : 0.0f, 8.0f, dt);
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
                if (std::fabs(side) < 0.15f) {
                    // parou: termina o passo até a pose de parado mais próxima à frente (os pés se acertam)
                    const std::vector<float>& idle = coverIdle[(coverF.low ? 2 : 0) + coverSide];
                    float ahead = 1.0f;
                    for (float ph : idle) ahead = std::min(ahead, std::fmod(ph - coverPhase + 2.0f, 1.0f));
                    if (ahead < 0.995f && ahead > 0.005f) advance = std::min(ahead, 0.9f / (cycle / cl.naturalSpeed()) * dt);
                    else advance = 0.0f;
                }
                coverPhase = std::fmod(coverPhase + advance, 1.0f);
                Pose cp = blendPose(cl.sample(coverPhase), cr.sample(coverPhase), coverSideW);
                pose = blendPose(pose, cp, coverAnimW);
            }
        }
        footIK.apply(skeleton, pose, charModel, world, animator.groundedWeight(), dt);
        if (coverPeek > 0.001f) {
            // tronco inclinado para o lado da quina (expõe o mínimo)
            glm::vec3 charRight(-std::cos(charYaw), 0.0f, std::sin(charYaw));
            float lean = (glm::dot(coverF.t * peekSide, charRight) > 0.0f ? 1.0f : -1.0f) * 0.22f * coverPeek;
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
            renderer.render(scene, realDt);

            menu::beginFrame();
            if (editorOpen) {
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
                else if (act == menu::Action::Compile) {
                    saveGunTuning(weaponsPath);
                    builder::start(root);
                }
            }
            menu::drawBuildStatus();
            if (!editorOpen) {
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
