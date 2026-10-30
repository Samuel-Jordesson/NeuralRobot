#include "level.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

const char* sceneName(int id) { return id == SceneOfficial ? "Oficial" : "Teste (arena)"; }
const char* sceneFile(int id) { return id == SceneOfficial ? "levels/oficial.txt" : "levels/teste.txt"; }
std::string scenePaintFile(int id) { return id == SceneOfficial ? "levels/oficial_pintura.png" : "levels/teste_pintura.png"; }
std::string sceneTerrainFile(int id) { return id == SceneOfficial ? "levels/oficial_relevo.bin" : "levels/teste_relevo.bin"; }

void addMeshColliders(World& w, const std::vector<glm::vec3>& tris, const glm::mat4& model, float cell, float band0,
                      float band1) {
    if (tris.empty()) return;
    const float floorY = w.groundY;
    // limites no chão
    glm::vec2 lo(1e9f), hi(-1e9f);
    std::vector<glm::vec3> t(tris.size());
    for (size_t i = 0; i < tris.size(); ++i) {
        t[i] = glm::vec3(model * glm::vec4(tris[i], 1.0f));
        lo = glm::min(lo, glm::vec2(t[i].x, t[i].z));
        hi = glm::max(hi, glm::vec2(t[i].x, t[i].z));
    }
    const int nx = (int)std::ceil((hi.x - lo.x) / cell) + 1, nz = (int)std::ceil((hi.y - lo.y) / cell) + 1;
    std::vector<char> blocked((size_t)nx * nz, 0);
    std::vector<float> top((size_t)nx * nz, floorY);
    std::vector<float> ceil((size_t)nx * nz, 1e9f); // teto: o mais baixo acima da cabeça

    auto mark = [&](const glm::vec3& p) {
        int x = (int)((p.x - lo.x) / cell), z = (int)((p.z - lo.y) / cell);
        if (x < 0 || z < 0 || x >= nx || z >= nz) return;
        size_t k = (size_t)z * nx + x;
        float h = p.y - floorY;
        if (h > band0 && h < band1) blocked[k] = 1;
        if (h < band1 + 0.5f) top[k] = std::max(top[k], p.y);
        if (h > band1 + 0.15f && h < 8.0f) ceil[k] = std::min(ceil[k], p.y);
    };
    // pontos espalhados em cada triângulo (mais densos que a célula, para não deixar buracos)
    for (size_t i = 0; i + 2 < t.size(); i += 3) {
        const glm::vec3 &a = t[i], &b = t[i + 1], &c = t[i + 2];
        float e = std::max({glm::length(b - a), glm::length(c - b), glm::length(a - c)});
        int n = std::clamp((int)std::ceil(e / (cell * 0.5f)), 1, 400);
        for (int u = 0; u <= n; ++u)
            for (int v = 0; u + v <= n; ++v) {
                float fu = (float)u / n, fv = (float)v / n;
                mark(a + (b - a) * fu + (c - a) * fv);
            }
    }
    // caixas: junta células vizinhas na linha com altura parecida; alto (parede/estante) vira pilar até 3,2 m
    int count = 0;
    auto heightOf = [&](size_t k) { return top[k] - floorY > band1 ? floorY + 3.2f : top[k]; };
    for (int z = 0; z < nz; ++z) {
        int x = 0;
        while (x < nx) {
            size_t k = (size_t)z * nx + x;
            if (!blocked[k]) { ++x; continue; }
            float h = heightOf(k);
            int x1 = x + 1;
            while (x1 < nx && blocked[(size_t)z * nx + x1] && std::fabs(heightOf((size_t)z * nx + x1) - h) < 0.15f) ++x1;
            Box b;
            b.min = glm::vec3(lo.x + x * cell, floorY, lo.y + z * cell);
            b.max = glm::vec3(lo.x + x1 * cell, h, lo.y + (z + 1) * cell);
            b.color = glm::vec3(0.5f);
            b.hidden = true;
            // mesma faixa da linha de cima? estica a caixa dela em vez de criar outra
            bool merged = false;
            for (int j = (int)w.boxes.size() - 1; j >= 0 && j >= (int)w.boxes.size() - 64; --j) {
                Box& o = w.boxes[j];
                if (o.hidden && o.min.x == b.min.x && o.max.x == b.max.x && std::fabs(o.max.z - b.min.z) < 1e-4f &&
                    std::fabs(o.max.y - b.max.y) < 0.15f) {
                    o.max.z = b.max.z;
                    merged = true;
                    break;
                }
            }
            if (!merged) { w.boxes.push_back(b); ++count; }
            x = x1;
        }
    }
    // teto: placas que só barram raios (câmera, tiros, visão), não o corpo
    int slabs = 0;
    for (int z = 0; z < nz; ++z) {
        int x = 0;
        while (x < nx) {
            size_t k = (size_t)z * nx + x;
            if (blocked[k] || ceil[k] > 1e8f) { ++x; continue; }
            float h = ceil[k];
            int x1 = x + 1;
            while (x1 < nx && !blocked[(size_t)z * nx + x1] && std::fabs(ceil[(size_t)z * nx + x1] - h) < 0.2f) ++x1;
            Box b;
            b.min = glm::vec3(lo.x + x * cell, h, lo.y + z * cell);
            b.max = glm::vec3(lo.x + x1 * cell, h + 0.3f, lo.y + (z + 1) * cell);
            b.color = glm::vec3(0.5f);
            b.hidden = true;
            b.solid = false;
            bool merged = false; // mesma faixa na linha de cima: estica a placa dela
            for (int j = (int)w.boxes.size() - 1; j >= 0 && j >= (int)w.boxes.size() - 64; --j) {
                Box& o = w.boxes[j];
                if (!o.solid && o.hidden && o.min.x == b.min.x && o.max.x == b.max.x && std::fabs(o.max.z - b.min.z) < 1e-4f &&
                    std::fabs(o.min.y - b.min.y) < 0.2f) {
                    o.max.z = b.max.z;
                    merged = true;
                    break;
                }
            }
            if (!merged) { w.boxes.push_back(b); ++slabs; }
            x = x1;
        }
    }
    count += slabs;
    // mapa de teto (só do primeiro: a malha do cenário; objetos postos depois não mexem nele)
    if (w.roof.nx == 0) {
        static int version = 0;
        w.roof.origin = lo;
        w.roof.cell = cell;
        w.roof.nx = nx;
        w.roof.nz = nz;
        w.roof.height.resize(ceil.size());
        for (size_t k = 0; k < ceil.size(); ++k) w.roof.height[k] = ceil[k] > 1e8f ? RoofMap::kNoRoof : ceil[k];
        w.roof.version = ++version;
    }
    std::printf("[cenario] colisao gerada: %d caixas (%dx%d celulas de %.2f m)\n", count, nx, nz, cell);
}

