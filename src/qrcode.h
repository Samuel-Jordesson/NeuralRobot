#pragma once
#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

// QR code simples (modo byte, correção L, versões 1 a 5: até 106 caracteres) para mostrar o
// endereço da página da câmera do celular.
struct QrCode {
    int size = 0;           // módulos por lado
    std::vector<bool> dark; // size*size, linha a linha
    bool encode(const std::string& text);
    bool at(int x, int y) const { return dark[y * size + x]; }
};
