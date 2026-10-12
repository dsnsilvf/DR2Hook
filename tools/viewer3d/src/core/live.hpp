// Jogo ao vivo: o core do mod (src/core/live_link.cpp) manda, a até 60 Hz, um datagrama UDP para
// 127.0.0.1:47820 com o carro do jogador e a câmera da especial em metros do mundo do jogo. Aqui: ler o
// pacote, escutar a porta sem bloquear e levar do mundo do jogo para o da pista aberta
// (game_transform.json, gravado pelo ring_deploy.py: o mesmo giro e deslocamento do porte, ao contrário).
// Câmeras juntas: o editor manda a sua câmera e o pedido de câmera livre para 127.0.0.1:47821 (LiveSender).
#pragma once

#include "core/track.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace dr2 {

struct LiveSample {
    std::uint32_t seq = 0;
    bool car = false, view = false, paused = false;
    bool free = false;                // câmera livre ligada no jogo (v2)
    std::uint32_t free_moves = 0;     // passos da câmera livre dados no jogo: sobe quando mexem nela lá (v2)
    std::uint32_t editor_pose = 0;    // última pose do editor que o jogo pôs (v2)
    Vec3 car_pos{};
    float car_rot[9] = {};  // linhas da rotação como o jogo guarda (+0x2f0/+0x300/+0x310)
    Vec3 car_vel{};
    Vec3 eye{}, forward{}, up{};
    std::uint32_t top = 0;  // estado no topo da pilha do jogo (rva)
    float speed_kmh = 0;
};

constexpr std::uint16_t kLivePort = 47820;
constexpr std::size_t kLivePacketSize = 128;  // versão 2 (a 1 tinha 120 e também é lida)

// false se não é um pacote "DR2L" versão 1 (120 bytes) ou 2 (128 bytes).
bool parse_live(const void* data, std::size_t size, LiveSample& out);

// Do jogo para a pista: p_pista = giro_y(p_jogo - offset, -yaw). Sem arquivo, identidade.
struct GameTransform {
    float yaw_deg = 0;
    Vec3 offset{};
    bool loaded = false;
    Vec3 point_to_track(const Vec3& p) const;
    Vec3 dir_to_track(const Vec3& d) const;
    // O caminho de volta: p_jogo = giro_y(p_pista, +yaw) + offset.
    Vec3 point_to_game(const Vec3& p) const;
    Vec3 dir_to_game(const Vec3& d) const;
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

// Do editor para o jogo ("DR2E" versão 1, 56 bytes; o par do EditorPacket em include/dr2hook/live_link.h).
// O jogo liga/desliga a câmera livre quando `request` muda (0 = nenhum pedido) e, com ela ligada, põe o olho e a
// frente quando `pose` muda. Outra `session` (sorteada por editor aberto) zera as contas lá.
struct EditorMessage {
    std::uint32_t seq = 0;
    bool want_free = false;
    std::uint32_t request = 0, pose = 0;
    Vec3 eye{}, forward{};  // em metros do mundo do jogo
    std::uint32_t session = 0;
};
constexpr std::uint16_t kEditorPort = 47821;
constexpr std::size_t kEditorPacketSize = 56;
void build_editor_packet(const EditorMessage& m, unsigned char out[kEditorPacketSize]);

// Manda o EditorMessage para 127.0.0.1:kEditorPort (ou DR2_EDITOR_PORT); sem jogo escutando, só se perde.
class LiveSender {
public:
    LiveSender() = default;
    ~LiveSender();
    LiveSender(const LiveSender&) = delete;
    LiveSender& operator=(const LiveSender&) = delete;
    bool send(const EditorMessage& m);

private:
    int fd_ = -1;
};

}  // namespace dr2
