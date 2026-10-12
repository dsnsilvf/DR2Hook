// Controle e volante: um estado só por quadro, juntando todos os aparelhos abertos.
// Controles que o SDL conhece (SDL_Gamepad) dão os nomes padrão; volantes e outros joysticks sem mapa entram pelo
// joystick cru: o hat vira o D-pad, os botões 0–3 viram A, B, X e Y, 6/8 o Back e 7/9 o Start, e o eixo 0 (o giro
// do volante) vira o analógico direito na horizontal (gira a câmera).
#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <vector>

namespace dr2::app {

struct PadState {
    float lx = 0, ly = 0, rx = 0, ry = 0;  // analógicos, −1..1 (y para baixo), já com zona morta
    float lt = 0, rt = 0;                  // gatilhos, 0..1
    bool a = false, b = false, x = false, y = false, back = false, start = false;
    bool up = false, down = false, left = false, right = false, l1 = false, r1 = false;
    bool connected = false;
};

class Pads {
public:
    // Abre os aparelhos já ligados (o SDL precisa do SDL_INIT_GAMEPAD); os ligados depois entram pelos eventos.
    void open_all();
    void event(const SDL_Event& e);
    // Estado atual e o que foi apertado desde o último poll (borda de subida).
    void poll();
    const PadState& now() const { return now_; }
    const PadState& pressed() const { return pressed_; }
    // Nome do primeiro aparelho (para a barra de status e as Preferências); vazio sem nenhum.
    std::string name() const;
    bool has_raw_joystick() const { return !joys_.empty(); }
    ~Pads();

private:
    void add(SDL_JoystickID id);
    void remove(SDL_JoystickID id);
    std::vector<SDL_Gamepad*> pads_;
    std::vector<SDL_Joystick*> joys_;  // sem mapa de gamepad (volantes)
    PadState now_, prev_, pressed_;
};

}  // namespace dr2::app
