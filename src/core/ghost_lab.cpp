#include "dr2hook/ghost_lab.h"
#include "dr2hook/ghost_trace.h"
#include "dr2hook/logger.h"
#include "dr2hook/player.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <thread>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <MinHook.h>
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace dr2hook {
namespace {

std::atomic<bool> g_opaque{false};
// Experimento de colisao: ToggleCollision faz os fantasmas virarem carros fisicos.
// Carregar, largar e pausar desligam.
std::atomic<bool> g_collide{false};
std::atomic<bool> g_hud{false};

// Trajeto do fantasma de referencia, copiado na thread do jogo.
struct PathPoint {
  float t;    // ms
  float x, y, z;
  float s;    // metros acumulados
};

std::mutex g_mutex;
std::vector<PathPoint> g_path;
const void *g_pathSlot = nullptr;
uint32_t g_pathCount = 0;
float g_ghostPos[3] = {};
uint64_t g_lastEvalTick = 0;
// Qualquer fantasma avaliado (todo frame da especial, pausa nao).
std::atomic<uint64_t> g_anyEvalTick{0};
int g_slotCount = 0;
int g_readyCount = 0;
GhostLab::Status g_status;
size_t g_playerHint = 0;
size_t g_ghostHint = 0;

// Projeta p no trajeto: tempo (ms) e distancia (m) interpolados no segmento
// mais proximo. Procura perto do ultimo indice e, se ficou longe, no todo.
bool Project(const std::vector<PathPoint> &path, const float p[3], size_t &hint,
             float &tOut, float &sOut, float &distOut) {
  if (path.size() < 2) return false;
  auto search = [&](size_t from, size_t to, size_t &best, float &bestD2,
                    float &bestU) {
    for (size_t i = from; i < to; ++i) {
      const PathPoint &a = path[i];
      const PathPoint &b = path[i + 1];
      const float abx = b.x - a.x, aby = b.y - a.y, abz = b.z - a.z;
      const float len2 = abx * abx + aby * aby + abz * abz;
      float u = 0.f;
      if (len2 > 1e-6f) {
        u = ((p[0] - a.x) * abx + (p[1] - a.y) * aby + (p[2] - a.z) * abz) / len2;
        u = std::clamp(u, 0.f, 1.f);
      }
      const float dx = a.x + abx * u - p[0];
      const float dy = a.y + aby * u - p[1];
      const float dz = a.z + abz * u - p[2];
      const float d2 = dx * dx + dy * dy + dz * dz;
      if (d2 < bestD2) {
        bestD2 = d2;
        best = i;
        bestU = u;
      }
    }
  };
  const size_t last = path.size() - 1;
  size_t best = 0;
  float bestD2 = 1e30f, bestU = 0.f;
  const size_t from = hint > 40 ? hint - 40 : 0;
  search(from, std::min(last, hint + 40), best, bestD2, bestU);
  if (bestD2 > 30.f * 30.f) {
    bestD2 = 1e30f;
    search(0, last, best, bestD2, bestU);
  }
  hint = best;
  const PathPoint &a = path[best];
  const PathPoint &b = path[best + 1];
  tOut = a.t + (b.t - a.t) * bestU;
  sOut = a.s + (b.s - a.s) * bestU;
  distOut = std::sqrt(bestD2);
  return true;
}

#if defined(_WIN32)

constexpr uintptr_t kImageBase = 0x140000000;

// EvaluateGhostState(owner, &tempo, ?, out): owner+0x20 = slot; retorna 2 se
// slot+0x1a8 != 2. Saida: +0x30 posicao, +0x54 progresso, +0x60 valido.
// Chamado pelo atualizador de cada carro fantasma (0x140518505).
constexpr uintptr_t kEvaluateRva = 0x1409ce4d0 - kImageBase;
constexpr uint8_t kEvaluatePrologue[] = {
    0x48, 0x89, 0x5c, 0x24, 0x10, // mov [rsp+10h], rbx
    0x55, 0x56, 0x57,             // push rbp; push rsi; push rdi
    0x48, 0x8b, 0xec,             // mov rbp, rsp
    0x48, 0x83, 0xec, 0x70        // sub rsp, 70h
};
constexpr size_t kOwnerSlot = 0x20;
constexpr size_t kOutPosition = 0x30;
constexpr size_t kOutValid = 0x60;

// Troca os 23 materiais do modelo (car_matt, carpaint_metallic, carglass...)
// pelas variantes *_ghost, desliga sombras e poe o passe ghost_car_depth_si.
// Chamada uma vez por carro fantasma, na criacao (0x140b948c5).
constexpr uintptr_t kMakeGhostMaterialsRva = 0x14095fe90 - kImageBase;
constexpr uint8_t kMakeGhostMaterialsPrologue[] = {
    0x48, 0x85, 0xd2,                   // test rdx, rdx
    0x0f, 0x84, 0xe9, 0x01, 0x00, 0x00, // je 0x140960082 (ret)
    0x55                                // push rbp
};

// CopyGhostLapData(dest, src, realocar): copia estado e canais; aloca com o
// alocador do jogo (0x1409cd610) se faltar capacidade.
// SpawnStageVehicles: cria os carros da especial a partir da lista de entradas
// da sessao ([0x1416951e8]+0x30, contagem +0x40). Hook so de diagnostico.
constexpr uintptr_t kSpawnVehiclesRva = 0x14046b320 - kImageBase;
constexpr uint8_t kSpawnVehiclesPrologue[] = {0x40, 0x56, 0x41, 0x54, 0x48, 0x83,
                                              0xec, 0x38, 0x4c, 0x8b, 0xe1};
// AddGhostEntry: cria a entrada de fantasma da sessao a partir de um registro
// de 0xb8 bytes. Registro com +0x08 (dado da volta) nulo = enchimento ate 5:
// a funcao marca a entrada com +0xb4 = 1 e o spawn a pula (so 2 carros).
constexpr uintptr_t kAddGhostEntryRva = 0x14057df00 - kImageBase;
constexpr uint8_t kAddGhostEntryPrologue[] = {0x4c, 0x8b, 0xdc, 0x49, 0x89, 0x5b, 0x08, 0x49,
                                              0x89, 0x6b, 0x10, 0x45, 0x89, 0x4b, 0x20};
constexpr size_t kGhostRecordSize = 0xb8;
constexpr uintptr_t kVehicleSystemIndexRva = 0x141693ffc - kImageBase;
constexpr uintptr_t kSessionGlobalRva = 0x1416951e8 - kImageBase;

constexpr uintptr_t kCopyLapRva = 0x1409cfd30 - kImageBase;
constexpr uint8_t kCopyLapPrologue[] = {
    0x48, 0x89, 0x5c, 0x24, 0x08, // mov [rsp+8], rbx
    0x48, 0x89, 0x6c, 0x24, 0x10  // mov [rsp+10h], rbp
};

// Gerenciador dos fantasmas: std::map<participante, slot> com a cabeca em
// +0x18 e o tamanho em +0x20.
constexpr uintptr_t kManagerGlobalRva = 0x141695228 - kImageBase;
constexpr size_t kManagerMapHead = 0x18;
constexpr size_t kManagerMapSize = 0x20;
// Deslocamento de tempo somado por EvaluateGhostState (float, segundos).
constexpr uintptr_t kTimeOffsetRva = 0x141f593e0 - kImageBase;

// Slot (0x260 bytes).
constexpr size_t kSlotState = 0x1a8; // 2 = pronto
struct Channel {
  size_t count, data, stride;
};
// rotacao, posicao, entradas, 1 Hz, rodas, eventos. Tempo u32 (ms) em +0x08.
constexpr Channel kChannels[] = {{0x1ac, 0x1c8, 0x18}, {0x1b0, 0x1d0, 0x30},
                                 {0x1b4, 0x1d8, 0x18}, {0x1b8, 0x1e0, 0x18},
                                 {0x1bc, 0x1e8, 0x30}, {0x1c0, 0x218, 0x28}};
constexpr size_t kPositionChannel = 1;
constexpr size_t kSampleTime = 0x08;
constexpr size_t kSamplePosition = 0x10;

// Controlador de carro fantasma (0x100 bytes; atualizador ~0x140518400):
// +0x00 veiculo, +0x08 dono passado a EvaluateGhostState, +0x28 slot,
// +0x62 liga o desenho do carro (chama 0x1409da680 a cada frame).
constexpr size_t kControllerVehicle = 0x00;
constexpr size_t kControllerOwner = 0x08;
constexpr size_t kControllerSlot = 0x28;
constexpr size_t kControllerDraw = 0x62;

// Empacota GhostCarValues em objeto+0x4f80. Chamado em 0x1409655b1 com edx = 0.
// x = clamp(1 - [objeto+0x5058], 0, 1). w = 0 se x == 1, senao 1.
// O valor em +0x5058 e o maximo de veiculo+0x88..+0x9c; no fantasma so +0x94
// fica diferente de zero (0,5 longe, sobe para 1 perto).
constexpr uintptr_t kPackOpacityRva = 0x140bd7500 - kImageBase;
constexpr uint8_t kPackOpacityPrologue[] = {
    0xf3, 0x0f, 0x10, 0x05, 0xb0, 0x07, 0x62, 0x00, // movss xmm0, [1.0]
};
constexpr size_t kGhostCarValues = 0x4f80;
// Fator de esmaecimento lido pelo getter 0x140bcf9d0. As funcoes de desenho
// (0x1409535fd, 0x140953a92, 0x140955dca) pulam o carro quando ele vale
// exatamente 1,0, o que so acontece colado no jogador (<= 5 m).
constexpr size_t kFadeFactor = 0x5058;
constexpr float kFadeBelowHidden = 0.999f;

// Submissao do carro. Le veiculo+0xbc: 0 desenha o passe opaco, qualquer
// outro (3 = fantasma) cai no passe transparente, que e a aura de 5–50 m.
// O campo so e trocado por 0 durante esta chamada e restaurado antes do
// retorno, para a fisica continuar vendo o tipo fantasma.
constexpr uintptr_t kSubmitCarRva = 0x140986320 - kImageBase;
constexpr uint8_t kSubmitCarPrologue[] = {
    0x48, 0x89, 0x5c, 0x24, 0x18, // mov [rsp+18h], rbx
    0x55,                         // push rbp
    0x56,                         // push rsi
    0x57,                         // push rdi
};
constexpr uintptr_t kVehicleVtableRva = 0x14127cc00 - kImageBase;
constexpr size_t kVehicleType = 0xbc;
constexpr uint32_t kVehicleTypeGhost = 3;
// 0x140dad720: corpo+0xf0 = 0, +0xf2 = 1 ("fantasma" no proximo passo).
constexpr uintptr_t kBodySleepRva = 0x140dad720 - kImageBase;
constexpr uint8_t kBodySleepPrologue[] = {
    0xc6, 0x81, 0xf0, 0x00, 0x00, 0x00, 0x00, // mov byte [rcx+0F0h], 0
    0xc6, 0x81, 0xf2, 0x00, 0x00, 0x00, 0x01, // mov byte [rcx+0F2h], 1
};


using EvaluateFn = int (*)(uint8_t *owner, void *time, void *arg, uint8_t *out);
using MakeGhostMaterialsFn = void (*)(void *self, void *model);
using CopyLapFn = void (*)(uint8_t *dest, const uint8_t *src, bool realloc);
using PackOpacityFn = void (*)(uint8_t *renderObj, int channel);
using SubmitCarFn = void (*)(uint8_t *renderObj, uint8_t *context, void *pass,
                             void *extra);

uintptr_t g_gameBase = 0;
EvaluateFn g_originalEvaluate = nullptr;
MakeGhostMaterialsFn g_originalMakeGhost = nullptr;
PackOpacityFn g_originalPack = nullptr;
SubmitCarFn g_originalSubmit = nullptr;
void *g_evaluateTarget = nullptr;
void *g_makeGhostTarget = nullptr;
void *g_packTarget = nullptr;
void *g_submitTarget = nullptr;
using SpawnVehiclesFn = void (*)(void *ctx);
using AddGhostEntryFn = uintptr_t (*)(void *, void *, void *, uintptr_t, uint8_t *, uintptr_t);
AddGhostEntryFn g_originalAddEntry = nullptr;
void *g_addEntryTarget = nullptr;
void *g_stageCtx = nullptr; // objeto da especial visto em SpawnStageVehicles
SpawnVehiclesFn g_originalSpawn = nullptr;
void *g_spawnTarget = nullptr;
bool g_opacityLogged = false;

std::atomic<int> g_inFlight{0};
struct InFlight {
  InFlight() { g_inFlight.fetch_add(1); }
  ~InFlight() { g_inFlight.fetch_sub(1); }
};

// -1 = nada pendente.
std::atomic<int> g_cloneCount{-1};
bool g_collideApplied = false;
// Carros com a colisao ligada: a thread do jogo poe, o Update desfaz na pausa.
std::mutex g_collideMutex;
std::vector<uint8_t *> g_collidingVehicles;

std::atomic<int> g_cloneStepMs{0};
std::atomic<bool> g_spawnPending{false}; // F7: +1 copia sobre as que existem

// Uma volta e reconhecida pelo conteudo, nao pelo slot: o jogo reorganiza
// os slots na largada e o F8 apaga o estado do core. Copias = mesma volta
// com os tempos deslocados. So na thread do jogo.
struct LapKey {
  uint32_t samples = 0;
  float x = 0.f, y = 0.f, z = 0.f; // 1a posicao
  bool operator==(const LapKey &o) const {
    return samples == o.samples && x == o.x && y == o.y && z == o.z;
  }
};
bool g_lapKnown = false;
LapKey g_lapKey;
uint32_t g_lapStart = 0;           // tempo original da 1a posicao (ms)
const uint8_t *g_source = nullptr; // so comparado, nunca lido

template <typename T> T Read(const uint8_t *base, size_t offset) {
  T value;
  std::memcpy(&value, base + offset, sizeof(T));
  return value;
}

uint8_t *ManagerPtr() {
  return *reinterpret_cast<uint8_t **>(g_gameBase + kManagerGlobalRva);
}

// Slots do gerenciador na ordem do mapa (MSVC: no = esquerda, pai, direita,
// cor, isnil em +0x19, chave +0x20, valor +0x28).
std::vector<uint8_t *> CollectSlots() {
  std::vector<uint8_t *> slots;
  uint8_t *manager = ManagerPtr();
  if (manager == nullptr) return slots;
  uint8_t *head = Read<uint8_t *>(manager, kManagerMapHead);
  const uint64_t size = Read<uint64_t>(manager, kManagerMapSize);
  if (head == nullptr || size == 0 || size > 64) return slots;
  std::vector<uint8_t *> stack;
  uint8_t *node = Read<uint8_t *>(head, 0x8); // raiz
  while ((node != nullptr && node[0x19] == 0) || !stack.empty()) {
    while (node != nullptr && node[0x19] == 0) {
      stack.push_back(node);
      node = Read<uint8_t *>(node, 0x0);
    }
    node = stack.back();
    stack.pop_back();
    if (uint8_t *slot = Read<uint8_t *>(node, 0x28)) slots.push_back(slot);
    if (slots.size() > size) break;
    node = Read<uint8_t *>(node, 0x10);
  }
  return slots;
}

// Carros fantasma realmente desenhados: controlador com veiculo e +0x62 ligado.
int CountDrawnGhostCars() {
  int drawn = 0;
  uint8_t *manager = ManagerPtr();
  if (manager == nullptr) return 0;
  uint8_t *head = Read<uint8_t *>(manager, kManagerMapHead);
  const uint64_t size = Read<uint64_t>(manager, kManagerMapSize);
  if (head == nullptr || size == 0 || size > 64) return 0;
  std::vector<uint8_t *> stack;
  uint8_t *node = Read<uint8_t *>(head, 0x8);
  int guard = 0;
  while (((node != nullptr && node[0x19] == 0) || !stack.empty()) && ++guard < 256) {
    while (node != nullptr && node[0x19] == 0) {
      stack.push_back(node);
      node = Read<uint8_t *>(node, 0x0);
    }
    node = stack.back();
    stack.pop_back();
    const uint8_t *controller = Read<const uint8_t *>(node, 0x40);
    if (controller != nullptr && Read<uintptr_t>(controller, kControllerVehicle) != 0 &&
        controller[kControllerDraw] != 0) {
      ++drawn;
    }
    node = Read<uint8_t *>(node, 0x10);
  }
  return drawn;
}

std::string g_spawnNotice; // g_mutex; mostrada pelo core no proximo frame

bool IsReady(const uint8_t *slot) { return Read<uint32_t>(slot, kSlotState) == 2; }

bool KeyOf(const uint8_t *slot, LapKey &key, uint32_t &start) {
  const Channel &ch = kChannels[kPositionChannel];
  const uint8_t *data = Read<const uint8_t *>(slot, ch.data);
  key.samples = Read<uint32_t>(slot, ch.count);
  if (!IsReady(slot) || data == nullptr || key.samples == 0) return false;
  std::memcpy(&key.x, data + kSamplePosition, sizeof(float) * 3);
  start = Read<uint32_t>(data, kSampleTime);
  return true;
}

void ShiftTimes(uint8_t *slot, int64_t deltaMs) {
  for (const Channel &ch : kChannels) {
    const uint32_t n = Read<uint32_t>(slot, ch.count);
    uint8_t *data = Read<uint8_t *>(slot, ch.data);
    if (data == nullptr) continue;
    for (uint32_t j = 0; j < n; ++j) {
      const int64_t t = Read<uint32_t>(data, j * ch.stride + kSampleTime) + deltaMs;
      const uint32_t value = static_cast<uint32_t>(std::max<int64_t>(0, t));
      std::memcpy(data + j * ch.stride + kSampleTime, &value, sizeof(value));
    }
  }
}

// Fonte = a volta do 1o slot pronto, no slot em que ela comeca mais cedo.
// Se ate a fonte ficou deslocada, volta ao tempo original.
uint8_t *ResolveSource(const std::vector<uint8_t *> &slots) {
  LapKey key{};
  uint32_t start = 0;
  uint8_t *source = nullptr;
  for (uint8_t *slot : slots) {
    LapKey k{};
    uint32_t t = 0;
    if (!KeyOf(slot, k, t)) continue;
    if (source == nullptr) {
      key = k;
    } else if (!(k == key) || t >= start) {
      continue;
    }
    source = slot;
    start = t;
  }
  if (source == nullptr) return nullptr;
  if (g_lapKnown && key == g_lapKey) {
    if (start != g_lapStart) {
      ShiftTimes(source, static_cast<int64_t>(g_lapStart) - start);
      Logger::Info("GhostLab: fantasma original estava deslocado; tempo restaurado.");
    }
  } else {
    g_lapKnown = true;
    g_lapKey = key;
    g_lapStart = start;
  }
  g_source = source;
  return source;
}

bool IsClone(const uint8_t *slot) {
  if (!g_lapKnown || slot == g_source) return false;
  LapKey k{};
  uint32_t t = 0;
  return KeyOf(slot, k, t) && k == g_lapKey;
}

// Controladores de cada slot que e copia: liga +0x62 (desenho) e +0x63 (o
// atualizador sai sem ele; o enchimento de AddGhostEntry nasce com 0). Roda a
// cada avaliacao, pois so o controlador do carro ligado chega a
// EvaluateGhostState.
void LinkAllCloneControllers() {
  uint8_t *manager = ManagerPtr();
  if (manager == nullptr) return;
  uint8_t *head = Read<uint8_t *>(manager, kManagerMapHead);
  const uint64_t size = Read<uint64_t>(manager, kManagerMapSize);
  if (head == nullptr || size == 0 || size > 64) return;
  std::vector<uint8_t *> stack;
  uint8_t *node = Read<uint8_t *>(head, 0x8);
  int guard = 0;
  while (((node != nullptr && node[0x19] == 0) || !stack.empty()) && ++guard < 256) {
    while (node != nullptr && node[0x19] == 0) {
      stack.push_back(node);
      node = Read<uint8_t *>(node, 0x0);
    }
    node = stack.back();
    stack.pop_back();
    uint8_t *controller = Read<uint8_t *>(node, 0x40);
    if (controller != nullptr && Read<uintptr_t>(controller, kControllerVehicle) != 0) {
      const uint8_t *slot = Read<const uint8_t *>(controller, kControllerSlot);
      if (slot != nullptr && IsClone(slot)) {
        controller[kControllerDraw] = 1;
        controller[0x63] = 1;
      }
    }
    node = Read<uint8_t *>(node, 0x10);
  }
}

void ApplyClones(int count, int stepMs) {
  const std::vector<uint8_t *> slots = CollectSlots();
  {
    // Antes de mexer: se o jogo cair daqui, o log mostra quantas eram.
    int ready = 0, linked = 0, vehicles = 0;
    for (uint8_t *slot : slots) ready += IsReady(slot) ? 1 : 0;
    char pre[200];
    std::snprintf(pre, sizeof(pre),
                  "GhostLab[limite]: aplicando %d copia(s) (slots=%zu prontos=%d).",
                  count, slots.size(), ready);
    (void)linked;
    (void)vehicles;
    Logger::Info(pre);
  }
  uint8_t *source = ResolveSource(slots);
  if (source == nullptr) {
    Logger::Warn("GhostLab: nenhum slot com fantasma pronto para clonar.");
    return;
  }
  auto copyLap = reinterpret_cast<CopyLapFn>(g_gameBase + kCopyLapRva);
  if (std::memcmp(reinterpret_cast<void *>(copyLap), kCopyLapPrologue,
                  sizeof(kCopyLapPrologue)) != 0) {
    Logger::Warn("GhostLab: prologo de CopyGhostLapData diferente; clones abortados.");
    return;
  }
  // So mexe em slots vazios ou em copias: fantasmas diferentes escolhidos no
  // proprio jogo ficam como estao.
  int made = 0;
  for (uint8_t *dest : slots) {
    if (dest == source) continue;
    const bool clone = IsClone(dest);
    if (!clone && IsReady(dest)) continue;
    if (made >= count) {
      if (clone) {
        const uint32_t idle = 0;
        std::memcpy(dest + kSlotState, &idle, sizeof(idle));
      }
      continue;
    }
    ++made;
    copyLap(dest, source, false);
    ShiftTimes(dest, static_cast<int64_t>(made) * stepMs);
  }
  char msg[200];
  std::snprintf(msg, sizeof(msg),
                "GhostLab: %d copia(s) a cada %.1f s (slots no gerenciador: %zu).",
                made, stepMs / 1000.0, slots.size());
  Logger::Info(msg);
  if (made < count) {
    std::snprintf(msg, sizeof(msg),
                  "GhostLab[limite]: pedidas %d, feitas %d: sem slot livre (limite do gerenciador).",
                  count, made);
    Logger::Warn(msg);
  }
}

// Na thread do jogo: le o trajeto do slot de referencia quando ele muda.
void CapturePath(const uint8_t *slot) {
  const Channel &ch = kChannels[kPositionChannel];
  const uint32_t n = Read<uint32_t>(slot, ch.count);
  const uint8_t *data = Read<const uint8_t *>(slot, ch.data);
  if (slot == g_pathSlot && n == g_pathCount) return;
  std::vector<PathPoint> path;
  if (data != nullptr && n >= 2 && n < 100000) {
    path.reserve(n);
    float s = 0.f;
    for (uint32_t j = 0; j < n; ++j) {
      const uint8_t *e = data + j * ch.stride;
      PathPoint p{};
      p.t = static_cast<float>(Read<uint32_t>(e, kSampleTime));
      std::memcpy(&p.x, e + kSamplePosition, sizeof(float) * 3);
      if (!path.empty()) {
        const PathPoint &q = path.back();
        s += std::sqrt((p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y) +
                       (p.z - q.z) * (p.z - q.z));
      }
      p.s = s;
      path.push_back(p);
    }
  }
  g_path.swap(path);
  g_pathSlot = slot;
  g_pathCount = n;
  g_playerHint = g_ghostHint = 0;
  char msg[128];
  std::snprintf(msg, sizeof(msg),
                "GhostLab: trajeto do fantasma lido (%u amostras, %.0f m, %.2f s).",
                n, g_path.empty() ? 0.f : g_path.back().s,
                g_path.empty() ? 0.f : g_path.back().t / 1000.f);
  Logger::Info(msg);
}

bool UsablePointer(uintptr_t p) {
  return p >= 0x10000 && p < 0x0000800000000000ULL && (p & 7) == 0;
}

// O jogo cria 2 carros fantasma, mas o controlador do segundo fica com
// +0x62 = 0: sem 0x1409da680 o carro nunca e desenhado. Ligando a flag, a
// copia aparece (validado em 2026-10-02 com o jogo recem-aberto).
void LinkCloneVehicle(uint8_t *owner) {
  uint8_t *controller = owner - kControllerOwner;
  const uint8_t *slot = Read<const uint8_t *>(controller, kControllerSlot);
  if (controller[kControllerDraw] != 0 || !IsClone(slot)) return;
  if (Read<uint8_t *>(controller, kControllerVehicle) == nullptr) return;
  controller[kControllerDraw] = 1;
}

// Colisao com o fantasma (2026-10-03). No corpo fisico, f0 e o pedido de
// "carro solido", f1 o estado aplicado e f2 o "aplique no proximo passo": o
// passo da fisica (0x140dbc500) copia f0 para f1 e, se mudou, monta as formas
// de contato (0x140da94c0, forma em corpo+0xc98) ou desmonta (0x140da6770).
// Desligar e so f0 = 0 + f2 = 1 (zerar f1 junto travava a forma montada).
// O tipo do veiculo (+0xbc = 3) fica intocado: trocar no meio da gravacao
// quebra o replay (crash em 0x140adb33e). Mas o tipo 3 chama 0x140dad720 a
// cada frame (f0 = 0), entao o hook dela ignora os corpos com colisao.
// O bit 4 das flags em corpo+0x190 (dword alto guardado multiplicado por
// 0x45fa8d8d) e apagado na criacao do fantasma e vai junto.
constexpr size_t kVehicleBody = 0x30;
constexpr size_t kBodyFlagsHigh = 0x194;
constexpr uint32_t kBodyFlagsEncode = 0x45fa8d8d;
constexpr uint32_t kBodyFlagsDecode = 0x6427d45;
constexpr uint32_t kBodyFlagCollides = 4;
constexpr size_t kBodySolidRequest = 0xf0;
constexpr size_t kBodySolidDirty = 0xf2;
// 3 = grade de contato de carro (5x7); o fantasma nasce com 2 (1x1).
constexpr size_t kBodyContactMode = 0x198;
constexpr uint32_t kBodyContactModeCar = 3;

// Corpos com colisao ligada, lidos pelo hook de 0x140dad720 (thread do jogo).
constexpr size_t kMaxSolidBodies = 8;
std::atomic<uint8_t *> g_solidBodies[kMaxSolidBodies] = {};

bool IsSolidBody(const uint8_t *body) {
  for (const auto &slot : g_solidBodies) {
    if (slot.load() == body) return true;
  }
  return false;
}

void MarkSolidBody(uint8_t *body, bool solid) {
  for (auto &slot : g_solidBodies) {
    uint8_t *expected = solid ? nullptr : body;
    if (slot.compare_exchange_strong(expected, solid ? body : nullptr)) return;
  }
}

void SetVehicleCollision(uint8_t *vehicle, bool collide) {
  uint8_t *body = Read<uint8_t *>(vehicle, kVehicleBody);
  if (!UsablePointer(reinterpret_cast<uintptr_t>(body))) return;
  MarkSolidBody(body, collide);
  uint32_t flags = Read<uint32_t>(body, kBodyFlagsHigh) * kBodyFlagsDecode;
  flags = collide ? (flags | kBodyFlagCollides) : (flags & ~kBodyFlagCollides);
  const uint32_t stored = flags * kBodyFlagsEncode;
  std::memcpy(body + kBodyFlagsHigh, &stored, sizeof(stored));
  if (collide) {
    std::memcpy(body + kBodyContactMode, &kBodyContactModeCar,
                sizeof(kBodyContactModeCar));
  }
  body[kBodySolidRequest] = collide ? 1 : 0;
  body[kBodySolidDirty] = 1;
}

using BodySleepFn = void (*)(uint8_t *body);
BodySleepFn g_originalBodySleep = nullptr;
void *g_bodySleepTarget = nullptr;

void DetourBodySleep(uint8_t *body) {
  if (IsSolidBody(body)) return;
  g_originalBodySleep(body);
}

void ApplyCollision(uint8_t *owner) {
  const bool collide = g_collide.load();
  if (collide != g_collideApplied) {
    g_collideApplied = collide;
    Logger::Info(std::string("GhostLab: colisao com o fantasma ") +
                 (collide ? "ligada." : "desligada."));
  }
  uint8_t *vehicle = Read<uint8_t *>(owner - kControllerOwner, kControllerVehicle);
  if (!UsablePointer(reinterpret_cast<uintptr_t>(vehicle)) ||
      Read<uintptr_t>(vehicle, 0) != g_gameBase + kVehicleVtableRva) {
    return;
  }
  std::lock_guard<std::mutex> lock(g_collideMutex);
  const bool listed = std::find(g_collidingVehicles.begin(),
                                g_collidingVehicles.end(),
                                vehicle) != g_collidingVehicles.end();
  if (collide == listed) return;
  SetVehicleCollision(vehicle, collide);
  if (collide) {
    g_collidingVehicles.push_back(vehicle);
  } else {
    g_collidingVehicles.erase(std::remove(g_collidingVehicles.begin(),
                                          g_collidingVehicles.end(), vehicle),
                              g_collidingVehicles.end());
  }
}

// Pausa desliga a colisao (F11 de novo para religar), para nenhum Reiniciar
// pegar o fantasma solido. Pedido ainda nao aplicado (F11 com o jogo pausado)
// fica esperando. So grava o pedido: o passo da fisica desmonta as formas
// quando o jogo voltar a rodar.
void RevertCollision() {
  std::lock_guard<std::mutex> lock(g_collideMutex);
  if (g_collidingVehicles.empty()) return;
  g_collide.store(false);
  for (uint8_t *vehicle : g_collidingVehicles) {
    if (Read<uintptr_t>(vehicle, 0) == g_gameBase + kVehicleVtableRva) {
      SetVehicleCollision(vehicle, false);
    }
  }
  Logger::Info("GhostLab: jogo pausado; colisao desligada (ToggleCollision religa).");
  g_collidingVehicles.clear();
  g_collideApplied = false;
}

void LogSubmitted();

// Ultima posicao avaliada por dono (controlador), para o log do F7.
struct OwnerPos {
  const uint8_t *owner = nullptr;
  float pos[3] = {0, 0, 0};
  uint64_t tick = 0;
};
OwnerPos g_ownerPos[16];

void RecordOwnerPos(const uint8_t *owner, const uint8_t *out) {
  OwnerPos *slot = nullptr;
  for (OwnerPos &p : g_ownerPos) {
    if (p.owner == owner) { slot = &p; break; }
    if (slot == nullptr && p.owner == nullptr) slot = &p;
  }
  if (slot == nullptr) return;
  slot->owner = owner;
  std::memcpy(slot->pos, out + kOutPosition, sizeof(slot->pos));
  slot->tick = GetTickCount64();
}

void LogOwnerPositions() {
  const uint64_t now = GetTickCount64();
  std::string line = "GhostLab[limite]: posicoes dos fantasmas (x z y, idade ms):";
  int n = 0;
  for (const OwnerPos &p : g_ownerPos) {
    if (p.owner == nullptr) continue;
    char one[96];
    std::snprintf(one, sizeof(one), " [%d] %.0f %.0f %.0f (%llu)", n++, p.pos[0], p.pos[1],
                  p.pos[2], static_cast<unsigned long long>(now - p.tick));
    line += one;
  }
  Logger::Info(line);
}

// ---- Pausa dos clones (F6) ----
// O jogo calcula o tempo do fantasma em [ctl+0x58] = (uint64)(relogio*1e6) ^ chave
// (chave no global 0x1415e3500) e o passa a EvaluateGhostState. O relogio so
// avanca com a simulacao, entao na pausa do jogo tudo congela junto; aqui
// congelamos so o tempo dos clones.
constexpr uintptr_t kTimeKeyRva = 0x1415e3500 - kImageBase;
std::atomic<bool> g_clonePaused{false};
struct CloneFreeze {
  bool active = false;
  double frozenEff = 0;  // tempo efetivo do clone congelado
  double pauseNow = 0;   // relogio do jogo quando pausou
  double accum = 0;      // soma das pausas ja encerradas
  double lastNow = 0;
};
CloneFreeze g_freeze; // so a thread do jogo

uint64_t TimeKey() { return *reinterpret_cast<const uint64_t *>(g_gameBase + kTimeKeyRva); }
double DecodeTime(uint64_t raw) {
  return static_cast<double>(static_cast<int64_t>(raw ^ TimeKey())) / 1e6;
}
uint64_t EncodeTime(double seconds) {
  if (seconds < 0) seconds = 0;
  return static_cast<uint64_t>(seconds * 1e6) ^ TimeKey();
}

// Reescreve o tempo de um clone antes de EvaluateGhostState. Devolve true se o
// clone esta congelado (a velocidade da saida deve ser zerada).
bool ApplyCloneFreeze(uint8_t *owner, uint64_t *time) {
  const uint64_t rawIn = *time;
  const double now = DecodeTime(rawIn);
  if (!(now >= 0.0 && now < 1e5)) {
    static bool warned = false;
    if (!warned) {
      warned = true;
      Logger::Warn("GhostLab[pausa]: tempo do clone fora do esperado; pausa ignorada.");
    }
    return false;
  }
  CloneFreeze &f = g_freeze;
  if (f.lastNow > 0 && now + 0.5 < f.lastNow) {
    // O relogio voltou (Reiniciar/nova especial): zera tudo.
    Logger::Info("GhostLab[pausa]: relogio voltou (" + std::to_string(f.lastNow) + " -> " +
                 std::to_string(now) + "); pausa dos clones desligada.");
    f = CloneFreeze{};
    g_clonePaused.store(false);
  }
  f.lastNow = now;
  const bool want = g_clonePaused.load();
  char note[400];
  if (want && !f.active) {
    f.active = true;
    f.frozenEff = now - f.accum;
    f.pauseNow = now;
    std::snprintf(note, sizeof(note),
                  "GhostLab[pausa]: fantasmas PAUSADOS (relogio %.3f, tempo do clone %.3f, acumulado %.3f).",
                  now, f.frozenEff, f.accum);
    Logger::Info(note);
    if (GhostTrace::Enabled()) GhostTrace::Note(note);
  } else if (!want && f.active) {
    f.accum += now - f.pauseNow;
    f.active = false;
    std::snprintf(note, sizeof(note),
                  "GhostLab[pausa]: fantasmas RETOMADOS (relogio %.3f, pausa de %.3f s, acumulado %.3f).",
                  now, now - f.pauseNow, f.accum);
    Logger::Info(note);
    if (GhostTrace::Enabled()) GhostTrace::Note(note);
  }
  const double eff = f.active ? f.frozenEff : now - f.accum;
  if (f.active || f.accum != 0.0) *time = EncodeTime(eff);
  if (GhostTrace::Enabled()) {
    std::snprintf(note, sizeof(note),
                  "FREEZE own=%p paused=%d active=%d now=%.6f eff=%.6f accum=%.6f raw_in=%016llx "
                  "raw_out=%016llx",
                  owner, want ? 1 : 0, f.active ? 1 : 0, now, eff, f.accum,
                  static_cast<unsigned long long>(rawIn),
                  static_cast<unsigned long long>(*time));
    GhostTrace::Note(note);
  }
  return f.active;
}

int WantedGhostCars(); // definido mais abaixo (arquivo de teste)

// Clones prontos sem F7: com dr2hook_ghost_cars.txt (N carros), assim que o
// fantasma original esta pronto, copia a volta para os slots vazios (ate N-1
// clones, 1 s de espacamento ou o ultimo do F7). Como o Reiniciar apaga as
// copias, confere de novo a cada 0,5 s. So na thread do jogo.
void AutoClones() {
  static uint64_t lastCheck = 0;
  const uint64_t now = GetTickCount64();
  if (now - lastCheck < 500) return;
  lastCheck = now;
  const int wanted = WantedGhostCars();
  if (wanted < 2) return;
  const std::vector<uint8_t *> slots = CollectSlots();
  int ready = 0, empty = 0, existing = 0;
  for (uint8_t *slot : slots) {
    if (!IsReady(slot)) {
      ++empty;
      continue;
    }
    ++ready;
    if (IsClone(slot)) ++existing;
  }
  if (ready == 0 || empty == 0) return;
  const int need = std::min({wanted - 1, static_cast<int>(slots.size()) - 1, existing + empty});
  if (existing >= need) return;
  const int stepMs = g_cloneStepMs.load();
  Logger::Info("GhostLab[auto]: clones prontos sem F7 (existem " + std::to_string(existing) +
               ", faltam " + std::to_string(need - existing) + ").");
  ApplyClones(need, stepMs > 0 ? stepMs : 1000);
}

int DetourEvaluate(uint8_t *owner, void *time, void *arg, uint8_t *out) {
  InFlight guard;
  AutoClones();
  const int pending = g_cloneCount.exchange(-1);
  if (pending >= 0) ApplyClones(pending, g_cloneStepMs.load());
  if (g_spawnPending.exchange(false)) {
    // Conta as copias que existem agora (o Reiniciar as apaga), nao um contador.
    const std::vector<uint8_t *> slots = CollectSlots();
    ResolveSource(slots);
    int existing = 0;
    for (uint8_t *slot : slots) existing += IsClone(slot) && IsReady(slot) ? 1 : 0;
    const int stepMs = g_cloneStepMs.load();
    Logger::Info("GhostLab[limite]: F7 -> copia #" + std::to_string(existing + 1) +
                 " (existentes: " + std::to_string(existing) + ").");
    ApplyClones(existing + 1, stepMs > 0 ? stepMs : 1000);
    const int copies = std::min(existing + 1, static_cast<int>(slots.size()) - 1);
    const int drawn = CountDrawnGhostCars();
    char notice[128];
    std::snprintf(notice, sizeof(notice),
                  "Ghost data copy %d; ghost cars drawn: %d (the game creates only 2).",
                  copies, drawn);
    Logger::Info(std::string("GhostLab[limite]: ") + notice);
    LogOwnerPositions();
    LogSubmitted();
    std::lock_guard<std::mutex> lock(g_mutex);
    g_spawnNotice = notice;
  }

  g_anyEvalTick.store(GetTickCount64());
  LinkCloneVehicle(owner);
  LinkAllCloneControllers();
  ApplyCollision(owner);
  bool frozenClone = false;
  if (time != nullptr && (g_clonePaused.load() || g_freeze.active || g_freeze.accum != 0.0)) {
    const uint8_t *cloneSlot = Read<const uint8_t *>(owner, kOwnerSlot);
    if (cloneSlot != nullptr) {
      frozenClone = ApplyCloneFreeze(owner, static_cast<uint64_t *>(time));
    }
  }
  const int result = g_originalEvaluate(owner, time, arg, out);
  if (frozenClone && result == 0 && out != nullptr && out[kOutValid] != 0) {
    char note[200];
    const float *v = reinterpret_cast<const float *>(out + 0x40);
    std::snprintf(note, sizeof(note), "FREEZE-OUT own=%p vel_antes=%.3f,%.3f,%.3f pos=%.2f,%.2f,%.2f",
                  owner, v[0], v[1], v[2], Read<float>(out, kOutPosition),
                  Read<float>(out, kOutPosition + 4), Read<float>(out, kOutPosition + 8));
    if (GhostTrace::Enabled()) GhostTrace::Note(note);
    std::memset(out + 0x40, 0, 12); // velocidade 0: o corpo nao desliza
  }
  if (GhostTrace::Enabled()) GhostTrace::OnEvaluate(owner, time, result, out);
  if (result == 0 && out != nullptr && out[kOutValid] != 0) RecordOwnerPos(owner, out);
  if (result != 0 || out == nullptr || out[kOutValid] == 0) return result;

  // A referencia e a fonte das copias (ou o 1o slot pronto, sem copias).
  const uint8_t *slot = Read<const uint8_t *>(owner, kOwnerSlot);
  const std::vector<uint8_t *> slots = CollectSlots();
  const uint8_t *reference = nullptr;
  int ready = 0;
  for (uint8_t *s : slots) {
    if (!IsReady(s)) continue;
    if (reference == nullptr || s == g_source) reference = s;
    ++ready;
  }
  if (slot != reference) return result;

  std::lock_guard<std::mutex> lock(g_mutex);
  CapturePath(slot);
  std::memcpy(g_ghostPos, out + kOutPosition, sizeof(g_ghostPos));
  g_lastEvalTick = GetTickCount64();
  g_slotCount = static_cast<int>(slots.size());
  g_readyCount = ready;
  return result;
}

// Objetos de render de carro vistos no packer (diagnostico do F7).
std::atomic<uint64_t> g_packSeenObj[32];
std::atomic<uint64_t> g_packSeenTick[32];

void NotePackedObject(uint8_t *renderObj) {
  const uint64_t key = reinterpret_cast<uint64_t>(renderObj);
  const uint64_t now = GetTickCount64();
  for (int i = 0; i < 32; ++i) {
    if (g_packSeenObj[i].load() == key) { g_packSeenTick[i].store(now); return; }
  }
  for (int i = 0; i < 32; ++i) {
    uint64_t expected = 0;
    if (g_packSeenObj[i].compare_exchange_strong(expected, key)) {
      g_packSeenTick[i].store(now);
      return;
    }
  }
}

void DetourPackOpacity(uint8_t *renderObj, int channel) {
  InFlight guard;
  if (renderObj != nullptr && channel == 0) NotePackedObject(renderObj);
  const bool opaque = g_opaque.load() && renderObj != nullptr;
  // Abaixo de 1 o desenho nao descarta o carro; a opacidade e forcada abaixo.
  if (opaque && channel == 0 && Read<float>(renderObj, kFadeFactor) >= 1.f) {
    std::memcpy(renderObj + kFadeFactor, &kFadeBelowHidden,
                sizeof(kFadeBelowHidden));
  }
  g_originalPack(renderObj, channel);
  if (!opaque) return;
  // w >= 0,5 marca o carro que o packer acabou de deixar transparente.
  // O jogador sai daqui com w = 0 e nao e reescrito.
  uint8_t *entry = renderObj + kGhostCarValues;
  if (Read<float>(entry, 0x0c) < 0.5f) return;
  const float one = 1.f;
  const float zero = 0.f;
  std::memcpy(entry, &one, sizeof(one));
  std::memcpy(entry + 0x0c, &zero, sizeof(zero));
  if (!g_opacityLogged) {
    g_opacityLogged = true;
    Logger::Info("GhostLab: fantasma solido (GhostCarValues.x = 1).");
  }
}

// A troca do tipo durante o desenho fica desligada: a fisica pode ler o 0 de
// outra thread (suspeita de batida leve na largada, 2026-10-02), e o fantasma
// solido ja vem do fator abaixo de 1 no packer.
constexpr bool kSpoofSubmitType = false;

// Indices de veiculo vistos na submissao de desenho (diagnostico do F7).
std::atomic<uint64_t> g_submitSeen[32];
std::atomic<uint32_t> g_submitCalls{0};

void LogSubmitted() {
  const uint64_t now = GetTickCount64();
  std::string line = "GhostLab[limite]: objetos de render de carro no packer (ultimos 500 ms):";
  int n = 0;
  for (int i = 0; i < 32; ++i) {
    const uint64_t t = g_packSeenTick[i].load();
    if (g_packSeenObj[i].load() != 0 && now - t < 500) {
      char one[32];
      std::snprintf(one, sizeof(one), " %llx",
                    static_cast<unsigned long long>(g_packSeenObj[i].load()));
      line += one;
      ++n;
    }
  }
  line += " (total " + std::to_string(n) + ")";
  (void)g_submitSeen;
  (void)g_submitCalls;
  Logger::Info(line);
}

void DetourSubmitCar(uint8_t *renderObj, uint8_t *context, void *pass,
                     void *extra) {
  InFlight guard;
  if (UsablePointer(reinterpret_cast<uintptr_t>(renderObj))) {
    const int index = Read<int>(renderObj, 0);
    if (index >= 0 && index < 32) g_submitSeen[index].store(GetTickCount64());
    g_submitCalls.fetch_add(1);
  }
  uint8_t *vehicle = nullptr;
  uint32_t savedType = 0;
  bool spoofed = false;
  if (kSpoofSubmitType && g_opaque.load() &&
      UsablePointer(reinterpret_cast<uintptr_t>(renderObj)) &&
      UsablePointer(reinterpret_cast<uintptr_t>(context))) {
    const int index = Read<int>(renderObj, 0);
    const uint8_t *table = Read<const uint8_t *>(context, 0xf8);
    if (index >= 0 && index < 32 &&
        UsablePointer(reinterpret_cast<uintptr_t>(table))) {
      vehicle = Read<uint8_t *>(table, static_cast<size_t>(index) * 8);
      if (UsablePointer(reinterpret_cast<uintptr_t>(vehicle)) &&
          Read<uintptr_t>(vehicle, 0) == g_gameBase + kVehicleVtableRva &&
          Read<uint32_t>(vehicle, kVehicleType) == kVehicleTypeGhost) {
        savedType = kVehicleTypeGhost;
        const uint32_t physical = 0;
        std::memcpy(vehicle + kVehicleType, &physical, sizeof(physical));
        spoofed = true;
      }
    }
  }
  g_originalSubmit(renderObj, context, pass, extra);
  if (spoofed) {
    std::memcpy(vehicle + kVehicleType, &savedType, sizeof(savedType));
  }
}

void DetourMakeGhostMaterials(void *self, void *model) {
  InFlight guard;
  if (g_opaque.load()) {
    Logger::Info("GhostLab: carro fantasma criado sem materiais *_ghost (opaco).");
    return;
  }
  g_originalMakeGhost(self, model);
}

// Lista da sessao: tipo (+0x2c) e o byte +0xb4 que faz o 2o passe pular a entrada.
// Fotografia dos limites que o teste de N carros pode estourar: corpos de
// fisica ativos (lista de 24), entradas da sessao (pool de 150), vetor de
// registros (capacidade), mapa de controladores e carros desenhados.
constexpr uintptr_t kPhysicsBodiesRva = 0x14201b930 - kImageBase;
constexpr size_t kSessionVec = 0x3320; // vetor de registros da sessao: dados, +8 capacidade, +0x10 contagem
int g_fillTarget = 5;                  // alvo do enchimento (imediato dos mov r13d, 5)
void LogLimits(const char *when) {
  uint8_t *session = Read<uint8_t *>(reinterpret_cast<const uint8_t *>(g_gameBase + kSessionGlobalRva), 0);
  if (!UsablePointer(reinterpret_cast<uintptr_t>(session))) return;
  const uint8_t *manager = ManagerPtr();
  char buf[320];
  std::snprintf(buf, sizeof(buf),
                "GhostLab[limites] %s: corpos de fisica=%d/%d (teto 24), entradas da sessao=%u "
                "(pool 150), vetor sessao=%llu/%llu, mapa de controladores=%llu, carros "
                "desenhados=%d, alvo do enchimento=%d, N pedido=%d.",
                when, Read<int32_t>(reinterpret_cast<const uint8_t *>(g_gameBase + kPhysicsBodiesRva), 0),
                Read<int32_t>(reinterpret_cast<const uint8_t *>(g_gameBase + kPhysicsBodiesRva), 4),
                Read<uint32_t>(session, 0x40),
                static_cast<unsigned long long>(Read<uint64_t>(session, kSessionVec + 0x10)),
                static_cast<unsigned long long>(Read<uint64_t>(session, kSessionVec + 8)),
                static_cast<unsigned long long>(manager != nullptr ? Read<uint64_t>(manager, kManagerMapSize) : 0),
                CountDrawnGhostCars(), g_fillTarget, WantedGhostCars());
  Logger::Info(buf);
}

void LogStageEntries(const char *when) {
  uint8_t *session = Read<uint8_t *>(reinterpret_cast<const uint8_t *>(g_gameBase + kSessionGlobalRva), 0);
  if (!UsablePointer(reinterpret_cast<uintptr_t>(session))) return;
  const uint32_t count = Read<uint32_t>(session, 0x40);
  uint8_t *entries = Read<uint8_t *>(session, 0x30);
  if (count > 64 || !UsablePointer(reinterpret_cast<uintptr_t>(entries))) return;
  std::string line = std::string("GhostLab[limite]: lista da sessao ") + when + ": " +
                     std::to_string(count) + " entradas [tipo/b4]";
  for (uint32_t i = 0; i < count; ++i) {
    const uint8_t *e = Read<const uint8_t *>(entries, i * 8);
    if (!UsablePointer(reinterpret_cast<uintptr_t>(e))) continue;
    line += " " + std::to_string(Read<uint32_t>(e, 0x2c)) + "/" +
            std::to_string(e[0xb4]);
  }
  Logger::Info(line);
}

// Teste de limite: o 2o passe de SpawnStageVehicles pula a entrada de fantasma
// com +0xb4 = 1 (as entradas 3 a 5 chegam assim). Se existir
// dr2hook_ghost_cars.txt com N, desmarca ate haver N entradas de fantasma
// liberadas; sem arquivo, nada muda.
void UnflagGhostEntries() {
  int want = 0;
  if (FILE *f = std::fopen("dr2hook_ghost_cars.txt", "r")) {
    if (std::fscanf(f, "%d", &want) != 1) want = 0;
    std::fclose(f);
  }
  if (want <= 0) return;
  uint8_t *session = Read<uint8_t *>(reinterpret_cast<const uint8_t *>(g_gameBase + kSessionGlobalRva), 0);
  if (!UsablePointer(reinterpret_cast<uintptr_t>(session))) return;
  const uint32_t count = Read<uint32_t>(session, 0x40);
  uint8_t *entries = Read<uint8_t *>(session, 0x30);
  if (count > 64 || !UsablePointer(reinterpret_cast<uintptr_t>(entries))) return;
  int open = 0, cleared = 0;
  for (uint32_t i = 0; i < count; ++i) {
    uint8_t *e = Read<uint8_t *>(entries, i * 8);
    if (!UsablePointer(reinterpret_cast<uintptr_t>(e)) || Read<uint32_t>(e, 0x2c) != 1) continue;
    if (e[0xb4] == 0) { ++open; continue; }
    if (open >= want) break;
    e[0xb4] = 0;
    ++open;
    ++cleared;
  }
  Logger::Info("GhostLab[limite]: pedidos " + std::to_string(want) +
               " carros fantasma; " + std::to_string(cleared) + " entrada(s) liberada(s).");
}

// Vigia do teste: se a carga travar depois do spawn de teste, mostra onde cada
// thread esta (RIP + retornos do jogo na pilha) 25 s depois. So roda com o
// arquivo de teste ativo.
bool Readable(uintptr_t a, size_t n) {
  MEMORY_BASIC_INFORMATION mbi{};
  if (VirtualQuery(reinterpret_cast<void *>(a), &mbi, sizeof(mbi)) == 0) return false;
  if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
  return a + n <= reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
}

void DumpThreads() {
  const DWORD self = GetCurrentThreadId();
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snap == INVALID_HANDLE_VALUE) return;
  THREADENTRY32 te{};
  te.dwSize = sizeof(te);
  int shown = 0;
  for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
    if (te.th32OwnerProcessID != GetCurrentProcessId() || te.th32ThreadID == self) continue;
    HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, te.th32ThreadID);
    if (th == nullptr) continue;
    if (SuspendThread(th) != static_cast<DWORD>(-1)) {
      CONTEXT c{};
      c.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
      if (GetThreadContext(th, &c)) {
        std::string chain;
        int found = 0;
        for (uintptr_t sp = c.Rsp; found < 20 && sp < c.Rsp + 0x6000; sp += 8) {
          if (!Readable(sp, 8)) break;
          const uintptr_t v = *reinterpret_cast<const uintptr_t *>(sp);
          if (v < g_gameBase + 0x1000 || v >= g_gameBase + 0x1099000) continue;
          const uint8_t *r = reinterpret_cast<const uint8_t *>(v);
          if (r[-5] != 0xe8 && !(r[-6] == 0xff && r[-5] == 0x15) && r[-2] != 0xff) continue;
          char one[24];
          std::snprintf(one, sizeof(one), " %llx", static_cast<unsigned long long>(v));
          chain += one;
          ++found;
        }
        const bool inExe = c.Rip >= g_gameBase && c.Rip < g_gameBase + 0x1099000;
        if (found > 0 || inExe) {
          char buf[200];
          std::snprintf(buf, sizeof(buf), "GhostLab[espera]: thread %lu rip=%llx%s retornos:",
                        static_cast<unsigned long>(te.th32ThreadID),
                        static_cast<unsigned long long>(c.Rip), inExe ? " (exe)" : "");
          Logger::Info(std::string(buf) + chain);
          ++shown;
        }
      }
      ResumeThread(th);
    }
    CloseHandle(th);
  }
  CloseHandle(snap);
  Logger::Info("GhostLab[espera]: " + std::to_string(shown) + " thread(s) com chamadas do jogo.");
}

