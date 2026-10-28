#include "menu.h"
#include "builder.h"
#include "gun_tuning.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sys/stat.h>

namespace menu {
namespace {

ImFont* g_body = nullptr;
ImFont* g_title = nullptr;
ImFont* g_section = nullptr;
float g_scale = 0.0f;       // escala atual da interface (acompanha o tamanho da janela)
GLFWwindow* g_win = nullptr;
ImGuiStyle g_baseStyle;     // estilo sem escala; cada mudança de escala parte dele

const ImVec4 kAccent(0.96f, 0.64f, 0.20f, 1.0f);

bool fileExists(const char* p) {
    struct stat st;
    return stat(p, &st) == 0;
}

void applyStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0.0f;
    s.WindowBorderSize = 0.0f;
    s.FrameRounding = 4.0f;
    s.GrabRounding = 4.0f;
    s.TabRounding = 4.0f;
    s.FramePadding = ImVec2(10, 6);
    s.ItemSpacing = ImVec2(10, 9);
    s.WindowPadding = ImVec2(34, 30);
    s.GrabMinSize = 14.0f;
    s.ScrollbarSize = 10.0f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.045f, 0.05f, 0.06f, 0.90f);
    c[ImGuiCol_Text] = ImVec4(0.93f, 0.93f, 0.92f, 1.0f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.56f, 0.58f, 1.0f);
    c[ImGuiCol_FrameBg] = ImVec4(1.0f, 1.0f, 1.0f, 0.07f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(1.0f, 1.0f, 1.0f, 0.12f);
    c[ImGuiCol_FrameBgActive] = ImVec4(1.0f, 1.0f, 1.0f, 0.16f);
    c[ImGuiCol_SliderGrab] = kAccent;
    c[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 0.76f, 0.38f, 1.0f);
    c[ImGuiCol_CheckMark] = kAccent;
    c[ImGuiCol_Button] = ImVec4(1.0f, 1.0f, 1.0f, 0.08f);
    c[ImGuiCol_ButtonHovered] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.85f);
    c[ImGuiCol_ButtonActive] = kAccent;
    c[ImGuiCol_Header] = ImVec4(1.0f, 1.0f, 1.0f, 0.08f);
    c[ImGuiCol_HeaderHovered] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.6f);
    c[ImGuiCol_HeaderActive] = kAccent;
    c[ImGuiCol_Tab] = ImVec4(1.0f, 1.0f, 1.0f, 0.05f);
    c[ImGuiCol_TabHovered] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.7f);
    c[ImGuiCol_TabSelected] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.9f);
    c[ImGuiCol_TabSelectedOverline] = kAccent;
    c[ImGuiCol_Separator] = ImVec4(1.0f, 1.0f, 1.0f, 0.10f);
    c[ImGuiCol_PopupBg] = ImVec4(0.07f, 0.075f, 0.085f, 0.98f);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(1.0f, 1.0f, 1.0f, 0.15f);
}

// --- linhas de opção: rótulo à esquerda, controle ocupando o resto da largura
float labelWidth() { return ImGui::GetContentRegionAvail().x * 0.42f; }

void label(const char* text, const char* tip) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text);
    if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    ImGui::SameLine(labelWidth());
    ImGui::SetNextItemWidth(-1.0f);
}

bool sliderF(const char* text, const char* id, float* v, float lo, float hi, const char* fmt, const char* tip = nullptr) {
    label(text, tip);
    return ImGui::SliderFloat(id, v, lo, hi, fmt);
}

bool checkbox(const char* text, const char* id, bool* v, const char* tip = nullptr) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text);
    if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    ImGui::SameLine(labelWidth());
    return ImGui::Checkbox(id, v);
}

bool combo(const char* text, const char* id, int* idx, const char* const* items, int n, const char* tip = nullptr) {
    label(text, tip);
    return ImGui::Combo(id, idx, items, n);
}

void section(const char* text) {
    ImGui::Dummy(ImVec2(0, 6 * g_scale));
    ImGui::PushFont(g_section);
    ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::Separator();
}

// Valor -> índice numa lista fixa (e volta), para combos como MSAA e resolução da sombra.
int indexOf(int v, const int* list, int n) {
    for (int i = 0; i < n; ++i)
        if (list[i] == v) return i;
    return 0;
}

