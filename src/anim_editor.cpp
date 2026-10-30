#include "anim_editor.h"

#include "aim_ik.h"
#include "guns.h"

#include <GLFW/glfw3.h>
#include <imgui.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

glm::quat rotOf(const glm::mat4& m) { return glm::normalize(glm::quat_cast(glm::mat3(m))); }
float ease(float f) { return f * f * (3.0f - 2.0f * f); } // entra e sai devagar entre as chaves

// Chaves vizinhas de t numa lista ordenada: índices (i, j) e fração entre elas.
template <class K> bool around(const std::vector<K>& ks, float t, size_t& i, size_t& j, float& f) {
    if (ks.empty()) return false;
    if (t <= ks.front().t) { i = j = 0; f = 0.0f; return true; }
    if (t >= ks.back().t) { i = j = ks.size() - 1; f = 0.0f; return true; }
    for (j = 1; j < ks.size() && ks[j].t < t; ++j) {}
    i = j - 1;
    f = ease((t - ks[i].t) / std::max(1e-4f, ks[j].t - ks[i].t));
    return true;
}

template <class K, class V> void upsert(std::vector<K>& ks, float t, const V& make) {
    for (K& k : ks)
        if (std::fabs(k.t - t) < 1e-3f) { k = make(t); return; }
    ks.push_back(make(t));
    std::sort(ks.begin(), ks.end(), [](const K& a, const K& b) { return a.t < b.t; });
}

const ImU32 kAccent = IM_COL32(240, 160, 40, 255);

} // namespace

// ---------------------------------------------------------------- clipe
Pose AnimClip::sample(float t) const {
    Pose p;
    for (int b = 0; b < BONE_COUNT; ++b) {
        size_t i, j;
        float f;
        if (around(bones[b], t, i, j, f)) p.rot[b] = glm::normalize(glm::slerp(bones[b][i].q, bones[b][j].q, f));
    }
    size_t i, j;
    float f;
    if (around(root, t, i, j, f)) p.rootOffset = glm::mix(root[i].p, root[j].p, f);
    return p;
}

bool AnimClip::hasAnyKey() const {
    for (int b = 0; b < BONE_COUNT; ++b)
        if (!bones[b].empty()) return true;
    return !root.empty();
}

bool AnimClip::save(const std::string& path) const {
    std::ofstream o(path);
    if (!o) return false;
    o << "# animacao do editor (GA)\\n";
    o << "len " << length << "\\nloop " << (loop ? 1 : 0) << "\\nprop " << prop << "\\n";
    for (int b = 0; b < BONE_COUNT; ++b)
        for (const BoneKey& k : bones[b]) o << "key " << b << " " << k.t << " " << k.q.x << " " << k.q.y << " " << k.q.z << " " << k.q.w << "\\n";
    for (const RootKey& k : root) o << "root " << k.t << " " << k.p.x << " " << k.p.y << " " << k.p.z << "\\n";
    for (const PropKey& k : propKeys)
        o << "pkey " << k.t << " " << (k.hand ? 1 : 0) << " " << k.p.x << " " << k.p.y << " " << k.p.z << " " << k.q.x << " " << k.q.y
          << " " << k.q.z << " " << k.q.w << "\\n";
    return (bool)o;
}

