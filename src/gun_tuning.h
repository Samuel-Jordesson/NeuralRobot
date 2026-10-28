#pragma once
#include "guns.h"

#include <string>

// Ajustes finos de onde cada arma fica em cada modo, feitos no editor de armas (menu > Editar arma)
// e salvos em weapons.ini. Na versão final compilada o arquivo vai embutido no executável.
//
// Cada ajuste move/gira a arma em volta da empunhadura, nos eixos da própria arma.
struct GunOffset {
    glm::vec3 move{0.0f}; // cm: frente (cano), cima, direita
    glm::vec3 turn{0.0f}; // graus: rolar (no eixo do cano), virar (esquerda/direita), inclinar (cano sobe)
    bool isZero() const { return move == glm::vec3(0.0f) && turn == glm::vec3(0.0f); }
};

struct GunTuning {
    GunOffset aim;               // mirando
    GunOffset carry;             // segurando sem mirar
    GunOffset body[kSlotCount];  // guardada: 0 = cintura, 1 = costas baixo, 2 = costas alto
};

GunTuning& gunTuning(const GunDef& d);
// Matriz da arma (espaço canônico -> qualquer) com o ajuste aplicado.
glm::mat4 applyGunOffset(const glm::mat4& gun, const GunDef& d, const GunOffset& o);

// weapons.ini: no editor lê/grava o arquivo; na versão final usa a cópia embutida.
void loadGunTuning(const std::string& path);
bool saveGunTuning(const std::string& path);
