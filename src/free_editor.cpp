#include "free_editor.h"
#include "terrain.h"

#include <stb_image.h>

#include <GLFW/glfw3.h>
#include <imgui.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <thread>

namespace {

// Raio x caixa alinhada: distância de entrada (ou de saída se a origem está dentro). -1 = não acerta.
float rayBox(const glm::vec3& o, const glm::vec3& d, const glm::vec3& lo, const glm::vec3& hi, bool& inside) {
    float t0 = -1e30f, t1 = 1e30f;
    for (int a = 0; a < 3; ++a) {
        if (std::fabs(d[a]) < 1e-9f) {
            if (o[a] < lo[a] || o[a] > hi[a]) return -1.0f;
            continue;
        }
        float ta = (lo[a] - o[a]) / d[a], tb = (hi[a] - o[a]) / d[a];
        if (ta > tb) std::swap(ta, tb);
        t0 = std::max(t0, ta);
        t1 = std::min(t1, tb);
        if (t0 > t1) return -1.0f;
    }
    if (t1 < 0.0f) return -1.0f;
    inside = t0 < 0.0f;
    return inside ? t1 : t0;
}

// Espaço da porta: origem na dobradiça, X ao longo da folha (qualquer ângulo).
glm::mat4 doorFrame(const DoorPlace& d) {
    return glm::translate(glm::mat4(1.0f), d.hinge) * glm::rotate(glm::mat4(1.0f), d.yaw, glm::vec3(0, 1, 0));
}

bool keyDown(GLFWwindow* w, int k) { return glfwGetKey(w, k) == GLFW_PRESS; }

const ImU32 kSel = IM_COL32(255, 160, 40, 255), kEnemy = IM_COL32(235, 70, 60, 230), kStart = IM_COL32(80, 220, 110, 230),
            kDoorCol = IM_COL32(90, 190, 255, 230);

} // namespace

void FreeEditor::begin(const glm::vec3& camPos, float yaw, float pitch, const std::string& root) {
    active_ = true;
    cam_ = camPos;
    yaw_ = yaw;
    pitch_ = pitch;
    root_ = root;
    kind_ = None;
    idx_ = -1;
    mode_ = 0;
    status_ = "Clique num objeto para selecionar";
    if (images_.empty()) scanImages();
    assetsScanned_ = false;
}

