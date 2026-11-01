#pragma once
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <string>

// Tamanho dos painéis dos editores (esquerda, direita, embaixo), ajustável arrastando as divisórias
// entre eles e a vista. Fica salvo num arquivo para a próxima vez.
struct PanelLayout {
    // frações da janela: largura dos painéis da esquerda e da direita, altura do de baixo
    float l = 0.2f, r = 0.2f, b = 0.25f;
    float left = 0.0f, right = 0.0f, bottom = 0.0f; // em px na janela atual (calculado)
    std::string file;

    void init(const std::string& path, float fl, float fr, float fb) {
        if (file == path) return;
        file = path;
        l = fl;
        r = fr;
        b = fb;
        std::ifstream f(path);
        float a, c, d;
        if (f >> a >> c >> d && a < 1.0f && c < 1.0f && d < 1.0f) { l = a; r = c; b = d; } // arquivo antigo (px) é ignorado
    }
    void save() const {
        std::ofstream f(file);
        f << l << " " << r << " " << b << "\n";
    }
    // a vista fica com pelo menos 35% da largura e da altura; cada painel com pelo menos 140 px
    void clamp(float W, float H) {
        const float minL = std::min(0.3f, 140.0f / std::max(1.0f, W)), minB = std::min(0.3f, 90.0f / std::max(1.0f, H));
        l = std::clamp(l, minL, 0.45f);
        r = std::clamp(r, minL, 0.45f);
        if (l + r > 0.65f) { float k = 0.65f / (l + r); l *= k; r *= k; }
        b = std::clamp(b, minB, 0.6f);
        left = l * W;
        right = r * W;
        bottom = b * H;
    }

    // Alças nas bordas da vista (x0..x1, top..y1): passar o mouse muda o cursor, arrastar ajusta.
    void handle(float top, float W, float H) {
        clamp(W, H);
        ImGuiIO& io = ImGui::GetIO();
        const ImVec2 m = io.MousePos;
        const float x0 = left, x1 = W - right, y1 = H - bottom, grab = 5.0f;
        int hover = -1;
        if (m.y > top && std::fabs(m.x - x0) < grab) hover = 0;
        else if (m.y > top && std::fabs(m.x - x1) < grab) hover = 1;
        else if (m.x > x0 && m.x < x1 && std::fabs(m.y - y1) < grab) hover = 2;
        if (drag_ < 0 && hover >= 0 && ImGui::IsMouseClicked(0)) drag_ = hover;
        const int show = drag_ >= 0 ? drag_ : hover;
        if (show >= 0) {
            ImGui::SetMouseCursor(show == 2 ? ImGuiMouseCursor_ResizeNS : ImGuiMouseCursor_ResizeEW);
            ImDrawList* dl = ImGui::GetForegroundDrawList();
            const ImU32 c = IM_COL32(240, 160, 40, drag_ >= 0 ? 255 : 150);
            if (show == 0) dl->AddLine(ImVec2(x0, top), ImVec2(x0, H), c, 3.0f);
            else if (show == 1) dl->AddLine(ImVec2(x1, top), ImVec2(x1, H), c, 3.0f);
            else dl->AddLine(ImVec2(x0, y1), ImVec2(x1, y1), c, 3.0f);
        }
        if (drag_ >= 0) {
            if (ImGui::IsMouseDown(0)) {
                if (drag_ == 0) l = m.x / W;
                else if (drag_ == 1) r = (W - m.x) / W;
                else b = (H - m.y) / H;
                clamp(W, H);
            } else {
                drag_ = -1;
                save();
            }
        }
    }
    bool dragging() const { return drag_ >= 0; }

private:
    int drag_ = -1;
};
