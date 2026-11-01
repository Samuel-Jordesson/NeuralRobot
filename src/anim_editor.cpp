#include "anim_editor.h"

#include "aim_ik.h"
#include "guns.h"

#include <GLFW/glfw3.h>
#include <imgui.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

glm::quat rotOf(const glm::mat4& m) { return glm::normalize(glm::quat_cast(glm::mat3(m))); }
float ease(float f) { return f * f * (3.0f - 2.0f * f); } // entra e sai devagar entre as chaves

// Chaves vizinhas de t numa lista ordenada: índices (i, j) e fração entre elas.
template <class K> bool around(const std::vector<K>& ks, float t, size_t& i, size_t& j, float& f, bool linear = false) {
    if (ks.empty()) return false;
    if (t <= ks.front().t) { i = j = 0; f = 0.0f; return true; }
    if (t >= ks.back().t) { i = j = ks.size() - 1; f = 0.0f; return true; }
    for (j = 1; j < ks.size() && ks[j].t < t; ++j) {}
    i = j - 1;
    f = (t - ks[i].t) / std::max(1e-4f, ks[j].t - ks[i].t);
    if (!linear) f = ease(f);
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
        if (around(bones[b], t, i, j, f, linear)) p.rot[b] = glm::normalize(glm::slerp(bones[b][i].q, bones[b][j].q, f));
    }
    size_t i, j;
    float f;
    if (around(root, t, i, j, f, linear)) p.rootOffset = glm::mix(root[i].p, root[j].p, f);
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
    o << "# animacao do editor (GA)\n";
    o << "len " << length << "\nloop " << (loop ? 1 : 0) << "\nprop " << prop << "\nlinear " << (linear ? 1 : 0) << "\n";
    for (int b = 0; b < BONE_COUNT; ++b)
        for (const BoneKey& k : bones[b]) o << "key " << b << " " << k.t << " " << k.q.x << " " << k.q.y << " " << k.q.z << " " << k.q.w << "\n";
    for (const CamKey& k : camKeys)
        o << "cam " << k.t << " " << k.p.x << " " << k.p.y << " " << k.p.z << " " << k.q.x << " " << k.q.y << " " << k.q.z << " "
          << k.q.w << " " << k.focal << " " << k.focus << "\n";
    for (const DevKey& k : device) o << "dev " << k.t << " " << k.ori.x << " " << k.ori.y << " " << k.ori.z << "\n";
    for (const RootKey& k : root) o << "root " << k.t << " " << k.p.x << " " << k.p.y << " " << k.p.z << "\n";
    for (const PropKey& k : propKeys)
        o << "pkey " << k.t << " " << (k.hand ? 1 : 0) << " " << k.p.x << " " << k.p.y << " " << k.p.z << " " << k.q.x << " " << k.q.y
          << " " << k.q.z << " " << k.q.w << "\n";
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
        else if (k == "linear") { int v; l >> v; linear = v != 0; }
        else if (k == "cam") {
            CamKey c;
            l >> c.t >> c.p.x >> c.p.y >> c.p.z >> c.q.x >> c.q.y >> c.q.z >> c.q.w >> c.focal >> c.focus;
            camKeys.push_back(c);
        } else if (k == "dev") { DevKey d; l >> d.t >> d.ori.x >> d.ori.y >> d.ori.z; device.push_back(d); }
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
    std::sort(camKeys.begin(), camKeys.end(), byT);
    return true;
}

// ---------------------------------------------------------------- editor
void AnimEditor::begin(const std::string& root, const glm::vec3& stagePos, float stageYaw) {
    active_ = true;
    root_ = root;
    layout_.init(root + "/editor_animacao.ini", 0.18f, 0.22f, 0.28f);
    stagePos_ = stagePos;
    stageYaw_ = stageYaw;
    playing_ = false;
    poseEdited_ = false;
    std::snprintf(nameBuf_, sizeof(nameBuf_), "%s", clip_.name.c_str());
    scanLibrary();
    status_ = "Clique num osso (bolinha) e arraste para girar. Shift + arrastar o quadril move o corpo.";
    if (std::getenv("GA_MOCAP")) serverOk_ = serverTried_ = mocap_.startServer(root_); // depuração: já escuta a câmera
}