void FreeEditor::scanImages() {
    namespace fs = std::filesystem;
    std::error_code ec;
    for (const char* dir : {"elements-cenari", "textures", "assets"}) {
        fs::path base = fs::path(root_) / dir;
        if (!fs::exists(base, ec)) continue;
        for (auto it = fs::recursive_directory_iterator(base, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            std::string ext = it->path().extension().string();
            for (char& c : ext) c = (char)std::tolower((unsigned char)c);
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg")
                images_.push_back(fs::relative(it->path(), root_, ec).string());
        }
    }
    std::sort(images_.begin(), images_.end());
    scanTexSets();
}

void FreeEditor::scanTexSets() {
    namespace fs = std::filesystem;
    // sufixo do nome -> papel no material (o mais específico primeiro)
    static const struct { const char* suf; int role; } kSuf[] = {
        {"_normalgl", 1}, {"_normal_gl", 1}, {"_nor_gl", 1}, {"_normaldx", 2}, {"_normal_dx", 2}, {"_nor_dx", 2},
        {"_normal", 1}, {"_nrm", 1}, {"_norm", 1}, {"_nor", 1}, {"_roughness", 3}, {"_rough", 3}, {"_rgh", 3},
        {"_displacement", 4}, {"_height", 4}, {"_disp", 4}, {"_bump", 4}, {"_ambientocclusion", 5}, {"_occlusion", 5},
        {"_ao", 5}, {"_opacity", 6}, {"_alpha", 6}, {"_metalness", 6}, {"_metallic", 6}, {"_basecolor", 0}, {"_base_color", 0}, {"_albedo", 0}, {"_diffuse", 0}, {"_color", 0}, {"_diff", 0},
        {"_col", 0}};
    std::map<std::string, TexSet> sets;
    std::vector<std::string> order;
    for (const std::string& img : images_) {
        // materiais só da pasta textures/ (as outras imagens são texturas internas dos modelos)
        if (img.rfind("textures/", 0) != 0 && img.rfind("textures\\", 0) != 0) continue;
        fs::path p(img);
        std::string stem = p.stem().string(), low = stem;
        for (char& ch : low) ch = (char)std::tolower((unsigned char)ch);
        int role = -1;
        std::string base = stem;
        for (const auto& s : kSuf) {
            size_t n = std::strlen(s.suf);
            if (low.size() > n && low.compare(low.size() - n, n, s.suf) == 0) { role = s.role; base = stem.substr(0, stem.size() - n); break; }
        }
        if (role < 0) role = 0; // imagem solta: vale como cor
        std::string key = p.parent_path().generic_string() + "/" + base;
        if (!sets.count(key)) order.push_back(key);
        TexSet& t = sets[key];
        t.name = base;
        switch (role) {
        case 0: if (t.color.empty()) t.color = img; break;
        case 1: t.normal = img; t.dx = false; break;   // GL tem preferência
        case 2: if (t.normal.empty()) { t.normal = img; t.dx = true; } break;
        case 3: t.rough = img; break;
        case 4: t.height = img; break;
        case 5: t.ao = img; break;
        }
    }
    texSets_.clear();
    for (const std::string& k : order)
        if (!sets[k].color.empty()) texSets_.push_back(sets[k]); // só conjuntos com cor
}

unsigned FreeEditor::thumb(const std::string& rel) {
    auto it = thumbs_.find(rel);
    if (it != thumbs_.end()) return it->second;
    GLuint t = 0;
    int w, h, ch;
    if (unsigned char* px = stbi_load((root_ + "/" + rel).c_str(), &w, &h, &ch, 4)) {
        const int s = 96; // miniatura: amostra por área
        std::vector<unsigned char> out((size_t)s * s * 4);
        for (int y = 0; y < s; ++y)
            for (int x = 0; x < s; ++x) {
                int x0 = x * w / s, x1 = std::max(x0 + 1, (x + 1) * w / s), y0 = y * h / s, y1 = std::max(y0 + 1, (y + 1) * h / s);
                int stepX = std::max(1, (x1 - x0) / 4), stepY = std::max(1, (y1 - y0) / 4);
                unsigned acc[4] = {0, 0, 0, 0}, n = 0;
                for (int yy = y0; yy < y1; yy += stepY)
                    for (int xx = x0; xx < x1; xx += stepX, ++n)
                        for (int k = 0; k < 4; ++k) acc[k] += px[((size_t)yy * w + xx) * 4 + k];
                for (int k = 0; k < 4; ++k) out[((size_t)y * s + x) * 4 + k] = (unsigned char)(acc[k] / std::max(1u, n));
            }
        stbi_image_free(px);
        t = makeTexture2D(out.data(), s, s, false);
    }
    thumbs_[rel] = t;
    return t;
}

FreeEditor::Ray FreeEditor::mouseRay(GLFWwindow* win, int, int) const {
    double mx, my;
    glfwGetCursorPos(win, &mx, &my);
    if (fakeMouse_) { mx = fakeMx_; my = fakeMy_; }
    return rayAt((float)mx, (float)my);
}

glm::vec3 FreeEditor::selPos(Context& c) const {
    switch (kind_) {
    case Prop: return c.layout.props[idx_].pos;
    case Box: return (c.world.boxes[idx_].min + c.world.boxes[idx_].max) * 0.5f;
    case DoorSel: return c.layout.doorHinge;
    case ExtraDoor: return c.layout.extraDoors[idx_].hinge;
    case Spawn: return c.layout.spawns[idx_];
    case Start: return c.layout.start;
    case Light: return c.layout.lights[idx_].pos;
    case Gun: {
        auto g = c.layout.guns.find(idx_);
        return g != c.layout.guns.end() ? g->second.first : c.gunPos[idx_];
    }
    default: return glm::vec3(0.0f);
    }
}

std::string FreeEditor::selName(const Context& c) const {
    char buf[64];
    switch (kind_) {
    case Prop: return c.layout.props[idx_].name;
    case Box: std::snprintf(buf, sizeof(buf), "Bloco %d", idx_); return buf;
    case DoorSel: return "Porta";
    case ExtraDoor: std::snprintf(buf, sizeof(buf), "Porta %d", idx_ + 2); return buf;
    case Spawn: std::snprintf(buf, sizeof(buf), "Inimigo %d", idx_ + 1); return buf;
    case Start: return "Início do personagem";
    case Gun: return idx_ < (int)c.gunNames.size() ? "Arma: " + c.gunNames[idx_] : "Arma";
    case Light: {
        const char* t[] = {"pontual", "spot", "ambiente", "sol", "área"};
        std::snprintf(buf, sizeof(buf), "Luz %d (%s)", idx_ + 1, t[std::clamp(c.layout.lights[idx_].type, 0, 4)]);
        return buf;
    }
    default: return "";
    }
}

void FreeEditor::pick(const Ray& r, Context& c) {
    float best = 1e30f, bestInside = 1e30f;
    Kind bk = None, bkIn = None;
    int bi = -1, biIn = -1;
    auto consider = [&](Kind k, int i, float t, bool inside) {
        if (t < 0.0f) return;
        if (inside) { if (t < bestInside) { bestInside = t; bkIn = k; biIn = i; } }
        else if (t < best) { best = t; bk = k; bi = i; }
    };
    // objetos com malha (caixa da malha no espaço do objeto)
    for (size_t i = 0; i < c.props.size() && i < c.propMeshes.size(); ++i) {
        if (!c.propMeshes[i]) continue;
        glm::mat4 inv = glm::inverse(c.props[i].model);
        glm::vec3 o = glm::vec3(inv * glm::vec4(r.o, 1.0f)), d = glm::vec3(inv * glm::vec4(r.d, 0.0f));
        bool in = false;
        consider(Prop, (int)i, rayBox(o, d, c.propMeshes[i]->boundsMin, c.propMeshes[i]->boundsMax, in), in);
    }
    for (size_t i = 0; i < c.world.boxes.size(); ++i) {
        const ::Box& b = c.world.boxes[i];
        if (b.hidden || !b.solid) continue;
        bool in = false;
        consider(Box, (int)i, rayBox(r.o, r.d, b.min, b.max, in), in);
    }
    if (c.layout.hasDoor) {
        bool in = false;
        glm::vec3 h = c.layout.doorHinge;
        consider(DoorSel, 0, rayBox(r.o, r.d, h - glm::vec3(0.0f, 0.0f, 0.12f), h + glm::vec3(c.layout.doorWidth, c.layout.doorHeight, 0.12f), in), in);
    }
    for (size_t i = 0; i < c.layout.extraDoors.size(); ++i) {
        const DoorPlace& d = c.layout.extraDoors[i];
        // raio no espaço da porta (que pode estar em qualquer ângulo)
        glm::mat4 inv = glm::inverse(doorFrame(d));
        glm::vec3 o = glm::vec3(inv * glm::vec4(r.o, 1.0f)), dd = glm::vec3(inv * glm::vec4(r.d, 0.0f));
        bool in = false;
        consider(ExtraDoor, (int)i, rayBox(o, dd, glm::vec3(0, 0, -0.12f), glm::vec3(d.width, d.height, 0.12f), in), in);
    }
    auto marker = [&](Kind k, int i, const glm::vec3& p) {
        bool in = false;
        consider(k, i, rayBox(r.o, r.d, p - glm::vec3(0.35f, 0.0f, 0.35f), p + glm::vec3(0.35f, 1.8f, 0.35f), in), false);
    };
    for (size_t i = 0; i < c.layout.spawns.size(); ++i) marker(Spawn, (int)i, c.layout.spawns[i]);
    for (size_t i = 0; i < c.gunPos.size(); ++i) {
        bool in = false;
        const glm::vec3& g = c.gunPos[i];
        consider(Gun, (int)i, rayBox(r.o, r.d, g - glm::vec3(0.4f, 0.05f, 0.4f), g + glm::vec3(0.4f, 0.3f, 0.4f), in), false);
    }
    marker(Start, 0, c.layout.start);
    for (size_t i = 0; i < c.layout.lights.size(); ++i) {
        bool in = false;
        const glm::vec3& p = c.layout.lights[i].pos;
        consider(Light, (int)i, rayBox(r.o, r.d, p - glm::vec3(0.25f), p + glm::vec3(0.25f), in), false);
    }
    // de dentro de uma sala (a caixa dela envolve a câmera) só seleciona se não acertou mais nada
    if (bk == None) { bk = bkIn; bi = biIn; }
    kind_ = bk;
    idx_ = bi;
    status_ = kind_ == None ? "Nada selecionado" : "Selecionado: " + selName(c) + "  (G mover, R girar, S escala)";
}

void FreeEditor::startGesture(int mode, GLFWwindow* win, Context& c, int fbw, int fbh) {
    if (kind_ == None) return;
    if (mode == 2 && (kind_ == Box || kind_ == DoorSel || kind_ == ExtraDoor)) return; // alinhadas aos eixos
    if (mode == 3 && (kind_ == Spawn || kind_ == Start || kind_ == Gun || kind_ == ExtraDoor)) return;
    if (kind_ == Light && mode == 2 && c.layout.lights[idx_].type != LightPlace::Spot && c.layout.lights[idx_].type != LightPlace::Sun &&
        c.layout.lights[idx_].type != LightPlace::Area) return; // só luzes com direção giram
    mode_ = mode;
    axis_ = -1;
    glfwGetCursorPos(win, &mx0_, &my0_);
    captureSel(c);
    Ray r = mouseRay(win, fbw, fbh);
    hit0_ = std::fabs(r.d.y) > 1e-4f ? r.o + r.d * ((pos0_.y - r.o.y) / r.d.y) : pos0_;
    const char* names[] = {"", "Movendo", "Girando", "Escala"};
    status_ = std::string(names[mode]) + " " + selName(c) + " - X/Y/Z trava o eixo, clique confirma, botão direito cancela";
}

void FreeEditor::captureSel(Context& c) {
    pos0_ = selPos(c);
    if (kind_ == Prop) {
        yaw0_ = c.layout.props[idx_].yaw;
        scale0_ = c.layout.props[idx_].scale;
        pivot0_ = glm::inverse(c.layout.props[idx_].matrix()) * c.props[idx_].model;
    } else if (kind_ == Spawn) yaw0_ = c.layout.spawnYaw[idx_];
    else if (kind_ == Start) yaw0_ = c.layout.startYaw;
    else if (kind_ == Gun) {
        auto g = c.layout.guns.find(idx_);
        yaw0_ = g != c.layout.guns.end() ? g->second.second : c.gunYaw[idx_];
    } else if (kind_ == Box) { bmin0_ = c.world.boxes[idx_].min; bmax0_ = c.world.boxes[idx_].max; }
    else if (kind_ == DoorSel) scale0_ = c.layout.doorWidth;
    else if (kind_ == Light) { yaw0_ = c.layout.lights[idx_].yaw; scale0_ = c.layout.lights[idx_].range; }
}

void FreeEditor::moveSel(Context& c, const glm::vec3& delta) {
    glm::vec3 p = pos0_ + delta;
    switch (kind_) {
    case Prop: c.layout.props[idx_].pos = p; livePropModel(c); break;
    case Box:
        c.world.boxes[idx_].min = bmin0_ + delta;
        c.world.boxes[idx_].max = bmax0_ + delta;
        c.layout.boxes[idx_] = {c.world.boxes[idx_].min, c.world.boxes[idx_].max};
        break;
    case DoorSel:
        c.layout.doorHinge = p;
        if (c.door) c.door->hinge = p;
        break;
    case Spawn: c.layout.spawns[idx_] = p; break;
    case Start: c.layout.start = p; break;
    case Gun: c.layout.guns[idx_] = {p, yaw0_}; break;
    case ExtraDoor: c.layout.extraDoors[idx_].hinge = p; break;
    case Light: c.layout.lights[idx_].pos = p; break;
    default: break;
    }
}

float FreeEditor::gizmoLen(Context& c) const { return std::max(0.25f, glm::length(cam_ - selPos(c)) * 0.2f); }

// Eixo do gizmo na tela: X, cima (Y), Z; o quadrado fica entre X e Z (move no chão).
namespace {
const glm::vec3 kGizmoAxis[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
float segDist(glm::vec2 p, glm::vec2 a, glm::vec2 b) {
    glm::vec2 ab = b - a;
    float t = glm::clamp(glm::dot(p - a, ab) / std::max(glm::dot(ab, ab), 1e-6f), 0.0f, 1.0f);
    return glm::length(p - (a + ab * t));
}
}

int FreeEditor::gizmoPick(float mx, float my, Context& c) const {
    const glm::mat4 vp = proj_ * view_;
    auto toScreen = [&](const glm::vec3& p, glm::vec2& out) {
        glm::vec4 cl = vp * glm::vec4(p, 1.0f);
        if (cl.w < 0.05f) return false;
        out = glm::vec2((cl.x / cl.w * 0.5f + 0.5f) * winW_, (0.5f - cl.y / cl.w * 0.5f) * winH_);
        return true;
    };
    const glm::vec3 p = selPos(c);
    const float L = gizmoLen(c);
    const glm::vec2 m(mx, my);
    glm::vec2 a, b;
    // quadrado do chão primeiro (fica perto do centro)
    glm::vec3 sq = p + glm::vec3(0.4f * L, 0.0f, 0.4f * L);
    if (toScreen(sq, a) && glm::length(m - a) < 16.0f) return 3;
    int best = -1;
    float bestD = 11.0f;
    if (!toScreen(p, a)) return -1;
    for (int k = 0; k < 3; ++k) {
        if (!toScreen(p + kGizmoAxis[k] * L, b)) continue;
        float d = segDist(m, a + (b - a) * 0.15f, b);
        if (d < bestD) { bestD = d; best = k; }
    }
    return best;
}

bool FreeEditor::gizmoParam(const Ray& r, int axis, glm::vec3& out) const {
    if (axis == 3) { // plano do chão na altura do objeto
        if (std::fabs(r.d.y) < 1e-4f) return false;
        float t = (pos0_.y - r.o.y) / r.d.y;
        if (t <= 0.0f || t > 500.0f) return false;
        out = r.o + r.d * t;
        return true;
    }
    // ponto do eixo mais perto do raio do mouse
    const glm::vec3 a = kGizmoAxis[axis], w0 = pos0_ - r.o;
    float b = glm::dot(a, r.d), den = 1.0f - b * b;
    if (den < 1e-4f) return false; // eixo apontando para a câmera
    float s = (b * glm::dot(r.d, w0) - glm::dot(a, w0)) / den;
    out = pos0_ + a * s;
    return true;
}

void FreeEditor::livePropModel(Context& c) {
    if (kind_ == Prop) c.props[idx_].model = c.layout.props[idx_].matrix(pivot0_);
}

void FreeEditor::applyGesture(GLFWwindow* win, Context& c, int fbw, int fbh) {
    double mx, my;
    glfwGetCursorPos(win, &mx, &my);
    if (mode_ == 1) {
        Ray r = mouseRay(win, fbw, fbh);
        glm::vec3 delta(0.0f);
        if (axis_ == 1) { // para cima/baixo: arrastar o mouse na vertical
            float dist = glm::length(cam_ - pos0_);
            delta.y = (float)(my0_ - my) * dist * 0.0022f;
        } else if (std::fabs(r.d.y) > 1e-4f) {
            float t = (pos0_.y - r.o.y) / r.d.y;
            if (t > 0.0f && t < 500.0f) delta = r.o + r.d * t - hit0_;
            delta.y = 0.0f;
            if (axis_ == 0) delta.z = 0.0f;
            if (axis_ == 2) delta.x = 0.0f;
        }
        moveSel(c, delta);
    } else if (mode_ == 2) {
        float yaw = yaw0_ + (float)(mx - mx0_) * 0.01f;
        if (kind_ == Prop) { c.layout.props[idx_].yaw = yaw; livePropModel(c); }
        else if (kind_ == Spawn) c.layout.spawnYaw[idx_] = yaw;
        else if (kind_ == Start) c.layout.startYaw = yaw;
        else if (kind_ == Gun) c.layout.guns[idx_] = {pos0_, yaw};
        else if (kind_ == Light) c.layout.lights[idx_].yaw = yaw;
    } else if (mode_ == 3) {
        float f = std::exp((float)(mx - mx0_) * 0.005f);
        if (kind_ == Prop) { c.layout.props[idx_].scale = std::max(0.01f, scale0_ * f); livePropModel(c); }
        else if (kind_ == Box) {
            glm::vec3 ctr = (bmin0_ + bmax0_) * 0.5f, half = (bmax0_ - bmin0_) * 0.5f;
            glm::vec3 k(axis_ < 0 || axis_ == 0 ? f : 1.0f, axis_ < 0 || axis_ == 1 ? f : 1.0f, axis_ < 0 || axis_ == 2 ? f : 1.0f);
            glm::vec3 nh = half * k;
            ctr.y = bmin0_.y + nh.y; // continua apoiada embaixo
            c.world.boxes[idx_].min = ctr - nh;
            c.world.boxes[idx_].max = ctr + nh;
            c.layout.boxes[idx_] = {c.world.boxes[idx_].min, c.world.boxes[idx_].max};
        } else if (kind_ == Light) {
            c.layout.lights[idx_].range = std::clamp(scale0_ * f, 0.3f, 80.0f); // S: alcance
        } else if (kind_ == DoorSel) {
            c.layout.doorWidth = std::clamp(scale0_ * f, 0.5f, 3.0f);
            if (c.door) c.door->width = c.layout.doorWidth;
        }
    }
}

void FreeEditor::restore(Context& c) {
    switch (kind_) {
    case Prop:
        c.layout.props[idx_].pos = pos0_;
        c.layout.props[idx_].yaw = yaw0_;
        c.layout.props[idx_].scale = scale0_;
        livePropModel(c);
        break;
    case Box:
        c.world.boxes[idx_].min = bmin0_;
        c.world.boxes[idx_].max = bmax0_;
        c.layout.boxes[idx_] = {bmin0_, bmax0_};
        break;
    case DoorSel:
        c.layout.doorHinge = pos0_;
        c.layout.doorWidth = scale0_;
        if (c.door) { c.door->hinge = pos0_; c.door->width = scale0_; }
        break;
    case Spawn: c.layout.spawns[idx_] = pos0_; c.layout.spawnYaw[idx_] = yaw0_; break;
    case Start: c.layout.start = pos0_; c.layout.startYaw = yaw0_; break;
    case Gun: c.layout.guns[idx_] = {pos0_, yaw0_}; break;
    case ExtraDoor: c.layout.extraDoors[idx_].hinge = pos0_; break;
    case Light:
        c.layout.lights[idx_].pos = pos0_;
        c.layout.lights[idx_].yaw = yaw0_;
        c.layout.lights[idx_].range = scale0_;
        break;
    default: break;
    }
}

FreeEditor::UndoState FreeEditor::snapshot(Context& c, bool withGround) const {
    UndoState s;
    s.layout = c.layout.serialize();
    if (withGround && c.paint && c.paint->active) { s.hasPaint = true; s.paint = c.paint->weights; }
    if (withGround && c.terrain && c.terrain->active) { s.hasTerrain = true; s.terrain = c.terrain->h; }
    return s;
}

void FreeEditor::pushUndo(UndoState s) {
    undo_.push_back(std::move(s));
    if (undo_.size() > 60) undo_.erase(undo_.begin());
    redo_.clear();
}

void FreeEditor::restoreSnapshot(Context& c, const UndoState& s) {
    c.layout = LevelLayout();
    c.layout.parse(s.layout);
    if (s.hasPaint && c.paint && c.paint->weights.size() == s.paint.size()) {
        c.paint->weights = s.paint;
        c.paint->uploadWeights();
    }
    if (s.hasTerrain && c.terrain && c.terrain->h.size() == s.terrain.size()) {
        c.terrain->h = s.terrain;
        c.terrain->upload();
    }
    lastLayout_ = c.layout.serialize();
}

FreeEditor::Result FreeEditor::undoRedo(Context& c, bool redo) {
    std::vector<UndoState>& from = redo ? redo_ : undo_;
    std::vector<UndoState>& to = redo ? undo_ : redo_;
    if (from.empty()) {
        status_ = redo ? "Nada para refazer" : "Nada para desfazer";
        return Result::None;
    }
    UndoState s = std::move(from.back());
    from.pop_back();
    to.push_back(snapshot(c, s.hasPaint || s.hasTerrain)); // o estado de agora, para voltar
    restoreSnapshot(c, s);
    kind_ = None;
    idx_ = -1;
    dirty_ = true;
    status_ = std::string(redo ? "Refeito" : "Desfeito") + " (" + std::to_string(undo_.size()) + " para desfazer)";
    return Result::Rebuild;
}

bool FreeEditor::duplicateSelected(Context& c) {
    if (kind_ == Prop) {
        PropPlace p = c.layout.props[idx_];
        if (p.asset.empty()) { status_ = "Esse objeto faz parte do cenário e não pode ser duplicado"; return false; }
        std::string base = p.name;
        while (!base.empty() && (std::isdigit((unsigned char)base.back()) || base.back() == '_')) base.pop_back();
        int n = 2;
        do p.name = base + "_" + std::to_string(n++);
        while (c.layout.prop(p.name));
        c.layout.props.push_back(p);
        // aparece já (mesma malha); a colisão vem ao confirmar
        RenderScene::Prop rp = c.props[idx_];
        c.props.push_back(rp);
        c.propMeshes.push_back(idx_ < (int)c.propMeshes.size() ? c.propMeshes[idx_] : nullptr);
        c.propTris.push_back(idx_ < (int)c.propTris.size() ? c.propTris[idx_] : nullptr);
        idx_ = (int)c.layout.props.size() - 1;
    } else if (kind_ == ExtraDoor) {
        c.layout.extraDoors.push_back(c.layout.extraDoors[idx_]);
        idx_ = (int)c.layout.extraDoors.size() - 1;
    } else if (kind_ == Light) {
        c.layout.lights.push_back(c.layout.lights[idx_]);
        idx_ = (int)c.layout.lights.size() - 1;
    } else if (kind_ == Spawn) {
        c.layout.spawns.push_back(c.layout.spawns[idx_]);
        c.layout.spawnYaw.push_back(c.layout.spawnYaw[idx_]);
        idx_ = (int)c.layout.spawns.size() - 1;
    } else {
        status_ = "Só objetos, portas, luzes e inimigos podem ser duplicados";
        return false;
    }
    dirty_ = true;
    status_ = "Duplicado: " + selName(c) + " - mova e clique para soltar";
    return true;
}

bool FreeEditor::removeSelected(Context& c) {
    if (kind_ == Prop && !c.layout.props[idx_].asset.empty()) {
        c.layout.props.erase(c.layout.props.begin() + idx_);
    } else if (kind_ == Spawn && c.layout.spawns.size() > 1) {
        c.layout.spawns.erase(c.layout.spawns.begin() + idx_);
        c.layout.spawnYaw.erase(c.layout.spawnYaw.begin() + idx_);
    } else if (kind_ == Light) {
        c.layout.lights.erase(c.layout.lights.begin() + idx_);
    } else if (kind_ == ExtraDoor) {
        c.layout.extraDoors.erase(c.layout.extraDoors.begin() + idx_);
    } else if (kind_ == Gun && c.layout.guns.count(idx_)) {
        c.layout.guns.erase(idx_); // volta ao lugar de fábrica
    } else {
        status_ = "Esse objeto faz parte do cenário e não pode ser removido";
        return false;
    }
    status_ = "Removido: " + selName(c);
    kind_ = None;
    idx_ = -1;
    dirty_ = true;
    return true;
}

FreeEditor::Rect FreeEditor::viewRect(float W, float H) const {
    const float top = std::max(56.0f, H * 0.068f);
    return {std::min(W * 0.22f, 380.0f), top, W - std::min(W * 0.19f, 330.0f), H - std::max(250.0f, H * 0.34f)};
}

glm::mat4 FreeEditor::viewportFix(float W, float H, float fullAspect) const {
    const Rect vr = viewRect(W, H);
    const float rw = vr.x1 - vr.x0, rh = vr.y1 - vr.y0;
    const glm::vec3 shift((vr.x0 + vr.x1) / W - 1.0f, 1.0f - (vr.y0 + vr.y1) / H, 0.0f);
    // leva a imagem para o retângulo e troca o aspecto da janela inteira pelo da vista
    return glm::translate(glm::mat4(1.0f), shift) * glm::scale(glm::mat4(1.0f), glm::vec3(rw / W, rh / H, 1.0f)) *
           glm::scale(glm::mat4(1.0f), glm::vec3(fullAspect / (rw / std::max(1.0f, rh)), 1.0f, 1.0f));
}

void FreeEditor::drawPlaying(Context& c) {
    playing_ = true;
    drawUi(c, glm::mat4(1.0f), 0, 0); // só os painéis; o mouse está preso no jogo
    playing_ = false;
}

FreeEditor::Ray FreeEditor::rayAt(float sx, float sy) const {
    glm::vec2 ndc(sx / winW_ * 2.0f - 1.0f, 1.0f - sy / winH_ * 2.0f);
    glm::mat4 inv = glm::inverse(proj_ * view_);
    glm::vec4 a = inv * glm::vec4(ndc, -1.0f, 1.0f), b = inv * glm::vec4(ndc, 1.0f, 1.0f);
    glm::vec3 pa = glm::vec3(a) / a.w, pb = glm::vec3(b) / b.w;
    return {pa, glm::normalize(pb - pa)};
}

// Onde um raio da vista bate primeiro: em cima de uma colisão (inclusive as da malha) ou no chão.
bool FreeEditor::groundHit(const Ray& r, Context& c, glm::vec3& out) const {
    float best = 1e30f;
    if (r.d.y < -1e-4f) {
        float t = -r.o.y / r.d.y;
        if (t > 0.0f && t < 300.0f) best = t;
    }
    for (const ::Box& b : c.world.boxes) {
        if (!b.solid) continue;
        bool in = false;
        float t = rayBox(r.o, r.d, b.min, b.max, in);
        if (t > 0.0f && !in && t < best) best = t;
    }
    if (best > 1e29f) return false;
    out = r.o + r.d * best;
    return true;
}

bool FreeEditor::surfaceHit(const Ray& r, Context& c, glm::vec3& pos, glm::vec3& normal) const {
    float best = 1e30f;
    glm::vec3 bestN(0.0f, 1.0f, 0.0f);
    for (size_t i = 0; i < c.props.size() && i < c.propTris.size(); ++i) {
        const std::vector<glm::vec3>* tris = c.propTris[i];
        if (!tris || tris->empty() || !c.props[i].mesh) continue;
        const glm::mat4& M = c.props[i].model;
        glm::mat4 inv = glm::inverse(M);
        glm::vec3 o = glm::vec3(inv * glm::vec4(r.o, 1.0f)), d = glm::vec3(inv * glm::vec4(r.d, 0.0f));
        bool in = false;
        if (i < c.propMeshes.size() && c.propMeshes[i]) {
            float tb = rayBox(o, d, c.propMeshes[i]->boundsMin - glm::vec3(0.01f), c.propMeshes[i]->boundsMax + glm::vec3(0.01f), in);
            if (tb < 0.0f && !in) continue;
            if (tb > best) continue;
        }
        // Möller-Trumbore em todos os triângulos (t no mesmo parâmetro do raio do mundo)
        for (size_t k = 0; k + 2 < tris->size(); k += 3) {
            const glm::vec3 &a = (*tris)[k], &b = (*tris)[k + 1], &cc = (*tris)[k + 2];
            glm::vec3 e1 = b - a, e2 = cc - a, pv = glm::cross(d, e2);
            float det = glm::dot(e1, pv);
            if (std::fabs(det) < 1e-12f) continue;
            float inv_ = 1.0f / det;
            glm::vec3 tv = o - a;
            float u = glm::dot(tv, pv) * inv_;
            if (u < 0.0f || u > 1.0f) continue;
            glm::vec3 qv = glm::cross(tv, e1);
            float v = glm::dot(d, qv) * inv_;
            if (v < 0.0f || u + v > 1.0f) continue;
            float t = glm::dot(e2, qv) * inv_;
            if (t > 1e-4f && t < best) {
                best = t;
                bestN = glm::normalize(glm::transpose(glm::inverse(glm::mat3(M))) * glm::cross(e1, e2));
            }
        }
    }
    // blocos e chão: só se nenhum modelo foi acertado ou se o bloco está bem na frente (a colisão
    // gerada da malha fica uns centímetros fora da parede e não pode ganhar dela)
    World::RayHit h = c.world.raycastHit(r.o, r.d, 400.0f);
    if (h.hit && (best > 1e29f || h.t < best - 0.3f)) { best = h.t; bestN = h.normal; }
    if (best > 1e29f) return false;
    pos = r.o + r.d * best;
    normal = glm::dot(bestN, r.d) > 0.0f ? -bestN : bestN;
    return true;
}

FreeEditor::Result FreeEditor::update(GLFWwindow* win, float dt, Context& c, int fbw, int fbh, glm::mat4& view,
                                      glm::mat4& proj, glm::vec3& camPos) {
    Result res = Result::None;
    ImGuiIO& io = ImGui::GetIO();
    auto pressed = [&](int k) {
        bool d = keyDown(win, k), p = d && !prevKey_[k];
        prevKey_[k] = d;
        return p && !io.WantTextInput;
    };
    double mx, my;
    glfwGetCursorPos(win, &mx, &my);
    bool lmb = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    const bool rmb = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    bool lmbPress = lmb && !lmbWas_, rmbPress = rmb && !rmbWas_;
    if (const char* ck = getenv("GA_CLICK")) { // depuração: "quadro x y" clica na vista nesse quadro
        static int fr = 0;
        int at = 0;
        float cx, cy;
        if (std::sscanf(ck, "%d %f %f", &at, &cx, &cy) == 3 && ++fr == at) {
            mx = cx;
            my = cy;
            lmbPress = true;
            viewHovered_ = true;
        }
    }
    fakeMouse_ = false;
    if (const char* dg = getenv("GA_DRAG")) { // depuração: "quadro x0 y0 x1 y1 quadros" arrasta segurando o botão
        static int fr = 0;
        int at = 0, n = 1;
        float x0, y0, x1, y1;
        ++fr;
        if (std::sscanf(dg, "%d %f %f %f %f %d", &at, &x0, &y0, &x1, &y1, &n) == 6 && fr >= at && fr <= at + n + 1) {
            float k = std::min(1.0f, (float)(fr - at) / n);
            mx = fakeMx_ = x0 + (x1 - x0) * k;
            my = fakeMy_ = y0 + (y1 - y0) * k;
            fakeMouse_ = true;
            viewHovered_ = true;
            lmbPress = fr == at;
            lmb = fr <= at + n;
        }
    }
    if (lmbPress && getenv("GA_CLICK")) { fakeMouse_ = true; fakeMx_ = mx; fakeMy_ = my; }

    if (getenv("GA_PAINT")) { // depuração: abre o pincel numa aba (0 textura...)
        static bool once = false;
        if (!once) { once = true; paintMode_ = true; paintTab_ = selectTab_ = std::atoi(getenv("GA_PAINT")); }
    }
    // desfazer: foto quando o cenário muda (fora de gestos, arrastes e campos sendo editados)
    if (lastScene_ != c.sceneId) { // outro cenário: histórico novo
        undo_.clear();
        redo_.clear();
        lastLayout_ = c.layout.serialize();
        lastScene_ = c.sceneId;
    } else if (mode_ == 0 && gizmoAxis_ < 0 && !brushing_ && !ImGui::IsAnyItemActive()) {
        std::string now = c.layout.serialize();
        if (now != lastLayout_) {
            if (!skipNextChange_) {
                UndoState u;
                u.layout = lastLayout_;
                pushUndo(u);
            }
            skipNextChange_ = false;
            lastLayout_ = now;
        }
    }
    {
        const bool ctrl = keyDown(win, GLFW_KEY_LEFT_CONTROL) || keyDown(win, GLFW_KEY_RIGHT_CONTROL);
        const bool shiftK = keyDown(win, GLFW_KEY_LEFT_SHIFT) || keyDown(win, GLFW_KEY_RIGHT_SHIFT);
        const bool z = keyDown(win, GLFW_KEY_Z), y = keyDown(win, GLFW_KEY_Y);
        if (const char* uf = getenv("GA_UNDO")) { // depuração: Ctrl+Z no quadro N
            static int fr = 0;
            if (++fr == std::atoi(uf)) res = undoRedo(c, false);
        }
        if (ctrl && mode_ == 0 && gizmoAxis_ < 0 && !brushing_ && !io.WantTextInput) {
            if (z && !zWas_) res = undoRedo(c, shiftK);
            else if (y && !yWas_) res = undoRedo(c, true);
        }
        zWas_ = z;
        yWas_ = y;
        if (ctrl) { prevKey_[GLFW_KEY_Z] = z; prevKey_[GLFW_KEY_Y] = y; } // Ctrl+Z não trava eixo
    }
    lmbWas_ = lmb;
    rmbWas_ = rmb;

    // câmera (com o botão direito segurado, como na Unity/Unreal): mouse gira; WASD anda,
    // Q/E desce/sobe, Shift acelera. Sem o botão as teclas são da edição (G, R, S...)
    if (rmb && mode_ == 0 && (looking_ || (rmbPress && viewHovered_))) {
        looking_ = true;
        yaw_ -= (float)(mx - lastMx_) * 0.004f;
        pitch_ = std::clamp(pitch_ + (float)(my - lastMy_) * 0.004f, -1.5f, 1.5f);
    }
    if (!rmb) looking_ = false;
    lastMx_ = mx;
    lastMy_ = my;
    glm::vec3 fwd(std::cos(pitch_) * std::sin(yaw_), -std::sin(pitch_), std::cos(pitch_) * std::cos(yaw_));
    glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
    if (!io.WantTextInput && mode_ == 0 && looking_) {
        float v = speed_ * dt * (keyDown(win, GLFW_KEY_LEFT_SHIFT) ? 3.0f : 1.0f);
        if (keyDown(win, GLFW_KEY_W)) cam_ += fwd * v;
        if (keyDown(win, GLFW_KEY_S)) cam_ -= fwd * v;
        if (keyDown(win, GLFW_KEY_D)) cam_ += right * v;
        if (keyDown(win, GLFW_KEY_A)) cam_ -= right * v;
        if (keyDown(win, GLFW_KEY_E)) cam_.y += v;
        if (keyDown(win, GLFW_KEY_Q)) cam_.y -= v;
    }
    // rodinha na vista: aproxima/afasta (com o botão direito segurado muda a velocidade de voo)
    if (viewHovered_ && io.MouseWheel != 0.0f && mode_ == 0) {
        if (looking_) speed_ = std::clamp(speed_ * std::pow(1.25f, io.MouseWheel), 0.5f, 60.0f);
        else cam_ += fwd * io.MouseWheel * std::max(0.5f, speed_ * 0.25f) * (keyDown(win, GLFW_KEY_LEFT_SHIFT) ? 3.0f : 1.0f);
    }
    view_ = glm::lookAt(cam_, cam_ + fwd, glm::vec3(0, 1, 0));
    // a imagem vai para o retângulo da vista (entre os painéis), com o aspecto dele
    int ww, wh;
    glfwGetWindowSize(win, &ww, &wh);
    winW_ = (float)std::max(1, ww);
    winH_ = (float)std::max(1, wh);
    const Rect vr = viewRect(winW_, winH_);
    const float rw = vr.x1 - vr.x0, rh = vr.y1 - vr.y0;
    const glm::vec3 shift((vr.x0 + vr.x1) / winW_ - 1.0f, 1.0f - (vr.y0 + vr.y1) / winH_, 0.0f);
    proj_ = glm::translate(glm::mat4(1.0f), shift) * glm::scale(glm::mat4(1.0f), glm::vec3(rw / winW_, rh / winH_, 1.0f)) *
            glm::perspective(glm::radians(60.0f), rw / std::max(1.0f, rh), 0.05f, 1000.0f);
    (void)fbw;
    (void)fbh;
    const bool inView = viewHovered_ && mx >= vr.x0 && mx <= vr.x1 && my >= vr.y0 && my <= vr.y1;

    // gestos estilo Blender
    if (mode_ == 0) {
        bool g = pressed(GLFW_KEY_G), r = pressed(GLFW_KEY_R), sc = pressed(GLFW_KEY_S);
        if (!rmb && !io.WantTextInput && !paintMode_) {
            if (g) startGesture(1, win, c, fbw, fbh);
            else if (r) startGesture(2, win, c, fbw, fbh);
            else if (sc) startGesture(3, win, c, fbw, fbh);
        }
        if (paintMode_) {
            // pincel: segura o botão esquerdo na vista para pintar; Shift inverte (apaga/desce)
            brushHasHit_ = false;
            if (inView || brushing_) {
                Ray ray = mouseRay(win, fbw, fbh);
                if (paintTab_ == 3) {
                    brushHasHit_ = surfaceHit(ray, c, brushHit_, brushNormal_);
                    brushView_ = ray.d;
                }
                else {
                    World::RayHit h = c.world.raycastHit(ray.o, ray.d, 400.0f);
                    if (h.hit) { brushHit_ = ray.o + ray.d * h.t; brushHasHit_ = true; }
                }
            }
            invert_ = keyDown(win, GLFW_KEY_LEFT_SHIFT) || keyDown(win, GLFW_KEY_RIGHT_SHIFT);
            if (lmbPress && inView && brushHasHit_ && !rmb) {
                brushing_ = true;
                pushUndo(snapshot(c, true)); // pincelada: pintura do chão e relevo antes dela
                skipNextChange_ = true;
                strokeProps_ = false;
                hasLastStamp_ = false;
                float y;
                levelY_ = c.terrain && c.terrain->heightAt(brushHit_.x, brushHit_.z, y) ? y : brushHit_.y;
            }
            if (brushing_ && lmb && brushHasHit_) applyBrush(c, dt);
            if (brushing_ && !lmb) {
                brushing_ = false;
                dirty_ = true;
                if (strokeProps_) res = Result::Rebuild; // folhagem nova: refaz colisões
            }
        } else {
            // setas de mover (como no Blender): passar o mouse acende, clicar e arrastar move nesse eixo
            if (gizmoAxis_ < 0) gizmoHover_ = kind_ != None && inView && !rmb ? gizmoPick((float)mx, (float)my, c) : -1;
            if (gizmoAxis_ >= 0) {
                if (lmb) {
                    glm::vec3 now;
                    if (gizmoParam(mouseRay(win, fbw, fbh), gizmoAxis_, now)) {
                        glm::vec3 d = now - gizmoHit0_;
                        if (gizmoAxis_ == 3) d.y = 0.0f;
                        moveSel(c, d);
                    }
                } else { // soltou: confirma
                    gizmoAxis_ = -1;
                    dirty_ = true;
                    status_ = "Movido: " + selName(c) + " (lembre de salvar)";
                    res = Result::Rebuild;
                }
            } else if (lmbPress && inView && gizmoHover_ >= 0) {
                captureSel(c);
                if (gizmoParam(mouseRay(win, fbw, fbh), gizmoHover_, gizmoHit0_)) {
                    gizmoAxis_ = gizmoHover_;
                    const char* nm[] = {"X", "altura", "Z", "chão"};
                    status_ = std::string("Movendo em ") + nm[gizmoAxis_] + " (solte para confirmar)";
                }
            } else if (lmbPress && inView) pick(mouseRay(win, fbw, fbh), c);
        }
        if (pressed(GLFW_KEY_ESCAPE)) { kind_ = None; idx_ = -1; status_ = "Nada selecionado"; }
        if (pressed(GLFW_KEY_DELETE) && kind_ != None && removeSelected(c)) res = Result::Rebuild;
        // Shift+D: duplica e a cópia já sai grudada no mouse (clique solta, botão direito desiste da posição)
        const bool shift = keyDown(win, GLFW_KEY_LEFT_SHIFT) || keyDown(win, GLFW_KEY_RIGHT_SHIFT);
        if (pressed(GLFW_KEY_D) && shift && !rmb && !paintMode_ && kind_ != None && duplicateSelected(c))
            startGesture(1, win, c, fbw, fbh);
        if (pressed(GLFW_KEY_F) && !rmb && kind_ != None) { // foca no selecionado
            glm::vec3 p = selPos(c);
            cam_ = p - fwd * 4.0f + glm::vec3(0.0f, 1.0f, 0.0f);
        }
    } else {
        if (pressed(GLFW_KEY_X)) axis_ = axis_ == 0 ? -1 : 0;
        if (pressed(GLFW_KEY_Y)) axis_ = axis_ == 1 ? -1 : 1;
        if (pressed(GLFW_KEY_Z)) axis_ = axis_ == 2 ? -1 : 2;
        applyGesture(win, c, fbw, fbh);
        if (lmbPress) { // confirma
            mode_ = 0;
            dirty_ = true;
            status_ = "Pronto: " + selName(c) + " (lembre de salvar)";
            res = Result::Rebuild;
        } else if (rmbPress || pressed(GLFW_KEY_ESCAPE)) { // cancela
            restore(c);
            mode_ = 0;
            status_ = "Cancelado";
        }
    }
    for (int k : {GLFW_KEY_G, GLFW_KEY_R, GLFW_KEY_S, GLFW_KEY_X, GLFW_KEY_Y, GLFW_KEY_Z, GLFW_KEY_ESCAPE, GLFW_KEY_DELETE, GLFW_KEY_F, GLFW_KEY_D})
        prevKey_[k] = keyDown(win, k);
    view = view_;
    proj = proj_;
    camPos = cam_;
    return res;
}

// ---------------------------------------------------------------- navegador de conteúdo
namespace {
const char* kCategories[] = {"Todos", "Cenário", "Armas", "Personagens", "Animações", "Importados", "Luzes"};

std::string lower(std::string s) {
    for (char& ch : s) ch = (char)std::tolower((unsigned char)ch);
    return s;
}
bool isModel(const std::filesystem::path& p) {
    std::string e = lower(p.extension().string());
    return e == ".glb" || e == ".gltf" || e == ".fbx" || e == ".obj";
}
} // namespace

void FreeEditor::scanAssets(const Context& c) {
    namespace fs = std::filesystem;
    assetList_.clear();
    std::error_code ec;
    auto add = [&](const fs::path& full) {
        Asset a;
        a.rel = fs::relative(full, root_, ec).generic_string();
        a.name = full.stem().string();
        a.category = 1;
        for (size_t g = 0; g < c.gunFiles.size(); ++g)
            if (c.gunFiles[g] == a.rel) a.gun = (int)g;
        std::string top = a.rel.substr(0, a.rel.find('/'));
        std::string ln = lower(a.name);
        if (a.gun >= 0 || top == "armas") a.category = 2;
        else if (top == "animation-teste") a.category = 4;
        else if (top == "assets") a.category = 5;
        else if (a.rel.find('/') == std::string::npos) {
            if (ln.find("dummy") != std::string::npos || ln.find("soldier") != std::string::npos) a.category = 3;
            // fbx pequenos na raiz são as animações (Walking, Running...)
            else if (lower(full.extension().string()) == ".fbx" && fs::file_size(full, ec) < 5u * 1024 * 1024) a.category = 4;
        }
        assetList_.push_back(a);
    };
    assetList_.push_back({"@porta", "Porta", 1, -1}); // porta de madeira que abre empurrando
    assetList_.push_back({"@porta_vidro", "Porta de vidro", 1, -1}); // vidro com moldura de alumínio
    // luzes (as do Blender + ambiente)
    assetList_.push_back({"@luz_ponto", "Luz pontual", 6, -1});
    assetList_.push_back({"@luz_spot", "Luz spot", 6, -1});
    assetList_.push_back({"@luz_area", "Luz de área", 6, -1});
    assetList_.push_back({"@luz_sol", "Sol", 6, -1});
    assetList_.push_back({"@luz_ambiente", "Luz ambiente", 6, -1});
    for (auto& e : fs::directory_iterator(root_, ec))
        if (e.is_regular_file(ec) && isModel(e.path())) add(e.path());
    for (const char* dir : {"armas", "elements-cenari", "assets", "animation-teste"}) {
        fs::path base = fs::path(root_) / dir;
        if (!fs::exists(base, ec)) continue;
        for (auto it = fs::recursive_directory_iterator(base, ec); it != fs::recursive_directory_iterator(); it.increment(ec))
            if (it->is_regular_file(ec) && isModel(it->path())) add(it->path());
    }
    std::sort(assetList_.begin(), assetList_.end(), [](const Asset& a, const Asset& b) {
        if (a.rel[0] == '@' || b.rel[0] == '@') return a.rel[0] == '@' && b.rel[0] != '@';
        return a.category != b.category ? a.category < b.category : lower(a.name) < lower(b.name);
    });
    assetsScanned_ = true;
}

bool FreeEditor::placeAsset(const Asset& a, const glm::vec3& pos, Context& c) {
    if (a.category == 4) {
        status_ = "Animações não vão para o cenário";
        return false;
    }
    if (a.rel.rfind("@luz_", 0) == 0) { // luz: um pouco acima de onde soltou
        LightPlace li;
        const std::string t = a.rel.substr(5);
        li.type = t == "spot" ? LightPlace::Spot : t == "ambiente" ? LightPlace::Ambient : t == "sol" ? LightPlace::Sun
                : t == "area" ? LightPlace::Area : LightPlace::Point;
        li.pos = pos + glm::vec3(0.0f, li.type == LightPlace::Ambient ? 1.5f : li.type == LightPlace::Sun ? 4.0f : 2.4f, 0.0f);
        li.yaw = yaw_;
        switch (li.type) {
        case LightPlace::Spot: li.intensity = 25.0f; li.range = 12.0f; break;
        case LightPlace::Ambient: li.intensity = 0.6f; li.range = 5.0f; li.color = glm::vec3(1.0f, 0.95f, 0.88f); break;
        case LightPlace::Sun: li.intensity = 2.0f; li.pitch = glm::radians(-50.0f); li.color = glm::vec3(1.0f, 0.93f, 0.82f); break;
        case LightPlace::Area: li.intensity = 10.0f; li.range = 10.0f; li.size = 1.0f; li.color = glm::vec3(1.0f, 0.97f, 0.92f); break;
        default: break;
        }
        c.layout.lights.push_back(li);
        kind_ = Light;
        idx_ = (int)c.layout.lights.size() - 1;
        dirty_ = true;
        status_ = "Luz posta: " + a.name + " (ajuste cor, força e alcance nos detalhes)";
        return true;
    }
    if (a.rel == "@porta" || a.rel == "@porta_vidro") { // folha de frente para a câmera, centrada onde soltou
        DoorPlace d;
        d.glass = a.rel == "@porta_vidro";
        if (d.glass) { d.width = 1.0f; d.height = 2.2f; }
        // de frente para a câmera, no ângulo reto mais próximo (depois gira à vontade)
        d.yaw = std::round((3.14159265f + yaw_) / 1.5707963f) * 1.5707963f;
        d.hinge = pos - d.axis() * (d.width * 0.5f);
        c.layout.extraDoors.push_back(d);
        kind_ = ExtraDoor;
        idx_ = (int)c.layout.extraDoors.size() - 1;
        dirty_ = true;
        status_ = "Porta posta (no cenário oficial o vão é recortado na parede)";
        return true;
    }
    if (a.gun >= 0) { // arma do jogo: muda o lugar onde ela fica no chão
        c.layout.guns[a.gun] = {pos + glm::vec3(0.0f, 0.01f, 0.0f), yaw_ + 1.5708f};
        kind_ = Gun;
        idx_ = a.gun;
        dirty_ = true;
        status_ = "Arma posta: " + c.gunNames[a.gun];
        return true;
    }
    // arquivo com vários objetos dentro (casa com porta, janelas, móveis...): uma peça por objeto,
    // cada uma no lugar que tem no arquivo; dá para mover, girar, pintar e apagar cada uma
    std::vector<std::string> parts = CharacterModel::listParts(root_ + "/" + a.rel);
    if (!parts.empty() && c.assets) {
        float minY = 1e9f;
        std::vector<int> ok;
        for (int k = 0; k < (int)parts.size(); ++k) {
            if (parts[k].empty()) continue;
            CharacterModel* pm = c.assets->mesh(a.rel + "#" + std::to_string(k));
            if (!pm) continue;
            minY = std::min(minY, pm->boundsMin.y);
            ok.push_back(k);
        }
        if (!ok.empty()) {
            const float yaw = yaw_ + 3.14159f;
            for (int k : ok) {
                PropPlace p;
                std::string base = a.name + "_" + parts[k];
                for (char& ch : base) if (ch == ' ') ch = '_';
                int n = 1;
                do p.name = base + (n > 1 ? "_" + std::to_string(n) : "");
                while (c.layout.prop(p.name) && ++n);
                p.asset = a.rel + "#" + std::to_string(k);
                p.pos = pos - glm::vec3(0.0f, minY, 0.0f); // o conjunto apoiado no chão
                p.yaw = yaw;
                c.layout.props.push_back(p);
            }
            kind_ = Prop;
            idx_ = (int)c.layout.props.size() - 1;
            dirty_ = true;
            status_ = "Adicionado " + a.name + " em " + std::to_string(ok.size()) + " partes separadas";
            return true;
        }
    }
    CharacterModel* m = c.assets ? c.assets->mesh(a.rel) : nullptr;
    if (!m) {
        status_ = "Não foi possível abrir " + a.rel;
        return false;
    }
    PropPlace p;
    int n = 1;
    do p.name = a.name + "_" + std::to_string(n++);
    while (c.layout.prop(p.name));
    for (char& ch : p.name) if (ch == ' ') ch = '_';
    p.asset = a.rel;
    p.pos = pos;
    p.yaw = yaw_ + 3.14159f; // de frente para a câmera
    glm::vec3 size = m->boundsMax - m->boundsMin;
    if (std::max({size.x, size.y, size.z}) > 60.0f) p.scale = 0.01f; // arquivo em centímetros
    c.layout.props.push_back(p);
    kind_ = Prop;
    idx_ = (int)c.layout.props.size() - 1;
    dirty_ = true;
    status_ = "Adicionado: " + p.name + " (G move, R gira, S escala)";
    return true;
}

void FreeEditor::startImport(bool image) {
    if (import_ && !import_->done) return;
    import_ = std::make_shared<Import>();
    import_->image = image;
    std::shared_ptr<Import> job = import_;
    std::thread([job]() {
        const char* cmd = job->image
            ? "zenity --file-selection --multiple --separator='|' --title='Importar textura (pode escolher todos os mapas juntos)' "
              "--file-filter='Imagens (png, jpg) | *.png *.jpg *.jpeg *.PNG *.JPG *.JPEG' 2>/dev/null"
            : "zenity --file-selection --title='Importar modelo 3D' "
              "--file-filter='Modelos 3D (glb, gltf, fbx, obj) | *.glb *.gltf *.fbx *.obj *.GLB *.FBX' 2>/dev/null";
        FILE* p = popen(cmd, "r");
        std::string out;
        if (p) {
            char buf[1024];
            while (std::fgets(buf, sizeof(buf), p)) out += buf;
            pclose(p);
        }
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
        job->path = out;
        job->done = true;
    }).detach();
}

// ---------------------------------------------------------------- interface
namespace {
const ImVec4 kBg(0.10f, 0.10f, 0.11f, 1.0f), kPanel(0.14f, 0.14f, 0.15f, 1.0f), kHeader(0.19f, 0.19f, 0.21f, 1.0f),
    kAccentV(0.95f, 0.62f, 0.16f, 1.0f);

// Botão da barra de ferramentas: ícone desenhado em cima do texto.
bool toolButton(const char* id, const char* label, float h, ImU32 tint, int icon) {
    ImGui::PushID(id);
    const float w = std::max(h * 1.25f, ImGui::CalcTextSize(label).x + 16.0f);
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton("b", ImVec2(w, h));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (ImGui::IsItemHovered()) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(255, 255, 255, 22), 4.0f);
    const float s = h * 0.42f;
    ImVec2 c(p.x + w * 0.5f, p.y + h * 0.08f + s * 0.5f);
    switch (icon) {
    case 0: // salvar (disquete)
        dl->AddRectFilled(ImVec2(c.x - s * 0.45f, c.y - s * 0.45f), ImVec2(c.x + s * 0.45f, c.y + s * 0.45f), tint, 2.0f);
        dl->AddRectFilled(ImVec2(c.x - s * 0.25f, c.y - s * 0.45f), ImVec2(c.x + s * 0.25f, c.y - s * 0.1f), IM_COL32(30, 30, 34, 255));
        dl->AddRectFilled(ImVec2(c.x - s * 0.3f, c.y + s * 0.08f), ImVec2(c.x + s * 0.3f, c.y + s * 0.4f), IM_COL32(230, 230, 230, 255));
        break;
    case 1: // importar (seta para baixo numa bandeja)
        dl->AddLine(ImVec2(c.x, c.y - s * 0.5f), ImVec2(c.x, c.y + s * 0.15f), tint, 3.0f);
        dl->AddTriangleFilled(ImVec2(c.x - s * 0.28f, c.y), ImVec2(c.x + s * 0.28f, c.y), ImVec2(c.x, c.y + s * 0.3f), tint);
        dl->AddLine(ImVec2(c.x - s * 0.45f, c.y + s * 0.45f), ImVec2(c.x + s * 0.45f, c.y + s * 0.45f), tint, 3.0f);
        break;
    case 2: // compilar (caixa)
        dl->AddRectFilled(ImVec2(c.x - s * 0.45f, c.y - s * 0.2f), ImVec2(c.x + s * 0.45f, c.y + s * 0.45f), tint, 2.0f);
        dl->AddTriangleFilled(ImVec2(c.x - s * 0.5f, c.y - s * 0.2f), ImVec2(c.x + s * 0.5f, c.y - s * 0.2f), ImVec2(c.x, c.y - s * 0.55f), tint);
        break;
    case 3: // jogar
        dl->AddTriangleFilled(ImVec2(c.x - s * 0.35f, c.y - s * 0.5f), ImVec2(c.x - s * 0.35f, c.y + s * 0.5f), ImVec2(c.x + s * 0.5f, c.y), tint);
        break;
    case 5: // parar
        dl->AddRectFilled(ImVec2(c.x - s * 0.4f, c.y - s * 0.4f), ImVec2(c.x + s * 0.4f, c.y + s * 0.4f), tint, 2.0f);
        break;
    case 6: // pincel
        dl->AddLine(ImVec2(c.x + s * 0.45f, c.y - s * 0.5f), ImVec2(c.x - s * 0.05f, c.y + s * 0.05f), tint, s * 0.18f);
        dl->AddTriangleFilled(ImVec2(c.x - s * 0.15f, c.y - s * 0.05f), ImVec2(c.x + s * 0.08f, c.y + s * 0.18f),
                              ImVec2(c.x - s * 0.5f, c.y + s * 0.5f), tint);
        break;
    case 7: // animação (bonequinho)
        dl->AddCircleFilled(ImVec2(c.x, c.y - s * 0.38f), s * 0.13f, tint);
        dl->AddLine(ImVec2(c.x, c.y - s * 0.25f), ImVec2(c.x, c.y + s * 0.1f), tint, 2.0f);
        dl->AddLine(ImVec2(c.x - s * 0.4f, c.y - s * 0.25f), ImVec2(c.x + s * 0.4f, c.y - s * 0.05f), tint, 2.0f);
        dl->AddLine(ImVec2(c.x, c.y + s * 0.1f), ImVec2(c.x - s * 0.3f, c.y + s * 0.5f), tint, 2.0f);
        dl->AddLine(ImVec2(c.x, c.y + s * 0.1f), ImVec2(c.x + s * 0.3f, c.y + s * 0.5f), tint, 2.0f);
        break;
    case 4: // configurações (controles deslizantes)
        for (int i = 0; i < 3; ++i) {
            float y = c.y - s * 0.35f + i * s * 0.35f, k = (i == 1 ? 0.2f : -0.2f) * s;
            dl->AddLine(ImVec2(c.x - s * 0.45f, y), ImVec2(c.x + s * 0.45f, y), tint, 2.0f);
            dl->AddCircleFilled(ImVec2(c.x + k, y), s * 0.12f, tint);
        }
        break;
    }
    ImVec2 ts = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(p.x + (w - ts.x) * 0.5f, p.y + h - ts.y - 2.0f), IM_COL32(225, 225, 225, 255), label);
    ImGui::PopID();
    return clicked;
}

// Ícone do tipo do modelo no navegador de conteúdo.
void assetIcon(ImDrawList* dl, ImVec2 a, ImVec2 b, int category) {
    const ImU32 cols[] = {IM_COL32(120, 120, 120, 255), IM_COL32(70, 150, 220, 255), IM_COL32(220, 120, 50, 255),
                          IM_COL32(90, 190, 110, 255), IM_COL32(170, 110, 220, 255), IM_COL32(220, 200, 70, 255)};
    ImU32 col = cols[std::clamp(category, 0, 5)];
    ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    float s = std::min(b.x - a.x, b.y - a.y) * 0.32f;
    if (category == 2) { // arma
        dl->AddRectFilled(ImVec2(c.x - s, c.y - s * 0.2f), ImVec2(c.x + s, c.y + s * 0.05f), col, 2.0f);
        dl->AddRectFilled(ImVec2(c.x + s * 0.2f, c.y), ImVec2(c.x + s * 0.5f, c.y + s * 0.6f), col, 2.0f);
    } else if (category == 3) { // personagem
        dl->AddCircleFilled(ImVec2(c.x, c.y - s * 0.6f), s * 0.3f, col);
        dl->AddRectFilled(ImVec2(c.x - s * 0.35f, c.y - s * 0.25f), ImVec2(c.x + s * 0.35f, c.y + s * 0.8f), col, 4.0f);
    } else if (category == 4) { // animação
        for (int i = 0; i < 3; ++i)
            dl->AddRectFilled(ImVec2(c.x - s + i * s * 0.7f, c.y - s * 0.5f), ImVec2(c.x - s * 0.5f + i * s * 0.7f, c.y + s * 0.5f), col, 2.0f);
    } else { // cubo
        ImVec2 f[4] = {{c.x - s * 0.7f, c.y - s * 0.3f}, {c.x + s * 0.3f, c.y - s * 0.3f}, {c.x + s * 0.3f, c.y + s * 0.7f}, {c.x - s * 0.7f, c.y + s * 0.7f}};
        dl->AddQuadFilled(f[0], f[1], f[2], f[3], col);
        dl->AddQuadFilled(f[0], ImVec2(f[0].x + s * 0.4f, f[0].y - s * 0.4f), ImVec2(f[1].x + s * 0.4f, f[1].y - s * 0.4f), f[1],
                          IM_COL32(255, 255, 255, 60) + (col & 0x00FFFFFF));
        dl->AddQuadFilled(f[1], ImVec2(f[1].x + s * 0.4f, f[1].y - s * 0.4f), ImVec2(f[2].x + s * 0.4f, f[2].y - s * 0.4f), f[2],
                          (col & 0x00FFFFFF) | 0xB0000000);
    }
}

bool beginPanel(const char* name, float x0, float y0, float x1, float y1) {
    ImGui::SetNextWindowPos(ImVec2(x0, y0));
    ImGui::SetNextWindowSize(ImVec2(x1 - x0, y1 - y0));
    return ImGui::Begin(name, nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                                           ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
}
} // namespace

void FreeEditor::drawToolbar(Context& c, Result& res, float W, float h) {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(W, h));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kBg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));
    ImGui::Begin("##barra", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                         ImGuiWindowFlags_NoBringToFrontOnFocus);
    const float bh = h - 8.0f;
    ImGui::TextColored(kAccentV, "GA");
    ImGui::SameLine();
    ImGui::TextDisabled("Editor");
    ImGui::SameLine(0, 24);
    auto save = [&]() {
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(c.root) / "levels", ec);
        saveGround(c);
        bool ok = c.layout.save(c.root + "/" + sceneFile(c.sceneId));
        status_ = ok ? std::string("Salvo em ") + sceneFile(c.sceneId) : "Erro ao salvar";
        if (ok) dirty_ = false;
        return ok;
    };
    ImGui::SetCursorPosY(4);
    if (toolButton("salvar", dirty_ ? "Salvar *" : "Salvar", bh, IM_COL32(120, 170, 235, 255), 0)) save();
    ImGui::SameLine();
    if (toolButton("importar", "Importar", bh, IM_COL32(110, 200, 120, 255), 1)) startImport();
    ImGui::SameLine();
    if (toolButton("compilar", "Compilar", bh, IM_COL32(200, 160, 90, 255), 2)) {
        save(); // o cenário oficial salvo vai embutido na versão final
        res = Result::Compile;
    }
    ImGui::SameLine();
    if (toolButton("config", "Configurações", bh, IM_COL32(190, 190, 190, 255), 4)) res = Result::Settings;
    ImGui::SameLine();
    if (paintMode_) { // botão aceso
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + bh * 1.6f, p.y + bh), IM_COL32(240, 160, 40, 60), 4.0f);
    }
    if (toolButton("pintar", "Pintar", bh, IM_COL32(240, 170, 60, 255), 6)) {
        paintMode_ = !paintMode_;
        kind_ = None;
        mode_ = 0;
        status_ = paintMode_ ? "Pincel: segure o botão esquerdo na vista para pintar (Shift inverte)" : "Clique num objeto para selecionar";
    }
    ImGui::SameLine();
    if (toolButton("animar", "Criar animação", bh, IM_COL32(120, 200, 255, 255), 7)) res = Result::Animate;
    ImGui::SameLine(0, 30);
    if (toolButton("jogar", playing_ ? "Parar (Esc)" : "Jogar", bh, playing_ ? IM_COL32(230, 80, 70, 255) : IM_COL32(90, 220, 90, 255), playing_ ? 5 : 3))
        res = Result::Play;
    ImGui::SameLine(0, 30);
    ImGui::SetCursorPosY((h - ImGui::GetFrameHeight()) * 0.5f);
    ImGui::TextDisabled("Cenário");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(170);
    if (ImGui::BeginCombo("##cena", sceneName(c.sceneId))) {
        for (int i = 0; i < SceneCount; ++i)
            if (ImGui::Selectable(sceneName(i), i == c.sceneId) && i != c.sceneId) {
                if (dirty_) save();
                sceneTarget_ = i;
                kind_ = None;
                res = Result::ChangeScene;
            }
        ImGui::EndCombo();
    }
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