// Teto do experimento de limite (carros fantasma pedidos no arquivo de teste).
constexpr int kMaxGhostCars = 32;

// Quantos carros fantasma pedir (arquivo de teste); 0 = comportamento do jogo.
int WantedGhostCars() {
  int want = 0;
  if (FILE *f = std::fopen("dr2hook_ghost_cars.txt", "r")) {
    if (std::fscanf(f, "%d", &want) != 1) want = 0;
    std::fclose(f);
  }
  return std::clamp(want, 0, kMaxGhostCars);
}

uint8_t g_realRecord[kGhostRecordSize];
bool g_haveRealRecord = false;
bool g_haveTypeZero = false;
uint8_t g_fillerRecords[kMaxGhostCars][kGhostRecordSize];
int g_realSeen = 0;
int g_fillerUsed = 0;

// ---- Mais de 5 carros (ghost-vectors-scan.md, secao 6) ----
// Os vetores de registros de 0x38 bytes da sessao (+0x3320) e da montagem
// (b+0x00, na pilha de 0x1405ba200) tem capacidade 5 e armazenamento embutido;
// ninguem os libera, entao podem apontar para um buffer externo. O alvo do
// enchimento (`5 - n`) e o imediato de dois `mov r13d, 5`.
constexpr size_t kVecData = 0x00, kVecCap = 0x08, kVecCount = 0x10, kVecInline = 0x20;