void carveColliders(World& w, const glm::vec3& lo, const glm::vec3& hi) {
    std::vector<Box> out;
    for (const Box& b : w.boxes) {
        bool inside = b.hidden && b.solid && b.max.x > lo.x && b.min.x < hi.x && b.max.z > lo.z && b.min.z < hi.z &&
                      b.max.y > lo.y && b.min.y < hi.y;
        if (!inside) { out.push_back(b); continue; }
        // pedaços de fora: antes/depois em x; no meio, antes/depois em z
        auto keep = [&](glm::vec3 mn, glm::vec3 mx) {
            if (mx.x - mn.x > 0.01f && mx.z - mn.z > 0.01f) { Box c = b; c.min = mn; c.max = mx; out.push_back(c); }
        };
        keep(b.min, {std::min(b.max.x, lo.x), b.max.y, b.max.z});
        keep({std::max(b.min.x, hi.x), b.min.y, b.min.z}, b.max);
        float x0 = std::max(b.min.x, lo.x), x1 = std::min(b.max.x, hi.x);
        keep({x0, b.min.y, b.min.z}, {x1, b.max.y, std::min(b.max.z, lo.z)});
        keep({x0, b.min.y, std::max(b.min.z, hi.z)}, {x1, b.max.y, b.max.z});
    }
    w.boxes.swap(out);
}

// ---------------------------------------------------------------- layout
glm::mat4 PropPlace::matrix(const glm::mat4& pivot) const {
    return glm::translate(glm::mat4(1.0f), pos) * glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0, 1, 0)) *
           glm::scale(glm::mat4(1.0f), glm::vec3(scale) * stretch) * pivot;
}

PropPlace* LevelLayout::prop(const std::string& name) {
    for (PropPlace& p : props)
        if (p.name == name) return &p;
    return nullptr;
}

std::vector<DoorPlace> LevelLayout::allDoors() const {
    std::vector<DoorPlace> v;
    if (hasDoor) {
        DoorPlace d;
        d.hinge = doorHinge;
        d.width = doorWidth;
        d.height = doorHeight;
        v.push_back(d);
    }
    v.insert(v.end(), extraDoors.begin(), extraDoors.end());
    return v;
}

