#pragma once

#include <cstdint>

namespace dr2hook {

// Elo ao vivo com o editor (tools/viewer3d): a cada quadro, no máximo 60 vezes por
// segundo, manda um datagrama UDP para 127.0.0.1:47820 com o carro do jogador e a
// câmera da especial em metros do mundo do jogo. Sem editor escutando, o envio só
// se perde. Roda na thread de render.
//
// Pacote (little endian, 128 bytes), versão 2:
//   +0 "DR2L"  +4 u32 versão  +8 u32 número  +12 u32 bits (LiveFlags)
//   +16 f32[3] posição do carro  +28 f32[9] linhas da rotação (+0x2f0/+0x300/+0x310)
//   +64 f32[3] velocidade  +76 f32[3] olho  +88 f32[3] frente  +100 f32[3] cima
//   +112 u32 estado do topo da pilha (rva)  +116 f32 km/h
//   +120 u32 passos da câmera livre dados no jogo (FreeCamera::LocalMoves)
//   +124 u32 última pose do editor aplicada (EditorPacket::pose)
//
// Câmera sincronizada: o editor manda, para 127.0.0.1:47821, o EditorPacket. O jogo
// liga/desliga a câmera livre quando `request` muda (o pedido vale uma vez; outra
// `session` zera a conta) e, com ela ligada, põe a pose do editor quando `pose` muda. Quem mexeu por último vale:
// o editor segue o jogo quando os passos (+120) sobem e manda a sua pose quando o
// usuário mexe nele.
enum LiveFlags : uint32_t {
  kLiveCar = 1u << 0,    // posição/rotação do carro valem
  kLiveView = 1u << 1,   // olho/frente/cima valem
  kLivePaused = 1u << 2, // menu de pausa no topo
  kLiveFree = 1u << 3,   // câmera livre ligada
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
  uint32_t freeMoves;
  uint32_t editorPose;
};
static_assert(sizeof(LivePacket) == 128, "pacote do LiveLink mudou de tamanho");

// Do editor para o jogo (little endian, 56 bytes), versão 1:
//   +0 "DR2E"  +4 u32 versão  +8 u32 número  +12 u32 bits (EditorFlags)
//   +16 u32 pedido (0 = nenhum)  +20 u32 pose (0 = nenhuma)
//   +24 f32[3] olho  +36 f32[3] frente  +48 u32 sessão (sorteada a cada abertura do editor)
//   +52 u32 reservado (0)
enum EditorFlags : uint32_t {
  kEditorWantFree = 1u << 0, // o pedido é ligar a câmera livre (senão, desligar)
};

struct EditorPacket {
  char magic[4];
  uint32_t version;
  uint32_t seq;
  uint32_t flags;
  uint32_t request;
  uint32_t pose;
  float eye[3];
  float forward[3];
  uint32_t session;
  uint32_t reserved;
};
static_assert(sizeof(EditorPacket) == 56, "pacote do editor mudou de tamanho");

class LiveLink {
public:
  static constexpr uint16_t kPort = 47820;
  static constexpr uint16_t kEditorPort = 47821;
  static void Install();
  static void Shutdown();
  // Primeira largada (RaceEvent): só a partir daqui o elo abre o socket e lê o jogo.
  static void OnStageStart();
  static void OnFrame();
};

} // namespace dr2hook