// Escala pela janela: 1.0 numa janela de ~1500x1000; diminui junto quando a janela encolhe.
float scaleForWindow() {
    int w = 1600, h = 900;
    glfwGetWindowSize(g_win, &w, &h);
    if (w <= 0 || h <= 0) return g_scale > 0.0f ? g_scale : 1.0f;
    return std::clamp(std::min(h / 1000.0f, w / 1500.0f), 0.5f, 2.5f);
}

// Recria as fontes no tamanho certo (texto nítido em qualquer escala) e reescala o estilo.
void rebuildForScale(float scale, bool uploadNow) {
    g_scale = scale;
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    const char* regular[] = {"/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
                             "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
                             "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"};
    const char* bold[] = {"/usr/share/fonts/truetype/ubuntu/Ubuntu-B.ttf", "/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf",
                          "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"};
    g_body = g_title = g_section = nullptr;
    for (const char* f : regular)
        if (fileExists(f)) { g_body = io.Fonts->AddFontFromFileTTF(f, 18.0f * g_scale); break; }
    for (const char* f : bold)
        if (fileExists(f)) {
            g_title = io.Fonts->AddFontFromFileTTF(f, 40.0f * g_scale);
            g_section = io.Fonts->AddFontFromFileTTF(f, 19.0f * g_scale);
            break;
        }
    if (!g_body) g_body = io.Fonts->AddFontDefault();
    if (!g_title) g_title = g_body;
    if (!g_section) g_section = g_body;
    io.FontDefault = g_body;
    ImGui::GetStyle() = g_baseStyle;
    ImGui::GetStyle().ScaleAllSizes(g_scale);
    if (uploadNow) {
        ImGui_ImplOpenGL3_DestroyFontsTexture();
        ImGui_ImplOpenGL3_CreateFontsTexture();
    }
}

} // namespace

bool init(GLFWwindow* win, bool installCallbacks) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    g_win = win;


    applyStyle();
    g_baseStyle = ImGui::GetStyle();
    rebuildForScale(scaleForWindow(), false);
    return ImGui_ImplGlfw_InitForOpenGL(win, installCallbacks) && ImGui_ImplOpenGL3_Init("#version 330 core");
}

void shutdown() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void beginFrame() {
    // janela mudou de tamanho: só reconstrói quando a escala muda de verdade (evita refazer a cada pixel)
    float sc = scaleForWindow();
    if (std::fabs(sc - g_scale) > 0.03f) rebuildForScale(sc, true);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void endFrame() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void drawFps(float fps, float frameMs) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 12 * g_scale, 12 * g_scale), ImGuiCond_Always, ImVec2(1, 0));
    ImGui::SetNextWindowBgAlpha(0.45f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10 * g_scale, 6 * g_scale));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4 * g_scale);
    ImGui::Begin("##fps", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImGui::Text("%.0f FPS  %.1f ms", fps, frameMs);
    ImGui::End();
    ImGui::PopStyleVar(2);
}

Action drawPauseMenu(GraphicsSettings& gs, bool& changed) {
    Action action = Action::None;
    ImGuiIO& io = ImGui::GetIO();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;

    // escurece a cena de leve (mais à esquerda, onde fica o painel) para ver as mudanças ao vivo
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    bg->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(W, H), IM_COL32(0, 0, 0, 150), IM_COL32(0, 0, 0, 20),
                                IM_COL32(0, 0, 0, 20), IM_COL32(0, 0, 0, 150));

    float panelW = std::min(W * 0.92f, 600.0f * g_scale);
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(panelW, H));
    ImGui::Begin("##pause", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

    ImGui::PushFont(g_title);
    ImGui::TextUnformatted("PAUSADO");
    ImGui::PopFont();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextUnformatted("Esc volta ao jogo. As mudanças aparecem na hora.");
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, 4 * g_scale));

    float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.06f, 0.05f, 0.04f, 1.0f));
    if (ImGui::Button("Continuar", ImVec2(bw, 40 * g_scale))) action = Action::Resume;
    ImGui::PopStyleColor(2);
    ImGui::SameLine();
    if (ImGui::Button("Sair do jogo", ImVec2(bw, 40 * g_scale))) action = Action::Quit;
