#pragma once
#include "skeleton.h"

#include <string>

// Captura de movimento de um vídeo: lê os pontos que tools/mocap/video_mocap.py tirou do vídeo
// (corpo, mãos e cabeça) e grava uma animação do editor (anims/<nome>.anim) com todos os ossos,
// dedos inclusive. seated = pernas de quem está sentado (o vídeo não mostra as pernas).
bool bakeVideoMocap(const Skeleton& sk, const std::string& in, const std::string& outAnim, bool seated, std::string& msg);
