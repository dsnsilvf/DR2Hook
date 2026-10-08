#pragma once

#include <cstdint>

namespace dr2hook {

// Elo ao vivo com o editor (tools/viewer3d): a cada quadro, no máximo 60 vezes por
// segundo, manda um datagrama UDP para 127.0.0.1:47820 com o carro do jogador e a
// câmera da especial em metros do mundo do jogo. Sem editor escutando, o envio só
// se perde. Roda na thread de render; não grava nada no jogo.
//
// Pacote (little endian, 120 bytes), versão 1:
//   +0 "DR2L"  +4 u32 versão  +8 u32 número  +12 u32 bits (LiveFlags)
//   +16 f32[3] posição do carro  +28 f32[9] linhas da rotação (+0x2f0/+0x300/+0x310)
//   +64 f32[3] velocidade  +76 f32[3] olho  +88 f32[3] frente  +100 f32[3] cima
//   +112 u32 estado do topo da pilha (rva)  +116 f32 km/h
enum LiveFlags : uint32_t {
  kLiveCar = 1u << 0,    // posição/rotação do carro valem
  kLiveView = 1u << 1,   // olho/frente/cima valem
  kLivePaused = 1u << 2, // menu de pausa no topo
};

struct LivePacket {
  char magic[4];
  uint32_t version;
  uint32_t seq;
  uint32_t flags;
  float carPos[3];
  float carRot[9];
  float carVel[3];
  float eye[3];
  float forward[3];
  float up[3];
  uint32_t top;
  float speedKmh;
};
static_assert(sizeof(LivePacket) == 120, "pacote do LiveLink mudou de tamanho");

class LiveLink {
public:
  static constexpr uint16_t kPort = 47820;
  static void Install();
  static void Shutdown();
  // Primeira largada (RaceEvent): só a partir daqui o elo abre o socket e lê o jogo.
  static void OnStageStart();
  static void OnFrame();
};

} // namespace dr2hook
