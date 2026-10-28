#!/usr/bin/env bash
# Compila (só o que mudou) e roda o jogo. Argumentos extras vão para o ./build/ga
set -e
cd "$(dirname "$0")"
[ -f build/CMakeCache.txt ] || cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
exec ./build/ga "$@"