glm::mat4 AnimEditor::stageMatrix() const {
    return glm::translate(glm::mat4(1.0f), stagePos_) * glm::rotate(glm::mat4(1.0f), stageYaw_, glm::vec3(0, 1, 0));
}

AnimEditor::Rect AnimEditor::viewRect() const {
    return {layout_.l * winW_, 56.0f, winW_ * (1.0f - layout_.r), winH_ * (1.0f - layout_.b)};
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
    const bool inView = mx >= vr.x0 + 6 && mx <= vr.x1 - 6 && my >= vr.y0 && my <= vr.y1 - 6 && !io.WantCaptureMouse &&
                        !layout_.dragging();
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
    if (pilot_) {
        // vista pela câmera de cena: botão direito gira, botão do meio desloca, rodinha anda para a frente
        static bool rot = false, pan = false, wasEditing = false;
        if (rmb && !rmbWas_ && inView) rot = true;
        if (!rmb) rot = false;
        if (mmb && !mmbWas_ && inView) pan = true;
        if (!mmb) pan = false;
        orbiting = panning = false;
        const glm::vec3 r = camRot_ * glm::vec3(1, 0, 0), u = camRot_ * glm::vec3(0, 1, 0), f = camRot_ * glm::vec3(0, 0, -1);
        bool editing = rot || pan;
        if (rot) camRot_ = glm::normalize(glm::angleAxis(-dx * 0.004f, glm::vec3(0, 1, 0)) * glm::angleAxis(-dy * 0.004f, r) * camRot_);
        if (pan) camPos_ += (-r * dx + u * dy) * 0.004f;
        if (inView && io.MouseWheel != 0.0f) { camPos_ += f * io.MouseWheel * 0.25f; editing = true; camEdited_ = true; if (autoKey_) setCamKey(); }
        if (rot || pan) camEdited_ = true;
        if (wasEditing && !editing && autoKey_) setCamKey();
        wasEditing = rot || pan;
        view_ = cineView();
        eye = cineEye();
        proj_ = glm::translate(glm::mat4(1.0f), shift) * glm::scale(glm::mat4(1.0f), glm::vec3(rw / winW_, rh / winH_, 1.0f)) *
                cineProj(rw / std::max(1.0f, rh));
    }
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
    // câmera ao vivo: o boneco copia a pessoa (gravando ou só olhando)
    mocap_.poll(sk, dt);
    if (mocapFollow_ && mocap_.live() && !playing_) pose_ = mocap_.pose();
    // câmera de cena: segue as chaves (tocando ou parado num ponto), a não ser que esteja sendo mexida
    if (playing_) camEdited_ = false;
    if (!camEdited_ && !clip_.camKeys.empty()) {
        size_t i, j;
        float f;
        if (around(clip_.camKeys, time_, i, j, f, clip_.linear)) {
            const auto &a = clip_.camKeys[i], &b = clip_.camKeys[j];
            camPos_ = glm::mix(a.p, b.p, f);
            camRot_ = glm::normalize(glm::slerp(a.q, b.q, f));
            focal_ = a.focal + (b.focal - a.focal) * f;
            focus_ = a.focus + (b.focus - a.focus) * f;
        }
    }
    // celular como câmera (AR): o giro do aparelho no mundo vira o giro da câmera de cena
    if (arCam_ && mocap_.oriLive()) {
        const glm::vec3 o = glm::radians(mocap_.orientation());
        const glm::quat dev = glm::angleAxis(o.x, glm::vec3(0, 0, 1)) * glm::angleAxis(o.y, glm::vec3(1, 0, 0)) *
                              glm::angleAxis(o.z, glm::vec3(0, 1, 0)); // W3C: Z(alpha) X(beta) Y(gamma), terra = leste/norte/cima
        if (!arInit_) { arRef_ = dev; arBase_ = camRot_; arInit_ = true; }
        // terra (leste, norte, cima) -> nosso mundo (X, -Z, Y); depois para o espaço do palco
        static const glm::quat toWorld = glm::quat_cast(glm::mat3(glm::vec3(1, 0, 0), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0)));
        glm::quat d = toWorld * (dev * glm::inverse(arRef_)) * glm::inverse(toWorld);
        d = glm::inverse(stageRot()) * d * stageRot();
        camRot_ = glm::normalize(glm::slerp(camRot_, glm::normalize(d * arBase_), 0.6f));
        camEdited_ = true;
    } else if (!arCam_) arInit_ = false;
    if (mocap_.recording()) recCam_.push_back({mocap_.recordedTime(), camPos_, camRot_, focal_, focus_});

    // ossos na tela
    const glm::mat4 M = stageMatrix();
    computeGlobals(sk, pose_, globals_);
    for (int b = 0; b < BONE_COUNT; ++b) {
        float sx, sy;
        bool ok = project(glm::vec3(M * globals_[b][3]), sx, sy);
        jointScreen_[b] = glm::vec3(sx, sy, ok ? 1.0f : 0.0f);
    }
    // câmera de cena na vista: arrastar o ícone move a câmera
    {
        float sx, sy;
        bool ok = !pilot_ && project(cineEye(), sx, sy);
        camScreen_ = glm::vec3(sx, sy, ok ? 1.0f : 0.0f);
        if (ok && lmb && !lmbWas_ && inView && glm::length(glm::vec2(sx, sy) - glm::vec2((float)mx, (float)my)) < 16.0f) {
            camGrab_ = true;
            playing_ = false;
            status_ = "Movendo a câmera de cena (solte para marcar a chave, se Auto-chave estiver ligado)";
        }
        if (camGrab_ && lmb && (dx != 0.0f || dy != 0.0f)) {
            const glm::mat3 inv = glm::transpose(glm::mat3(stageMatrix()));
            camPos_ += (inv * right * dx - inv * up * dy) * camDist_ * 0.0016f;
            camEdited_ = true;
        }
        if (camGrab_ && !lmb) {
            camGrab_ = false;
            if (autoKey_) setCamKey();
        }
    }
    // clicar num osso seleciona e começa a girar (Shift no quadril: move o corpo)
    if (lmb && !lmbWas_ && inView && !camGrab_) {
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
    layout_.handle(56.0f, winW_, winH_); // divisórias arrastáveis entre os painéis
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
        for (char& ch : n) if (ch == ' ' || ch == '/' || ch == '\\') ch = '_';
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
                    ImGui::TextDisabled("Solto: o objeto voa até essa posição\n(ex.: arma jogada para outra pessoa).");
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
    cameraUi();
    ImGui::Separator();
    mocapUi();
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
    // prévia da câmera de cena (16:9) à direita da linha do tempo
    float pw = 0.0f;
    if (preview_ && previewTex_) {
        pw = std::min(h * 16.0f / 9.0f, w * 0.5f);
        float ph = pw * 9.0f / 16.0f;
        ImVec2 a(p.x + w - pw, p.y);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddImage((ImTextureID)(intptr_t)previewTex_, a, ImVec2(a.x + pw, a.y + ph), ImVec2(0, 1), ImVec2(1, 0));
        dl->AddRect(a, ImVec2(a.x + pw, a.y + ph), IM_COL32(240, 160, 40, 255));
        char cap[64];
        std::snprintf(cap, sizeof(cap), "Câmera  %.0f mm  f/%.1f  foco %.1f m", focal_, fstop_, focus_);
        dl->AddRectFilled(ImVec2(a.x, a.y), ImVec2(a.x + ImGui::CalcTextSize(cap).x + 10, a.y + ImGui::GetTextLineHeight() + 4),
                          IM_COL32(0, 0, 0, 150));
        dl->AddText(ImVec2(a.x + 5, a.y + 2), IM_COL32(255, 255, 255, 255), cap);
        pw += 10.0f;
    }
    timelineUi(p.x, p.y, w - pw, h);
    ImGui::End();

    // ---- câmera de cena na vista: corpo e cone de visão (o que aparece na prévia)
    ImDrawList* fg = ImGui::GetBackgroundDrawList();
    if (!pilot_) {
        const glm::vec3 e = cineEye();
        const glm::quat q = stageRot() * camRot_;
        const float tx = 18.0f / focal_, ty = tx * 9.0f / 16.0f, d = 0.6f;
        glm::vec3 c[4];
        for (int i = 0; i < 4; ++i) c[i] = e + q * glm::vec3((i & 1 ? 1 : -1) * tx * d, (i & 2 ? 1 : -1) * ty * d, -d);
        float ex, ey;
        ImVec2 cs[4];
        bool ok = project(e, ex, ey);
        for (int i = 0; i < 4; ++i) { float x, y; ok = ok && project(c[i], x, y); cs[i] = ImVec2(x, y); }
        const ImU32 col = camGrab_ ? IM_COL32(255, 210, 90, 255) : IM_COL32(240, 160, 40, 230);
        if (ok) {
            for (int i = 0; i < 4; ++i) fg->AddLine(ImVec2(ex, ey), cs[i], col, 1.5f);
            fg->AddLine(cs[0], cs[1], col, 1.5f);
            fg->AddLine(cs[1], cs[3], col, 1.5f);
            fg->AddLine(cs[3], cs[2], col, 1.5f);
            fg->AddLine(cs[2], cs[0], col, 1.5f);
            fg->AddCircleFilled(ImVec2(ex, ey), 7.0f, col);
            fg->AddText(ImVec2(ex + 10, ey - 8), col, "Câmera");
        }
    }
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

// ---------------------------------------------------------------- captura de movimento (câmera)
void AnimEditor::bakeRecording() {
    const auto& fr = mocap_.frames();
    if (fr.size() < 2) { status_ = "Gravação curta demais"; return; }
    for (int b = 0; b < kBodyBones; ++b) clip_.bones[b].clear(); // os dedos continuam como estavam
    clip_.root.clear();
    clip_.device.clear();
    float last = -1.0f;
    for (const MocapLive::Frame& f : fr) {
        if (f.t - last < 1.0f / 60.0f) continue; // no máximo 60 chaves por segundo
        last = f.t;
        for (int b = 0; b < kBodyBones; ++b) clip_.bones[b].push_back({f.t, f.pose.rot[b]});
        clip_.root.push_back({f.t, f.pose.rootOffset});
        if (f.hasOri) clip_.device.push_back({f.t, f.ori});
    }
    if (arCam_ && recCam_.size() > 1) { // câmera mexida pelo celular durante a gravação
        clip_.camKeys.clear();
        float lastC = -1.0f;
        for (const auto& k : recCam_)
            if (k.t - lastC >= 1.0f / 60.0f) { clip_.camKeys.push_back(k); lastC = k.t; }
    }
    clip_.length = std::max(0.1f, fr.back().t);
    clip_.loop = false;
    clip_.linear = true;
    time_ = 0.0f;
    playing_ = false;
    poseEdited_ = false;
    dirty_ = true;
    char buf[160];
    std::snprintf(buf, sizeof(buf), "Gravado: %.1f s (%zu quadros%s). Salve para guardar a animação.", clip_.length,
                  clip_.root.size(), clip_.device.empty() ? "" : ", com giroscópio");
    status_ = buf;
}

void AnimEditor::mocapUi() {
    ImGui::TextColored(ImVec4(0.95f, 0.62f, 0.16f, 1), "Captura de movimento (câmera)");
    auto ensureServer = [&]() {
        if (!serverOk_) {
            serverOk_ = mocap_.startServer(root_);
            serverTried_ = true;
            if (!serverOk_) status_ = "Não deu para iniciar o servidor (o Node.js está instalado?)";
        }
        return serverOk_;
    };
    const float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button("Câmera do PC", ImVec2(bw, 0)) && ensureServer()) {
        mocap_.openPcCamera();
        showQr_ = false;
        status_ = "Abrindo a câmera no navegador: clique em \"Ativar câmera\" e permita o acesso.";
    }
    ImGui::SameLine();
    if (ImGui::Button(showQr_ ? "Esconder QR" : "Câmera do celular", ImVec2(bw, 0)) && (showQr_ || ensureServer())) showQr_ = !showQr_;

    if (showQr_) {
        const QrCode& qr = mocap_.phoneQr();
        ImGui::TextWrapped("Leia com a câmera do celular (mesma rede wi-fi):");
        const float side = std::min(ImGui::GetContentRegionAvail().x, 260.0f);
        const float cell = side / (qr.size + 8);
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + side, p.y + side), IM_COL32(255, 255, 255, 255));
        for (int y = 0; y < qr.size; ++y)
            for (int x = 0; x < qr.size; ++x)
                if (qr.at(x, y)) {
                    ImVec2 a(p.x + (x + 4) * cell, p.y + (y + 4) * cell);
                    dl->AddRectFilled(a, ImVec2(a.x + cell, a.y + cell), IM_COL32(0, 0, 0, 255));
                }
        ImGui::Dummy(ImVec2(side, side));
        ImGui::TextDisabled("%s", mocap_.phoneUrl().c_str());
        ImGui::TextWrapped("O navegador vai avisar que a conexão não é particular (o certificado é deste PC): "
                           "toque em Avançado > Continuar. Depois toque em Ativar câmera e permita câmera e movimento.");
    }

    if (serverTried_) {
        if (mocap_.live())
            ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1), "Ao vivo: %s, %.0f quadros/s", mocap_.source(), mocap_.fps());
        else
            ImGui::TextDisabled("%s", serverOk_ ? "Esperando a câmera..." : "Servidor desligado");
    }
    ImGui::Checkbox("Boneco copia a câmera ao vivo", &mocapFollow_);
    ImGui::Checkbox("Compensar o giro do celular", &mocap_.useGyro);
    ImGui::Checkbox("Andar pelo palco (experimental)", &mocap_.moveRoot);
    ImGui::SetNextItemWidth(-90);
    ImGui::SliderFloat("Suavizar", &mocap_.smoothing, 0.0f, 0.9f, "%.2f");
    if (ImGui::Button("Calibrar (posição e giro atuais = frente)", ImVec2(-1, 0))) mocap_.calibrate();

    // gravar / parar
    const bool rec = mocap_.recording();
    ImGui::PushStyleColor(ImGuiCol_Button, rec ? ImVec4(0.75f, 0.18f, 0.15f, 1) : ImVec4(0.55f, 0.12f, 0.1f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.25f, 0.2f, 1));
    char label[64];
    if (rec) std::snprintf(label, sizeof(label), "Parar gravação (%.1f s)", mocap_.recordedTime());
    else std::snprintf(label, sizeof(label), "Gravar movimento");
    if (ImGui::Button(label, ImVec2(-1, 36))) {
        if (rec) {
            mocap_.stopRecording();
            bakeRecording();
        } else if (!mocap_.live()) {
            status_ = "Ative uma câmera primeiro (nenhum quadro chegando)";
        } else {
            mocap_.startRecording();
            recCam_.clear();
            status_ = "Gravando: faça os movimentos e clique em Parar.";
        }
    }
    ImGui::PopStyleColor(2);
    if (rec) {
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImVec2 c(viewRect().x0 + 24, viewRect().y0 + 24);
        if (std::fmod(ImGui::GetTime(), 1.0) < 0.6) dl->AddCircleFilled(c, 9, IM_COL32(230, 40, 30, 255));
        dl->AddText(ImVec2(c.x + 16, c.y - 8), IM_COL32(255, 255, 255, 255), "GRAVANDO");
    }
}