// Combo "Colisão" de um modelo (vale para todas as cópias dele; salvo em assets.ini).
static bool collisionCombo(AssetLibrary* lib, const std::string& rel) {
    if (!lib || rel.empty() || rel[0] == '@') return false;
    static const char* names[] = {"Sem colisão (atravessa)", "Sólido (parado)", "Física (cai e é empurrado)"};
    int m = lib->collisionOf(rel);
    ImGui::TextDisabled("Colisão (todas as cópias deste modelo)");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Combo("##colisao", &m, names, 3)) {
        lib->collision[rel] = m;
        lib->saveConfig();
        return true;
    }
    return false;
}

void FreeEditor::drawDetails(Context& c, Result& res, const Rect& r) {
    beginPanel("Detalhes", r.x0, r.y0, r.x1, r.y1);
    bool edited = false, finished = false;
    auto track = [&](bool changed) {
        edited |= changed;
        finished |= ImGui::IsItemDeactivatedAfterEdit();
    };
    auto section = [](const char* t) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, kHeader);
        bool open = ImGui::CollapsingHeader(t, ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::PopStyleColor();
        return open;
    };
    ImGui::PushItemWidth(-90.0f);
    if (kind_ != None) {
        ImGui::TextColored(kAccentV, "%s", selName(c).c_str());
        if (kind_ == Prop) {
            PropPlace& p = c.layout.props[idx_];
            if (!p.asset.empty()) ImGui::TextDisabled("%s", p.asset.c_str());
            pivot0_ = glm::inverse(p.matrix()) * c.props[idx_].model;
            if (section("Transformação")) {
                float yawDeg = glm::degrees(p.yaw);
                track(ImGui::DragFloat3("Posição", &p.pos.x, 0.01f));
                track(ImGui::DragFloat("Giro (°)", &yawDeg, 0.5f));
                track(ImGui::DragFloat("Escala", &p.scale, 0.005f, 0.001f, 100.0f));
                p.yaw = glm::radians(yawDeg);
                if (edited) c.props[idx_].model = p.matrix(pivot0_);
            }
            CharacterModel* m = idx_ < (int)c.propMeshes.size() ? c.propMeshes[idx_] : nullptr;
            if (m && !m->materials.empty() && section("Materiais")) {
                for (int mi = 0; mi < (int)m->materials.size(); ++mi) {
                    auto it = p.textures.find(mi);
                    std::string cur = it == p.textures.end() ? "(original)" : it->second;
                    ImGui::TextDisabled("%s", mi < (int)m->materialNames.size() ? m->materialNames[mi].c_str() : "material");
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::BeginCombo(("##tex" + std::to_string(mi)).c_str(), cur.c_str())) {
                        if (ImGui::Selectable("(original)", it == p.textures.end()) && it != p.textures.end()) {
                            p.textures.erase(mi);
                            dirty_ = true;
                            status_ = "Textura original volta ao abrir o cenário de novo";
                        }
                        for (const std::string& img : images_)
                            if (ImGui::Selectable(img.c_str(), img == cur) && m->setBaseTexture(mi, root_ + "/" + img)) {
                                p.textures[mi] = img;
                                dirty_ = true;
                            }
                        ImGui::EndCombo();
                    }
                }
            }
            if (!p.asset.empty() && section("Física")) {
                if (collisionCombo(c.assets, p.asset)) res = Result::Rebuild;
            }
            if (!p.asset.empty()) {
                ImGui::Spacing();
                if (ImGui::Button("Pintar textura neste modelo", ImVec2(-1, 0))) {
                    paintMode_ = true;
                    paintTab_ = 3;
                    selectTab_ = 3;
                    status_ = "Pintar textura: clique e arraste em cima do modelo (Shift apaga)";
                }
                if (ImGui::Button("Duplicar")) {
                    PropPlace d = p;
                    int n = 1;
                    std::string base = d.name.substr(0, d.name.rfind('_'));
                    do d.name = base + "_" + std::to_string(n++);
                    while (c.layout.prop(d.name));
                    d.pos += glm::vec3(0.5f, 0.0f, 0.5f);
                    c.layout.props.push_back(d);
                    idx_ = (int)c.layout.props.size() - 1;
                    finished = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Remover (Delete)") && removeSelected(c)) res = Result::Rebuild;
            }
        } else if (kind_ == Box) {
            ::Box& b = c.world.boxes[idx_];
            glm::vec3 ctr = (b.min + b.max) * 0.5f, size = b.max - b.min;
            if (section("Transformação")) {
                track(ImGui::DragFloat3("Centro", &ctr.x, 0.01f));
                track(ImGui::DragFloat3("Tamanho", &size.x, 0.01f, 0.02f, 100.0f));
            }
            if (section("Aparência")) track(ImGui::ColorEdit3("Cor", &b.color.x));
            if (edited) {
                b.min = ctr - size * 0.5f;
                b.max = ctr + size * 0.5f;
                c.layout.boxes[idx_] = {b.min, b.max};
            }
        } else if (kind_ == DoorSel) {
            if (section("Porta")) {
                track(ImGui::DragFloat3("Dobradiça", &c.layout.doorHinge.x, 0.01f));
                track(ImGui::DragFloat("Largura", &c.layout.doorWidth, 0.01f, 0.5f, 3.0f));
                track(ImGui::DragFloat("Altura", &c.layout.doorHeight, 0.01f, 1.5f, 3.5f));
                if (edited && c.door) { c.door->hinge = c.layout.doorHinge; c.door->width = c.layout.doorWidth; c.door->height = c.layout.doorHeight; }
                ImGui::TextDisabled("No oficial o vão é recortado de novo nas paredes.");
            }
        } else if (kind_ == ExtraDoor) {
            DoorPlace& d = c.layout.extraDoors[idx_];
            if (section("Porta")) {
                track(ImGui::DragFloat3("Dobradiça", &d.hinge.x, 0.01f));
                track(ImGui::DragFloat("Largura", &d.width, 0.01f, 0.5f, 3.0f));
                track(ImGui::DragFloat("Altura", &d.height, 0.01f, 1.5f, 3.5f));
                float yawDeg = glm::degrees(d.yaw);
                track(ImGui::DragFloat("Ângulo (°)", &yawDeg, 0.5f));
                d.yaw = glm::radians(yawDeg);
                if (ImGui::Checkbox("Vidro", &d.glass)) finished = true;
                ImGui::SameLine();
                if (ImGui::Checkbox("Espelhada", &d.mirror)) finished = true;
                if (d.glass) {
                    ImGui::ColorEdit3("Cor do vidro", &d.glassColor.x);
                    finished |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::TextDisabled("Escuro = vidro fumê; claro = transparente.");
                } else {
                    // textura da folha: miniaturas das imagens (cor) + madeira padrão
                    ImGui::TextUnformatted("Textura da porta");
                    if (ImGui::Button(d.texture.empty() ? "Madeira (padrão)" : "Voltar à madeira")) { d.texture.clear(); finished = true; }
                    int col = 0;
                    for (size_t si = 0; si < texSets_.size(); ++si) {
                        const TexSet& t = texSets_[si];
                        ImGui::PushID((int)si + 1000);
                        if (col++ % 4) ImGui::SameLine();
                        bool cur = d.texture == t.color;
                        if (cur) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.95f, 0.62f, 0.16f, 1.0f));
                        if (ImGui::ImageButton("##tx", (ImTextureID)(intptr_t)thumb(t.color), ImVec2(52, 52))) { d.texture = t.color; finished = true; }
                        if (cur) ImGui::PopStyleColor();
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", t.name.c_str());
                        ImGui::PopID();
                    }
                    if (texSets_.empty()) ImGui::TextDisabled("Nenhuma imagem em textures/ (use Importar)");
                }
                if (ImGui::Button("Girar 90°")) { d.yaw += 1.5707963f; finished = true; }
                ImGui::SameLine();
                if (ImGui::Button("Remover (Delete)") && removeSelected(c)) res = Result::Rebuild;
            }
        } else if (kind_ == Light) {
            LightPlace& li = c.layout.lights[idx_];
            if (section("Luz")) {
                const char* tn[] = {"Pontual", "Spot", "Ambiente", "Sol", "Área"};
                if (ImGui::Combo("Tipo", &li.type, tn, 5)) finished = true;
                track(ImGui::DragFloat3("Posição", &li.pos.x, 0.01f));
                track(ImGui::ColorEdit3("Cor", &li.color.x));
                track(ImGui::DragFloat("Força", &li.intensity, 0.05f, 0.0f, 500.0f, "%.2f"));
                if (li.type != LightPlace::Sun) track(ImGui::DragFloat("Alcance (m)", &li.range, 0.05f, 0.3f, 80.0f, "%.1f"));
                if (li.type == LightPlace::Spot || li.type == LightPlace::Sun || li.type == LightPlace::Area) {
                    float yawDeg = glm::degrees(li.yaw), pitchDeg = glm::degrees(li.pitch);
                    track(ImGui::DragFloat("Giro (°)", &yawDeg, 0.5f));
                    track(ImGui::DragFloat("Inclinação (°)", &pitchDeg, 0.5f, -90.0f, 90.0f));
                    li.yaw = glm::radians(yawDeg);
                    li.pitch = glm::radians(pitchDeg);
                }
                if (li.type == LightPlace::Spot) {
                    track(ImGui::DragFloat("Cone (°)", &li.outer, 0.2f, 2.0f, 89.0f, "%.0f"));
                    track(ImGui::DragFloat("Borda macia (°)", &li.inner, 0.2f, 0.0f, li.outer, "%.0f"));
                }
                if (li.type == LightPlace::Area) track(ImGui::DragFloat("Tamanho (m)", &li.size, 0.01f, 0.05f, 10.0f, "%.2f"));
                ImGui::TextDisabled("%s", li.type == LightPlace::Ambient ? "Clareia por igual tudo dentro do alcance (salas escuras)."
                                    : li.type == LightPlace::Sun ? "Luz de longe numa direção, vale para o cenário todo."
                                    : li.type == LightPlace::Area ? "Painel que ilumina para a frente; maior = luz mais macia."
                                    : li.type == LightPlace::Spot ? "Cone de luz (lanterna, luminária)." : "Lâmpada: ilumina para todo lado.");
                if (ImGui::Button("Duplicar (Shift+D)")) duplicateSelected(c);
                ImGui::SameLine();
                if (ImGui::Button("Remover (Delete)") && removeSelected(c)) res = Result::Rebuild;
                if (edited) dirty_ = true;
            }
        } else if (kind_ == Spawn || kind_ == Start) {
            glm::vec3& p = kind_ == Spawn ? c.layout.spawns[idx_] : c.layout.start;
            float& y = kind_ == Spawn ? c.layout.spawnYaw[idx_] : c.layout.startYaw;
            if (section("Transformação")) {
                float yawDeg = glm::degrees(y);
                track(ImGui::DragFloat3("Posição", &p.x, 0.01f));
                track(ImGui::DragFloat("Virado para (°)", &yawDeg, 0.5f));
                y = glm::radians(yawDeg);
            }
            if (kind_ == Spawn && c.layout.spawns.size() > 1 && ImGui::Button("Remover inimigo") && removeSelected(c)) res = Result::Rebuild;
        } else if (kind_ == Gun) {
            if (section("No cenário")) {
                auto g = c.layout.guns.find(idx_);
                glm::vec3 p = g != c.layout.guns.end() ? g->second.first : c.gunPos[idx_];
                float yawDeg = glm::degrees(g != c.layout.guns.end() ? g->second.second : c.gunYaw[idx_]);
                bool ch = false;
                ch |= ImGui::DragFloat3("Posição", &p.x, 0.01f);
                finished |= ImGui::IsItemDeactivatedAfterEdit();
                ch |= ImGui::DragFloat("Giro (°)", &yawDeg, 0.5f);
                finished |= ImGui::IsItemDeactivatedAfterEdit();
                if (ch) c.layout.guns[idx_] = {p, glm::radians(yawDeg)};
                if (g != c.layout.guns.end() && ImGui::Button("Voltar ao lugar de fábrica") && removeSelected(c)) res = Result::Rebuild;
            }
            if (section("Na mão e no corpo")) {
                ImGui::TextWrapped("Pose da mão, mira, coldre/costas e pontos da arma (salvos em weapons.ini).");
                if (ImGui::Button("Abrir editor da arma", ImVec2(-1, 0))) {
                    gunToEdit_ = idx_;
                    res = Result::EditGun;
                }
            }
        }
    } else if (selAsset_ >= 0 && selAsset_ < (int)assetList_.size()) {
        const Asset& a = assetList_[selAsset_];
        ImGui::TextColored(kAccentV, "%s", a.name.c_str());
        ImGui::TextDisabled("%s", a.rel.c_str());
        std::error_code ec;
        auto sz = std::filesystem::file_size(std::filesystem::path(root_) / a.rel, ec);
        if (a.rel[0] != '@') ImGui::Text("Tipo: %s   %.1f MB", kCategories[a.category], ec ? 0.0 : sz / 1048576.0);
        else if (a.rel.rfind("@luz_", 0) == 0) ImGui::TextWrapped("Luz: arraste para a vista. Cor, força, alcance e direção ficam nos detalhes.");
        else if (a.rel == "@porta_vidro") ImGui::TextWrapped("Porta de vidro com moldura de alumínio: abre empurrando. A cor do vidro muda nos detalhes da porta.");
        else ImGui::TextWrapped("Porta de madeira que abre empurrando (mão ou ombro). A textura muda nos detalhes da porta.");
        if (a.category != 4 && a.gun < 0 && collisionCombo(c.assets, a.rel)) res = Result::Rebuild;
        ImGui::Spacing();
        if (a.category == 4) ImGui::TextWrapped("Animação: usada pelo personagem, não vai para o cenário.");
        else {
            ImGui::TextWrapped("Arraste para a vista para pôr no cenário.");
            if (ImGui::Button("Pôr na frente da câmera", ImVec2(-1, 0))) {
                glm::vec3 f(std::sin(yaw_), 0.0f, std::cos(yaw_)), at;
                Ray ray{cam_, glm::normalize(f * 3.0f + glm::vec3(0.0f, -1.2f, 0.0f))};
                if (!groundHit(ray, c, at)) at = glm::vec3(cam_.x, 0.0f, cam_.z) + f * 3.0f;
                if (placeAsset(a, at, c)) res = Result::Rebuild;
            }
        }
        if (a.gun >= 0 && ImGui::Button("Abrir editor da arma", ImVec2(-1, 0))) {
            gunToEdit_ = a.gun;
            res = Result::EditGun;
        }
    } else {
        ImGui::TextDisabled("Nada selecionado");
        ImGui::TextWrapped("Clique num objeto na vista ou na lista à direita, ou num modelo do navegador de conteúdo.");
    }
    ImGui::PopItemWidth();
    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("Adicionar inimigo na frente da câmera", ImVec2(-1, 0))) {
        glm::vec3 f(std::sin(yaw_), 0.0f, std::cos(yaw_));
        c.layout.spawns.push_back(glm::vec3(cam_.x, 0.0f, cam_.z) + f * 3.0f);
        c.layout.spawnYaw.push_back(yaw_ + 3.14159f);
        kind_ = Spawn;
        idx_ = (int)c.layout.spawns.size() - 1;
        finished = true;
    }
    if (finished) { dirty_ = true; res = Result::Rebuild; }
    ImGui::End();
}

