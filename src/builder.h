#pragma once
#include <string>

// Compila a versão final do jogo (sem editor) em segundo plano: cmake -DGA_EDITOR=OFF em
// <raiz>/build-final, e copia o jogo pronto (executável + shaders e modelos) para <raiz>/Jogo-final.
namespace builder {

enum class State { Idle, Running, Ok, Failed };

void start(const std::string& root);
State state();
std::string log();           // saída do cmake até agora
std::string outputPath();    // onde fica o executável

} // namespace builder