bool AnimClip::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;
    *this = AnimClip();
    name = std::filesystem::path(path).stem().string();
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream l(line);
        std::string k;
        if (!(l >> k) || k[0] == '#') continue;
        if (k == "len") l >> length;
        else if (k == "loop") { int v; l >> v; loop = v != 0; }
        else if (k == "prop") l >> prop;
        else if (k == "key") {
            int b;
            BoneKey bk;
            l >> b >> bk.t >> bk.q.x >> bk.q.y >> bk.q.z >> bk.q.w;
            if (b >= 0 && b < BONE_COUNT) bones[b].push_back(bk);
        } else if (k == "root") {
            RootKey rk;
            l >> rk.t >> rk.p.x >> rk.p.y >> rk.p.z;
            root.push_back(rk);
        } else if (k == "pkey") {
            PropKey pk;
            int h;
            l >> pk.t >> h >> pk.p.x >> pk.p.y >> pk.p.z >> pk.q.x >> pk.q.y >> pk.q.z >> pk.q.w;
            pk.hand = h != 0;
            propKeys.push_back(pk);
        }
    }
    auto byT = [](const auto& a, const auto& b) { return a.t < b.t; };
    for (auto& v : bones) std::sort(v.begin(), v.end(), byT);
    std::sort(root.begin(), root.end(), byT);
    std::sort(propKeys.begin(), propKeys.end(), byT);
    return true;
}

// ---------------------------------------------------------------- editor
void AnimEditor::begin(const std::string& root, const glm::vec3& stagePos, float stageYaw) {
    active_ = true;
    root_ = root;
    stagePos_ = stagePos;
    stageYaw_ = stageYaw;
    playing_ = false;
    poseEdited_ = false;
    std::snprintf(nameBuf_, sizeof(nameBuf_), "%s", clip_.name.c_str());
    scanLibrary();
    status_ = "Clique num osso (bolinha) e arraste para girar. Shift + arrastar o quadril move o corpo.";
}

glm::mat4 AnimEditor::stageMatrix() const {
    return glm::translate(glm::mat4(1.0f), stagePos_) * glm::rotate(glm::mat4(1.0f), stageYaw_, glm::vec3(0, 1, 0));
}

AnimEditor::Rect AnimEditor::viewRect() const {
    return {300.0f, 56.0f, winW_ - 320.0f, winH_ - 230.0f};
}

bool AnimEditor::project(const glm::vec3& p, float& sx, float& sy) const {
    glm::vec4 c = proj_ * view_ * glm::vec4(p, 1.0f);
    if (c.w < 0.05f) return false;
    sx = (c.x / c.w * 0.5f + 0.5f) * winW_;
    sy = (0.5f - c.y / c.w * 0.5f) * winH_;
    return true;
}

void AnimEditor::scanLibrary() {
    namespace fs = std::filesystem;
    library_.clear();
    std::error_code ec;
    fs::create_directories(fs::path(root_) / "anims", ec);
    for (auto& e : fs::directory_iterator(fs::path(root_) / "anims", ec))
        if (e.path().extension() == ".anim") library_.push_back(e.path().stem().string());
    std::sort(library_.begin(), library_.end());
}

void AnimEditor::setBoneKey(int b, float t, const glm::quat& q) {
    upsert(clip_.bones[b], t, [&](float tt) { return AnimClip::BoneKey{tt, q}; });
    dirty_ = true;
}

void AnimEditor::setRootKey(float t, const glm::vec3& p) {
    upsert(clip_.root, t, [&](float tt) { return AnimClip::RootKey{tt, p}; });
    dirty_ = true;
}

void AnimEditor::keyAll(const Skeleton&) {
    for (int b = 0; b < BONE_COUNT; ++b) setBoneKey(b, time_, pose_.rot[b]);
    setRootKey(time_, pose_.rootOffset);
    poseEdited_ = false;
    status_ = "Keyframe inserido em " + std::to_string(time_).substr(0, 4) + " s (todos os ossos)";
}

void AnimEditor::deleteKeysAt(float t, bool onlySelected) {
    auto near = [&](float kt) { return std::fabs(kt - t) < 0.02f; };
    int n = 0;
    for (int b = 0; b < BONE_COUNT; ++b) {
        if (onlySelected && b != selBone_) continue;
        auto& v = clip_.bones[b];
        size_t s = v.size();
        v.erase(std::remove_if(v.begin(), v.end(), [&](const AnimClip::BoneKey& k) { return near(k.t); }), v.end());
        n += (int)(s - v.size());
    }
    if (!onlySelected || selBone_ == HIPS)
        clip_.root.erase(std::remove_if(clip_.root.begin(), clip_.root.end(), [&](const AnimClip::RootKey& k) { return near(k.t); }),
                         clip_.root.end());
    dirty_ = true;
    poseEdited_ = false;
    status_ = n ? "Keyframe apagado" : "Nenhum keyframe neste ponto";
}