void FreeEditor::drawOutliner(Context& c, const Rect& r) {
    beginPanel("Cenário", r.x0, r.y0, r.x1, r.y1);
    auto item = [&](Kind k, int i, const std::string& name) {
        if (ImGui::Selectable((name + "##" + std::to_string(k) + "_" + std::to_string(i)).c_str(), kind_ == k && idx_ == i,
                              ImGuiSelectableFlags_AllowDoubleClick)) {
            kind_ = k;
            idx_ = i;
            status_ = "Selecionado: " + name + "  (G mover, R girar, S escala, F focar)";
            if (ImGui::IsMouseDoubleClicked(0)) { // voa até ele
                glm::vec3 fwd(std::cos(pitch_) * std::sin(yaw_), -std::sin(pitch_), std::cos(pitch_) * std::cos(yaw_));
                cam_ = selPos(c) - fwd * 4.0f + glm::vec3(0.0f, 1.0f, 0.0f);
            }
        }
    };
    ImGui::TextDisabled("%s", sceneName(c.sceneId));
    ImGui::Separator();
    ImGui::BeginChild("arvore");
    if (ImGui::TreeNodeEx("Objetos", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (size_t i = 0; i < c.layout.props.size(); ++i) item(Prop, (int)i, c.layout.props[i].name);
        ImGui::TreePop();
    }
    if (ImGui::TreeNodeEx("Jogo", ImGuiTreeNodeFlags_DefaultOpen)) {
        item(Start, 0, "Início do personagem");
        if (c.layout.hasDoor) item(DoorSel, 0, "Porta");
        for (size_t i = 0; i < c.layout.extraDoors.size(); ++i) item(ExtraDoor, (int)i, "Porta " + std::to_string(i + 2));
        for (size_t i = 0; i < c.layout.spawns.size(); ++i) item(Spawn, (int)i, "Inimigo " + std::to_string(i + 1));
        for (size_t i = 0; i < c.gunNames.size(); ++i) item(Gun, (int)i, "Arma: " + c.gunNames[i]);
        ImGui::TreePop();
    }
    if (!c.layout.lights.empty() && ImGui::TreeNodeEx("Luzes", ImGuiTreeNodeFlags_DefaultOpen)) {
        Kind k0 = kind_;
        int i0 = idx_;
        for (size_t i = 0; i < c.layout.lights.size(); ++i) {
            kind_ = Light;
            idx_ = (int)i;
            std::string nm = selName(c);
            kind_ = k0;
            idx_ = i0;
            item(Light, (int)i, nm);
        }
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Blocos")) {
        for (size_t i = 0; i < c.world.boxes.size(); ++i)
            if (!c.world.boxes[i].hidden && c.world.boxes[i].solid) item(Box, (int)i, "Bloco " + std::to_string(i));
        ImGui::TreePop();
    }
    ImGui::EndChild();
    ImGui::End();
}

void FreeEditor::drawContent(Context& c, Result& res, const Rect& r) {
    if (!assetsScanned_) scanAssets(c);
    beginPanel("Navegador de conteúdo", r.x0, r.y0, r.x1, r.y1);
    if (ImGui::Button("Importar...")) startImport();
    ImGui::SameLine();
    if (ImGui::Button("Atualizar")) scanAssets(c);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220);
    ImGui::InputTextWithHint("##filtro", "Procurar modelos", filter_, sizeof(filter_));
    ImGui::SameLine();
    ImGui::TextDisabled("%s", status_.c_str());
    ImGui::Separator();
    ImGui::BeginChild("pastas", ImVec2(150, 0), true);
    for (int i = 0; i < 7; ++i) {
        int n = 0;
        for (const Asset& a : assetList_) n += i == 0 || a.category == i;
        if (ImGui::Selectable((std::string(kCategories[i]) + " (" + std::to_string(n) + ")").c_str(), category_ == i)) category_ = i;
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("modelos", ImVec2(0, 0), true);
    const float tile = 96.0f, gap = ImGui::GetStyle().ItemSpacing.x;
    int perRow = std::max(1, (int)((ImGui::GetContentRegionAvail().x + gap) / (tile + gap)));
    const std::string f = lower(filter_);
    int col = 0;
    for (int i = 0; i < (int)assetList_.size(); ++i) {
        const Asset& a = assetList_[i];
        if (category_ != 0 && a.category != category_) continue;
        if (!f.empty() && lower(a.name).find(f) == std::string::npos) continue;
        if (col++ % perRow) ImGui::SameLine();
        ImGui::PushID(i);
        ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("t", ImVec2(tile, tile + 30))) {
            selAsset_ = i;
            kind_ = None;
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        bool hov = ImGui::IsItemHovered(), sel = selAsset_ == i && kind_ == None;
        dl->AddRectFilled(p, ImVec2(p.x + tile, p.y + tile + 30), sel ? IM_COL32(70, 60, 40, 255) : hov ? IM_COL32(55, 55, 60, 255) : IM_COL32(38, 38, 42, 255), 4.0f);
        dl->AddRectFilled(ImVec2(p.x + 4, p.y + 4), ImVec2(p.x + tile - 4, p.y + tile - 4), IM_COL32(24, 24, 27, 255), 3.0f);
        if (a.rel == "@porta" || a.rel == "@porta_vidro") { // ícone de porta (vidro: azulada com moldura)
            const bool gl = a.rel == "@porta_vidro";
            ImVec2 c0(p.x + tile * 0.33f, p.y + tile * 0.18f), c1(p.x + tile * 0.67f, p.y + tile * 0.82f);
            dl->AddRectFilled(c0, c1, gl ? IM_COL32(150, 190, 205, 170) : IM_COL32(150, 95, 50, 255), 2.0f);
            if (gl) {
                dl->AddRect(c0, c1, IM_COL32(190, 190, 195, 255), 2.0f, 0, 3.0f);
                dl->AddLine(ImVec2(c0.x + 6, c1.y - 10), ImVec2(c1.x - 10, c0.y + 8), IM_COL32(235, 245, 250, 160), 2.0f);
            }
            dl->AddRect(ImVec2(c0.x - 3, c0.y - 3), ImVec2(c1.x + 3, c1.y), IM_COL32(90, 70, 55, 255), 2.0f, 0, 3.0f);
            dl->AddCircleFilled(ImVec2(c1.x - 7, (c0.y + c1.y) * 0.5f), 2.5f, IM_COL32(220, 210, 180, 255));
        } else if (a.rel.rfind("@luz_", 0) == 0) { // ícones de luz
            ImVec2 cc(p.x + tile * 0.5f, p.y + tile * 0.47f);
            const ImU32 yel = IM_COL32(255, 210, 90, 255), soft = IM_COL32(255, 210, 90, 70);
            const std::string t = a.rel.substr(5);
            if (t == "ambiente") {
                for (int k = 4; k >= 1; --k) dl->AddCircleFilled(cc, 7.0f * k, IM_COL32(255, 210, 90, 40 + 20 * (4 - k)));
            } else if (t == "spot") {
                dl->AddTriangleFilled(ImVec2(cc.x, cc.y - 22), ImVec2(cc.x - 22, cc.y + 22), ImVec2(cc.x + 22, cc.y + 22), soft);
                dl->AddCircleFilled(ImVec2(cc.x, cc.y - 22), 7.0f, yel);
            } else if (t == "area") {
                dl->AddRectFilled(ImVec2(cc.x - 18, cc.y - 18), ImVec2(cc.x + 18, cc.y - 8), yel, 2.0f);
                for (int k = -1; k <= 1; ++k) dl->AddLine(ImVec2(cc.x + k * 12, cc.y - 4), ImVec2(cc.x + k * 16, cc.y + 20), yel, 2.0f);
            } else { // ponto e sol: círculo com raios
                const float r = t == "sol" ? 11.0f : 8.0f;
                dl->AddCircleFilled(cc, r, yel);
                for (int k = 0; k < 8; ++k) {
                    float an = k * 0.785398f, r1 = r + (t == "sol" ? 14.0f : 10.0f);
                    dl->AddLine(ImVec2(cc.x + std::cos(an) * (r + 5), cc.y + std::sin(an) * (r + 5)),
                                ImVec2(cc.x + std::cos(an) * r1, cc.y + std::sin(an) * r1), yel, 2.0f);
                }
            }
        } else
            assetIcon(dl, ImVec2(p.x + 4, p.y + 4), ImVec2(p.x + tile - 4, p.y + tile - 4), a.category);
        if (sel) dl->AddRect(p, ImVec2(p.x + tile, p.y + tile + 30), IM_COL32(240, 160, 40, 255), 4.0f, 0, 2.0f);
        std::string label = a.name;
        while (label.size() > 3 && ImGui::CalcTextSize(label.c_str()).x > tile - 6) label = label.substr(0, label.size() - 4) + "..";
        dl->AddText(ImVec2(p.x + 4, p.y + tile + 4), IM_COL32(220, 220, 220, 255), label.c_str());
        if (hov) ImGui::SetTooltip("%s\n%s", a.rel.c_str(), a.category == 4 ? "animação" : "arraste para a vista");
        if (a.category != 4 && ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("GA_ASSET", &i, sizeof(int));
            ImGui::Text("%s", a.name.c_str());
            ImGui::EndDragDropSource();
        }
        ImGui::PopID();
    }
    if (col == 0) ImGui::TextDisabled("Nenhum modelo aqui. Use Importar para trazer .glb ou .fbx.");
    ImGui::EndChild();
    ImGui::End();
    (void)res;
}

FreeEditor::Result FreeEditor::drawUi(Context& c, const glm::mat4& vp, int, int) {
    Result res = Result::None;
    if (rebuildPending_) { rebuildPending_ = false; res = Result::Rebuild; }
    ImGuiIO& io = ImGui::GetIO();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    const Rect vr = viewRect(W, H);

    // importação terminou: copia para assets/ e mostra no navegador
    if (import_ && import_->done) {
        namespace fs = std::filesystem;
        std::string src = import_->path;
        const bool image = import_->image;
        import_.reset();
        if (!src.empty() && image) {
            // vários arquivos (todos os mapas de um material): copia todos para textures/
            std::error_code ec;
            std::vector<std::string> files;
            for (size_t a = 0;;) {
                size_t bar = src.find('|', a);
                files.push_back(src.substr(a, bar == std::string::npos ? std::string::npos : bar - a));
                if (bar == std::string::npos) break;
                a = bar + 1;
            }
            int copied = 0;
            std::string firstRel;
            for (const std::string& f : files) {
                fs::path dst = fs::path(root_) / "textures" / fs::path(f).filename();
                fs::create_directories(dst.parent_path(), ec);
                if (fs::copy_file(f, dst, fs::copy_options::overwrite_existing, ec)) ++copied;
                if (firstRel.empty()) firstRel = fs::relative(dst, root_, ec).string();
            }
            images_.clear();
            scanImages();
            status_ = "Importadas " + std::to_string(copied) + " imagens";
            for (const TexSet& t : texSets_)
                if (t.color == firstRel || t.normal == firstRel || t.rough == firstRel || t.height == firstRel || t.ao == firstRel) {
                    int maps = 1 + !t.normal.empty() + !t.rough.empty() + !t.height.empty() + !t.ao.empty();
                    status_ = "Material " + t.name + " importado (" + std::to_string(maps) + " mapas)";
                }
        } else if (!src.empty()) {
            std::error_code ec;
            fs::path dst = fs::path(root_) / "assets" / fs::path(src).filename();
            fs::create_directories(dst.parent_path(), ec);
            fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
            fs::path fbm = fs::path(src).replace_extension(".fbm"); // texturas soltas de um .fbx
            if (!ec && fs::is_directory(fbm, ec))
                fs::copy(fbm, dst.parent_path() / fbm.filename(), fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
            status_ = ec ? "Erro ao importar: " + ec.message() : "Importado: " + dst.filename().string();
            scanAssets(c);
            std::string rel = fs::relative(dst, root_, ec).generic_string();
            for (int i = 0; i < (int)assetList_.size(); ++i)
                if (assetList_[i].rel == rel) { selAsset_ = i; kind_ = None; category_ = 5; }
        }
    }

    ImGui::PushStyleColor(ImGuiCol_WindowBg, kPanel);
    ImGui::PushStyleColor(ImGuiCol_TitleBg, kBg);
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, kBg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));

    if (playing_) {
        const char* t = "JOGANDO  -  Esc para parar e voltar ao editor";
        ImVec2 ts = ImGui::CalcTextSize(t);
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        ImVec2 p((vr.x0 + vr.x1 - ts.x) * 0.5f, vr.y0 + 8.0f);
        fg->AddRectFilled(ImVec2(p.x - 12, p.y - 4), ImVec2(p.x + ts.x + 12, p.y + ts.y + 4), IM_COL32(20, 90, 30, 210), 5.0f);
        fg->AddText(p, IM_COL32(235, 255, 235, 255), t);
        drawToolbar(c, res, W, vr.y0);
        drawDetails(c, res, Rect{0, vr.y0, vr.x0, H});
        drawOutliner(c, Rect{vr.x1, vr.y0, W, H});
        drawContent(c, res, Rect{vr.x0, vr.y1, vr.x1, H});
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);
        return Result::None;
    }
    // vista: janela transparente (recebe o arrastar e soltar) com as marcações por cima da cena
    ImGui::SetNextWindowPos(ImVec2(vr.x0, vr.y0));
    ImGui::SetNextWindowSize(ImVec2(vr.x1 - vr.x0, vr.y1 - vr.y0));
    ImGui::Begin("##vista", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
                                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoScrollbar);
    viewHovered_ = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    ImGui::SetCursorPos(ImVec2(0, 0));
    ImGui::InvisibleButton("##alvo", ImVec2(vr.x1 - vr.x0, vr.y1 - vr.y0));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    auto project = [&](const glm::vec3& p, ImVec2& out) {
        glm::vec4 cl = vp * glm::vec4(p, 1.0f);
        if (cl.w < 0.05f) return false;
        out = ImVec2((cl.x / cl.w * 0.5f + 0.5f) * W, (0.5f - cl.y / cl.w * 0.5f) * H);
        return true;
    };
    auto boxLines = [&](const glm::mat4& m, const glm::vec3& lo, const glm::vec3& hi, ImU32 col, float th) {
        glm::vec3 cn[8];
        for (int i = 0; i < 8; ++i)
            cn[i] = glm::vec3(m * glm::vec4((i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z, 1.0f));
        const int e[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        for (auto& ed : e) {
            ImVec2 a, b;
            if (project(cn[ed[0]], a) && project(cn[ed[1]], b)) dl->AddLine(a, b, col, th);
        }
    };
    auto marker = [&](const glm::vec3& p, float yaw, ImU32 col, const char* text, bool sel, float h = 1.8f, float r = 0.3f) {
        boxLines(glm::mat4(1.0f), p - glm::vec3(r, 0.0f, r), p + glm::vec3(r, h, r), sel ? kSel : col, sel ? 2.5f : 1.5f);
        ImVec2 a, b, t;
        glm::vec3 f(std::sin(yaw), 0.0f, std::cos(yaw));
        if (project(p + glm::vec3(0, 0.05f, 0), a) && project(p + f * 0.8f + glm::vec3(0, 0.05f, 0), b)) dl->AddLine(a, b, col, 3.0f);
        if (project(p + glm::vec3(0, h + 0.2f, 0), t)) dl->AddText(t, col, text);
    };
    char buf[96];
    for (size_t i = 0; i < c.layout.spawns.size(); ++i) {
        std::snprintf(buf, sizeof(buf), "Inimigo %zu", i + 1);
        marker(c.layout.spawns[i], c.layout.spawnYaw[i], kEnemy, buf, kind_ == Spawn && idx_ == (int)i);
    }
    marker(c.layout.start, c.layout.startYaw, kStart, "Início", kind_ == Start);
    const ImU32 kGunCol = IM_COL32(240, 200, 80, 220);
    for (size_t i = 0; i < c.gunPos.size(); ++i) {
        bool sel = kind_ == Gun && idx_ == (int)i;
        glm::vec3 p = c.gunPos[i];
        float y = c.gunYaw[i];
        if (auto g = c.layout.guns.find((int)i); g != c.layout.guns.end()) { p = g->second.first; y = g->second.second; }
        marker(p, y, kGunCol, c.gunNames[i].c_str(), sel, 0.25f, 0.35f);
    }
    if (c.layout.hasDoor) {
        glm::vec3 h = c.layout.doorHinge;
        boxLines(glm::mat4(1.0f), h - glm::vec3(0, 0, 0.08f), h + glm::vec3(c.layout.doorWidth, c.layout.doorHeight, 0.08f),
                 kind_ == DoorSel ? kSel : kDoorCol, kind_ == DoorSel ? 2.5f : 1.5f);
    }
    for (size_t i = 0; i < c.layout.extraDoors.size(); ++i) {
        const DoorPlace& d = c.layout.extraDoors[i];
        bool sel = kind_ == ExtraDoor && idx_ == (int)i;
        boxLines(doorFrame(d), glm::vec3(0, 0, -0.08f), glm::vec3(d.width, d.height, 0.08f), sel ? kSel : kDoorCol, sel ? 2.5f : 1.5f);
    }
    if (kind_ == Prop && idx_ < (int)c.propMeshes.size() && c.propMeshes[idx_])
        boxLines(c.props[idx_].model, c.propMeshes[idx_]->boundsMin, c.propMeshes[idx_]->boundsMax, kSel, 2.5f);
    if (kind_ == Box) boxLines(glm::mat4(1.0f), c.world.boxes[idx_].min, c.world.boxes[idx_].max, kSel, 2.5f);
    // luzes: bolinha da cor da luz; direção (spot, sol, área); selecionada mostra o alcance
    for (size_t i = 0; i < c.layout.lights.size(); ++i) {
        const LightPlace& li = c.layout.lights[i];
        const bool sel = kind_ == Light && idx_ == (int)i;
        ImVec2 sp;
        if (!project(li.pos, sp)) continue;
        glm::vec3 lc = glm::clamp(li.color, 0.0f, 1.0f);
        ImU32 col = IM_COL32((int)(lc.r * 255), (int)(lc.g * 255), (int)(lc.b * 255), 255);
        dl->AddCircleFilled(sp, sel ? 9.0f : 7.0f, col);
        dl->AddCircle(sp, sel ? 13.0f : 10.0f, sel ? kSel : IM_COL32(20, 20, 20, 200), 0, 2.0f);
        if (li.type == LightPlace::Spot || li.type == LightPlace::Sun || li.type == LightPlace::Area) {
            ImVec2 tp;
            if (project(li.pos + li.dir() * (li.type == LightPlace::Sun ? 2.0f : 1.2f), tp)) dl->AddLine(sp, tp, col, 2.5f);
        }
        if (sel && li.type != LightPlace::Sun) { // alcance no chão
            ImVec2 prev;
            bool havePrev = false;
            for (int k = 0; k <= 48; ++k) {
                float an = k * 6.2831853f / 48;
                glm::vec3 q = li.pos + glm::vec3(std::cos(an), 0.0f, std::sin(an)) * li.range;
                ImVec2 qs;
                bool ok = project(q, qs);
                if (ok && havePrev) dl->AddLine(prev, qs, IM_COL32(255, 210, 90, 140), 1.5f);
                prev = qs;
                havePrev = ok;
            }
        }
        const char* tn[] = {"Luz", "Spot", "Ambiente", "Sol", "Área"};
        dl->AddText(ImVec2(sp.x + 12, sp.y - 8), col, tn[std::clamp(li.type, 0, 4)]);
    }

    // setas de mover no selecionado (vermelho X, verde Z, azul para cima; quadrado = no chão)
    if (kind_ != None && !paintMode_ && mode_ == 0) {
        const glm::vec3 p = selPos(c);
        const float L = gizmoLen(c);
        const ImU32 cols[3] = {IM_COL32(230, 60, 70, 255), IM_COL32(70, 120, 235, 255), IM_COL32(110, 200, 60, 255)};
        const ImU32 hot = IM_COL32(255, 235, 120, 255);
        const int active = gizmoAxis_ >= 0 ? gizmoAxis_ : gizmoHover_;
        ImVec2 o;
        if (project(p, o)) {
            // quadrado do chão
            ImVec2 q[4];
            bool ok = true;
            const float a0 = 0.25f * L, a1 = 0.55f * L;
            const glm::vec3 cn[4] = {{a0, 0, a0}, {a1, 0, a0}, {a1, 0, a1}, {a0, 0, a1}};
            for (int k = 0; k < 4; ++k) ok &= project(p + cn[k], q[k]);
            if (ok) {
                dl->AddQuadFilled(q[0], q[1], q[2], q[3], active == 3 ? IM_COL32(255, 235, 120, 150) : IM_COL32(230, 200, 80, 90));
                dl->AddQuad(q[0], q[1], q[2], q[3], active == 3 ? hot : IM_COL32(230, 200, 80, 220), 1.5f);
            }
            for (int k = 0; k < 3; ++k) {
                ImVec2 tip, b0;
                if (!project(p + kGizmoAxis[k] * L, tip) || !project(p + kGizmoAxis[k] * L * 0.15f, b0)) continue;
                ImU32 col = active == k ? hot : cols[k];
                dl->AddLine(b0, tip, col, active == k ? 4.0f : 3.0f);
                // ponta de seta
                glm::vec2 d(tip.x - b0.x, tip.y - b0.y);
                float len = glm::length(d);
                if (len > 1.0f) {
                    d /= len;
                    glm::vec2 n(-d.y, d.x);
                    float s = 11.0f;
                    ImVec2 t2(tip.x + d.x * s, tip.y + d.y * s);
                    dl->AddTriangleFilled(t2, ImVec2(tip.x + n.x * s * 0.55f, tip.y + n.y * s * 0.55f),
                                          ImVec2(tip.x - n.x * s * 0.55f, tip.y - n.y * s * 0.55f), col);
                }
            }
            dl->AddCircleFilled(o, 4.5f, IM_COL32(255, 255, 255, 230));
            dl->AddCircle(o, 8.0f, IM_COL32(255, 255, 255, 200), 0, 1.5f);
        }
    }

    // anel do pincel no chão
    if (paintMode_ && brushHasHit_) {
        const int segs = 48;
        ImVec2 prev;
        bool havePrev = false;
        for (int i = 0; i <= segs; ++i) {
            float a = i * 6.2831853f / segs;
            glm::vec3 q;
            if (paintTab_ == 3) { // no plano da superfície (parede, teto, chão)
                glm::vec3 n = brushNormal_;
                glm::vec3 t = glm::normalize(glm::cross(n, std::fabs(n.y) < 0.95f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0)));
                q = brushHit_ + n * 0.01f + (t * std::cos(a) + glm::cross(n, t) * std::sin(a)) * brushRadius_;
            } else {
                q = brushHit_ + glm::vec3(std::cos(a), 0.0f, std::sin(a)) * brushRadius_;
                float y;
                if (c.terrain && c.terrain->heightAt(q.x, q.z, y)) q.y = y;
                q.y += 0.03f;
            }
            ImVec2 sp;
            bool ok = project(q, sp);
            if (ok && havePrev) dl->AddLine(prev, sp, invert_ ? IM_COL32(255, 90, 80, 230) : IM_COL32(255, 200, 60, 230), 2.0f);
            prev = sp;
            havePrev = ok;
        }
        ImVec2 cp;
        if (project(brushHit_, cp)) dl->AddCircleFilled(cp, 3.0f, IM_COL32(255, 220, 120, 255));
    }
    // soltar um modelo do navegador na vista
    if (ImGui::BeginDragDropTarget()) {
        glm::vec3 at;
        Ray ray = rayAt(io.MousePos.x, io.MousePos.y);
        bool hit = groundHit(ray, c, at);
        if (hit) marker(at, yaw_ + 3.14159f, kSel, "Soltar aqui", true, 1.0f, 0.4f);
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("GA_ASSET")) {
            int i = *(const int*)pl->Data;
            if (hit && i >= 0 && i < (int)assetList_.size() && placeAsset(assetList_[i], at, c)) res = Result::Rebuild;
        }
        ImGui::EndDragDropTarget();
    }
    // ajuda e estado no canto da vista
    ImGui::SetCursorPos(ImVec2(10, 8));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 0.85f));
    ImGui::TextUnformatted("Botão direito + mouse/WASD/QE: voar   Clique: selecionar   G mover  R girar  S escala  "
                           "X/Y/Z eixo  F focar  Delete remover");
    ImGui::PopStyleColor();
    if (mode_ != 0) ImGui::TextColored(kAccentV, "%s", status_.c_str());
    ImGui::End();

    drawToolbar(c, res, W, vr.y0);
    Rect left{0, vr.y0, vr.x0, H}, right{vr.x1, vr.y0, W, H}, bottom{vr.x0, vr.y1, vr.x1, H};
    if (paintMode_) drawBrush(c, res, left);
    else drawDetails(c, res, left);
    drawOutliner(c, right);
    drawContent(c, res, bottom);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
    return res;
}