std::string LevelLayout::serialize() const {
    std::ostringstream o;
    o << "# Cenario (editor livre). Angulos em graus.\n";
    for (const PropPlace& p : props) {
        o << "prop " << p.name << " " << p.pos.x << " " << p.pos.y << " " << p.pos.z << " " << glm::degrees(p.yaw) << " "
          << p.scale;
        if (!p.asset.empty()) o << " " << p.asset;
        o << "\n";
        for (auto& [m, path] : p.textures) o << "tex " << p.name << " " << m << " " << path << "\n";
        if (p.stretch != glm::vec3(1.0f))
            o << "stretch " << p.name << " " << p.stretch.x << " " << p.stretch.y << " " << p.stretch.z << "\n";
    }
    for (auto& [i, b] : boxes)
        o << "box " << i << " " << b.first.x << " " << b.first.y << " " << b.first.z << " " << b.second.x << " "
          << b.second.y << " " << b.second.z << "\n";
    if (!hasDoor) o << "nodoor\n";
    if (spawns.empty()) o << "nospawns\n";
    if (hasDoor)
        o << "door " << doorHinge.x << " " << doorHinge.y << " " << doorHinge.z << " " << doorWidth << " " << doorHeight << "\n";
    // portas extras: dobradiça, largura, altura, ângulo (graus), tipo (m = espelhada, v = vidro)
    for (const DoorPlace& d : extraDoors) {
        std::string flags = std::string(d.mirror ? "m" : "") + (d.glass ? "v" : "");
        o << "door3 " << d.hinge.x << " " << d.hinge.y << " " << d.hinge.z << " " << d.width << " " << d.height << " "
          << glm::degrees(d.yaw) << " " << (flags.empty() ? "-" : flags) << " " << d.glassColor.r << " " << d.glassColor.g << " "
          << d.glassColor.b << " " << (d.texture.empty() ? "-" : d.texture) << "\n";
    }
    if (extraDoors.empty()) o << "nodoors\n"; // tirou todas: o arquivo não volta para as de fábrica
    // luzes: tipo x y z giro inclinação r g b brilho alcance cone-interno cone-externo
    for (const LightPlace& li : lights)
        o << "light " << li.type << " " << li.pos.x << " " << li.pos.y << " " << li.pos.z << " " << glm::degrees(li.yaw) << " "
          << glm::degrees(li.pitch) << " " << li.color.r << " " << li.color.g << " " << li.color.b << " " << li.intensity
          << " " << li.range << " " << li.inner << " " << li.outer << " " << li.size << "\n";
    // pintura nos modelos: posição, normal, raio, giro, força, suavidade, imagem (- = sujeira automática)
    for (const StainPlace& st : stains)
        o << "stain " << st.pos.x << " " << st.pos.y << " " << st.pos.z << " " << st.normal.x << " " << st.normal.y << " "
          << st.normal.z << " " << st.size << " " << st.angle << " " << st.strength << " " << st.soft << " "
          << (st.tex.empty() ? "-" : st.tex) << "\n";
    for (auto& [i, g] : guns)
        o << "gun " << i << " " << g.first.x << " " << g.first.y << " " << g.first.z << " " << glm::degrees(g.second) << "\n";
    for (size_t i = 0; i < spawns.size(); ++i)
        o << "spawn " << spawns[i].x << " " << spawns[i].y << " " << spawns[i].z << " " << glm::degrees(spawnYaw[i]) << "\n";
    if (paintOn) {
        o << "paint " << paintOrigin.x << " " << paintOrigin.y << " " << paintSize << " " << paintRes << "\n";
        for (const PaintLayer& l : paintLayers)
            o << "layer " << l.tile << " " << l.albedo << "|" << l.normal << "|" << l.rough << "|" << l.height << "|" << l.ao << "|"
              << (l.normalDX ? "dx" : "") << "|" << (l.kind == 1 ? "agua" : "") << "|" << (l.grass ? "grama" : "") << "|"
              << l.depth << "\n";
    }
    if (terrainOn)
        o << "terrain " << terrainOrigin.x << " " << terrainOrigin.y << " " << terrainSize << " " << terrainN << "\n";
    o << "start " << start.x << " " << start.y << " " << start.z << " " << glm::degrees(startYaw) << "\n";
    return o.str();
}

