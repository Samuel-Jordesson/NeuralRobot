#pragma once
#include "graphics_settings.h"

#include <string>

struct GLFWwindow;

// Menu de pausa (Esc) com as opções gráficas, feito com Dear ImGui.
namespace menu {

enum class Action { None, Resume, Quit, EditGuns, Compile };

bool init(GLFWwindow* win, bool installCallbacks);
void shutdown();
void beginFrame();
// Desenha o menu. 'changed' fica true quando alguma opção foi mexida.
Action drawPauseMenu(GraphicsSettings& gs, bool& changed);
void drawFps(float fps, float frameMs);

// HUD do combate: vida, inimigos, marcador de acerto e avisos.
struct CombatHud {
    float health = 100.0f, hurtFlash = 0.0f, hitMarker = 0.0f;
    bool hitKill = false;
    int alive = 0, total = 0, kills = 0, wave = 1;
    bool inArena = false, dead = false;
    bool coverHint = false, inCover = false; // dá para pegar cobertura / está nela
    float nextWave = -1.0f; // >= 0: arena limpa, segundos até a próxima onda
};
void drawCombatHud(const CombatHud& h);

// ---- editor de armas (só na versão de desenvolvimento, GA_EDITOR)
struct GunEditor {
    int gun = 0;        // arma escolhida
    int mode = 0;       // 0 = mirando, 1 = segurando, 2 = guardada no corpo
    int slot = 1;       // fuzil guardado: 1 = costas baixo, 2 = costas alto
    int anim = 0;       // 0 = parado, 1 = andando, 2 = correndo
    bool dirty = false; // mudou algo que ainda não foi aplicado
    std::string status; // mensagem do último aplicar
};
enum class EditorAction { None, Apply, Revert, Close, Compile };
// Painel à direita (fração 'panelFrac' da largura); o personagem aparece no resto da tela.
EditorAction drawGunEditor(GunEditor& ed, const char* const* gunNames, const bool* gunIsRifle, int gunCount,
                           float& panelFrac);
// Andamento da compilação da versão final (canto da tela).
void drawBuildStatus();
void endFrame();

} // namespace menu