// ---------------------------------------------------------------- pincel: textura, relevo e folhagem
void FreeEditor::saveGround(Context& c) {
    LevelLayout& L = c.layout;
    if (c.paint && c.paint->active) {
        L.paintOn = true;
        L.paintOrigin = c.paint->origin;
        L.paintSize = c.paint->size;
        L.paintRes = c.paint->res;
        L.paintLayers.clear();
        for (const GroundPaint::Layer& l : c.paint->layers)
            L.paintLayers.push_back({l.albedo, l.normal, l.rough, l.height, l.ao, l.normalDX, l.tile, l.kind, l.grass, l.depth});
        c.paint->saveWeights(c.root + "/" + scenePaintFile(c.sceneId));
    } else L.paintOn = false;
    if (c.terrain && c.terrain->active) {
        L.terrainOn = true;
        L.terrainOrigin = c.terrain->origin;
        L.terrainSize = c.terrain->size;
        L.terrainN = c.terrain->n;
        c.terrain->save(c.root + "/" + sceneTerrainFile(c.sceneId));
    } else L.terrainOn = false;
}

// Área da pintura: tudo o que tem colisão no cenário, com folga (texel de ~8 cm).
void FreeEditor::ensurePaint(Context& c) {
    if (!c.paint || c.paint->active) return;
    glm::vec2 lo(1e9f), hi(-1e9f);
    for (const ::Box& b : c.world.boxes) {
        lo = glm::min(lo, glm::vec2(b.min.x, b.min.z));
        hi = glm::max(hi, glm::vec2(b.max.x, b.max.z));
    }
    if (lo.x > hi.x) { lo = glm::vec2(cam_.x, cam_.z) - 30.0f; hi = lo + 60.0f; }
    lo -= 6.0f;
    hi += 6.0f;
    float size = std::max(hi.x - lo.x, hi.y - lo.y);
    c.paint->create(lo, size, std::clamp((int)(size / 0.08f), 256, 2048));
    c.paint->uploadLayers(c.root);
}

