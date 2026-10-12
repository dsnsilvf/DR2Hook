#include "app/pad.hpp"

#include <algorithm>
#include <cmath>

namespace dr2::app {

namespace {

float axis(Sint16 v, float dead = 0.22f) {
    const float f = std::clamp(static_cast<float>(v) / 32767.0f, -1.0f, 1.0f);
    if (std::abs(f) < dead) return 0.0f;
    return (f - std::copysign(dead, f)) / (1.0f - dead);
}

float pick(float a, float b) { return std::abs(a) >= std::abs(b) ? a : b; }

}  // namespace

Pads::~Pads() {
    for (SDL_Gamepad* p : pads_) SDL_CloseGamepad(p);
    for (SDL_Joystick* j : joys_) SDL_CloseJoystick(j);
}

void Pads::open_all() {
    int n = 0;
    if (SDL_JoystickID* ids = SDL_GetJoysticks(&n)) {
        for (int i = 0; i < n; ++i) add(ids[i]);
        SDL_free(ids);
    }
}

void Pads::add(SDL_JoystickID id) {
    for (SDL_Gamepad* p : pads_)
        if (SDL_GetGamepadID(p) == id) return;
    for (SDL_Joystick* j : joys_)
        if (SDL_GetJoystickID(j) == id) return;
    if (SDL_IsGamepad(id)) {
        if (SDL_Gamepad* p = SDL_OpenGamepad(id)) pads_.push_back(p);
    } else if (SDL_Joystick* j = SDL_OpenJoystick(id)) {
        // sem eixo nem hat não é volante (fones e teclados com botões de mídia aparecem como joystick)
        if (SDL_GetNumJoystickAxes(j) > 0 || SDL_GetNumJoystickHats(j) > 0) joys_.push_back(j);
        else SDL_CloseJoystick(j);
    }
}

void Pads::remove(SDL_JoystickID id) {
    std::erase_if(pads_, [&](SDL_Gamepad* p) {
        if (SDL_GetGamepadID(p) != id) return false;
        SDL_CloseGamepad(p);
        return true;
    });
    std::erase_if(joys_, [&](SDL_Joystick* j) {
        if (SDL_GetJoystickID(j) != id) return false;
        SDL_CloseJoystick(j);
        return true;
    });
}

void Pads::event(const SDL_Event& e) {
    if (e.type == SDL_EVENT_JOYSTICK_ADDED) add(e.jdevice.which);
    else if (e.type == SDL_EVENT_JOYSTICK_REMOVED) remove(e.jdevice.which);
}

std::string Pads::name() const {
    if (!pads_.empty()) if (const char* n = SDL_GetGamepadName(pads_.front())) return n;
    if (!joys_.empty()) if (const char* n = SDL_GetJoystickName(joys_.front())) return n;
    return {};
}

void Pads::poll() {
    PadState s;
    s.connected = !pads_.empty() || !joys_.empty();
    for (SDL_Gamepad* p : pads_) {
        auto btn = [&](SDL_GamepadButton b) { return SDL_GetGamepadButton(p, b); };
        s.lx = pick(s.lx, axis(SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFTX)));
        s.ly = pick(s.ly, axis(SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFTY)));
        s.rx = pick(s.rx, axis(SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_RIGHTX)));
        s.ry = pick(s.ry, axis(SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_RIGHTY)));
        s.lt = std::max(s.lt, axis(SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFT_TRIGGER), 0.05f));
        s.rt = std::max(s.rt, axis(SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER), 0.05f));
        s.a |= btn(SDL_GAMEPAD_BUTTON_SOUTH);
        s.b |= btn(SDL_GAMEPAD_BUTTON_EAST);
        s.x |= btn(SDL_GAMEPAD_BUTTON_WEST);
        s.y |= btn(SDL_GAMEPAD_BUTTON_NORTH);
        s.back |= btn(SDL_GAMEPAD_BUTTON_BACK);
        s.start |= btn(SDL_GAMEPAD_BUTTON_START);
        s.up |= btn(SDL_GAMEPAD_BUTTON_DPAD_UP);
        s.down |= btn(SDL_GAMEPAD_BUTTON_DPAD_DOWN);
        s.left |= btn(SDL_GAMEPAD_BUTTON_DPAD_LEFT);
        s.right |= btn(SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
        s.l1 |= btn(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
        s.r1 |= btn(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    }
    for (SDL_Joystick* j : joys_) {
        const int nb = SDL_GetNumJoystickButtons(j);
        auto btn = [&](int b) { return b < nb && SDL_GetJoystickButton(j, b); };
        if (SDL_GetNumJoystickHats(j) > 0) {
            const Uint8 h = SDL_GetJoystickHat(j, 0);
            s.up |= (h & SDL_HAT_UP) != 0;
            s.down |= (h & SDL_HAT_DOWN) != 0;
            s.left |= (h & SDL_HAT_LEFT) != 0;
            s.right |= (h & SDL_HAT_RIGHT) != 0;
        }
        if (SDL_GetNumJoystickAxes(j) > 0) s.rx = pick(s.rx, axis(SDL_GetJoystickAxis(j, 0), 0.08f));
        s.a |= btn(0);
        s.b |= btn(1);
        s.x |= btn(2);
        s.y |= btn(3);
        s.back |= btn(6) || btn(8);
        s.start |= btn(7) || btn(9);
    }
    pressed_ = PadState{};
    pressed_.a = s.a && !prev_.a;
    pressed_.b = s.b && !prev_.b;
    pressed_.x = s.x && !prev_.x;
    pressed_.y = s.y && !prev_.y;
    pressed_.back = s.back && !prev_.back;
    pressed_.start = s.start && !prev_.start;
    pressed_.up = s.up && !prev_.up;
    pressed_.down = s.down && !prev_.down;
    pressed_.left = s.left && !prev_.left;
    pressed_.right = s.right && !prev_.right;
    pressed_.l1 = s.l1 && !prev_.l1;
    pressed_.r1 = s.r1 && !prev_.r1;
    prev_ = s;
    now_ = s;
}

}  // namespace dr2::app