glm::mat4 AnimEditor::propMatrix(const glm::mat4 globals[BONE_COUNT], const GunDef& def) const {
    const glm::mat4 M = stageMatrix();
    auto keyM = [&](const AnimClip::PropKey& k) {
        if (k.hand) return M * gunInHand(globals[HAND_R], def);
        glm::mat4 m = glm::mat4_cast(k.q);
        m[3] = glm::vec4(k.p, 1.0f);
        return M * m;
    };
    size_t i, j;
    float f;
    if (!around(clip_.propKeys, time_, i, j, f)) return M * gunInHand(globals[HAND_R], def);
    return blendRigid(keyM(clip_.propKeys[i]), keyM(clip_.propKeys[j]), f);
}

void AnimEditor::update(GLFWwindow* win, float dt, const Skeleton& sk, glm::mat4& view, glm::mat4& proj, glm::vec3& eye) {
    ImGuiIO& io = ImGui::GetIO();
    int ww, wh;
    glfwGetWindowSize(win, &ww, &wh);
    winW_ = (float)std::max(1, ww);
    winH_ = (float)std::max(1, wh);
    const Rect vr = viewRect();
    double mx, my;
    glfwGetCursorPos(win, &mx, &my);
    const bool inView = mx >= vr.x0 && mx <= vr.x1 && my >= vr.y0 && my <= vr.y1 && !io.WantCaptureMouse;
    const bool lmb = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    const bool rmb = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    const bool mmb = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
    const float dx = (float)(mx - lastMx_), dy = (float)(my - lastMy_);
    lastMx_ = mx;
    lastMy_ = my;

    // câmera: botão direito gira em volta, rodinha aproxima, botão do meio arrasta
    static bool orbiting = false, panning = false;
    if (rmb && !rmbWas_ && inView) orbiting = true;
    if (!rmb) orbiting = false;
    if (mmb && !mmbWas_ && inView) panning = true;
    if (!mmb) panning = false;
    if (orbiting) {
        camYaw_ -= dx * 0.006f;
        camPitch_ = std::clamp(camPitch_ + dy * 0.006f, -1.3f, 1.3f);
    }
    if (inView && io.MouseWheel != 0.0f) camDist_ = std::clamp(camDist_ * std::pow(0.88f, io.MouseWheel), 0.6f, 25.0f);
    glm::vec3 fwd(std::cos(camPitch_) * std::sin(camYaw_), -std::sin(camPitch_), std::cos(camPitch_) * std::cos(camYaw_));
    glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0))), up = glm::cross(right, fwd);
    if (panning) target_ += (-right * dx + up * dy) * camDist_ * 0.0016f;
    const glm::vec3 center = stagePos_ + target_;
    eye = center - fwd * camDist_;
    view_ = glm::lookAt(eye, center, glm::vec3(0, 1, 0));
    const float rw = vr.x1 - vr.x0, rh = vr.y1 - vr.y0;
    const glm::vec3 shift((vr.x0 + vr.x1) / winW_ - 1.0f, 1.0f - (vr.y0 + vr.y1) / winH_, 0.0f);
    proj_ = glm::translate(glm::mat4(1.0f), shift) * glm::scale(glm::mat4(1.0f), glm::vec3(rw / winW_, rh / winH_, 1.0f)) *
            glm::perspective(glm::radians(45.0f), rw / std::max(1.0f, rh), 0.05f, 500.0f);
    view = view_;
    proj = proj_;

    // atalhos: espaço toca/pausa, K insere keyframe, setas andam quadro a quadro
    if (!io.WantTextInput) {
        static bool spaceWas = false, kWas = false, lWas = false, rWas = false, delWas = false;
        bool sp = glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS, k = glfwGetKey(win, GLFW_KEY_K) == GLFW_PRESS;
        bool l = glfwGetKey(win, GLFW_KEY_LEFT) == GLFW_PRESS, r = glfwGetKey(win, GLFW_KEY_RIGHT) == GLFW_PRESS;
        bool del = glfwGetKey(win, GLFW_KEY_DELETE) == GLFW_PRESS;
        if (sp && !spaceWas) playing_ = !playing_;
        if (k && !kWas) keyAll(sk);
        if (l && !lWas) { time_ = std::max(0.0f, time_ - 1.0f / 30.0f); poseEdited_ = false; }
        if (r && !rWas) { time_ = std::min(clip_.length, time_ + 1.0f / 30.0f); poseEdited_ = false; }
        if (del && !delWas) deleteKeysAt(time_, false);
        spaceWas = sp; kWas = k; lWas = l; rWas = r; delWas = del;
    }
    if (playing_) {
        time_ += dt;
        if (time_ > clip_.length) {
            if (clip_.loop) time_ = std::fmod(time_, std::max(0.01f, clip_.length));
            else { time_ = clip_.length; playing_ = false; }
        }
        poseEdited_ = false;
    }
    if (!poseEdited_) pose_ = clip_.sample(time_);

    // ossos na tela
    const glm::mat4 M = stageMatrix();
    computeGlobals(sk, pose_, globals_);
    for (int b = 0; b < BONE_COUNT; ++b) {
        float sx, sy;
        bool ok = project(glm::vec3(M * globals_[b][3]), sx, sy);
        jointScreen_[b] = glm::vec3(sx, sy, ok ? 1.0f : 0.0f);
    }
    // clicar num osso seleciona e começa a girar (Shift no quadril: move o corpo)
    if (lmb && !lmbWas_ && inView) {
        int best = -1;
        float bestD = 14.0f;
        for (int b = 0; b < BONE_COUNT; ++b) {
            if (jointScreen_[b].z < 0.5f) continue;
            float d = glm::length(glm::vec2(jointScreen_[b]) - glm::vec2((float)mx, (float)my));
            if (d < bestD) { bestD = d; best = b; }
        }
        if (best >= 0) {
            selBone_ = best;
            dragging_ = true;
            draggingRoot_ = best == HIPS && (glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(win, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);
            playing_ = false;
            status_ = std::string(draggingRoot_ ? "Movendo " : "Girando ") + sk.name[best];
        }
    }
    if (dragging_ && lmb && (dx != 0.0f || dy != 0.0f)) {
        // eixos da câmera no espaço do personagem
        const glm::mat3 inv = glm::transpose(glm::mat3(M));
        const glm::vec3 camR = inv * right, camU = inv * up;
        if (draggingRoot_) {
            pose_.rootOffset += (camR * dx - camU * dy) * camDist_ * 0.0016f;
        } else {
            const int b = selBone_;
            glm::quat q = glm::angleAxis(dx * 0.012f, camU) * glm::angleAxis(dy * 0.012f, camR);
            glm::quat g = rotOf(globals_[b]);
            glm::quat pg = sk.parent[b] >= 0 ? rotOf(globals_[sk.parent[b]]) : glm::quat(1, 0, 0, 0);
            pose_.rot[b] = glm::normalize(glm::inverse(pg) * q * g);
        }
        poseEdited_ = true;
    }
    if (dragging_ && !lmb) {
        dragging_ = false;
        if (poseEdited_ && autoKey_) {
            if (draggingRoot_) setRootKey(time_, pose_.rootOffset);
            else setBoneKey(selBone_, time_, pose_.rot[selBone_]);
            poseEdited_ = false;
        }
    }
    lmbWas_ = lmb;
    rmbWas_ = rmb;
    mmbWas_ = mmb;
}