// ---------------------------------------------------------------- câmera de cena (filmagem)
glm::quat AnimEditor::stageRot() const { return glm::angleAxis(stageYaw_, glm::vec3(0, 1, 0)); }

glm::vec3 AnimEditor::cineEye() const { return glm::vec3(stageMatrix() * glm::vec4(camPos_, 1.0f)); }

glm::mat4 AnimEditor::cineView() const {
    glm::mat4 w = glm::translate(glm::mat4(1.0f), cineEye()) * glm::mat4_cast(stageRot() * camRot_);
    return glm::inverse(w);
}

// Lente de cinema: sensor de 36 mm de largura (super 35 / full frame) e imagem 16:9.
glm::mat4 AnimEditor::cineProj(float aspect) const {
    const float halfW = 18.0f / std::max(5.0f, focal_);          // tan(meio campo horizontal) da imagem 16:9
    const float halfH = halfW * 9.0f / 16.0f;
    // aspecto diferente de 16:9: mostra pelo menos todo o quadro 16:9
    const float fovY = 2.0f * std::atan(std::max(halfH, halfW / std::max(0.1f, aspect)));
    return glm::perspective(fovY, aspect, 0.05f, 500.0f);
}

void AnimEditor::setCamKey() {
    upsert(clip_.camKeys, time_, [&](float tt) { return AnimClip::CamKey{tt, camPos_, camRot_, focal_, focus_}; });
    for (auto& k : clip_.camKeys)
        if (std::fabs(k.t - time_) < 0.02f) { k.p = camPos_; k.q = camRot_; k.focal = focal_; k.focus = focus_; }
    camEdited_ = false;
    dirty_ = true;
}

