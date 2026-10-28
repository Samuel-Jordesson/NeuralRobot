#include "graphics_settings.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

void GraphicsSettings::applyPreset(int p) {
    preset = p;
    switch (p) {
    case Low:
        msaa = 1; fxaa = true;
        shadowRes = 1024; shadowCascades = 2; shadowDistance = 50.0f;
        ssao = false; volumetric = false; volumetricSteps = 16;
        cloudShadows = false; bloom = true;
        break;
    case Medium:
        msaa = 2; fxaa = true;
        shadowRes = 2048; shadowCascades = 2; shadowDistance = 70.0f;
        ssao = true; volumetric = false; volumetricSteps = 16;
        cloudShadows = true; bloom = true;
        break;
    case High:
        msaa = 4; fxaa = true;
        shadowRes = 2048; shadowCascades = 3; shadowDistance = 90.0f;
        ssao = true; volumetric = true; volumetricSteps = 24;
        cloudShadows = true; bloom = true;
        break;
    case Ultra:
        msaa = 4; fxaa = true;
        shadowRes = 4096; shadowCascades = 4; shadowDistance = 150.0f;
        ssao = true; volumetric = true; volumetricSteps = 40;
        cloudShadows = true; bloom = true;
        break;
    default: break;
    }
}

void GraphicsSettings::resetDefaults() { *this = GraphicsSettings(); }

namespace {
// Lista única de campos: nome no arquivo -> ponteiro.
template <class F>
void forEachField(GraphicsSettings& s, F&& f) {
    f("preset", &s.preset);
    f("renderScale", &s.renderScale);
    f("vsync", &s.vsync);
    f("fov", &s.fov);
    f("msaa", &s.msaa);
    f("fxaa", &s.fxaa);
    f("showFps", &s.showFps);
    f("immortal", &s.immortal);
    f("scene", &s.scene);
    f("shadowRes", &s.shadowRes);
    f("shadowCascades", &s.shadowCascades);
    f("shadowDistance", &s.shadowDistance);
    f("shadowSoftness", &s.shadowSoftness);
    f("ssao", &s.ssao);
    f("ssaoRadius", &s.ssaoRadius);
    f("ssaoIntensity", &s.ssaoIntensity);
    f("volumetric", &s.volumetric);
    f("volumetricSteps", &s.volumetricSteps);
    f("volumetricIntensity", &s.volumetricIntensity);
    f("timeOfDay", &s.timeOfDay);
    f("dayCycle", &s.dayCycle);
    f("dayCycleSpeed", &s.dayCycleSpeed);
    f("sunAzimuth", &s.sunAzimuth);
    f("fogDensity", &s.fogDensity);
    f("clouds", &s.clouds);
    f("cloudCoverage", &s.cloudCoverage);
    f("cloudShadows", &s.cloudShadows);
    f("bloom", &s.bloom);
    f("bloomIntensity", &s.bloomIntensity);
    f("autoExposure", &s.autoExposure);
    f("exposure", &s.exposure);
    f("contrast", &s.contrast);
    f("saturation", &s.saturation);
    f("temperature", &s.temperature);
    f("vignette", &s.vignette);
    f("filmGrain", &s.filmGrain);
    f("chromaticAberration", &s.chromaticAberration);
}

void writeValue(std::ostream& o, const int* v) { o << *v; }
void writeValue(std::ostream& o, const float* v) { o << *v; }
void writeValue(std::ostream& o, const bool* v) { o << (*v ? 1 : 0); }
void readValue(const std::string& t, int* v) { *v = std::atoi(t.c_str()); }
void readValue(const std::string& t, float* v) { *v = (float)std::atof(t.c_str()); }
void readValue(const std::string& t, bool* v) { *v = std::atoi(t.c_str()) != 0; }
} // namespace

bool GraphicsSettings::setValue(const std::string& key, const std::string& value) {
    bool found = false;
    forEachField(*this, [&](const char* name, auto* ptr) {
        if (key == name) {
            readValue(value, ptr);
            found = true;
        }
    });
    return found;
}

bool GraphicsSettings::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos || line[0] == '#') continue;
        setValue(line.substr(0, eq), line.substr(eq + 1));
    }
    // valores fora da faixa (arquivo editado à mão)
    renderScale = std::clamp(renderScale, 0.5f, 1.0f);
    msaa = msaa >= 8 ? 8 : msaa >= 4 ? 4 : msaa >= 2 ? 2 : 1;
    shadowRes = shadowRes >= 4096 ? 4096 : shadowRes >= 2048 ? 2048 : 1024;
    shadowCascades = std::clamp(shadowCascades, 1, 4);
    volumetricSteps = std::clamp(volumetricSteps, 8, 64);
    timeOfDay = std::clamp(timeOfDay, 5.5f, 19.0f);
    preset = std::clamp(preset, 0, (int)Custom);
    return true;
}

bool GraphicsSettings::save(const std::string& path) const {
    std::ofstream out(path);
    if (!out) return false;
    out << "# configuracoes graficas (menu Esc)\n";
    forEachField(const_cast<GraphicsSettings&>(*this), [&](const char* name, auto* ptr) {
        out << name << "=";
        writeValue(out, ptr);
        out << "\n";
    });
    return true;
}