bool LevelLayout::parse(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    bool any = false, spawnSeen = false, door2Seen = false, lightSeen = false, stainSeen = false;
    while (std::getline(in, line)) {
        std::istringstream l(line);
        std::string k;
        if (!(l >> k) || k[0] == '#') continue;
        if (k == "prop") {
            PropPlace p;
            float yawDeg = 0.0f;
            l >> p.name >> p.pos.x >> p.pos.y >> p.pos.z >> yawDeg >> p.scale;
            std::getline(l >> std::ws, p.asset);
            p.yaw = glm::radians(yawDeg);
            if (PropPlace* e = prop(p.name)) { p.textures = e->textures; *e = p; }
            else props.push_back(p);
        } else if (k == "tex") {
            std::string name, path;
            int m;
            l >> name >> m;
            std::getline(l >> std::ws, path);
            if (PropPlace* e = prop(name)) e->textures[m] = path;
        } else if (k == "box") {
            int i;
            glm::vec3 a, b;
            l >> i >> a.x >> a.y >> a.z >> b.x >> b.y >> b.z;
            boxes[i] = {a, b};
        } else if (k == "door") {
            hasDoor = true;
            l >> doorHinge.x >> doorHinge.y >> doorHinge.z >> doorWidth >> doorHeight;
        } else if (k == "door2") {
            if (!door2Seen) { extraDoors.clear(); door2Seen = true; }
            DoorPlace d;
            std::string ax;
            l >> d.hinge.x >> d.hinge.y >> d.hinge.z >> d.width >> d.height >> ax;
            d.yaw = ax.empty() || ax[0] != 'z' ? 0.0f : -glm::half_pi<float>();
            d.mirror = ax.size() > 1 && ax[1] == 'm'; // "xm"/"zm": folha espelhada
            extraDoors.push_back(d);
        } else if (k == "door3" || k == "nodoors") { // porta em qualquer ângulo (formato novo)
            if (!door2Seen) { extraDoors.clear(); door2Seen = true; }
            if (k == "nodoors") { any = true; continue; }
            DoorPlace d;
            float yawDeg = 0.0f;
            std::string flags;
            l >> d.hinge.x >> d.hinge.y >> d.hinge.z >> d.width >> d.height >> yawDeg >> flags;
            d.yaw = glm::radians(yawDeg);
            d.mirror = flags.find('m') != std::string::npos;
            d.glass = flags.find('v') != std::string::npos;
            glm::vec3 gc;
            if (l >> gc.r >> gc.g >> gc.b) { // cor do vidro e textura (arquivos novos)
                d.glassColor = gc;
                std::getline(l >> std::ws, d.texture);
                if (d.texture == "-") d.texture.clear();
            }
            extraDoors.push_back(d);
        } else if (k == "stretch") {
            std::string name;
            glm::vec3 s3(1.0f);
            l >> name >> s3.x >> s3.y >> s3.z;
            if (PropPlace* e = prop(name)) e->stretch = s3;
        } else if (k == "nodoor") { // cenário sem a porta principal
            hasDoor = false;
        } else if (k == "nospawns") { // cenário sem inimigos
            spawns.clear();
            spawnYaw.clear();
            spawnSeen = true;
        } else if (k == "stain") {
            if (!stainSeen) { stains.clear(); stainSeen = true; }
            StainPlace st;
            l >> st.pos.x >> st.pos.y >> st.pos.z >> st.normal.x >> st.normal.y >> st.normal.z >> st.size >> st.angle >>
                st.strength >> st.soft;
            std::getline(l >> std::ws, st.tex);
            if (st.tex == "-") st.tex.clear();
            stains.push_back(st);
        } else if (k == "light") {
            if (!lightSeen) { lights.clear(); lightSeen = true; }
            LightPlace li;
            float y = 0.0f, pi = -90.0f;
            l >> li.type >> li.pos.x >> li.pos.y >> li.pos.z >> y >> pi >> li.color.r >> li.color.g >> li.color.b >> li.intensity
                >> li.range >> li.inner >> li.outer;
            float sz;
            if (l >> sz) li.size = sz;
            li.yaw = glm::radians(y);
            li.pitch = glm::radians(pi);
            lights.push_back(li);
        } else if (k == "gun") {
            int i;
            glm::vec3 g;
            float y;
            l >> i >> g.x >> g.y >> g.z >> y;
            guns[i] = {g, glm::radians(y)};
        } else if (k == "spawn") {
            if (!spawnSeen) { spawns.clear(); spawnYaw.clear(); spawnSeen = true; } // o arquivo substitui a lista
            glm::vec3 p;
            float y;
            l >> p.x >> p.y >> p.z >> y;
            spawns.push_back(p);
            spawnYaw.push_back(glm::radians(y));
        } else if (k == "paint") {
            paintOn = true;
            paintLayers.clear();
            l >> paintOrigin.x >> paintOrigin.y >> paintSize >> paintRes;
        } else if (k == "layer") {
            PaintLayer pl;
            std::string rest;
            l >> pl.tile;
            std::getline(l >> std::ws, rest);
            // cor|normal|rugosidade|relevo|oclusão|dx (os arquivos antigos têm só cor|normal)
            std::vector<std::string> f;
            size_t a = 0;
            for (;;) {
                size_t bar = rest.find('|', a);
                f.push_back(rest.substr(a, bar == std::string::npos ? std::string::npos : bar - a));
                if (bar == std::string::npos) break;
                a = bar + 1;
            }
            f.resize(9);
            pl.albedo = f[0];
            pl.normal = f[1];
            pl.rough = f[2];
            pl.height = f[3];
            pl.ao = f[4];
            pl.normalDX = f[5] == "dx";
            pl.kind = f[6] == "agua" ? 1 : 0;
            pl.grass = f[7] == "grama";
            pl.depth = f[8].empty() ? 0.0f : (float)std::atof(f[8].c_str());
            paintLayers.push_back(pl);
        } else if (k == "terrain") {
            terrainOn = true;
            l >> terrainOrigin.x >> terrainOrigin.y >> terrainSize >> terrainN;
        } else if (k == "start") {
            float y;
            l >> start.x >> start.y >> start.z >> y;
            startYaw = glm::radians(y);
        } else continue;
        any = true;
    }
    return any;
}