constexpr size_t kSessionVecInline = 0x3340; // embutido
constexpr size_t kGhostVecElem = 0x38;
constexpr uintptr_t kFillImmA = 0x1405ba802 - kImageBase; // mov r13d, 5 (lista vazia)
constexpr uintptr_t kFillImmB = 0x1405ba989 - kImageBase; // mov r13d, 5 (normal)


// Memoria do buffer sobrevive ao F8 (o core pode ser descarregado com a sessao
// ainda apontando para ele).
uint8_t *AllocVector() {
  void *p = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  return static_cast<uint8_t *>(p);
}

// Move um vetor de 0x38 do embutido para um buffer externo com capacidade
// kMaxGhostCars: copia os elementos, grava o ponteiro e so depois a capacidade.
bool ExpandVector(uint8_t *vec, size_t dataOff, size_t capOff, size_t countOff,
                  const uint8_t *inlineBuf, const char *name) {
  if (Read<const uint8_t *>(vec, dataOff) != inlineBuf || Read<uint64_t>(vec, capOff) != 5) {
    return false;
  }
  uint8_t *buffer = AllocVector();
  if (buffer == nullptr) return false;
  const uint64_t count = std::min<uint64_t>(Read<uint64_t>(vec, countOff), 5);
  std::memcpy(buffer, inlineBuf, count * kGhostVecElem);
  const uint64_t cap = kMaxGhostCars;
  std::memcpy(vec + dataOff, &buffer, sizeof(buffer));
  std::memcpy(vec + capOff, &cap, sizeof(cap));
  Logger::Info(std::string("GhostLab[limite]: vetor de registros (") + name +
               ") movido para buffer externo, capacidade " + std::to_string(kMaxGhostCars) +
               " (" + std::to_string(count) + " elemento(s) copiados).");
  return true;
}

