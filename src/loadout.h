#pragma once
#include "guns.h"

#include <vector>

// Armas do mapa e o que o personagem carrega: até 3, cada uma com seu lugar no corpo
// (pistola na cintura = tecla 1, fuzis nas costas = teclas 2 e 3).
struct Loadout {
    std::vector<GunItem> items;
    int hand = -1;                              // arma na mão
    int slots[kSlotCount] = {-1, -1, -1};       // arma dona de cada lugar (na mão ou guardada)

    // Lugar livre para essa arma no corpo (-1 se não cabe).
    int freeSlotFor(const GunDef& d) const;
    // Arma que dá para pegar daqui (a mais próxima, com lugar livre no corpo); -1 se nenhuma.
    int pickable(const glm::vec3& charPos) const;
    // Mudanças de lugar (chamadas nos eventos das animações).
    void toHand(int item);  // reserva o lugar no corpo se ainda não tem
    void toBody(int item);
    void drop(int item, const glm::vec3& pos, float yaw);
    void equipDirect(int item, bool inHand); // modo foto / testes: equipa sem animação
};