bool LevelLayout::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    return parse(ss.str());
}

bool LevelLayout::save(const std::string& path) const {
    std::ofstream f(path);
    if (!f) return false;
    f << serialize();
    return (bool)f;
}

void defaultTestLayout(LevelLayout& L) {
    L = LevelLayout();
    PropPlace m1, m2;
    m1.name = "mesa1";
    m1.pos = {2.2f, 0.0f, 3.0f};
    m1.yaw = glm::radians(90.0f);
    m2 = m1;
    m2.name = "mesa2";
    m2.pos = {4.3f, 0.0f, 3.0f};
    L.props = {m1, m2};
    L.hasDoor = true;
    L.doorHinge = {-0.75f, 0.0f, 21.8f};
    L.doorWidth = 1.5f;
    L.doorHeight = 2.4f;
    L.spawns = {{-6.0f, 0.0f, 41.5f}, {6.5f, 0.0f, 44.5f}, {0.0f, 0.0f, 45.5f}, {-10.5f, 0.0f, 35.0f}, {10.5f, 0.0f, 37.0f}};
    for (const glm::vec3& p : L.spawns) L.spawnYaw.push_back(std::atan2(0.0f - p.x, 21.8f - p.z));
    L.start = glm::vec3(0.0f);
    L.startYaw = 0.0f;
}

Arena buildTestScene(World& w, LevelLayout& L, std::vector<Table>& tables) {
    w = World::makeTestMap();
    Arena a = Arena::build(w); // paredes, caixotes e porta da arena (a porta vem do layout)
    a.door.hinge = L.doorHinge;
    a.door.width = L.doorWidth;
    a.door.height = L.doorHeight;
    a.spawns = L.spawns;
    a.spawnYaw = L.spawnYaw;
    // caixas movidas no editor (paredes, blocos, caixotes)
    for (auto& [i, b] : L.boxes)
        if (i >= 0 && i < (int)w.boxes.size()) { w.boxes[i].min = b.first; w.boxes[i].max = b.second; }
    // mesas: giro em quartos de volta (a colisão delas é de caixas alinhadas)
    tables.clear();
    for (const PropPlace& p : L.props) {
        if (!p.asset.empty()) continue; // objeto posto pelo editor (desenhado pela AssetLibrary), não é mesa
        Table t;
        t.pos = p.pos;
        t.quarterTurns = (int)std::lround(p.yaw / 1.5707963f);
        t.addColliders(w);
        tables.push_back(t);
    }
    addExtraDoors(w, a, L);
    return a;
}

// ---------------------------------------------------------------- cenário oficial
namespace {
// Mapa montado no Blender (Novomapa.glb, em metros): corredor no meio (a esfera na origem marca
// onde o personagem começa), sala com móveis no fundo (z < -11,6) e sala à esquerda (x < -1,6),
// com a cidade em volta. O chão do arquivo fica 7 cm abaixo de zero: sobe para os pés apoiarem.
const char* kMapFile = "Novomapa.glb";
const glm::mat4 kMapPivot = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0737f, 0.0f));

// Caixa (lo, hi) do mundo levada para o espaço de um objeto (a caixa que contém os 8 cantos).
std::pair<glm::vec3, glm::vec3> toLocal(const glm::mat4& model, const glm::vec3& lo, const glm::vec3& hi) {
    glm::mat4 inv = glm::inverse(model);
    glm::vec3 a(1e9f), b(-1e9f);
    for (int i = 0; i < 8; ++i) {
        glm::vec3 c((i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z);
        glm::vec3 p = glm::vec3(inv * glm::vec4(c, 1.0f));
        a = glm::min(a, p);
        b = glm::max(b, p);
    }
    return {a, b};
}

// Vão de uma porta no mundo: atravessa a parede (0,25 m para cada lado da linha da folha).
// Em ângulo (fora de X/Z) é a caixa que contém a folha.
std::pair<glm::vec3, glm::vec3> doorCut(const DoorPlace& d) {
    const glm::vec3 a = d.hinge, b = d.end();
    const glm::vec3 n = glm::abs(glm::vec3(-d.axis().z, 0.0f, d.axis().x)) * 0.25f; // espessura, para os dois lados
    glm::vec3 lo = glm::min(a, b) - n, hi = glm::max(a, b) + n;
    lo.y = d.hinge.y - 0.05f;
    hi.y = d.hinge.y + d.height;
    return {lo, hi};
}

glm::mat4 propMatrix(const LevelLayout& L, const std::string& name, const glm::mat4& pivot = glm::mat4(1.0f)) {
    for (const PropPlace& p : L.props)
        if (p.name == name) return p.matrix(pivot);
    return glm::mat4(1.0f);
}
} // namespace