void ExpandGhostVectors(uint8_t *assemblyVec) {
  uint8_t *session = Read<uint8_t *>(reinterpret_cast<const uint8_t *>(g_gameBase + kSessionGlobalRva), 0);
  if (UsablePointer(reinterpret_cast<uintptr_t>(session))) {
    ExpandVector(session, kSessionVec, kSessionVec + 8, kSessionVec + 0x10,
                 session + kSessionVecInline, "sessao");
  }
  if (UsablePointer(reinterpret_cast<uintptr_t>(assemblyVec))) {
    ExpandVector(assemblyVec, kVecData, kVecCap, kVecCount, assemblyVec + kVecInline,
                 "montagem");
  }
}

// Troca o 5 dos dois `mov r13d, 5` do enchimento. So aceita 5..kMaxGhostCars.
void SetFillTarget(int target) {
  target = std::clamp(target, 5, kMaxGhostCars);
  if (target == g_fillTarget) return;
  for (uintptr_t rva : {kFillImmA, kFillImmB}) {
    uint8_t *imm = reinterpret_cast<uint8_t *>(g_gameBase + rva);
    // O opcode `41 bd` esta 2 bytes antes do imediato.
    if (imm[-2] != 0x41 || imm[-1] != 0xbd) {
      Logger::Warn("GhostLab[limite]: opcode do alvo do enchimento diferente do esperado; ignorado.");
      return;
    }
  }
  DWORD old = 0;
  for (uintptr_t rva : {kFillImmA, kFillImmB}) {
    uint8_t *imm = reinterpret_cast<uint8_t *>(g_gameBase + rva);
    if (!VirtualProtect(imm, 4, PAGE_EXECUTE_READWRITE, &old)) continue;
    const int32_t value = target;
    std::memcpy(imm, &value, sizeof(value));
    FlushInstructionCache(GetCurrentProcess(), imm, 4);
    VirtualProtect(imm, 4, old, &old);
  }
  g_fillTarget = target;
  Logger::Info("GhostLab[limite]: alvo do enchimento = " + std::to_string(target) + ".");
}