void AnimEditor::timelineUi(float x0, float y0, float w, float h) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float len = std::max(0.05f, clip_.length);
    auto tx = [&](float t) { return x0 + t / len * w; };
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + w, y0 + h), IM_COL32(28, 28, 31, 255), 4.0f);
    // régua: marca a cada 0,1 s, número a cada segundo
    for (float t = 0.0f; t <= len + 1e-3f; t += 0.1f) {
        bool sec = std::fabs(t - std::round(t)) < 1e-3f;
        dl->AddLine(ImVec2(tx(t), y0), ImVec2(tx(t), y0 + (sec ? 14.0f : 7.0f)), IM_COL32(110, 110, 120, 255));
        if (sec) dl->AddText(ImVec2(tx(t) + 3, y0 + 1), IM_COL32(160, 160, 170, 255), (std::to_string((int)std::round(t)) + "s").c_str());
    }
    // linha "todos os ossos" e linha do osso escolhido
    const float rowAll = y0 + 30, rowSel = y0 + 58;
    dl->AddText(ImVec2(x0 + 4, rowAll - 22), IM_COL32(150, 150, 160, 255), "");
    std::vector<float> all;
    for (int b = 0; b < BONE_COUNT; ++b)
        for (auto& k : clip_.bones[b]) all.push_back(k.t);
    for (auto& k : clip_.root) all.push_back(k.t);
    auto diamond = [&](float x, float y, float r, ImU32 c) {
        dl->AddQuadFilled(ImVec2(x, y - r), ImVec2(x + r, y), ImVec2(x, y + r), ImVec2(x - r, y), c);
    };
    for (float t : all) diamond(tx(t), rowAll, 5.0f, IM_COL32(170, 170, 180, 255));
    for (auto& k : clip_.bones[selBone_]) diamond(tx(k.t), rowSel, 7.0f, IM_COL32(255, 205, 80, 255));
    if (selBone_ == HIPS)
        for (auto& k : clip_.root) diamond(tx(k.t), rowSel, 5.0f, IM_COL32(120, 200, 255, 255));
    for (auto& k : clip_.propKeys) diamond(tx(k.t), y0 + h - 12, 6.0f, k.hand ? IM_COL32(120, 220, 120, 255) : IM_COL32(220, 120, 220, 255));
    // cabeça de leitura
    dl->AddLine(ImVec2(tx(time_), y0), ImVec2(tx(time_), y0 + h), kAccent, 2.0f);
    dl->AddTriangleFilled(ImVec2(tx(time_) - 6, y0), ImVec2(tx(time_) + 6, y0), ImVec2(tx(time_), y0 + 8), kAccent);
    // clicar/arrastar na régua: muda o tempo
    ImGui::SetCursorScreenPos(ImVec2(x0, y0));
    ImGui::InvisibleButton("##linha", ImVec2(w, h));
    if (ImGui::IsItemActive()) {
        float t = (ImGui::GetIO().MousePos.x - x0) / w * len;
        // perto de uma chave: gruda nela
        for (float kt : all)
            if (std::fabs(tx(kt) - ImGui::GetIO().MousePos.x) < 6.0f) t = kt;
        time_ = std::clamp(t, 0.0f, len);
        playing_ = false;
        poseEdited_ = false;
    }
}