void defaultOfficialLayout(LevelLayout& L) {
    L = LevelLayout();
    PropPlace map;
    map.name = "mapa";
    L.props.push_back(map);
    L.hasDoor = true;
    // porta principal: do corredor para a sala do fundo
    L.doorHinge = {-0.45f, 0.0f, -11.63f};
    L.doorWidth = 0.9f;
    L.doorHeight = 2.1f;
    // porta lateral: do corredor para a sala da esquerda; porta do norte: do fim do corredor (atrás
    // do início) para a sala com piso xadrez, no vão da parede de tijolos (Novomapa.glb)
    auto door = [&](glm::vec3 hinge, float w, float h, float yawDeg, bool mirror) {
        DoorPlace d;
        d.hinge = hinge;
        d.width = w;
        d.height = h;
        d.yaw = glm::radians(yawDeg);
        d.mirror = mirror;
        L.extraDoors.push_back(d);
    };
    door({-1.62f, 0.0f, 0.05f}, 0.92f, 2.1f, -90.0f, false);
    door({-0.94f, 0.0f, 4.11f}, 1.03f, 2.09f, 0.0f, false);
    // entrada da casa (parede leste da sala do norte, para a cidade): porta dupla, uma folha de cada lado
    door({8.49f, 0.0f, 7.05f}, 1.435f, 2.5f, -90.0f, false);
    door({8.49f, 0.0f, 8.485f}, 1.435f, 2.5f, -90.0f, true);
    L.spawns = {{-1.5f, 0.0f, -14.5f}, {1.6f, 0.0f, -17.0f}};
    glm::vec3 doorC = L.doorHinge + glm::vec3(L.doorWidth * 0.5f, 0.0f, 0.0f);
    for (const glm::vec3& p : L.spawns) L.spawnYaw.push_back(std::atan2(doorC.x - p.x, doorC.z - p.z));
    L.start = glm::vec3(0.0f);
    L.startYaw = glm::pi<float>(); // olhando para o fundo do corredor (-z)
}

bool OfficialLevel::load(const std::string& root, const LevelLayout& L) {
    std::vector<std::pair<glm::vec3, glm::vec3>> cuts;
    const glm::mat4 mapM = propMatrix(L, "mapa", kMapPivot);
    for (const DoorPlace& d : L.allDoors()) {
        if (!d.axisAligned()) continue; // em ângulo: não recorta a parede (põe num vão que já existe)
        auto [lo, hi] = doorCut(d);
        cuts.push_back(toLocal(mapM, lo, hi));
    }
    if (loaded && cuts == cuts_) return true;
    if (loaded && map.recut(cuts, &mapTris)) { // porta mudou: só refaz os vãos (sem ler o arquivo)
        cuts_ = cuts;
        return true;
    }
    if (!std::filesystem::exists(root + "/" + kMapFile)) { // sem o arquivo do mapa: cenário vazio (só chão)
        if (!loaded) std::printf("[cenario] %s não existe: cenário oficial vazio\n", kMapFile);
        map.reset();
        mapTris.clear();
        cuts_ = cuts;
        loaded = true;
        return true;
    }
    CharacterModel::LoadOptions o;
    o.groundAtZero = false;
    o.maxTextureSize = 1024;
    o.skipMesh = "Icosphere"; // marcador do início, não aparece no jogo
    o.keepSource = true;
    o.chunkSize = 8.0f; // mapa grande: desenha só os blocos que aparecem
    o.triangles = &mapTris;
    o.cuts = cuts;
    map.reset();
    mapTris.clear();
    if (!map.load(root + "/" + kMapFile, nullptr, o)) return false;
    cuts_ = cuts;
    loaded = true;
    applyTextures(root, L);
    return true;
}

CharacterModel* OfficialLevel::mesh(const std::string& n) { return n == "mapa" ? &map : nullptr; }

void OfficialLevel::applyTextures(const std::string& root, const LevelLayout& L) {
    for (const PropPlace& p : L.props)
        if (CharacterModel* m = mesh(p.name))
            for (auto& [mat, path] : p.textures) m->setBaseTexture(mat, root + "/" + path);
}