// Troca o registro de enchimento (+0x08 nulo) por copia de um registro real:
// a entrada nasce sem +0xb4 e o spawn cria o carro.
uintptr_t DetourAddGhostEntry(void *a, void *b, void *c, uintptr_t d, uint8_t *record,
                              uintptr_t f) {
  InFlight guard;
  const int wantCars = WantedGhostCars();
  if (wantCars > 0) {
    SetFillTarget(wantCars); // 5 ou mais; 5 deixa o jogo como esta
    if (wantCars > 5) ExpandGhostVectors(static_cast<uint8_t *>(b));
  }
  if (UsablePointer(reinterpret_cast<uintptr_t>(record))) {
    if (Read<uintptr_t>(record, 0x08) != 0) {
      if (g_realSeen == 0) {
        g_fillerUsed = 0; // comeco de uma carga
        g_haveTypeZero = false;
      }
      ++g_realSeen;
      // Prefere copiar o registro de tipo 0 (fantasma proprio, nasce com o desenho
      // ligado); o de tipo 2 (RecordingGhost) nasce oculto.
      const bool typeZero = record[0xb0] == 0;
      char info[160];
      std::snprintf(info, sizeof(info),
                    "GhostLab[limite]: registro real #%d: +0x00=%u +0x01=%u tipo(+0xb0)=%u id=%llx",
                    g_realSeen, record[0], record[1], record[0xb0],
                    static_cast<unsigned long long>(Read<uint64_t>(record, 0x08)));
      Logger::Info(info);
      if (typeZero || !g_haveTypeZero) {
        std::memcpy(g_realRecord, record, kGhostRecordSize);
        g_haveRealRecord = true;
        g_haveTypeZero = typeZero;
      }
    } else {
      const int want = WantedGhostCars();
      if (want > 0 && g_haveRealRecord && g_realSeen + g_fillerUsed < want &&
          g_fillerUsed < kMaxGhostCars) {
        uint8_t *copy = g_fillerRecords[g_fillerUsed++];
        std::memcpy(copy, g_realRecord, kGhostRecordSize);
        Logger::Info("GhostLab[limite]: registro vazio trocado por copia de um real (carro " +
                     std::to_string(g_realSeen + g_fillerUsed) + " de " +
                     std::to_string(want) + ").");
        record = copy;
      }
    }
  }
  return g_originalAddEntry(a, b, c, d, record, f);
}

