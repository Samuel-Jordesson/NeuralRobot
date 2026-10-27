#pragma once
#include "graphics_settings.h"

struct GLFWwindow;

// Menu de pausa (Esc) com as opções gráficas, feito com Dear ImGui.
namespace menu {

enum class Action { None, Resume, Quit };

bool init(GLFWwindow* win, bool installCallbacks);
void shutdown();
void beginFrame();
// Desenha o menu. 'changed' fica true quando alguma opção foi mexida.
Action drawPauseMenu(GraphicsSettings& gs, bool& changed);
void drawFps(float fps, float frameMs);
void endFrame();

} // namespace menu
