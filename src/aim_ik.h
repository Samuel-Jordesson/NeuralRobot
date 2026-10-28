#pragma once
#include "guns.h"
#include "skeleton.h"

// Poses das armas feitas com IK por cima da animação do corpo: segurar, mirar, apoiar com a
// mão esquerda, guardar no corpo (cintura/costas) e levar a mão até a arma onde ela estiver.
// Todas as matrizes de arma estão no espaço canônico da GunDef (cano -X, topo +Y).

// Interpola duas matrizes rígidas (posição + rotação; escala uniforme).
glm::mat4 blendRigid(const glm::mat4& a, const glm::mat4& b, float t);

// Arma na mão direita a partir da matriz global da mão, e o inverso (mão que segura 'gun').
glm::mat4 gunInHand(const glm::mat4& handGlobal, const GunDef& d);
glm::mat4 handForGun(const glm::mat4& gun, const GunDef& d);

// Arma guardada no corpo: slot 0 = cintura direita (cano para baixo), 1 = costas com a
// empunhadura embaixo à direita, 2 = costas com a empunhadura em cima à direita (cruzadas em X).
// Com o ajuste do editor (gun_tuning); slotBase = sem ajuste.
glm::mat4 slotMatrix(const Skeleton& sk, const glm::mat4 globals[BONE_COUNT], int slot, const GunDef& d);
glm::mat4 slotBase(const Skeleton& sk, const glm::mat4 globals[BONE_COUNT], int slot, const GunDef& d);
// Fuzil "em pé" na frente do ombro direito (passagem entre as costas e a frente ao sacar/guardar).
glm::mat4 rifleShoulderMatrix(const Skeleton& sk, const glm::mat4 globals[BONE_COUNT], int slot, const GunDef& d);

// ---- pistola (uma mão; as duas só na mira)
// Mão direita só segurando a arma (sem mirar): corrige o pulso para o cano apontar para frente.
void applyCarryGrip(const Skeleton& sk, Pose& pose, const GunDef& d, float weight);
// target: ponto mirado no espaço do modelo. recoil: 0..1 (coice do tiro).
void applyAimPose(const Skeleton& sk, Pose& pose, const GunDef& d, const glm::vec3& target, float weight, float recoil);

// ---- fuzil (duas mãos)
// Tronco e cabeça acompanham a mira (o fuzil vira o tronco de lado, ombro esquerdo à frente).
void applyRifleTorso(const Skeleton& sk, Pose& pose, const glm::vec3& target, float weight);
// Onde o fuzil fica: entre "pronto baixo" (carregando, cano para frente e para baixo) e mira
// (coronha no ombro, olho na linha de mira), conforme aimW.
glm::mat4 rifleHold(const Skeleton& sk, const Pose& pose, const GunDef& d, const glm::vec3& target, float aimW,
                    float recoil);
// Braço direito por IK até a matriz da mão.
void applyRightHand(const Skeleton& sk, Pose& pose, const glm::mat4& hand, float weight);
// Mão esquerda no guarda-mão da arma que está na mão direita.
void applySupportHand(const Skeleton& sk, Pose& pose, const GunDef& d, float weight);

// Mão direita indo até um alvo (pegar/guardar/sacar). O tronco inclina e os joelhos dobram
// para alcançar coisas baixas, sem tirar os pés do lugar; a cabeça olha para onde a mão vai.
struct HandReach {
    glm::mat4 hand{1.0f};    // matriz global desejada da mão (espaço do modelo)
    float arm = 0.0f;        // peso do braço
    glm::vec3 pole{-0.45f, -0.5f, -0.35f}; // para onde o cotovelo aponta (relativo ao ombro)
    float lean = 0.0f;       // inclinação do tronco para frente (rad)
    float crouch = 0.0f;     // quanto o quadril desce (m)
    glm::vec3 lookAt{0.0f};  // ponto que a cabeça olha
    float look = 0.0f;
    float support = 0.0f;    // peso da mão esquerda no guarda-mão (fuzil)
};
void applyHandReach(const Skeleton& sk, Pose& pose, const HandReach& r);

// Mão espalmada numa superfície (ex.: empurrando a porta), dedos para cima.
// point/normal no espaço do modelo; a normal aponta da superfície para o personagem.
void applyPalmOnSurface(const Skeleton& sk, Pose& pose, bool right, const glm::vec3& point, const glm::vec3& normal,
                        float weight);
// Leva a mão (direita ou esquerda) até a matriz 'hand' (espaço do modelo) por IK, com o peso.
void applyHandTarget(const Skeleton& sk, Pose& pose, bool right, const glm::mat4& hand, float weight);
// Agachado: quadril desce 'amount' metros com os pés no lugar; 'lean' inclina o tronco.
void applyCrouch(const Skeleton& sk, Pose& pose, float amount, float lean);
// Tronco de lado, ombro esquerdo na frente (empurrando com o ombro), com o peso 0..1.
void applyShoulderPush(Pose& pose, float weight);