// Estado da tela de carga (leituras sugeridas na analise do Grok).
void LogLoadState() {
  const uint8_t *ctx = static_cast<const uint8_t *>(g_stageCtx);
  if (!UsablePointer(reinterpret_cast<uintptr_t>(ctx))) return;
  char buf[200];
  std::snprintf(buf, sizeof(buf),
                "GhostLab[espera]: carga +0x80=%u +0xae=%u +0xc0=%u",
                Read<uint32_t>(ctx, 0x80), ctx[0xae], Read<uint32_t>(ctx, 0xc0));
  Logger::Info(buf);
  const uint8_t *owner = Read<const uint8_t *>(ctx, 0x28);
  if (!UsablePointer(reinterpret_cast<uintptr_t>(owner))) return;
  const uint8_t *table = Read<const uint8_t *>(owner, 0x8);
  const int index = Read<int32_t>(reinterpret_cast<const uint8_t *>(g_gameBase + kVehicleSystemIndexRva), 0);
  if (!UsablePointer(reinterpret_cast<uintptr_t>(table)) || index < 0 || index > 64) return;
  const uint8_t *system = Read<const uint8_t *>(table, index * 8);
  if (!UsablePointer(reinterpret_cast<uintptr_t>(system))) return;
  const uint8_t *begin = Read<const uint8_t *>(system, 0xf8);
  const uint8_t *end = Read<const uint8_t *>(system, 0x100);
  if (!UsablePointer(reinterpret_cast<uintptr_t>(begin)) || end < begin || end - begin > 8 * 32) return;
  for (const uint8_t *p = begin; p < end; p += 8) {
    const uint8_t *v = Read<const uint8_t *>(p, 0);
    if (!UsablePointer(reinterpret_cast<uintptr_t>(v))) continue;
    std::snprintf(buf, sizeof(buf),
                  "GhostLab[espera]: veiculo %p +0x30=%p +0x38=%p tipo=%u", v,
                  Read<const void *>(v, 0x30), Read<const void *>(v, 0x38),
                  Read<uint32_t>(v, 0xbc));
    Logger::Info(buf);
  }
}