#if GA_EDITOR
    if (ImGui::Button("Editar arma", ImVec2(bw, 40 * g_scale))) action = Action::EditGuns;
    ImGui::SameLine();
    ImGui::BeginDisabled(builder::state() == builder::State::Running);
    if (ImGui::Button("Compilar", ImVec2(bw, 40 * g_scale))) action = Action::Compile;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Gera a versão final em build-final/ga, sem o editor e sem este botão");
    ImGui::EndDisabled();
#endif
    ImGui::Dummy(ImVec2(0, 4 * g_scale));

    bool quality = false; // mexeu em algo de qualidade -> preset vira "Personalizado"
    bool any = false;
    float footer = 56.0f * g_scale;

    if (ImGui::BeginTabBar("tabs")) {
        if (ImGui::BeginTabItem("  Gráficos  ")) {
            ImGui::BeginChild("g", ImVec2(0, -footer));
            section("Qualidade");
            const char* presets[] = {"Baixo", "Médio", "Alto", "Ultra", "Personalizado"};
            int p = gs.preset;
            if (combo("Predefinição", "##preset", &p, presets, 5,
                      "Ajusta sombras, anti-serrilhado, SSAO e luz volumétrica de uma vez")) {
                if (p != GraphicsSettings::Custom) gs.applyPreset(p);
                else gs.preset = p;
                any = true;
            }

            section("Tela");
            int scalePct = (int)std::lround(gs.renderScale * 100.0f);
            label("Resolução interna", "Abaixo de 100% roda mais leve, mas a imagem fica mais macia");
            if (ImGui::SliderInt("##scale", &scalePct, 50, 100, "%d%%")) {
                gs.renderScale = scalePct / 100.0f;
                quality = true;
            }
            static const int msaaVals[] = {1, 2, 4, 8};
            const char* msaaNames[] = {"Desligado", "2x", "4x", "8x"};
            int mi = indexOf(gs.msaa, msaaVals, 4);
            if (combo("Anti-serrilhado MSAA", "##msaa", &mi, msaaNames, 4, "Suaviza as bordas da geometria")) {
                gs.msaa = msaaVals[mi];
                quality = true;
            }
            quality |= checkbox("FXAA", "##fxaa", &gs.fxaa, "Suaviza bordas e brilhos finos (custo baixo)");
            any |= sliderF("Campo de visão", "##fov", &gs.fov, 50.0f, 90.0f, "%.0f°");
            any |= checkbox("Sincronização vertical", "##vsync", &gs.vsync, "Trava no Hz do monitor, evita cortes na imagem");
            any |= checkbox("Mostrar FPS", "##fps", &gs.showFps);
#if GA_EDITOR
            any |= checkbox("Jogador imortal", "##immortal", &gs.immortal,
                            "Só para testar: os tiros não tiram vida. Não existe na versão final compilada.");
#endif

            section("Sombras");
            static const int resVals[] = {1024, 2048, 4096};
            const char* resNames[] = {"Baixa (1024)", "Alta (2048)", "Ultra (4096)"};
            int ri = indexOf(gs.shadowRes, resVals, 3);
            if (combo("Resolução", "##shres", &ri, resNames, 3, "Nitidez das sombras")) {
                gs.shadowRes = resVals[ri];
                quality = true;
            }
            const char* cascNames[] = {"1", "2", "3", "4"};
            int ci = gs.shadowCascades - 1;
            if (combo("Cascatas", "##casc", &ci, cascNames, 4, "Mais cascatas = sombras nítidas perto e longe ao mesmo tempo")) {
                gs.shadowCascades = ci + 1;
                quality = true;
            }
            quality |= sliderF("Distância", "##shdist", &gs.shadowDistance, 30.0f, 200.0f, "%.0f m");
            any |= sliderF("Suavidade", "##soft", &gs.shadowSoftness, 0.2f, 3.0f, "%.1f",
                           "Tamanho da penumbra (borda macia da sombra)");

            section("Iluminação");
            quality |= checkbox("Oclusão de ambiente (SSAO)", "##ssao", &gs.ssao,
                                "Escurece cantos, frestas e o contato dos objetos com o chão");
            if (gs.ssao) {
                any |= sliderF("   Raio", "##ssaor", &gs.ssaoRadius, 0.2f, 2.0f, "%.2f m");
                any |= sliderF("   Intensidade", "##ssaoi", &gs.ssaoIntensity, 0.0f, 3.0f, "%.2f");
            }
            quality |= checkbox("Luz volumétrica", "##vol", &gs.volumetric, "Raios de sol visíveis na névoa entre as sombras");
            if (gs.volumetric) {
                label("   Qualidade", "Passos por pixel (mais = menos granulado, mais pesado)");
                quality |= ImGui::SliderInt("##vols", &gs.volumetricSteps, 8, 64);
                any |= sliderF("   Intensidade", "##voli", &gs.volumetricIntensity, 0.0f, 3.0f, "%.2f");
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("  Atmosfera  ")) {
            ImGui::BeginChild("a", ImVec2(0, -footer));
            section("Sol");
            int mins = (int)std::lround(gs.timeOfDay * 60.0f);
            char clock[16];
            std::snprintf(clock, sizeof(clock), "%02d:%02d", mins / 60, mins % 60);
            any |= sliderF("Hora do dia", "##hour", &gs.timeOfDay, 5.5f, 19.0f, clock,
                           "Do amanhecer ao pôr do sol: muda a cor da luz, do céu e o tamanho das sombras");
            any |= checkbox("Ciclo do dia", "##cycle", &gs.dayCycle, "O tempo passa sozinho enquanto joga");
            if (gs.dayCycle) any |= sliderF("   Velocidade", "##cyclespd", &gs.dayCycleSpeed, 0.1f, 10.0f, "%.1f h/min");
            any |= sliderF("Direção do sol", "##az", &gs.sunAzimuth, -180.0f, 180.0f, "%.0f°");

            section("Céu e névoa");
            any |= checkbox("Nuvens", "##clouds", &gs.clouds);
            if (gs.clouds) {
                any |= sliderF("   Quantidade", "##cov", &gs.cloudCoverage, 0.0f, 1.0f, "%.2f");
                quality |= checkbox("   Sombra das nuvens", "##cshadow", &gs.cloudShadows, "Nuvens fazem sombra no chão");
            }
            any |= sliderF("Névoa", "##fog", &gs.fogDensity, 0.0f, 5.0f, "%.2f", "Densidade da névoa perto do chão");
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("  Imagem  ")) {
            ImGui::BeginChild("i", ImVec2(0, -footer));
            section("Exposição");
            any |= checkbox("Exposição automática", "##autoexp", &gs.autoExposure,
                            "O olho se adapta: claro ao sair da sombra, escuro olhando o céu");
            any |= sliderF("Brilho (EV)", "##ev", &gs.exposure, -3.0f, 3.0f, "%+.1f");
            any |= checkbox("Bloom", "##bloom", &gs.bloom, "Brilho que vaza das luzes fortes (sol, clarão do tiro)");
            if (gs.bloom) any |= sliderF("   Intensidade", "##bloomi", &gs.bloomIntensity, 0.0f, 4.0f, "%.2f");

            section("Cor");
            any |= sliderF("Contraste", "##contrast", &gs.contrast, 0.7f, 1.5f, "%.2f");
            any |= sliderF("Saturação", "##sat", &gs.saturation, 0.0f, 2.0f, "%.2f");
            any |= sliderF("Temperatura", "##temp", &gs.temperature, -1.0f, 1.0f, "%+.2f", "Negativo = mais frio/azul, positivo = mais quente");

            section("Lente");
            any |= sliderF("Vinheta", "##vig", &gs.vignette, 0.0f, 1.0f, "%.2f", "Escurece os cantos da tela");
            any |= sliderF("Granulação", "##grain", &gs.filmGrain, 0.0f, 1.0f, "%.2f", "Grão de filme");
            any |= sliderF("Aberração cromática", "##ca", &gs.chromaticAberration, 0.0f, 1.0f, "%.2f",
                           "Franja colorida nas bordas da tela, como em lentes reais");
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    if (quality) gs.preset = GraphicsSettings::Custom;
    changed = any || quality;

    ImGui::Separator();
    if (ImGui::Button("Restaurar padrão", ImVec2(-1, 36 * g_scale))) {
        bool vs = gs.vsync;
        gs.resetDefaults();
        gs.vsync = vs;
        changed = true;
    }
    ImGui::End();
    return action;
}

void drawCombatHud(const CombatHud& h) {
    ImGuiIO& io = ImGui::GetIO();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y, sc = g_scale;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    // levou tiro: bordas da tela ficam vermelhas (mais forte com pouca vida)
    float red = std::clamp(h.hurtFlash * 0.55f + (1.0f - h.health / 100.0f) * 0.35f, 0.0f, 0.8f);
    if (h.dead) red = 0.6f;
    if (red > 0.01f) {
        ImU32 c0 = IM_COL32(140, 0, 0, (int)(red * 255)), c1 = IM_COL32(140, 0, 0, 0);
        float e = std::min(W, H) * 0.22f;
        dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(W, e), c0, c0, c1, c1);
        dl->AddRectFilledMultiColor(ImVec2(0, H - e), ImVec2(W, H), c1, c1, c0, c0);
        dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(e, H), c0, c1, c1, c0);
        dl->AddRectFilledMultiColor(ImVec2(W - e, 0), ImVec2(W, H), c1, c0, c0, c1);
    }
    // marcador de acerto em X no centro (vermelho quando mata)
    if (h.hitMarker > 0.01f) {
        ImU32 c = h.hitKill ? IM_COL32(255, 60, 50, (int)(h.hitMarker * 255)) : IM_COL32(255, 255, 255, (int)(h.hitMarker * 255));
        ImVec2 m(W * 0.5f, H * 0.5f);
        float a = 7 * sc, b = 15 * sc;
        for (int sx = -1; sx <= 1; sx += 2)
            for (int sy = -1; sy <= 1; sy += 2)
                dl->AddLine(ImVec2(m.x + sx * a, m.y + sy * a), ImVec2(m.x + sx * b, m.y + sy * b), c, 2.5f * sc);
    }
    // vida (canto inferior esquerdo)
    ImVec2 p0(24 * sc, H - 44 * sc);
    float bw = 260 * sc, bh = 14 * sc;
    dl->AddRectFilled(ImVec2(p0.x - 2, p0.y - 2), ImVec2(p0.x + bw + 2, p0.y + bh + 2), IM_COL32(0, 0, 0, 140), 3 * sc);
    float f = std::clamp(h.health / 100.0f, 0.0f, 1.0f);
    ImU32 hc = f > 0.5f ? IM_COL32(235, 235, 230, 230) : f > 0.25f ? IM_COL32(245, 170, 60, 235) : IM_COL32(235, 60, 50, 240);
    if (f > 0.0f) dl->AddRectFilled(p0, ImVec2(p0.x + bw * f, p0.y + bh), hc, 2 * sc);
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%d", (int)std::ceil(h.health));
    ImGui::PushFont(g_section);
    dl->AddText(ImVec2(p0.x, p0.y - 26 * sc), IM_COL32(255, 255, 255, 230), "VIDA");
    dl->AddText(ImVec2(p0.x + bw - ImGui::CalcTextSize(buf).x, p0.y - 26 * sc), IM_COL32(255, 255, 255, 230), buf);
    // inimigos (canto superior esquerdo), só perto/dentro da arena
    if (h.inArena && !h.dead) {
        std::snprintf(buf, sizeof(buf), "ONDA %d   INIMIGOS %d/%d   ABATES %d", h.wave, h.alive, h.total, h.kills);
        dl->AddText(ImVec2(24 * sc, 20 * sc), IM_COL32(255, 255, 255, 220), buf);
    }
    ImGui::PopFont();
    auto banner = [&](const char* big, const char* small, ImU32 col) {
        ImGui::PushFont(g_title);
        ImVec2 ts = ImGui::CalcTextSize(big);
        dl->AddText(ImVec2((W - ts.x) * 0.5f + 2, H * 0.3f + 2), IM_COL32(0, 0, 0, 160), big);
        dl->AddText(ImVec2((W - ts.x) * 0.5f, H * 0.3f), col, big);
        ImGui::PopFont();
        ImVec2 ss = ImGui::CalcTextSize(small);
        dl->AddText(ImVec2((W - ss.x) * 0.5f, H * 0.3f + ts.y + 8 * sc), IM_COL32(255, 255, 255, 220), small);
    };
    if (!h.dead && (h.coverHint || h.inCover)) { // dica de tecla embaixo da mira
        const char* tip = h.inCover ? "Q  sair da cobertura" : "Q  cobertura";
        ImVec2 ts = ImGui::CalcTextSize(tip);
        ImVec2 p((W - ts.x) * 0.5f, H * 0.62f);
        dl->AddRectFilled(ImVec2(p.x - 10 * sc, p.y - 5 * sc), ImVec2(p.x + ts.x + 10 * sc, p.y + ts.y + 5 * sc),
                          IM_COL32(0, 0, 0, 120), 4 * sc);
        dl->AddText(p, IM_COL32(255, 255, 255, h.inCover ? 150 : 230), tip);
    }
    if (h.dead) banner("VOCÊ MORREU", "Voltando ao início...", IM_COL32(235, 60, 50, 255));
    else if (h.nextWave >= 0.0f && h.inArena) {
        std::snprintf(buf, sizeof(buf), "Próxima onda em %d s", (int)std::ceil(h.nextWave));
        banner("ARENA LIMPA", buf, IM_COL32(kAccent.x * 255, kAccent.y * 255, kAccent.z * 255, 255));
    }
}