Arena OfficialLevel::build(World& w, std::vector<RenderScene::Prop>& props, const LevelLayout& L) const {
    Arena a;
    w = World();
    const glm::mat4 mapM = propMatrix(L, "mapa", kMapPivot);
    for (const PropPlace& p : L.props)
        if (p.name == "mapa") props.push_back({&map, p.matrix(kMapPivot)});
    // entulho baixo (até 45 cm) não bloqueia: dá para pisar por cima
    addMeshColliders(w, mapTris, mapM, 0.15f, 0.45f);
    const glm::vec3 frame(0.22f, 0.17f, 0.13f);
    for (const DoorPlace& d : L.allDoors()) {
        auto [lo, hi] = doorCut(d);
        carveColliders(w, {lo.x, -1.0f, lo.z}, {hi.x, 5.0f, hi.z});
    }
    if (L.hasDoor) { // batente escuro em volta do vão da porta principal (as outras: addExtraDoors)
        const float x0 = L.doorHinge.x, x1 = x0 + L.doorWidth, z = L.doorHinge.z, y0 = L.doorHinge.y, top = y0 + L.doorHeight;
        auto box = [&](glm::vec3 mn, glm::vec3 mx) { w.boxes.push_back({mn, mx, frame}); };
        box({x0 - 0.06f, y0, z - 0.09f}, {x0, top + 0.06f, z + 0.09f});
        box({x1, y0, z - 0.09f}, {x1 + 0.06f, top + 0.06f, z + 0.09f});
        box({x0 - 0.06f, top, z - 0.09f}, {x1 + 0.06f, top + 0.06f, z + 0.09f});
    }
    a.door.hinge = L.doorHinge;
    a.door.width = L.doorWidth;
    a.door.height = L.doorHeight;
    a.door.thick = 0.05f;
    a.door.enabled = L.hasDoor;
    w.boxes.push_back({glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(0.0f), true, false}); // barra raios com a porta fechada
    a.door.collider = (int)w.boxes.size() - 1;
    addExtraDoors(w, a, L);
    // área de combate = a sala do fundo
    a.min = glm::vec3(-3.9f, 0.0f, -19.4f);
    a.max = glm::vec3(3.0f, 0.0f, -11.8f);
    a.spawns = L.spawns;
    a.spawnYaw = L.spawnYaw;
    return a;
}

// ---------------------------------------------------------------- objetos postos pelo editor
AssetLibrary::Entry* AssetLibrary::get(const std::string& rel) {
    auto it = cache_.find(rel);
    if (it == cache_.end()) {
        auto e = std::make_unique<Entry>();
        CharacterModel::LoadOptions o;
        o.groundAtZero = true; // apoiado no chão onde for posto
        o.maxTextureSize = 2048;
        o.triangles = &e->tris;
        std::string file = rel;
        if (size_t h = rel.rfind('#'); h != std::string::npos) { // "arquivo.glb#n": só a parte n, no lugar dela
            file = rel.substr(0, h);
            o.part = std::atoi(rel.c_str() + h + 1);
            o.groundAtZero = false;
        }
        e->ok = e->model.load(root + "/" + file, nullptr, o);
        if (!e->ok) std::fprintf(stderr, "[asset] falha ao abrir %s\n", rel.c_str());
        it = cache_.emplace(rel, std::move(e)).first;
    }
    return it->second->ok ? it->second.get() : nullptr;
}

void AssetLibrary::loadConfig() {
    std::ifstream f(root + "/assets.ini");
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream l(line);
        std::string k;
        int mode;
        if (!(l >> k) || k[0] == '#' || !(l >> mode)) continue;
        collision[k] = mode;
    }
}

bool AssetLibrary::saveConfig() const {
    std::ofstream f(root + "/assets.ini");
    if (!f) return false;
    f << "# Modelos: colisao 0 = atravessa, 1 = solido parado, 2 = fisica (cai e e empurrado)\n";
    for (auto& [k, m] : collision) f << k << " " << m << "\n";
    return (bool)f;
}

void AssetLibrary::stepDynamics(World& w, std::vector<RenderScene::Prop>& props, const std::vector<CharacterBody*>& pushers,
                                float dt) {
    if (dt <= 0.0f) return;
    for (Dynamic& d : dynamics) {
        glm::vec3 lo = d.lo + d.offset, hi = d.hi + d.offset;
        glm::vec3 c = (lo + hi) * 0.5f, half = (hi - lo) * 0.5f;
        // empurrão: quem encosta (círculo do corpo contra a caixa no chão) tira o objeto da frente
        for (CharacterBody* b : pushers) {
            if (b->pos.y > hi.y || b->pos.y + b->height < lo.y) continue;
            float px = std::clamp(b->pos.x, lo.x, hi.x), pz = std::clamp(b->pos.z, lo.z, hi.z);
            glm::vec2 dv(px - b->pos.x, pz - b->pos.z);
            float dist = glm::length(dv);
            if (dist >= b->radius) continue;
            glm::vec2 n = dist > 1e-4f ? dv / dist : glm::normalize(glm::vec2(c.x - b->pos.x, c.z - b->pos.z) + glm::vec2(1e-4f));
            float pen = b->radius - dist;
            d.offset.x += n.x * pen;
            d.offset.z += n.y * pen;
            float vn = glm::dot(glm::vec2(b->vel.x, b->vel.z), n);
            if (vn > 0.0f) { d.vel.x += n.x * vn * 0.6f; d.vel.z += n.y * vn * 0.6f; }
        }
        d.vel.y -= kGravity * dt;
        d.offset += d.vel * dt;
        lo = d.lo + d.offset;
        hi = d.hi + d.offset;
        c = (lo + hi) * 0.5f;
        // paredes e caixas sólidas: sai pelo lado de menor penetração (no plano)
        for (const ::Box& b : w.boxes) {
            if (!b.solid || b.max.y <= lo.y + 0.05f || b.min.y >= hi.y) continue;
            float ox = std::min(hi.x, b.max.x) - std::max(lo.x, b.min.x), oz = std::min(hi.z, b.max.z) - std::max(lo.z, b.min.z);
            if (ox <= 0.0f || oz <= 0.0f) continue;
            if (b.max.y - lo.y < 0.3f) continue; // baixo: o chão abaixo resolve
            if (ox < oz) { float s = c.x < (b.min.x + b.max.x) * 0.5f ? -ox : ox; d.offset.x += s; d.vel.x = 0.0f; }
            else { float s = c.z < (b.min.z + b.max.z) * 0.5f ? -oz : oz; d.offset.z += s; d.vel.z = 0.0f; }
            lo = d.lo + d.offset;
            hi = d.hi + d.offset;
        }
        c = (lo + hi) * 0.5f;
        float ground = w.groundHeightAt(c.x, c.z, lo.y + 0.3f);
        if (lo.y <= ground) {
            d.offset.y += ground - lo.y;
            d.vel.y = 0.0f;
            float k = std::exp(-5.0f * dt); // atrito no chão
            d.vel.x *= k;
            d.vel.z *= k;
        }
        (void)half;
        props[d.prop].model = glm::translate(glm::mat4(1.0f), d.offset) * d.base;
    }
}