void FreeEditor::applyBrush(Context& c, float dt) {
    const glm::vec2 p(brushHit_.x, brushHit_.z);
    if (paintTab_ == 3) { // pintar textura no modelo
        if (invert_) { // apaga os carimbos dentro do pincel
            auto& v = c.layout.stains;
            size_t n0 = v.size();
            v.erase(std::remove_if(v.begin(), v.end(), [&](const StainPlace& st) { return glm::length(st.pos - brushHit_) < brushRadius_; }),
                    v.end());
            if (v.size() != n0) status_ = "Apagado: " + std::to_string(n0 - v.size()) + " carimbos";
            return;
        }
        // arrastando: um carimbo a cada pedaço do tamanho do pincel
        if (hasLastStamp_ && glm::length(brushHit_ - lastStamp_) < brushRadius_ * stainSpacing_) return;
        StainPlace st;
        st.pos = brushHit_;
        // direção do carimbo: entre a superfície clicada e a câmera (em telha ondulada, friso ou
        // quina pinta o que se vê, mesmo clicando na lateral de uma onda)
        st.normal = glm::normalize(brushNormal_ - brushView_);
        st.size = brushRadius_;
        st.angle = stainRandomTurn_ ? (float)std::rand() / RAND_MAX * 6.2831853f : 0.0f;
        st.strength = stainStrength_;
        st.soft = brushSoft_;
        st.tex = stainTex_;
        c.layout.stains.push_back(st);
        lastStamp_ = brushHit_;
        hasLastStamp_ = true;
        status_ = std::to_string(c.layout.stains.size()) + " carimbos no cenário";
        return;
    }
    if (paintTab_ == 0) {
        ensurePaint(c);
        if (!c.paint || c.paint->layers.empty()) { status_ = "Adicione uma textura (botão +) para pintar"; return; }
        const int layer = std::min(activeLayer_, (int)c.paint->layers.size() - 1);
        const float depth = c.paint->layers[layer].depth;
        // camada que afunda o chão: guarda o peso antes e mexe no relevo pela diferença
        std::vector<std::pair<size_t, float>> before;
        if (depth != 0.0f && c.terrain && c.terrain->active) {
            const float cell = c.terrain->size / (c.terrain->n - 1);
            for (int j = 0; j < c.terrain->n; ++j)
                for (int i = 0; i < c.terrain->n; ++i) {
                    glm::vec2 q = c.terrain->origin + glm::vec2(i, j) * cell;
                    if (glm::length(q - p) > brushRadius_ + cell) continue;
                    before.push_back({(size_t)j * c.terrain->n + i, c.paint->weightAt(q, layer)});
                }
        }
        c.paint->brush(p, brushRadius_, brushStrength_, brushSoft_, layer, invert_, dt);
        if (!before.empty()) {
            const float cell = c.terrain->size / (c.terrain->n - 1);
            for (auto& [k, w0] : before) {
                glm::vec2 q = c.terrain->origin + glm::vec2((float)(k % c.terrain->n), (float)(k / c.terrain->n)) * cell;
                c.terrain->h[k] += (c.paint->weightAt(q, layer) - w0) * depth;
            }
            c.terrain->upload();
        }
    } else if (paintTab_ == 1) {
        if (!c.terrain || !c.terrain->active) { status_ = "Crie um terreno primeiro"; return; }
        int tool = terrainTool_;
        if (invert_ && tool <= 1) tool = 1 - tool; // Shift: desce em vez de subir (e vice-versa)
        c.terrain->brush(p, brushRadius_, terrainStrength_, brushSoft_, tool, levelY_, dt);
    } else {
        if (foliageAsset_ < 0 || foliageAsset_ >= (int)assetList_.size()) { status_ = "Escolha um modelo de folhagem"; return; }
        const Asset& a = assetList_[foliageAsset_];
        if (invert_) { // apaga as cópias desse modelo dentro do pincel
            for (int i = (int)c.layout.props.size() - 1; i >= 0; --i) {
                const PropPlace& q = c.layout.props[i];
                if (q.asset != a.rel || glm::length(glm::vec2(q.pos.x, q.pos.z) - p) > brushRadius_) continue;
                c.layout.props.erase(c.layout.props.begin() + i);
                if (i < (int)c.props.size()) c.props.erase(c.props.begin() + i);
                strokeProps_ = true;
            }
            return;
        }
        CharacterModel* m = c.assets ? c.assets->mesh(a.rel) : nullptr;
        if (!m) return;
        if (!c.assets->collision.count(a.rel)) { // grama atravessa; árvores e o resto são sólidos
            std::string ln = lower(a.name);
            bool soft = ln.find("gram") != std::string::npos || ln.find("grass") != std::string::npos ||
                        ln.find("flor") != std::string::npos || ln.find("bush") != std::string::npos;
            c.assets->collision[a.rel] = soft ? AssetLibrary::None : AssetLibrary::Solid;
            c.assets->saveConfig();
        }
        // quantas cópias tentar neste quadro: densidade (por m²) x área x tempo
        foliageAcc_ += foliageDensity_ * 3.14159f * brushRadius_ * brushRadius_ * dt * 0.8f;
        while (foliageAcc_ >= 1.0f) {
            foliageAcc_ -= 1.0f;
            float ang = (float)std::rand() / RAND_MAX * 6.2831853f, rr = std::sqrt((float)std::rand() / RAND_MAX) * brushRadius_;
            glm::vec2 q = p + glm::vec2(std::cos(ang), std::sin(ang)) * rr;
            bool tooClose = false;
            for (const PropPlace& o : c.layout.props)
                if (o.asset == a.rel && glm::length(glm::vec2(o.pos.x, o.pos.z) - q) < foliageSpacing_) { tooClose = true; break; }
            if (tooClose) continue;
            World::RayHit h = c.world.raycastHit(glm::vec3(q.x, brushHit_.y + 3.0f, q.y), glm::vec3(0, -1, 0), 10.0f);
            if (!h.hit || h.normal.y < 0.6f) continue; // só em chão
            PropPlace np;
            static int counter = 0;
            do np.name = "folha_" + std::to_string(++counter);
            while (c.layout.prop(np.name));
            np.asset = a.rel;
            np.pos = glm::vec3(q.x, brushHit_.y + 3.0f - h.t, q.y);
            np.yaw = (float)std::rand() / RAND_MAX * 6.2831853f;
            float base = 1.0f;
            glm::vec3 size = m->boundsMax - m->boundsMin;
            if (std::max({size.x, size.y, size.z}) > 60.0f) base = 0.01f;
            np.scale = base * (foliageScale_[0] + (foliageScale_[1] - foliageScale_[0]) * (float)std::rand() / RAND_MAX);
            c.layout.props.push_back(np);
            c.props.push_back({m, np.matrix()}); // aparece já; a colisão vem ao soltar o botão
            strokeProps_ = true;
        }
    }
}