EditorAction drawGunEditor(GunEditor& ed, const char* const* gunNames, const bool* gunIsRifle, int gunCount,
                           float& panelFrac) {
    EditorAction action = EditorAction::None;
    ImGuiIO& io = ImGui::GetIO();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    float panelW = std::min(W * 0.5f, 560.0f * g_scale);
    panelFrac = panelW / std::max(W, 1.0f);
    ImGui::SetNextWindowPos(ImVec2(W - panelW, 0));
    ImGui::SetNextWindowSize(ImVec2(panelW, H));
    ImGui::Begin("##guneditor", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

    ImGui::PushFont(g_title);
    ImGui::TextUnformatted("EDITAR ARMA");
    ImGui::PopFont();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextWrapped("Arraste com o mouse no personagem para girar a câmera; roda do mouse aproxima.");
    ImGui::PopStyleColor();

    section("Arma");
    float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * (gunCount - 1)) / gunCount;
    for (int i = 0; i < gunCount; ++i) {
        if (i) ImGui::SameLine();
        bool sel = ed.gun == i;
        if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.85f));
        if (ImGui::Button(gunNames[i], ImVec2(bw, 38 * g_scale))) ed.gun = i;
        if (sel) ImGui::PopStyleColor();
    }
    const bool rifle = gunIsRifle[ed.gun];

    section("Modo");
    const char* modes[] = {"Mirando", "Segurando", rifle ? "Nas costas" : "Na cintura"};
    float mw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2) / 3;
    for (int i = 0; i < 3; ++i) {
        if (i) ImGui::SameLine();
        bool sel = ed.mode == i;
        if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.85f));
        if (ImGui::Button(modes[i], ImVec2(mw, 34 * g_scale))) ed.mode = i;
        if (sel) ImGui::PopStyleColor();
    }
    if (ed.mode == 2 && rifle) {
        const char* slots[] = {"Costas (embaixo)", "Costas (em cima)"};
        int si = ed.slot - 1;
        if (combo("Lugar", "##slot", &si, slots, 2, "Os dois fuzis ficam cruzados nas costas; cada lugar tem seu ajuste"))
            ed.slot = si + 1;
    }
    const char* anims[] = {"Parado", "Andando", "Correndo"};
    combo("Animação", "##anim", &ed.anim, anims, 3);

    GunDef dummy;
    dummy.name = gunNames[ed.gun];
    GunTuning& t = gunTuning(dummy);
    GunOffset& o = ed.mode == 0 ? t.aim : ed.mode == 1 ? t.carry : t.body[rifle ? ed.slot : 0];

    section("Posição (cm)");
    bool ch = false;
    ch |= sliderF("Frente / trás", "##mx", &o.move.x, -40.0f, 40.0f, "%.1f", "Para onde o cano aponta");
    ch |= sliderF("Cima / baixo", "##my", &o.move.y, -40.0f, 40.0f, "%.1f");
    ch |= sliderF("Direita / esquerda", "##mz", &o.move.z, -40.0f, 40.0f, "%.1f");
    section("Rotação (graus)");
    ch |= sliderF("Rolar", "##rx", &o.turn.x, -180.0f, 180.0f, "%.1f°", "Gira em volta do cano");
    ch |= sliderF("Virar", "##ry", &o.turn.y, -180.0f, 180.0f, "%.1f°", "Cano para a esquerda / direita");
    ch |= sliderF("Inclinar", "##rz", &o.turn.z, -180.0f, 180.0f, "%.1f°", "Cano para cima / baixo");
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextWrapped("Ctrl + clique num controle para digitar o valor.");
    ImGui::PopStyleColor();
    if (ImGui::Button("Zerar este modo", ImVec2(-1, 32 * g_scale))) { o = GunOffset(); ch = true; }
    if (ch) { ed.dirty = true; ed.status.clear(); }

    // rodapé: aplicar / desfazer / voltar / compilar
    float footer = 3 * 44.0f * g_scale + 40.0f * g_scale;
    ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), H - footer));
    ImGui::Separator();
    if (ed.dirty) ImGui::TextColored(kAccent, "Mudanças não aplicadas");
    else ImGui::TextDisabled("%s", ed.status.empty() ? "Tudo aplicado" : ed.status.c_str());
    float hw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.06f, 0.05f, 0.04f, 1.0f));
    if (ImGui::Button("Aplicar", ImVec2(hw, 38 * g_scale))) action = EditorAction::Apply;
    ImGui::PopStyleColor(2);
    ImGui::SameLine();
    ImGui::BeginDisabled(!ed.dirty);
    if (ImGui::Button("Desfazer mudanças", ImVec2(hw, 38 * g_scale))) action = EditorAction::Revert;
    ImGui::EndDisabled();
    if (ImGui::Button("Voltar ao menu", ImVec2(hw, 38 * g_scale))) action = EditorAction::Close;
    ImGui::SameLine();
    ImGui::BeginDisabled(builder::state() == builder::State::Running);
    if (ImGui::Button("Compilar", ImVec2(hw, 38 * g_scale))) action = EditorAction::Compile;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Aplica e gera a versão final em build-final/ga, sem o editor e sem este botão");
    ImGui::EndDisabled();
    ImGui::End();
    return action;
}

