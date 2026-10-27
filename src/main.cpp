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
#include "animation.h"
#include "character_model.h"
#include "config.h"
#include "foot_ik.h"
#include "graphics_settings.h"
#include "gun_handling.h"
#include "loadout.h"
#include "menu.h"
#include "physics.h"
#include "renderer.h"
#include "skeleton.h"
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
        float sh = shoulder + (0.62f - shoulder) * aimBlend;
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
    // mesas com as armas em cima (pistola e AK-47 na primeira, AK-12 na segunda)
    std::vector<Table> tables = {{glm::vec3(2.2f, 0.0f, 3.0f), 1}, {glm::vec3(4.3f, 0.0f, 3.0f), 1}};
    for (const Table& t : tables) t.addColliders(world);
    CharacterBody body;
    // Captura de movimento (Mixamo) convertida para o esqueleto do boneco
    MocapClip walkClip, runClip;
    loadMixamoClip(root + "/Walking.fbx", skeleton, walkClip);
    loadMixamoClip(root + "/Running.fbx", skeleton, runClip);
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
        auto poseWithGun = [&](float recoil) {
            Pose p = pose;
            poseArms(p, model, aimPoint, args.aim ? 1.0f : 0.0f, recoil);
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
        if (esc && !escWas) setMenu(!menuOpen);
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
        lastMx = mx;
        lastMy = my;
        if (!menuOpen) cam.targetDist = std::clamp(cam.targetDist - g_scroll * 0.4f, 1.6f, 9.0f);
        g_scroll = 0.0f;

        auto key = [&](int k) { return !menuOpen && glfwGetKey(win, k) == GLFW_PRESS ? 1.0f : 0.0f; };
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
        bool armed = held && !handling.busy();
        if (armed && mouseCaptured && (lmbPressed || (held->automatic && lmb))) {
            fireQueued = true;
            hipFireTimer = 0.8f;
        }
        if (!armed) fireQueued = false;
        hipFireTimer -= dt;
        bool aiming = armed && (rmb || hipFireTimer > 0.0f);
        aimW = approach(aimW, aiming ? 1.0f : 0.0f, aiming ? 16.0f : 8.0f, dt);
        cam.aimBlend = approach(cam.aimBlend, armed && rmb ? 1.0f : 0.0f, 11.0f, dt);
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

        // --- direção do corpo: mirando, fica de frente para a mira; senão, vira para o movimento
        float prevYaw = charYaw;
        bool moving = glm::length(move) > 0.1f;
        if (aiming) {
            charYaw = wrapAngle(charYaw + wrapAngle(cam.yaw - charYaw) * (1.0f - std::exp(-18.0f * dt)));
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
        ai.turnRate = aiming ? 0.0f : turnRate;
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
        glm::mat4 charModel = glm::translate(glm::mat4(1.0f), visualPos) *
                              glm::rotate(glm::mat4(1.0f), charYaw, glm::vec3(0, 1, 0));
        Pose& pose = animator.pose();
        footIK.apply(skeleton, pose, charModel, world, animator.groundedWeight(), dt);

        // --- câmera, mira e pose de tiro
        cam.update(world, visualPos, dt);
        glm::vec3 aimPoint = cam.aimPoint(world);
        int moved = applyEvents(handling.update(dt));
        if (moved >= 0) {
            transferItem = moved;
            transferFrom = lastGuns[moved];
            transferT = 0.0f;
        }
        poseArms(pose, charModel, aimPoint, aimW, weapon.recoil);
        fillGuns(charModel, gunMats);
        if (transferT < 1.0f) {
            transferT = std::min(1.0f, transferT + dt / 0.12f);
            gunMats[transferItem] = blendRigid(transferFrom, gunMats[transferItem], glm::smoothstep(0.0f, 1.0f, transferT));
        }
        lastGuns = gunMats;

        held = loadout.hand >= 0 ? loadout.items[loadout.hand].def : nullptr;
        if (fireQueued && held && aimW > 0.85f && weapon.ready()) {
            weapon.fire(world, *held, gunMats[loadout.hand], aimPoint);
            cam.kick += held->recoilKick;
            fireQueued = false;
        }
        weapon.update(world, dt);
        promptAlpha = approach(promptAlpha, pick >= 0 ? 1.0f : 0.0f, 14.0f, dt);
        if (pick >= 0) promptPos = promptFor(pick);

        // --- render
        glfwGetFramebufferSize(win, &fbw, &fbh);
        if (fbw > 0 && fbh > 0) {
            renderer.resize(fbw, fbh);
            scene.characterModel = charModel;
            scene.characterPos = visualPos;
            sceneGuns(gunMats);
            scene.view = cam.view;
            scene.proj = glm::perspective(cam.fov(), (float)fbw / fbh, 0.05f, 1000.0f);
            scene.camPos = cam.pos;
            scene.debugWeights = debugWeights;
            scene.crosshairAlpha = armed ? std::clamp((aimW - 0.3f) / 0.7f, 0.0f, 1.0f) : 0.0f;
            scene.promptAlpha = promptAlpha;
            scene.promptWorld = promptPos;
            scene.time = gameTime;
            renderer.render(scene, realDt);

            menu::beginFrame();
            if (menuOpen) {
                bool changed = false;
                menu::Action act = menu::drawPauseMenu(gfx, changed);
                if (act == menu::Action::Resume) setMenu(false);
                else if (act == menu::Action::Quit) glfwSetWindowShouldClose(win, 1);
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
