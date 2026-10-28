#pragma once
#include <string>

// Compila a versão final do jogo (sem editor de armas e sem botão de compilar) em segundo
// plano: cmake -DGA_EDITOR=OFF em <raiz>/build-final. O executável fica em build-final/ga.
namespace builder {

enum class State { Idle, Running, Ok, Failed };

void start(const std::string& root);
State state();
std::string log();           // saída do cmake até agora
std::string outputPath();    // onde fica o executável

} // namespace builder