void AnimEditor::cameraUi() {
    ImGui::TextColored(ImVec4(0.95f, 0.62f, 0.16f, 1), "Câmera de cena");
    ImGui::Checkbox("Prévia embaixo", &preview_);
    ImGui::SameLine();
    if (ImGui::Checkbox("Ver pela câmera", &pilot_) && pilot_)
        status_ = "Vendo pela câmera: botão direito gira, botão do meio desloca, rodinha anda para a frente.";
    ImGui::PushItemWidth(-110);
    bool ch = false, done = false;
    ch |= ImGui::DragFloat3("Posição##cam", &camPos_.x, 0.01f);
    done |= ImGui::IsItemDeactivatedAfterEdit();
    glm::vec3 e = glm::degrees(glm::eulerAngles(camRot_));
    if (ImGui::DragFloat3("Giro##cam", &e.x, 0.3f)) { camRot_ = glm::normalize(glm::quat(glm::radians(e))); ch = true; }
    done |= ImGui::IsItemDeactivatedAfterEdit();
    ch |= ImGui::SliderFloat("Lente (mm)", &focal_, 12.0f, 200.0f, "%.0f mm", ImGuiSliderFlags_Logarithmic);
    done |= ImGui::IsItemDeactivatedAfterEdit();
    // lentes comuns
    for (int mm : {18, 24, 35, 50, 85, 135}) {
        if (mm != 18) ImGui::SameLine();
        if (ImGui::SmallButton((std::to_string(mm) + "##lente").c_str())) { focal_ = (float)mm; ch = done = true; }
    }
    ImGui::Checkbox("Desfoque (profundidade de campo)", &dof_);
    if (dof_) {
        ch |= ImGui::SliderFloat("Foco (m)", &focus_, 0.2f, 60.0f, "%.2f m", ImGuiSliderFlags_Logarithmic);
        done |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SliderFloat("Abertura f/", &fstop_, 1.2f, 22.0f, "f/%.1f", ImGuiSliderFlags_Logarithmic);
        if (ImGui::Button("Focar no personagem", ImVec2(-1, 0))) {
            focus_ = glm::length(camPos_ - glm::vec3(globals_[CHEST][3]));
            ch = done = true;
        }
    }
    ImGui::PopItemWidth();
    if (ch) camEdited_ = true;
    if (done && autoKey_) setCamKey();
    const float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button("Câmera = minha vista", ImVec2(bw, 0))) {
        // põe a câmera de cena onde a vista do editor está
        glm::mat4 inv = glm::inverse(view_);
        const glm::mat4 toStage = glm::inverse(stageMatrix());
        camPos_ = glm::vec3(toStage * inv[3]);
        camRot_ = glm::normalize(glm::inverse(stageRot()) * glm::quat_cast(glm::mat3(inv)));
        camEdited_ = true;
        if (autoKey_) setCamKey();
    }
    ImGui::SameLine();
    if (ImGui::Button("Chave da câmera", ImVec2(bw, 0))) setCamKey();
    if (!clip_.camKeys.empty()) {
        ImGui::TextDisabled("%zu chaves de câmera", clip_.camKeys.size());
        ImGui::SameLine();
        if (ImGui::SmallButton("Apagar todas")) { clip_.camKeys.clear(); dirty_ = true; }
    }
    if (ImGui::Checkbox("Celular move a câmera (AR)", &arCam_)) {
        arInit_ = false;
        if (arCam_) status_ = mocap_.oriLive() ? "Gire o celular: a câmera gira junto (o giro atual é o de agora)."
                                                : "Abra a página no celular (Câmera do celular) para usar o giroscópio.";
    }
    if (arCam_) {
        ImGui::TextDisabled("%s", mocap_.oriLive() ? "Giroscópio chegando" : "Esperando o giroscópio do celular...");
        if (ImGui::SmallButton("Recentrar")) arInit_ = false;
    }
}
