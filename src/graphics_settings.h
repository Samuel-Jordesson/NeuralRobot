#pragma once
#include <string>

// Tudo que dá para mexer no menu de gráficos (Esc). Salvo em settings.ini.
struct GraphicsSettings {
    enum Preset { Low, Medium, High, Ultra, Custom };
    int preset = High;

    // tela
    float renderScale = 1.0f; // resolução interna (0.5 a 1.0 da janela)
    bool vsync = true;
    float fov = 60.0f;
    int msaa = 4;             // 1, 2, 4 ou 8 amostras
    bool fxaa = true;
    bool showFps = true;
    bool immortal = true;
    int scene = 0;            // só na versão de desenvolvimento: cenário (0 = teste/arena, 1 = oficial)     // só na versão de desenvolvimento: o jogador não morre

    // sombras
    int shadowRes = 2048;     // por cascata
    int shadowCascades = 3;   // 1 a 4
    float shadowDistance = 90.0f;
    float shadowSoftness = 1.0f;

    // iluminação
    bool ssao = true;
    float ssaoRadius = 0.6f;
    float ssaoIntensity = 1.0f;
    bool volumetric = true;   // luz volumétrica (raios de sol na névoa)
    int volumetricSteps = 24;
    float volumetricIntensity = 1.0f;

    // atmosfera
    float timeOfDay = 16.0f;  // 5.5 a 19.0
    bool dayCycle = false;
    float dayCycleSpeed = 1.0f; // horas do jogo por minuto real
    float sunAzimuth = 25.0f;   // graus, gira o caminho do sol
    float fogDensity = 1.0f;
    bool clouds = true;
    float cloudCoverage = 0.45f;
    bool cloudShadows = true;

    // pós-processamento
    bool bloom = true;
    float bloomIntensity = 1.0f;
    bool autoExposure = true;
    float exposure = 0.0f;    // compensação em EV
    float contrast = 1.05f;
    float saturation = 1.05f;
    float temperature = 0.0f; // -1 frio .. +1 quente
    float vignette = 0.35f;
    float filmGrain = 0.12f;
    float chromaticAberration = 0.15f;

    // Aplica um preset de qualidade (não mexe nas opções artísticas).
    void applyPreset(int p);
    void resetDefaults();

    // Muda uma opção pelo nome (mesmo nome do arquivo). Usado pelo modo de captura.
    bool setValue(const std::string& key, const std::string& value);

    bool load(const std::string& path);
    bool save(const std::string& path) const;
};