void drawBuildStatus() {
    builder::State st = builder::state();
    if (st == builder::State::Idle) return;
    static bool hidden = false;
    static builder::State last = builder::State::Idle;
    if (st != last) { hidden = false; last = st; }
    if (hidden) return;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(12 * g_scale, io.DisplaySize.y - 12 * g_scale), ImGuiCond_Always, ImVec2(0, 1));
    ImGui::SetNextWindowSize(ImVec2(std::min(io.DisplaySize.x * 0.45f, 620 * g_scale), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14 * g_scale, 10 * g_scale));
    ImGui::Begin("##build", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize);
    if (st == builder::State::Running) {
        const char* dots[] = {"", ".", "..", "..."};
        ImGui::TextColored(kAccent, "Compilando a versão final%s", dots[(int)(ImGui::GetTime() * 3) % 4]);
    } else if (st == builder::State::Ok) {
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1), "Versão final pronta:");
        ImGui::TextWrapped("%s", builder::outputPath().c_str());
    } else {
        ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.3f, 1), "A compilação falhou");
    }
    if (ImGui::TreeNode("Saída do cmake")) {
        std::string lg = builder::log();
        ImGui::BeginChild("buildlog", ImVec2(0, 200 * g_scale), true, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(lg.c_str());
        if (st == builder::State::Running) ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
        ImGui::TreePop();
    }
    if (st != builder::State::Running && ImGui::SmallButton("Fechar")) hidden = true;
    ImGui::End();
    ImGui::PopStyleVar();
}

} // namespace menu
