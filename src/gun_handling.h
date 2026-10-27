#pragma once
#include "aim_ik.h"

// Animações de manuseio das armas, feitas com IK por cima da animação do corpo:
//  - pegar da mesa: anda até a borda, inclina, a mão desce, fecha na empunhadura e traz a arma;
//  - guardar: a mão leva a arma até o lugar dela no corpo (pistola na cintura, fuzil nas costas);
//  - sacar: a mão vai até a arma no corpo, puxa e volta à pose de segurar (fuzil: a mão
//    esquerda vem pegar o guarda-mão no final).
class GunHandling {
public:
    enum class Action { None, PickUp, Stow, Draw };

    Action action() const { return action_; }
    bool busy() const { return action_ != Action::None; }
    int item() const { return item_; } // arma envolvida no gesto
    int slot() const { return slot_; } // lugar no corpo (guardar/sacar)
    // Pegando da mesa o personagem não anda sozinho; antes do gesto ele caminha até a borda.
    bool approaching() const { return action_ == Action::PickUp && !started_; }
    bool locksMovement() const { return action_ == Action::PickUp; }

    void startPickUp(int item) { start(Action::PickUp, item, -1); }
    void startStow(int item, int slot) { start(Action::Stow, item, slot); }
    void startDraw(int item, int slot) { start(Action::Draw, item, slot); }
    void begin() { started_ = true; } // chegou na borda da mesa: começa o gesto
    void cancel() { action_ = Action::None; }

    struct Events {
        bool grabbed = false; // arma passou para a mão neste quadro
        bool stowed = false;  // arma foi para o corpo neste quadro
    };
    Events update(float dt);

    // Alvos da mão (espaço do modelo) e pesos para este quadro.
    // tableGun = onde a arma estava deitada (pegar), no espaço do modelo.
    HandReach reach(const Skeleton& sk, const glm::mat4 globals[BONE_COUNT], const GunDef& def,
                    const glm::mat4& tableGun) const;
    // Fechamento dos dedos da mão direita (abre antes de pegar).
    float fingerCurl(bool gunInHand) const;

    float approachTime = 0.0f;

private:
    void start(Action a, int item, int slot);
    Action action_ = Action::None;
    bool started_ = false;
    int item_ = -1, slot_ = -1;
    float t_ = 0.0f;
};