void DetourSpawnVehicles(void *ctx) {
  InFlight guard;
  g_stageCtx = ctx;
  g_realSeen = 0;
  LogStageEntries("antes do spawn");
  g_originalSpawn(ctx);
  LogStageEntries("depois do spawn");
  LogLimits("depois do spawn");
  if (std::FILE *f = std::fopen("dr2hook_ghost_cars.txt", "r")) {
    std::fclose(f);
    std::thread([] {
      Sleep(8000);
      LogLimits("8 s depois");
      Sleep(17000);
      LogLimits("25 s depois");
      LogLoadState();
      DumpThreads();
    }).detach();
  }
}

// Diagnostico de crash: registra violacoes de acesso (e afins) com o RIP, o
// endereco acessado e os retornos do jogo na pilha. Nao trata a excecao.
void *g_crashHandler = nullptr;
std::atomic<int> g_crashLogged{0};

LONG CALLBACK CrashLogger(EXCEPTION_POINTERS *info) {
  const DWORD code = info->ExceptionRecord->ExceptionCode;
  if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
      code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_STACK_OVERFLOW &&
      code != EXCEPTION_PRIV_INSTRUCTION) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  if (g_crashLogged.fetch_add(1) >= 12) return EXCEPTION_CONTINUE_SEARCH;
  const CONTEXT *c = info->ContextRecord;
  const uintptr_t base = g_gameBase;
  char buf[320];
  std::snprintf(buf, sizeof(buf),
                "GhostLab[crash]: excecao 0x%08lx rip=0x%llx (exe+0x%llx) acesso=0x%llx tipo=%llu thread=%lu",
                static_cast<unsigned long>(code),
                static_cast<unsigned long long>(c->Rip),
                static_cast<unsigned long long>(c->Rip - base),
                static_cast<unsigned long long>(info->ExceptionRecord->NumberParameters > 1 ? info->ExceptionRecord->ExceptionInformation[1] : 0),
                static_cast<unsigned long long>(info->ExceptionRecord->NumberParameters > 0 ? info->ExceptionRecord->ExceptionInformation[0] : 0),
                static_cast<unsigned long>(GetCurrentThreadId()));
  Logger::Error(buf);
  std::snprintf(buf, sizeof(buf),
                "GhostLab[crash]: rax=%llx rbx=%llx rcx=%llx rdx=%llx rsi=%llx rdi=%llx r8=%llx r9=%llx rsp=%llx",
                static_cast<unsigned long long>(c->Rax), static_cast<unsigned long long>(c->Rbx),
                static_cast<unsigned long long>(c->Rcx), static_cast<unsigned long long>(c->Rdx),
                static_cast<unsigned long long>(c->Rsi), static_cast<unsigned long long>(c->Rdi),
                static_cast<unsigned long long>(c->R8), static_cast<unsigned long long>(c->R9),
                static_cast<unsigned long long>(c->Rsp));
  Logger::Error(buf);
  const NT_TIB *tib = reinterpret_cast<const NT_TIB *>(NtCurrentTeb());
  const uintptr_t top = reinterpret_cast<uintptr_t>(tib->StackBase);
  std::string chain = "GhostLab[crash]: pilha (retornos no exe):";
  int found = 0;
  for (uintptr_t sp = c->Rsp; sp + 8 <= top && sp < c->Rsp + 0x8000 && found < 24; sp += 8) {
    const uintptr_t v = *reinterpret_cast<const uintptr_t *>(sp);
    if (v < base + 0x1000 || v >= base + 0x1099000) continue;
    const uint8_t *ret = reinterpret_cast<const uint8_t *>(v);
    if (ret[-5] != 0xe8 && !(ret[-6] == 0xff && ret[-5] == 0x15) && ret[-2] != 0xff) continue;
    char one[40];
    std::snprintf(one, sizeof(one), " 0x%llx", static_cast<unsigned long long>(v));
    chain += one;
    ++found;
  }
  Logger::Error(chain);
  return EXCEPTION_CONTINUE_SEARCH;
}