void FreeEditor::drawBrush(Context& c, Result& res, const Rect& r) {
    beginPanel("Pincel", r.x0, r.y0, r.x1, r.y1);
    if (ImGui::BeginTabBar("abas")) {
        const char* tabs[] = {"Textura", "Relevo", "Folhagem", "Pintar textura"};
        for (int i = 0; i < 4; ++i)
            if (ImGui::BeginTabItem(tabs[i], nullptr, selectTab_ == i ? ImGuiTabItemFlags_SetSelected : 0)) {
                paintTab_ = i;
                ImGui::EndTabItem();
            }
        selectTab_ = -1;
        ImGui::EndTabBar();
    }
    ImGui::PushItemWidth(-110.0f);
    ImGui::SliderFloat("Tamanho (m)", &brushRadius_, 0.1f, 15.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("Suavidade", &brushSoft_, 0.0f, 1.0f, "%.2f");
    ImGui::PopItemWidth();
    ImGui::TextDisabled("Segure o botão esquerdo na vista. Shift inverte.");
    ImGui::Separator();
    auto imageCombo = [&](const char* id, std::string& cur, bool allowNone) {
        bool changed = false;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo(id, cur.empty() ? "(nenhuma)" : cur.c_str())) {
            if (allowNone && ImGui::Selectable("(nenhuma)", cur.empty())) { cur.clear(); changed = true; }
            for (const std::string& img : images_)
                if (ImGui::Selectable(img.c_str(), img == cur)) { cur = img; changed = true; }
            ImGui::EndCombo();
        }
        return changed;
    };

    if (paintTab_ == 3) { // ---- pintar textura no modelo (sujeira, manchas, pichação...)
        ImGui::TextWrapped("Pinta em cima de qualquer modelo ou parede: clique e arraste no lugar. Shift apaga.");
        ImGui::Text("Imagem:");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", stainTex_.empty() ? "sujeira automática" : stainTex_.c_str());
        if (imageCombo("##carimbo", stainTex_, true)) {}
        if (ImGui::Button("Sujeira automática", ImVec2(-1, 0))) stainTex_.clear();
        ImGui::PushItemWidth(-110.0f);
        ImGui::SliderFloat("Força", &stainStrength_, 0.05f, 1.0f, "%.2f");
        ImGui::SliderFloat("Espaçamento", &stainSpacing_, 0.1f, 1.5f, "%.2f");
        ImGui::PopItemWidth();
        ImGui::Checkbox("Girar cada carimbo", &stainRandomTurn_);
        if (ImGui::Button("Importar imagem (png/jpg)...", ImVec2(-1, 0))) startImport(true);
        ImGui::TextDisabled("%zu carimbos no cenário", c.layout.stains.size());
        if (!c.layout.stains.empty() && ImGui::Button("Apagar toda a pintura dos modelos", ImVec2(-1, 0))) {
            c.layout.stains.clear();
            dirty_ = true;
        }
    } else if (paintTab_ == 0) { // ---- textura
        ImGui::PushItemWidth(-110.0f);
        ImGui::SliderFloat("Força", &brushStrength_, 0.1f, 10.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
        ImGui::PopItemWidth();
        ImGui::Spacing();
        ImGui::Text("Texturas");
        bool reload = false;
        if (c.paint) {
            auto removeLayer = [&](int i) { // tira a camada e desloca os pesos das de cima
                c.paint->layers.erase(c.paint->layers.begin() + i);
                for (size_t k = 0; k < c.paint->weights.size(); k += 4) {
                    for (int j = i; j < 3; ++j) c.paint->weights[k + j] = c.paint->weights[k + j + 1];
                    c.paint->weights[k + 3] = 0;
                }
                c.paint->uploadWeights();
                activeLayer_ = std::max(0, std::min(activeLayer_, (int)c.paint->layers.size() - 1));
                reload = true;
                dirty_ = true;
            };
            auto setName = [&](const GroundPaint::Layer& l) {
                if (l.kind == 1) return std::string("Poça d'água");
                std::string n = l.albedo.empty() ? "(escolha)" : std::filesystem::path(l.albedo).stem().string();
                for (const TexSet& t : texSets_) if (t.color == l.albedo) n = t.name;
                return n;
            };
            // cards lado a lado: clicar seleciona a camada; o lápis abre a edição; "+" adiciona
            const float card = 88.0f, gap = ImGui::GetStyle().ItemSpacing.x;
            const int perRow = std::max(1, (int)((ImGui::GetContentRegionAvail().x + gap) / (card + gap)));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            bool openEdit = false, openNew = false;
            int col = 0;
            const int nLayers = (int)c.paint->layers.size();
            for (int i = 0; i <= nLayers; ++i) {
                const bool plus = i == nLayers;
                if (plus && nLayers >= GroundPaint::kMaxLayers) break;
                ImGui::PushID(i);
                if (col++ % perRow) ImGui::SameLine();
                ImGui::BeginGroup();
                ImVec2 p0 = ImGui::GetCursorScreenPos();
                ImVec2 p1(p0.x + card, p0.y + card + 20);
                ImGui::SetNextItemAllowOverlap();
                bool clicked = ImGui::InvisibleButton("card", ImVec2(card, card + 20));
                bool hov = ImGui::IsItemHovered();
                if (plus) {
                    dl->AddRectFilled(p0, p1, hov ? IM_COL32(60, 60, 66, 255) : IM_COL32(40, 40, 44, 255), 6.0f);
                    dl->AddRect(p0, p1, IM_COL32(110, 110, 120, 255), 6.0f, 0, 1.5f);
                    ImVec2 cc(p0.x + card * 0.5f, p0.y + card * 0.45f);
                    dl->AddLine(ImVec2(cc.x - 14, cc.y), ImVec2(cc.x + 14, cc.y), IM_COL32(220, 220, 220, 255), 3.0f);
                    dl->AddLine(ImVec2(cc.x, cc.y - 14), ImVec2(cc.x, cc.y + 14), IM_COL32(220, 220, 220, 255), 3.0f);
                    dl->AddText(ImVec2(p0.x + 8, p0.y + card + 1), IM_COL32(200, 200, 200, 255), "Adicionar");
                    if (clicked) openNew = true;
                    ImGui::EndGroup();
                    ImGui::PopID();
                    break;
                }
                GroundPaint::Layer& l = c.paint->layers[i];
                const bool sel = activeLayer_ == i;
                if (clicked) activeLayer_ = i;
                dl->AddRectFilled(p0, p1, sel ? IM_COL32(70, 55, 30, 255) : hov ? IM_COL32(55, 55, 60, 255) : IM_COL32(38, 38, 42, 255), 6.0f);
                ImVec2 i0(p0.x + 4, p0.y + 4), i1(p0.x + card - 4, p0.y + card - 4);
                if (l.kind == 1) {
                    dl->AddRectFilled(i0, i1, IM_COL32(40, 70, 95, 255), 4.0f);
                    dl->AddCircleFilled(ImVec2((i0.x + i1.x) * 0.5f, (i0.y + i1.y) * 0.5f + 4), 22.0f, IM_COL32(120, 170, 210, 255));
                } else if (unsigned th = l.albedo.empty() ? 0 : thumb(l.albedo))
                    dl->AddImageRounded((ImTextureID)(intptr_t)th, i0, i1, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 4.0f);
                else
                    dl->AddRectFilled(i0, i1, IM_COL32(90, 90, 90, 255), 4.0f);
                std::string nm = setName(l);
                while (nm.size() > 3 && ImGui::CalcTextSize(nm.c_str()).x > card - 8) nm = nm.substr(0, nm.size() - 3) + "..";
                dl->AddText(ImVec2(p0.x + 4, p0.y + card + 1), sel ? IM_COL32(255, 200, 120, 255) : IM_COL32(220, 220, 220, 255), nm.c_str());
                if (sel) dl->AddRect(p0, p1, IM_COL32(240, 160, 40, 255), 6.0f, 0, 3.0f);
                // lápis no canto: abre a edição dessa camada
                ImVec2 e0(p0.x + card - 28, p0.y + 6);
                ImGui::SetCursorScreenPos(e0);
                bool edit = ImGui::InvisibleButton("lapis", ImVec2(22, 22));
                bool ehov = ImGui::IsItemHovered();
                dl->AddRectFilled(e0, ImVec2(e0.x + 22, e0.y + 22), ehov ? IM_COL32(240, 160, 40, 255) : IM_COL32(20, 20, 22, 210), 4.0f);
                // lápis desenhado: corpo inclinado + ponta
                ImVec2 a0(e0.x + 6, e0.y + 16), a1(e0.x + 16, e0.y + 6);
                dl->AddLine(a0, a1, IM_COL32(240, 240, 240, 255), 3.0f);
                dl->AddTriangleFilled(ImVec2(e0.x + 4, e0.y + 18), ImVec2(e0.x + 5, e0.y + 14), ImVec2(e0.x + 8, e0.y + 17), IM_COL32(240, 240, 240, 255));
                if (ehov) ImGui::SetTooltip("Editar textura");
                if (edit) { editLayer_ = i; activeLayer_ = i; openEdit = true; }
                ImGui::SetCursorScreenPos(p0); // o grupo ocupa só o card
                ImGui::Dummy(ImVec2(card, card + 20));
                ImGui::EndGroup();
                ImGui::PopID();
            }
            if (const char* el = getenv("GA_EDITLAYER")) { // depuração: abre a edição dessa camada
                static bool once = false;
                if (!once) { once = true; editLayer_ = std::atoi(el); openEdit = true; }
            }
            if (openNew) ImGui::OpenPopup("nova_camada");
            if (openEdit) ImGui::OpenPopup("Editar textura");
            if (ImGui::BeginPopup("nova_camada")) {
                if (ImGui::Selectable("Textura (material)")) {
                    ensurePaint(c);
                    GroundPaint::Layer l;
                    if (!texSets_.empty()) {
                        const TexSet& t = texSets_[0];
                        l.albedo = t.color; l.normal = t.normal; l.normalDX = t.dx; l.rough = t.rough; l.height = t.height; l.ao = t.ao;
                    } else if (!images_.empty()) l.albedo = images_[0];
                    c.paint->layers.push_back(l);
                    activeLayer_ = editLayer_ = (int)c.paint->layers.size() - 1;
                    reload = true;
                    dirty_ = true;
                    openEdit = true;
                }
                bool hasWater = false;
                for (const GroundPaint::Layer& q : c.paint->layers) hasWater |= q.kind == 1;
                if (!hasWater && ImGui::Selectable("Poça d'água")) {
                    ensurePaint(c);
                    GroundPaint::Layer l;
                    l.kind = 1;
                    c.paint->layers.push_back(l);
                    activeLayer_ = (int)c.paint->layers.size() - 1;
                    reload = true;
                    dirty_ = true;
                }
                ImGui::EndPopup();
            }
            if (openEdit && !ImGui::IsPopupOpen("Editar textura")) ImGui::OpenPopup("Editar textura");
            // janela de edição da camada
            ImGui::SetNextWindowSize(ImVec2(470, 0), ImGuiCond_Appearing);
            bool keep = true;
            if (ImGui::BeginPopupModal("Editar textura", &keep)) {
                if (editLayer_ < 0 || editLayer_ >= (int)c.paint->layers.size()) ImGui::CloseCurrentPopup();
                else {
                    GroundPaint::Layer& l = c.paint->layers[editLayer_];
                    ImGui::TextColored(kAccentV, "Camada %d: %s", editLayer_ + 1, setName(l).c_str());
                    ImGui::Separator();
                    if (l.kind == 1) {
                        ImGui::TextWrapped("Poça d'água: pinte por cima do chão; a água junta nas partes baixas e mexe com passos e tiros.");
                    } else {
                        ImGui::TextUnformatted("Material");
                        ImGui::BeginChild("materiais", ImVec2(0, 250), true);
                        int mc = 0;
                        const int per = std::max(1, (int)((ImGui::GetContentRegionAvail().x + gap) / (84 + gap)));
                        for (size_t si = 0; si < texSets_.size(); ++si) {
                            const TexSet& t = texSets_[si];
                            ImGui::PushID((int)si + 5000);
                            if (mc++ % per) ImGui::SameLine();
                            ImGui::BeginGroup();
                            bool cur = l.albedo == t.color;
                            if (cur) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.95f, 0.62f, 0.16f, 1.0f));
                            if (ImGui::ImageButton("##m", (ImTextureID)(intptr_t)thumb(t.color), ImVec2(72, 72))) {
                                l.albedo = t.color; l.normal = t.normal; l.normalDX = t.dx; l.rough = t.rough; l.height = t.height; l.ao = t.ao;
                                reload = true;
                            }
                            if (cur) ImGui::PopStyleColor();
                            std::string nm = t.name;
                            while (nm.size() > 3 && ImGui::CalcTextSize(nm.c_str()).x > 80) nm = nm.substr(0, nm.size() - 3) + "..";
                            ImGui::TextUnformatted(nm.c_str());
                            ImGui::EndGroup();
                            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", t.name.c_str());
                            ImGui::PopID();
                        }
                        if (texSets_.empty()) ImGui::TextDisabled("Nenhuma textura em textures/.");
                        ImGui::EndChild();
                        if (ImGui::Button("Importar textura (todos os mapas)...")) startImport(true);
                        ImGui::Separator();
                        ImGui::PushItemWidth(-150.0f);
                        ImGui::SliderFloat("Repetir a cada (m)", &l.tile, 0.2f, 20.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
                        if (ImGui::IsItemDeactivatedAfterEdit()) dirty_ = true;
                        float cm = -l.depth * 100.0f + 0.0f; // sem "-0"
                        if (ImGui::SliderFloat("Afundar (cm)", &cm, 0.0f, 20.0f, "%.0f")) l.depth = -cm / 100.0f;
                        if (ImGui::IsItemDeactivatedAfterEdit()) dirty_ = true;
                        ImGui::PopItemWidth();
                        if (ImGui::Checkbox("Grama 3D (folhas de verdade)", &l.grass)) dirty_ = true;
                        if (ImGui::TreeNode("Avançado (cada mapa)")) {
                            ImGui::TextDisabled("Cor (albedo)");
                            reload |= imageCombo("##alb", l.albedo, false);
                            ImGui::TextDisabled("Normal");
                            reload |= imageCombo("##nrm", l.normal, true);
                            reload |= ImGui::Checkbox("Normal DirectX (verde invertido)", &l.normalDX);
                            ImGui::TextDisabled("Rugosidade");
                            reload |= imageCombo("##rgh", l.rough, true);
                            ImGui::TextDisabled("Relevo (displacement/altura)");
                            reload |= imageCombo("##hgt", l.height, true);
                            ImGui::TextDisabled("Oclusão (AO)");
                            reload |= imageCombo("##ao", l.ao, true);
                            ImGui::TreePop();
                        }
                    }
                    ImGui::Separator();
                    if (ImGui::Button("Remover camada")) {
                        removeLayer(editLayer_);
                        editLayer_ = -1;
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Fechar")) ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            ImGui::Spacing();
            if (ImGui::Button("Importar textura (todos os mapas)...", ImVec2(-1, 0))) startImport(true);
            if (reload) { ensurePaint(c); c.paint->uploadLayers(c.root); dirty_ = true; }
            if (c.paint->active && ImGui::Button("Apagar toda a pintura", ImVec2(-1, 0))) {
                std::fill(c.paint->weights.begin(), c.paint->weights.end(), 0);
                c.paint->uploadWeights();
                dirty_ = true;
            }
        }
        ImGui::TextWrapped("A textura vale no chão (superfícies viradas para cima). Shift + pincel apaga.");
    } else if (paintTab_ == 1) { // ---- relevo
        if (!c.terrain || !c.terrain->active) {
            ImGui::TextWrapped("Crie um terreno (uma malha de chão que pode ser esculpida) no lugar para onde a câmera olha.");
            ImGui::SetNextItemWidth(-110.0f);
            ImGui::SliderFloat("Lado (m)", &newTerrainSize_, 5.0f, 120.0f, "%.0f");
            if (c.terrain && ImGui::Button("Criar terreno aqui", ImVec2(-1, 0))) {
                glm::vec3 f(std::sin(yaw_), 0.0f, std::cos(yaw_));
                glm::vec3 ctr = glm::vec3(cam_.x, 0.0f, cam_.z) + f * (newTerrainSize_ * 0.4f);
                c.terrain->create(glm::vec2(ctr.x, ctr.z), newTerrainSize_, (int)(newTerrainSize_ / 0.25f) + 1, 0.0f);
                c.world.terrain = c.terrain;
                dirty_ = true;
            }
        } else {
            const char* tools[] = {"Subir", "Descer", "Suavizar", "Nivelar"};
            for (int i = 0; i < 4; ++i) {
                if (i) ImGui::SameLine();
                if (ImGui::RadioButton(tools[i], terrainTool_ == i)) terrainTool_ = i;
            }
            ImGui::SetNextItemWidth(-110.0f);
            ImGui::SliderFloat("Força (m/s)", &terrainStrength_, 0.05f, 5.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
            ImGui::TextWrapped("Nivelar deixa na altura de onde o clique começou. Shift troca subir/descer.");
            ImGui::TextDisabled("Terreno de %.0f m (%d x %d pontos)", c.terrain->size, c.terrain->n, c.terrain->n);
            if (ImGui::Button("Remover terreno", ImVec2(-1, 0))) {
                c.terrain->release();
                dirty_ = true;
            }
        }
    } else { // ---- folhagem
        if (!assetsScanned_) scanAssets(c);
        std::string cur = foliageAsset_ >= 0 && foliageAsset_ < (int)assetList_.size() ? assetList_[foliageAsset_].name : "(escolha um modelo)";
        ImGui::TextDisabled("Modelo (grama, árvores, pedras...)");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##folha", cur.c_str())) {
            for (int i = 0; i < (int)assetList_.size(); ++i) {
                const Asset& a = assetList_[i];
                if (a.category == 4 || a.gun >= 0 || a.rel[0] == '@') continue;
                if (ImGui::Selectable((a.name + "##" + a.rel).c_str(), i == foliageAsset_)) foliageAsset_ = i;
            }
            ImGui::EndCombo();
        }
        ImGui::PushItemWidth(-110.0f);
        ImGui::SliderFloat("Densidade (/m²)", &foliageDensity_, 0.05f, 30.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("Espaço mínimo (m)", &foliageSpacing_, 0.05f, 10.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
        ImGui::DragFloatRange2("Escala", &foliageScale_[0], &foliageScale_[1], 0.01f, 0.05f, 10.0f, "%.2f", "%.2f");
        ImGui::PopItemWidth();
        if (foliageAsset_ >= 0 && foliageAsset_ < (int)assetList_.size() && collisionCombo(c.assets, assetList_[foliageAsset_].rel))
            res = Result::Rebuild;
        int count = 0;
        if (foliageAsset_ >= 0 && foliageAsset_ < (int)assetList_.size())
            for (const PropPlace& q : c.layout.props) count += q.asset == assetList_[foliageAsset_].rel;
        ImGui::TextDisabled("%d cópias deste modelo no cenário", count);
        ImGui::TextWrapped("Pinta cópias do modelo com giro e tamanho aleatórios. Shift + pincel apaga. "
                           "A grama de exemplo está em assets/grama.glb.");
    }
    ImGui::Separator();
    ImGui::TextWrapped("%s", status_.c_str());
    ImGui::End();
}