bool AnimEditor::drawUi(const Skeleton& sk, const std::vector<std::string>& gunNames) {
    bool back = false;
    const ImGuiWindowFlags fixed = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    const Rect vr = viewRect();
    namespace fs = std::filesystem;

    // ---- barra de cima
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(winW_, vr.y0));
    ImGui::Begin("##anim_barra", nullptr, fixed | ImGuiWindowFlags_NoTitleBar);
    if (ImGui::Button("< Voltar ao cenário")) back = true;
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.95f, 0.62f, 0.16f, 1), "Criar animação");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220);
    if (ImGui::InputText("##nome", nameBuf_, sizeof(nameBuf_))) { clip_.name = nameBuf_; dirty_ = true; }
    ImGui::SameLine();
    if (ImGui::Button(dirty_ ? "Salvar *" : "Salvar")) {
        std::string n = clip_.name.empty() ? "animacao" : clip_.name;
        for (char& ch : n) if (ch == ' ' || ch == '/' || ch == '\\\\') ch = '_';
        clip_.name = n;
        std::snprintf(nameBuf_, sizeof(nameBuf_), "%s", n.c_str());
        file_ = (fs::path(root_) / "anims" / (n + ".anim")).string();
        if (clip_.save(file_)) { dirty_ = false; status_ = "Salvo em anims/" + n + ".anim"; scanLibrary(); }
        else status_ = "Erro ao salvar";
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", status_.c_str());
    ImGui::End();

    // ---- esquerda: biblioteca e ossos
    ImGui::SetNextWindowPos(ImVec2(0, vr.y0));
    ImGui::SetNextWindowSize(ImVec2(vr.x0, winH_ - vr.y0));
    ImGui::Begin("Biblioteca de animações", nullptr, fixed);
    if (ImGui::Button("+ Nova animação", ImVec2(-1, 0))) {
        clip_ = AnimClip();
        int n = 1;
        do clip_.name = "animacao_" + std::to_string(n++);
        while (std::find(library_.begin(), library_.end(), clip_.name) != library_.end());
        std::snprintf(nameBuf_, sizeof(nameBuf_), "%s", clip_.name.c_str());
        file_.clear();
        time_ = 0.0f;
        dirty_ = true;
        poseEdited_ = false;
    }
    ImGui::BeginChild("lib", ImVec2(0, 200), true);
    for (const std::string& n : library_) {
        if (ImGui::Selectable(n.c_str(), n == clip_.name)) {
            AnimClip c;
            if (c.load((fs::path(root_) / "anims" / (n + ".anim")).string())) {
                clip_ = c;
                std::snprintf(nameBuf_, sizeof(nameBuf_), "%s", n.c_str());
                time_ = 0.0f;
                dirty_ = false;
                poseEdited_ = false;
                status_ = "Aberta: " + n;
            }
        }
    }
    if (library_.empty()) ImGui::TextDisabled("Nenhuma animação salva ainda.");
    ImGui::EndChild();
    ImGui::Separator();
    ImGui::TextUnformatted("Ossos do personagem");
    ImGui::BeginChild("ossos", ImVec2(0, 0), true);
    for (int b = 0; b < BONE_COUNT; ++b) {
        std::string label = std::string(sk.name[b]) + (clip_.bones[b].empty() ? "" : "  *");
        if (ImGui::Selectable(label.c_str(), selBone_ == b)) selBone_ = b;
    }
    ImGui::EndChild();
    ImGui::End();

    // ---- direita: osso escolhido e objeto da cena
    ImGui::SetNextWindowPos(ImVec2(vr.x1, vr.y0));
    ImGui::SetNextWindowSize(ImVec2(winW_ - vr.x1, winH_ - vr.y0));
    ImGui::Begin("Detalhes##anim", nullptr, fixed);
    ImGui::TextColored(ImVec4(0.95f, 0.62f, 0.16f, 1), "Osso: %s", sk.name[selBone_]);
    {
        glm::vec3 e = glm::degrees(glm::eulerAngles(pose_.rot[selBone_]));
        ImGui::PushItemWidth(-60);
        bool ch = ImGui::DragFloat("Giro X", &e.x, 0.5f, -180.0f, 180.0f, "%.1f°");
        bool done = ImGui::IsItemDeactivatedAfterEdit();
        ch |= ImGui::DragFloat("Giro Y", &e.y, 0.5f, -180.0f, 180.0f, "%.1f°");
        done |= ImGui::IsItemDeactivatedAfterEdit();
        ch |= ImGui::DragFloat("Giro Z", &e.z, 0.5f, -180.0f, 180.0f, "%.1f°");
        done |= ImGui::IsItemDeactivatedAfterEdit();
        if (ch) { pose_.rot[selBone_] = glm::normalize(glm::quat(glm::radians(e))); poseEdited_ = true; playing_ = false; }
        if (selBone_ == HIPS) {
            ch = ImGui::DragFloat3("Posição do corpo", &pose_.rootOffset.x, 0.005f);
            if (ch) { poseEdited_ = true; playing_ = false; }
            if (ImGui::IsItemDeactivatedAfterEdit() && autoKey_) { setRootKey(time_, pose_.rootOffset); poseEdited_ = false; }
        }
        ImGui::PopItemWidth();
        if (done && autoKey_) { setBoneKey(selBone_, time_, pose_.rot[selBone_]); poseEdited_ = false; }
        if (ImGui::Button("Zerar este osso")) {
            pose_.rot[selBone_] = glm::quat(1, 0, 0, 0);
            if (autoKey_) setBoneKey(selBone_, time_, pose_.rot[selBone_]);
            else poseEdited_ = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Chave só deste osso")) { setBoneKey(selBone_, time_, pose_.rot[selBone_]); poseEdited_ = false; }
    }
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.95f, 0.62f, 0.16f, 1), "Objeto na cena");
    {
        std::string cur = clip_.prop < 0 || clip_.prop >= (int)gunNames.size() ? "(nenhum)" : gunNames[clip_.prop];
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##prop", cur.c_str())) {
            if (ImGui::Selectable("(nenhum)", clip_.prop < 0)) { clip_.prop = -1; dirty_ = true; }
            for (int i = 0; i < (int)gunNames.size(); ++i)
                if (ImGui::Selectable(gunNames[i].c_str(), clip_.prop == i)) { clip_.prop = i; dirty_ = true; }
            ImGui::EndCombo();
        }
        if (clip_.prop >= 0) {
            AnimClip::PropKey* k = nullptr;
            for (auto& pk : clip_.propKeys)
                if (std::fabs(pk.t - time_) < 0.02f) k = &pk;
            if (!k) {
                ImGui::TextWrapped("Sem chave do objeto neste ponto. Sem chaves ele fica na mão direita.");
                if (ImGui::Button("Chave: na mão", ImVec2(-1, 0))) {
                    upsert(clip_.propKeys, time_, [&](float tt) { return AnimClip::PropKey{tt, true, glm::vec3(0.0f), glm::quat(1, 0, 0, 0)}; });
                    dirty_ = true;
                }
                if (ImGui::Button("Chave: solto (a partir da mão)", ImVec2(-1, 0))) {
                    // começa onde a mão está agora (no espaço do personagem)
                    glm::mat4 h = globals_[HAND_R];
                    upsert(clip_.propKeys, time_, [&](float tt) { return AnimClip::PropKey{tt, false, glm::vec3(h[3]) + glm::vec3(0.0f, 0.0f, 0.2f), rotOf(h)}; });
                    dirty_ = true;
                }
            } else {
                if (ImGui::Checkbox("Na mão direita", &k->hand)) dirty_ = true;
                if (!k->hand) {
                    ImGui::PushItemWidth(-60);
                    if (ImGui::DragFloat3("Posição", &k->p.x, 0.01f)) dirty_ = true;
                    glm::vec3 e = glm::degrees(glm::eulerAngles(k->q));
                    if (ImGui::DragFloat3("Giro", &e.x, 0.5f)) { k->q = glm::normalize(glm::quat(glm::radians(e))); dirty_ = true; }
                    ImGui::PopItemWidth();
                    ImGui::TextDisabled("Solto: o objeto voa até essa posição\\n(ex.: arma jogada para outra pessoa).");
                }
                if (ImGui::Button("Apagar chave do objeto", ImVec2(-1, 0))) {
                    float t0 = k->t;
                    clip_.propKeys.erase(std::remove_if(clip_.propKeys.begin(), clip_.propKeys.end(),
                                                        [&](const AnimClip::PropKey& p) { return std::fabs(p.t - t0) < 0.02f; }),
                                         clip_.propKeys.end());
                    dirty_ = true;
                }
            }
        }
    }
    ImGui::Separator();
    ImGui::TextWrapped("Vista: botão direito gira a câmera, rodinha aproxima, botão do meio arrasta. "
                       "Atalhos: Espaço toca/pausa, K insere keyframe, setas andam quadro a quadro, Delete apaga keyframe.");
    ImGui::End();

    // ---- embaixo: linha do tempo
    ImGui::SetNextWindowPos(ImVec2(vr.x0, vr.y1));
    ImGui::SetNextWindowSize(ImVec2(vr.x1 - vr.x0, winH_ - vr.y1));
    ImGui::Begin("Linha do tempo", nullptr, fixed);
    if (ImGui::Button("|<")) { time_ = 0.0f; poseEdited_ = false; }
    ImGui::SameLine();
    if (ImGui::Button("< chave")) {
        float best = -1.0f;
        for (int b = 0; b < BONE_COUNT; ++b)
            for (auto& k : clip_.bones[b]) if (k.t < time_ - 1e-3f) best = std::max(best, k.t);
        if (best >= 0.0f) { time_ = best; poseEdited_ = false; }
    }
    ImGui::SameLine();
    if (ImGui::Button(playing_ ? "Pausar" : "Tocar", ImVec2(70, 0))) playing_ = !playing_;
    ImGui::SameLine();
    if (ImGui::Button("chave >")) {
        float best = 1e9f;
        for (int b = 0; b < BONE_COUNT; ++b)
            for (auto& k : clip_.bones[b]) if (k.t > time_ + 1e-3f) best = std::min(best, k.t);
        if (best < 1e8f) { time_ = best; poseEdited_ = false; }
    }
    ImGui::SameLine();
    ImGui::Text("%.2f s", time_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    if (ImGui::DragFloat("Duração", &clip_.length, 0.05f, 0.1f, 120.0f, "%.2f s")) dirty_ = true;
    ImGui::SameLine();
    if (ImGui::Checkbox("Repetir", &clip_.loop)) dirty_ = true;
    ImGui::SameLine();
    ImGui::Checkbox("Auto-chave", &autoKey_);
    ImGui::SameLine();
    if (ImGui::Button("Inserir keyframe (K)")) keyAll(sk);
    ImGui::SameLine();
    if (ImGui::Button("Apagar keyframe")) deleteKeysAt(time_, false);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x, h = std::max(60.0f, ImGui::GetContentRegionAvail().y - 4.0f);
    timelineUi(p.x, p.y, w, h);
    ImGui::End();

    // ---- ossos desenhados por cima da vista
    ImDrawList* fg = ImGui::GetBackgroundDrawList();
    for (int b = 0; b < BONE_COUNT; ++b) {
        int pb = sk.parent[b];
        if (pb >= 0 && jointScreen_[b].z > 0.5f && jointScreen_[pb].z > 0.5f)
            fg->AddLine(ImVec2(jointScreen_[pb].x, jointScreen_[pb].y), ImVec2(jointScreen_[b].x, jointScreen_[b].y), IM_COL32(90, 200, 255, 200), 2.0f);
    }
    for (int b = 0; b < BONE_COUNT; ++b) {
        if (jointScreen_[b].z < 0.5f) continue;
        bool sel = b == selBone_;
        bool finger = b >= FING1_L;
        fg->AddCircleFilled(ImVec2(jointScreen_[b].x, jointScreen_[b].y), sel ? 7.0f : finger ? 3.0f : 5.0f,
                            sel ? kAccent : clip_.bones[b].empty() ? IM_COL32(230, 230, 235, 230) : IM_COL32(255, 215, 110, 240));
        if (sel) fg->AddText(ImVec2(jointScreen_[b].x + 10, jointScreen_[b].y - 8), kAccent, sk.name[b]);
    }
    return back;
}