bool Hook(uintptr_t rva, const uint8_t *prologue, size_t size, void *detour,
          void **original, void **target, const char *name) {
  void *fn = reinterpret_cast<void *>(g_gameBase + rva);
  if (std::memcmp(fn, prologue, size) != 0) {
    Logger::Warn(std::string("GhostLab: prologo de ") + name +
                 " diferente do esperado; hook abortado.");
    return false;
  }
  if (MH_CreateHook(fn, detour, original) != MH_OK) {
    Logger::Error(std::string("GhostLab: falha ao criar hook de ") + name);
    return false;
  }
  if (MH_EnableHook(fn) != MH_OK) {
    MH_RemoveHook(fn);
    Logger::Error(std::string("GhostLab: falha ao habilitar hook de ") + name);
    return false;
  }
  *target = fn;
  return true;
}

#endif

} // namespace

bool GhostLab::Install(uintptr_t gameBase) {
#if defined(_WIN32)
  if (gameBase == 0) return false;
  g_gameBase = gameBase;
  const MH_STATUS init = MH_Initialize();
  if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
    Logger::Error("GhostLab: MH_Initialize falhou.");
    return false;
  }
  const bool evaluate =
      Hook(kEvaluateRva, kEvaluatePrologue, sizeof(kEvaluatePrologue),
           reinterpret_cast<void *>(&DetourEvaluate),
           reinterpret_cast<void **>(&g_originalEvaluate), &g_evaluateTarget,
           "EvaluateGhostState");
  const bool materials = Hook(
      kMakeGhostMaterialsRva, kMakeGhostMaterialsPrologue,
      sizeof(kMakeGhostMaterialsPrologue),
      reinterpret_cast<void *>(&DetourMakeGhostMaterials),
      reinterpret_cast<void **>(&g_originalMakeGhost), &g_makeGhostTarget,
      "MakeGhostMaterials");
  const bool pack = Hook(
      kPackOpacityRva, kPackOpacityPrologue, sizeof(kPackOpacityPrologue),
      reinterpret_cast<void *>(&DetourPackOpacity),
      reinterpret_cast<void **>(&g_originalPack), &g_packTarget, "PackOpacity");
  const bool submit = Hook(
      kSubmitCarRva, kSubmitCarPrologue, sizeof(kSubmitCarPrologue),
      reinterpret_cast<void *>(&DetourSubmitCar),
      reinterpret_cast<void **>(&g_originalSubmit), &g_submitTarget,
      "SubmitCar");
  Hook(kSpawnVehiclesRva, kSpawnVehiclesPrologue, sizeof(kSpawnVehiclesPrologue),
       reinterpret_cast<void *>(&DetourSpawnVehicles),
       reinterpret_cast<void **>(&g_originalSpawn), &g_spawnTarget,
       "SpawnStageVehicles");
  Hook(kAddGhostEntryRva, kAddGhostEntryPrologue, sizeof(kAddGhostEntryPrologue),
       reinterpret_cast<void *>(&DetourAddGhostEntry),
       reinterpret_cast<void **>(&g_originalAddEntry), &g_addEntryTarget,
       "AddGhostEntry");
  if (g_crashHandler == nullptr) {
    g_crashHandler = AddVectoredExceptionHandler(1, CrashLogger);
    g_crashLogged.store(0);
  }
  const bool sleep = Hook(
      kBodySleepRva, kBodySleepPrologue, sizeof(kBodySleepPrologue),
      reinterpret_cast<void *>(&DetourBodySleep),
      reinterpret_cast<void **>(&g_originalBodySleep), &g_bodySleepTarget,
      "BodySleep");
  Logger::Info(std::string("GhostLab: hooks (EvaluateGhostState ") +
               (evaluate ? "ok" : "FALHOU") + ", materiais " +
               (materials ? "ok" : "FALHOU") + ", opacidade " +
               (pack ? "ok" : "FALHOU") + ", submissao " +
               (submit ? "ok" : "FALHOU") + ", colisao " +
               (sleep ? "ok" : "FALHOU") + ").");
  return evaluate || materials || pack || submit;
#else
  (void)gameBase;
  return false;
#endif
}

void GhostLab::Shutdown() {
#if defined(_WIN32)
  SetFillTarget(5); // sem o hook, o enchimento ate N estouraria o vetor de 5
  for (void *target : {g_evaluateTarget, g_makeGhostTarget, g_packTarget,
                       g_submitTarget, g_bodySleepTarget, g_spawnTarget, g_addEntryTarget}) {
    if (target != nullptr) MH_DisableHook(target);
  }
  if (g_crashHandler != nullptr) {
    RemoveVectoredExceptionHandler(g_crashHandler);
    g_crashHandler = nullptr;
  }
  for (int waited = 0; g_inFlight.load() > 0 && waited < 2000; ++waited) {
    Sleep(1);
  }
  for (void **target : {&g_evaluateTarget, &g_makeGhostTarget, &g_packTarget,
                        &g_submitTarget, &g_bodySleepTarget, &g_spawnTarget, &g_addEntryTarget}) {
    if (*target != nullptr) {
      MH_RemoveHook(*target);
      *target = nullptr;
    }
  }
#endif
  std::lock_guard<std::mutex> lock(g_mutex);
  g_path.clear();
  g_pathSlot = nullptr;
  g_pathCount = 0;
  g_status = Status{};
}

void GhostLab::Update() {
#if defined(_WIN32)
  std::lock_guard<std::mutex> lock(g_mutex);
  if (GetTickCount64() - g_anyEvalTick.load() > 500) RevertCollision();
  Status status;
  status.slots = g_slotCount;
  status.readySlots = g_readyCount;
  // Pausado o jogo nao avalia fantasmas: mantem o ultimo valor.
  const bool fresh = GetTickCount64() - g_lastEvalTick < 3000;
  if (!fresh || g_path.size() < 2 || Player::GetVehicleAddress() == 0) {
    g_status = status;
    return;
  }
  CarState car{};
  if (!Player::CaptureState(car, true)) {
    g_status = status;
    return;
  }
  const float playerPos[3] = {car.position.x, car.position.y, car.position.z};
  float tPlayer = 0.f, sPlayer = 0.f, dPlayer = 0.f;
  float tGhost = 0.f, sGhost = 0.f, dGhost = 0.f;
  if (!Project(g_path, playerPos, g_playerHint, tPlayer, sPlayer, dPlayer) ||
      !Project(g_path, g_ghostPos, g_ghostHint, tGhost, sGhost, dGhost)) {
    g_status = status;
    return;
  }
  status.active = true;
  status.lapSeconds = g_path.back().t / 1000.f;
  status.ghostSeconds = tGhost / 1000.f;
  status.playerSeconds = tPlayer / 1000.f;
  status.deltaSeconds = (tGhost - tPlayer) / 1000.f;
  status.gapMeters = sGhost - sPlayer;
  status.offTrackMeters = dPlayer;
  g_status = status;
  // Tempo do fantasma desde o inicio do trajeto: no Reiniciar volta a ~0.
#endif
}

GhostLab::Status GhostLab::GetStatus() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_status;
}

void GhostLab::RequestClones(int count, float stepSeconds) {
#if defined(_WIN32)
  g_cloneStepMs.store(static_cast<int>(std::max(0.f, stepSeconds) * 1000.f));
  g_cloneCount.store(std::clamp(count, 0, 16));
  Logger::Info("GhostLab: clones pedidos (" + std::to_string(count) +
               "); aplicados no proximo frame da especial.");
#else
  (void)count;
  (void)stepSeconds;
#endif
}

bool GhostLab::TakeSpawnNotice(std::string &out) {
#if defined(_WIN32)
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_spawnNotice.empty()) return false;
  out.swap(g_spawnNotice);
  g_spawnNotice.clear();
  return true;
#else
  (void)out;
  return false;
#endif
}

bool GhostLab::TestModeActive() {
#if defined(_WIN32)
  if (std::FILE *f = std::fopen("dr2hook_ghost_cars.txt", "r")) {
    std::fclose(f);
    return true;
  }
#endif
  return false;
}

int GhostLab::SpawnClone() {
#if defined(_WIN32)
  g_spawnPending.store(true);
  return 0;
#else
  return 0;
#endif
}

void GhostLab::SetOpaque(bool opaque) {
  if (g_opaque.exchange(opaque) != opaque) {
    g_opacityLogged = false;
    Logger::Info(std::string("GhostLab: fantasma ") +
                 (opaque ? "solido (opacidade 1, sem aura)"
                         : "transparente de novo") +
                 ".");
  }
}

bool GhostLab::IsOpaque() { return g_opaque.load(); }

void GhostLab::OnStageLoad() {
  g_collide.store(false);
  // Os carros da especial anterior somem com ela.
  std::lock_guard<std::mutex> lock(g_collideMutex);
  g_collidingVehicles.clear();
  for (auto &slot : g_solidBodies) slot.store(nullptr);
}

void GhostLab::OnStageStart() { g_collide.store(false); }

bool GhostLab::ToggleClonePause() {
  const bool paused = !g_clonePaused.load();
  g_clonePaused.store(paused);
  Logger::Info(std::string("GhostLab[pausa]: F6 -> pedido de pausa dos clones ") +
               (paused ? "ligado." : "desligado."));
  return paused;
}

bool GhostLab::ClonesPaused() { return g_clonePaused.load(); }

bool GhostLab::ToggleCollision() {
  const bool collide = !g_collide.load();
  g_collide.store(collide);
  Logger::Info(std::string("GhostLab: ToggleCollision -> pedido de colisao ") +
               (collide ? "ligado." : "desligado."));
  return collide;
}

bool GhostLab::SetTimeOffset(float seconds) {
#if defined(_WIN32)
  if (g_gameBase == 0) return false;
  *reinterpret_cast<volatile float *>(g_gameBase + kTimeOffsetRva) = seconds;
  return true;
#else
  (void)seconds;
  return false;
#endif
}

void GhostLab::SetHudVisible(bool visible) { g_hud.store(visible); }
bool GhostLab::IsHudVisible() { return g_hud.load(); }

} // namespace dr2hook