void addExtraDoors(World& w, Arena& a, const LevelLayout& L) {
    // ponta de uma porta encostada na de outra (porta dupla): sem poste do batente no meio
    auto sharedEdge = [&](const DoorPlace& d, const glm::vec3& p) {
        for (const DoorPlace& o : L.extraDoors) {
            if (&o == &d) continue;
            if (glm::length(o.hinge - p) < 0.02f || glm::length(o.end() - p) < 0.02f) return true;
        }
        return false;
    };
    for (const DoorPlace& d : L.extraDoors) {
        Door e;
        e.hinge = d.hinge;
        e.width = d.width;
        e.height = d.height;
        e.thick = d.glass ? 0.04f : 0.05f;
        e.base = d.yaw;
        e.glass = d.glass;
        e.glassColor = d.glassColor;
        e.texture = d.texture;
        e.framed = true;
        bool post0 = !sharedEdge(d, d.hinge), post1 = !sharedEdge(d, d.end());
        if (d.mirror) { // dobradiça na outra ponta, folha virada para trás
            e.hinge = d.end();
            e.base += glm::pi<float>();
            std::swap(post0, post1);
        }
        e.post0 = post0;
        e.post1 = post1;
        // batente: o desenho fica na porta (gira junto); aqui só a colisão dos postes
        for (int k = 0; k < 2; ++k) {
            if (!(k ? post1 : post0)) continue;
            const glm::vec3 p = e.hinge + glm::vec3(std::cos(e.base), 0.0f, -std::sin(e.base)) * (k ? e.width + 0.03f : -0.03f);
            Box b{p - glm::vec3(0.07f, 0.0f, 0.07f), p + glm::vec3(0.07f, d.height + 0.06f, 0.07f) + glm::vec3(0, d.hinge.y, 0),
                  glm::vec3(0.5f)};
            b.min.y = d.hinge.y;
            b.hidden = true;
            w.boxes.push_back(b);
        }
        w.boxes.push_back({glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(0.0f), true, false});
        e.collider = (int)w.boxes.size() - 1;
        a.extraDoors.push_back(e);
    }
}

void AssetLibrary::build(World& w, std::vector<RenderScene::Prop>& props, const LevelLayout& L) {
    dynamics.clear();
    for (const PropPlace& p : L.props) {
        if (p.asset.empty()) continue;
        Entry* e = get(p.asset);
        if (!e) {
            props.push_back({nullptr, p.matrix()}); // mantém a ordem (não desenha)
            continue;
        }
        for (auto& [mat, path] : p.textures) e->model.setBaseTexture(mat, root + "/" + path);
        glm::mat4 M = p.matrix();
        props.push_back({&e->model, M});
        const int mode = collisionOf(p.asset);
        if (mode == Solid) addMeshColliders(w, e->tris, M, 0.12f, 0.35f);
        else if (mode == Physics) {
            Dynamic d;
            d.prop = (int)props.size() - 1;
            d.base = M;
            glm::vec3 a(1e9f), b(-1e9f);
            for (int i = 0; i < 8; ++i) {
                glm::vec3 cn((i & 1) ? e->model.boundsMax.x : e->model.boundsMin.x, (i & 2) ? e->model.boundsMax.y : e->model.boundsMin.y,
                             (i & 4) ? e->model.boundsMax.z : e->model.boundsMin.z);
                glm::vec3 q = glm::vec3(M * glm::vec4(cn, 1.0f));
                a = glm::min(a, q);
                b = glm::max(b, q);
            }
            d.lo = a;
            d.hi = b;
            dynamics.push_back(d);
        }
    }
}
