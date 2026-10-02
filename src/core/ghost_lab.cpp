#include "dr2hook/ghost_lab.h"
#include "dr2hook/logger.h"
#include "dr2hook/player.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <MinHook.h>
#include <windows.h>
#endif

namespace dr2hook {
namespace {

std::atomic<bool> g_opaque{false};
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

using EvaluateFn = int (*)(uint8_t *owner, void *time, void *arg, uint8_t *out);
using MakeGhostMaterialsFn = void (*)(void *self, void *model);
using CopyLapFn = void (*)(uint8_t *dest, const uint8_t *src, bool realloc);

uintptr_t g_gameBase = 0;
EvaluateFn g_originalEvaluate = nullptr;
MakeGhostMaterialsFn g_originalMakeGhost = nullptr;
void *g_evaluateTarget = nullptr;
void *g_makeGhostTarget = nullptr;

std::atomic<int> g_inFlight{0};
struct InFlight {
  InFlight() { g_inFlight.fetch_add(1); }
  ~InFlight() { g_inFlight.fetch_sub(1); }
};

// -1 = nada pendente.
std::atomic<int> g_cloneCount{-1};
std::atomic<int> g_cloneStepMs{0};

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

void ApplyClones(int count, int stepMs) {
  const std::vector<uint8_t *> slots = CollectSlots();
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
  char msg[160];
  std::snprintf(msg, sizeof(msg),
                "GhostLab: %d copia(s) a cada %.1f s (slots no gerenciador: %zu).",
                made, stepMs / 1000.0, slots.size());
  Logger::Info(msg);
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

int DetourEvaluate(uint8_t *owner, void *time, void *arg, uint8_t *out) {
  InFlight guard;
  const int pending = g_cloneCount.exchange(-1);
  if (pending >= 0) ApplyClones(pending, g_cloneStepMs.load());

  LinkCloneVehicle(owner);
  const int result = g_originalEvaluate(owner, time, arg, out);
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

void DetourMakeGhostMaterials(void *self, void *model) {
  InFlight guard;
  if (g_opaque.load()) {
    Logger::Info("GhostLab: carro fantasma criado sem materiais *_ghost (opaco).");
    return;
  }
  g_originalMakeGhost(self, model);
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
  Logger::Info(std::string("GhostLab: hooks (EvaluateGhostState ") +
               (evaluate ? "ok" : "FALHOU") + ", materiais " +
               (materials ? "ok" : "FALHOU") + ").");
  return evaluate || materials;
#else
  (void)gameBase;
  return false;
#endif
}

void GhostLab::Shutdown() {
#if defined(_WIN32)
  for (void *target : {g_evaluateTarget, g_makeGhostTarget}) {
    if (target != nullptr) MH_DisableHook(target);
  }
  for (int waited = 0; g_inFlight.load() > 0 && waited < 2000; ++waited) {
    Sleep(1);
  }
  for (void **target : {&g_evaluateTarget, &g_makeGhostTarget}) {
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

void GhostLab::SetOpaque(bool opaque) {
  if (g_opaque.exchange(opaque) != opaque) {
    Logger::Info(std::string("GhostLab: fantasma ") +
                 (opaque ? "opaco" : "transparente") +
                 " a partir do proximo carregamento.");
  }
}

bool GhostLab::IsOpaque() { return g_opaque.load(); }

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
