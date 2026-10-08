// Jogo ao vivo: o core do mod (src/core/live_link.cpp) manda, a até 60 Hz, um datagrama UDP para
// 127.0.0.1:47820 com o carro do jogador e a câmera da especial em metros do mundo do jogo. Aqui: ler o
// pacote, escutar a porta sem bloquear e levar do mundo do jogo para o da pista aberta
// (game_transform.json, gravado pelo ring_deploy.py: o mesmo giro e deslocamento do porte, ao contrário).
#pragma once

#include "core/track.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace dr2 {

struct LiveSample {
    std::uint32_t seq = 0;
    bool car = false, view = false, paused = false;
    Vec3 car_pos{};
    float car_rot[9] = {};  // linhas da rotação como o jogo guarda (+0x2f0/+0x300/+0x310)
    Vec3 car_vel{};
    Vec3 eye{}, forward{}, up{};
    std::uint32_t top = 0;  // estado no topo da pilha do jogo (rva)
    float speed_kmh = 0;
};

constexpr std::uint16_t kLivePort = 47820;
constexpr std::size_t kLivePacketSize = 120;

// false se não é um pacote "DR2L" versão 1 de 120 bytes.
bool parse_live(const void* data, std::size_t size, LiveSample& out);

// Do jogo para a pista: p_pista = giro_y(p_jogo - offset, -yaw). Sem arquivo, identidade.
struct GameTransform {
    float yaw_deg = 0;
    Vec3 offset{};
    bool loaded = false;
    Vec3 point_to_track(const Vec3& p) const;
    Vec3 dir_to_track(const Vec3& d) const;
};
// Lê <dir>/game_transform.json ({"yaw_deg": .., "offset": [x, y, z]}); sem o arquivo devolve a identidade.
GameTransform load_game_transform(const std::string& track_dir);

// Escuta 127.0.0.1:kLivePort (ou a porta em DR2_LIVE_PORT) sem bloquear e guarda o pacote mais novo.
class LiveReceiver {
public:
    LiveReceiver() = default;
    ~LiveReceiver();
    LiveReceiver(const LiveReceiver&) = delete;
    LiveReceiver& operator=(const LiveReceiver&) = delete;

    // Abre a porta (uma vez). false e `error` se outra janela do editor já a usa.
    bool open(std::string& error);
    bool is_open() const { return fd_ >= 0; }
    // Lê tudo o que chegou; true se chegou pacote novo.
    bool poll();
    const LiveSample& last() const { return last_; }
    // Segundos desde o último pacote (infinito se nunca chegou).
    double age() const;
    std::uint64_t received() const { return received_; }

private:
    int fd_ = -1;
    LiveSample last_;
    double last_time_ = -1;
    std::uint64_t received_ = 0;
};

}  // namespace dr2
