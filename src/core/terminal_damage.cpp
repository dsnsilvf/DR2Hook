#include "dr2hook/terminal_damage.h"
#include "dr2hook/logger.h"
#include "dr2hook/safety.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#if defined(_WIN32)
#include <MinHook.h>
#include <windows.h>
#endif

namespace dr2hook {
namespace {

#if defined(_WIN32)

constexpr uintptr_t kImageBase = 0x140000000ULL;
constexpr uintptr_t kTickRva = 0x1403f7b60ULL - kImageBase;
constexpr uintptr_t kControllerVtableRva = 0x141270ac8ULL - kImageBase;
constexpr uint8_t kTickPrologue[] = {0x40, 0x55, 0x53, 0x57, 0x48, 0x8d, 0x6c, 0x24};

constexpr size_t kControllerOwner = 0x28;   // controlador -> dono
constexpr size_t kOwnerA = 0x30;            // dono -> A
constexpr size_t kAInner = 0x08;            // A -> bloco
constexpr size_t kInnerComponent = 0x2BD0;  // bloco -> ponteiro do componente
constexpr size_t kReasonByte = 0x556;       // motivo; 7 = carro inteiro
constexpr size_t kPostEnabled = 0x555;      // != 0: o setter avisa o jogo
constexpr uintptr_t kSetterRva = 0x140744460ULL - kImageBase;
constexpr uint8_t kSetterPrologue[] = {0x80, 0xfa, 0x04, 0x74, 0x13, 0x80, 0xfa, 0x02};
constexpr size_t kControllerMode = 0x74;
constexpr size_t kControllerRequested = 0x77;
constexpr uint8_t kIntact = 7;
constexpr uint8_t kImpact = 4;
constexpr ULONGLONG kFreshMs = 500;         // tick visto ha menos que isso

using TickFn = void (*)(uint8_t *, void *);
using SetterFn = void (*)(uint8_t *component, uint8_t reason, int64_t index);

uintptr_t g_gameBase = 0;
void *g_tickTarget = nullptr;
TickFn g_originalTick = nullptr;
std::atomic<uint8_t *> g_controller{nullptr};
std::atomic<ULONGLONG> g_lastTickMs{0};

bool UsablePointer(uintptr_t p) {
  return p >= 0x10000 && p < 0x0000800000000000ULL && (p & 7) == 0;
}

template <typename T> T Read(const uint8_t *base, size_t offset) {
  T value;
  std::memcpy(&value, base + offset, sizeof(T));
  return value;
}

constexpr uintptr_t kFlowCoordinatorRva = 0x1416951e0ULL - kImageBase;
constexpr uintptr_t kRunnerVtableRva = 0x141249898ULL - kImageBase;
constexpr ULONGLONG kProbeMs = 40000;
// Estados do dano terminal em que o topo nao produz o evento `pause`:
// StateCutscene e StateTerminalDamageFreeze (REPORT3 da rodada 3).
constexpr uintptr_t kCutsceneVtableRva = 0x14124b810ULL - kImageBase;
constexpr uintptr_t kPauseScreenVtableRva = 0x141250b50ULL - kImageBase;
constexpr uintptr_t kFreezeVtableRva = 0x141254ad0ULL - kImageBase;
constexpr uintptr_t kPauseNameRva = 0x14124e000ULL - kImageBase;
// Slot +0x70 de StateCutscene (0x14027e660): o runner chama em cada estado da
// pilha e, se `*out` voltar nao nulo, procura o link com esse nome no no.
constexpr uintptr_t kPollRva = 0x14027e660ULL - kImageBase;
constexpr uint8_t kPollPrologue[] = {0x40, 0x53, 0x56, 0x57, 0x48, 0x83, 0xec, 0x20};
constexpr ULONGLONG kPauseFreshMs = 400;
using PollFn = void **(*)(uint8_t *state, void **out);
PollFn g_originalPoll = nullptr;
void *g_pollTarget = nullptr;
std::atomic<ULONGLONG> g_pauseRequestMs{0};
bool g_restartForced = false;
bool g_seenTerminal = false; // a sequencia ja entrou no estado terminal
bool g_repairPending = false; // reiniciou depois do dano: limpar o estado sobrando
ULONGLONG g_repairSinceMs = 0;
constexpr uintptr_t kRaceVtableRva = 0x141251500ULL - kImageBase;

// A proxy esconde o Reiniciar no menu de pausa do dano terminal; liga so
// depois de um crash feito pelo F11 (que ja exige sessao offline).
void SetRestartVisible(bool visible) {
  using Fn = int (*)(int);
  static const Fn fn = [] {
    const HMODULE host = GetModuleHandleA("dxgi.dll");
    return host == nullptr ? nullptr
                           : reinterpret_cast<Fn>(GetProcAddress(host, "Dr2Host_SetRestartVisible"));
  }();
  if (fn != nullptr) fn(visible ? 1 : 0);
  g_restartForced = visible;
  g_seenTerminal = false;
}

void **DetourPoll(uint8_t *state, void **out) {
  void **result = g_originalPoll(state, out);
  const ULONGLONG requested = g_pauseRequestMs.load();
  if (requested != 0 && out != nullptr && *out == nullptr) {
    if (GetTickCount64() - requested <= kPauseFreshMs) {
      *out = reinterpret_cast<void *>(g_gameBase + kPauseNameRva);
      Logger::Info("TerminalDamage: StateCutscene devolveu o nome 'pause'.");
    }
    g_pauseRequestMs.store(0);
  }
  return result;
}
std::atomic<ULONGLONG> g_probeStartMs{0};
ULONGLONG g_lastPauseMs = 0;
std::string g_lastStack;

bool Plausible(uintptr_t p) { return UsablePointer(p); }

// Pilha do fluxo: [coordinator+0x28] = runner; [runner+0x38] = vetor com base
// em +0x10 e contagem em +0x20 (docs/reverse_engineering/camera.md). Cada no:
// vtable em +0, flags em +0xc (dword), byte +0x59 (acesso a pausa, StatePause).
std::string DescribeStack(uintptr_t *topRva = nullptr, uintptr_t *topState = nullptr) {
  const uintptr_t coordinator =
      Read<uintptr_t>(reinterpret_cast<const uint8_t *>(g_gameBase + kFlowCoordinatorRva), 0);
  if (!Plausible(coordinator)) return "sem coordenador";
  const uintptr_t runner = Read<uintptr_t>(reinterpret_cast<const uint8_t *>(coordinator), 0x28);
  if (!Plausible(runner) ||
      Read<uintptr_t>(reinterpret_cast<const uint8_t *>(runner), 0) !=
          g_gameBase + kRunnerVtableRva) {
    return "sem runner";
  }
  const uintptr_t registry = Read<uintptr_t>(reinterpret_cast<const uint8_t *>(runner), 0x18);
  const uintptr_t stack = Read<uintptr_t>(reinterpret_cast<const uint8_t *>(runner), 0x38);
  if (!Plausible(registry) || !Plausible(stack)) return "sem pilha";
  const uintptr_t map = Read<uintptr_t>(reinterpret_cast<const uint8_t *>(registry), 0x10);
  if (!Plausible(map)) return "sem mapa";
  const uintptr_t begin = Read<uintptr_t>(reinterpret_cast<const uint8_t *>(map), 0x8);
  const uintptr_t end = Read<uintptr_t>(reinterpret_cast<const uint8_t *>(map), 0x10);
  if (!Plausible(begin) || end < begin || (end - begin) % 16 != 0 ||
      (end - begin) / 16 > 4096) {
    return "mapa invalido";
  }
  const uintptr_t nodes = Read<uintptr_t>(reinterpret_cast<const uint8_t *>(stack), 0x10);
  const uint64_t count = Read<uint64_t>(reinterpret_cast<const uint8_t *>(stack), 0x20);
  if (count == 0 || count > 32 || !Plausible(nodes)) return "pilha vazia";
  std::string out;
  char buf[96];
  for (uint64_t i = 0; i < count; ++i) {
    const uintptr_t node = Read<uintptr_t>(reinterpret_cast<const uint8_t *>(nodes), i * 8);
    if (!Plausible(node)) { out += " [?]"; continue; }
    const uint32_t key = Read<uint32_t>(reinterpret_cast<const uint8_t *>(node), 0xc);
    uintptr_t lo = begin;
    uintptr_t hi = end;
    while (lo < hi) {
      const uintptr_t mid = lo + ((hi - lo) / 32) * 16;
      if (Read<uint32_t>(reinterpret_cast<const uint8_t *>(mid), 0) < key) lo = mid + 16;
      else hi = mid;
    }
    if (lo >= end || Read<uint32_t>(reinterpret_cast<const uint8_t *>(lo), 0) != key) {
      std::snprintf(buf, sizeof(buf), " [id=%u sem estado]", key);
      out += buf;
      continue;
    }
    const uintptr_t state = Read<uintptr_t>(reinterpret_cast<const uint8_t *>(lo), 8);
    if (!Plausible(state)) { out += " [?estado]"; continue; }
    const uint8_t *st = reinterpret_cast<const uint8_t *>(state);
    const uintptr_t vt = Read<uintptr_t>(st, 0);
    if (topRva != nullptr) *topRva = vt >= g_gameBase ? vt - g_gameBase : 0;
    if (topState != nullptr) *topState = state;
    std::snprintf(buf, sizeof(buf), " [st=%llx id=%u +59=%u]",
                  static_cast<unsigned long long>(vt >= g_gameBase ? vt - g_gameBase : 0),
                  key, st[0x59]);
    out += buf;
  }
  return out;
}

// REPORT4: root [0x141695150] -> +8 tabela -> [tabela + idx*8] = host do laco
// (vtable 0x14126f440); lista de controladores em [host+0x30, host+0x38). O do
// jogador tem dono com vtable 0x14127cc00 e dono+0xbc == 0.
bool IsHostVtable(uintptr_t vtable);

uint8_t *FindPlayerController() {
  constexpr uintptr_t kRootRva = 0x141695150ULL - kImageBase;
  constexpr uintptr_t kIndexRva = 0x141694064ULL - kImageBase;
  constexpr uintptr_t kOwnerVtableRva = 0x14127cc00ULL - kImageBase;
  const uint8_t *base = reinterpret_cast<const uint8_t *>(g_gameBase);
  const uintptr_t root = Read<uintptr_t>(base, kRootRva);
  if (!UsablePointer(root)) return nullptr;
  const uintptr_t table = Read<uintptr_t>(reinterpret_cast<const uint8_t *>(root), 8);
  if (!UsablePointer(table)) return nullptr;
  const int32_t index = Read<int32_t>(base, kIndexRva);
  if (index < 0 || index > 4096) return nullptr;
  const uintptr_t host = Read<uintptr_t>(reinterpret_cast<const uint8_t *>(table), index * 8);
  if (!UsablePointer(host)) return nullptr;
  const uint8_t *h = reinterpret_cast<const uint8_t *>(host);
  if (!IsHostVtable(Read<uintptr_t>(h, 0))) return nullptr;
  const uintptr_t begin = Read<uintptr_t>(h, 0x30);
  const uintptr_t end = Read<uintptr_t>(h, 0x38);
  if (!UsablePointer(begin) || end < begin || (end - begin) / 8 > 1024) return nullptr;
  for (uintptr_t p = begin; p < end; p += 8) {
    const uintptr_t c = Read<uintptr_t>(reinterpret_cast<const uint8_t *>(p), 0);
    if (!UsablePointer(c)) continue;
    const uint8_t *cb = reinterpret_cast<const uint8_t *>(c);
    if (Read<uintptr_t>(cb, 0) != g_gameBase + kControllerVtableRva) continue;
    const uintptr_t owner = Read<uintptr_t>(cb, kControllerOwner);
    if (!UsablePointer(owner)) continue;
    const uint8_t *ob = reinterpret_cast<const uint8_t *>(owner);
    if (Read<uintptr_t>(ob, 0) != g_gameBase + kOwnerVtableRva || ob[0xbc] != 0) continue;
    return const_cast<uint8_t *>(cb);
  }
  return nullptr;
}

// O construtor 0x1403d7ab0 grava 0x14126f440 (base); ao vivo o objeto tem a
// vtable derivada 0x14126f588 (lida em 2026-10-04). Aceita as duas.
bool IsHostVtable(uintptr_t vtable) {
  return vtable == g_gameBase + (0x14126f440ULL - kImageBase) ||
         vtable == g_gameBase + (0x14126f588ULL - kImageBase);
}

// Sistema do indice `idxRva` na tabela do root (mesmo caminho do host do laco).
uint8_t *SystemAt(uintptr_t idxRva) {
  constexpr uintptr_t kRootRva = 0x141695150ULL - kImageBase;
  const uint8_t *base = reinterpret_cast<const uint8_t *>(g_gameBase);
  const uintptr_t root = Read<uintptr_t>(base, kRootRva);
  if (!UsablePointer(root)) return nullptr;
  const uintptr_t table = Read<uintptr_t>(reinterpret_cast<const uint8_t *>(root), 8);
  if (!UsablePointer(table)) return nullptr;
  const int32_t index = Read<int32_t>(base, idxRva);
  if (index < 0 || index > 4096) return nullptr;
  const uintptr_t sys = Read<uintptr_t>(reinterpret_cast<const uint8_t *>(table), index * 8);
  return UsablePointer(sys) ? reinterpret_cast<uint8_t *>(sys) : nullptr;
}

uint8_t *PlayerComponent(uint8_t *controller) {
  const uintptr_t owner = Read<uintptr_t>(controller, kControllerOwner);
  if (!UsablePointer(owner)) return nullptr;
  const uintptr_t a = Read<uintptr_t>(reinterpret_cast<uint8_t *>(owner), kOwnerA);
  if (!UsablePointer(a)) return nullptr;
  const uintptr_t inner = Read<uintptr_t>(reinterpret_cast<uint8_t *>(a), kAInner);
  if (!UsablePointer(inner)) return nullptr;
  const uintptr_t comp = Read<uintptr_t>(reinterpret_cast<uint8_t *>(inner), kInnerComponent);
  return UsablePointer(comp) ? reinterpret_cast<uint8_t *>(comp) : nullptr;
}

// REPORT5: o EndRace zera o gate host+0x2350 e so 0x1404069b0 (subida da
// especial) o religa; a cutscene inicial liga ffb+0xe1 sem restaurar. O
// Reiniciar nao desfaz nenhum dos dois. Chama as funcoes do proprio jogo.
void RepairAfterRestart() {
  constexpr uintptr_t kHostIdxRva = 0x141694064ULL - kImageBase;
  constexpr uintptr_t kVehicleIdxRva = 0x141693ffcULL - kImageBase;
  using GateFn = void (*)(uint8_t *host);
  using FfbSetFn = void (*)(uint8_t *ffb, uint8_t on);
  using FfbApplyFn = void (*)(uint8_t *ffb);
  uint8_t *controller = FindPlayerController();
  uint8_t *comp = controller != nullptr ? PlayerComponent(controller) : nullptr;
  uint8_t *host = SystemAt(kHostIdxRva);
  if (host != nullptr && !IsHostVtable(Read<uintptr_t>(host, 0))) host = nullptr;
  uint8_t *ffb = nullptr;
  if (uint8_t *vs = SystemAt(kVehicleIdxRva)) {
    const uintptr_t f = Read<uintptr_t>(vs, 0x2f0);
    if (UsablePointer(f)) ffb = reinterpret_cast<uint8_t *>(f);
  }
  // Som do motor: o tick de audio (0x140b1db50) corta o motor todo quadro
  // enquanto veiculo+0x368 == 1 (carro aposentado, 0x1409913e0). O Reiniciar
  // nao chama 0x140999990, que desfaz isso.
  using VehicleAtFn = uint8_t *(*)(uint8_t *vs, int index);
  using UnretireFn = void (*)(uint8_t *vehicle);
  uint8_t *vehicle = nullptr;
  if (uint8_t *vs = SystemAt(kVehicleIdxRva)) {
    uint8_t *v = reinterpret_cast<VehicleAtFn>(g_gameBase + (0x1409713e0ULL - kImageBase))(vs, 0);
    if (UsablePointer(reinterpret_cast<uintptr_t>(v))) vehicle = v;
  }
  char vbuf[160];
  std::snprintf(vbuf, sizeof(vbuf),
                "TerminalDamage: veiculo antes: +0x368=%d +0x35c=%d +0xa7=%d +0x6c=%d.",
                vehicle ? vehicle[0x368] : -1, vehicle ? vehicle[0x35c] : -1,
                vehicle ? vehicle[0xa7] : -1, vehicle ? vehicle[0x6c] : -1);
  Logger::Info(vbuf);
  if (vehicle != nullptr && vehicle[0x368] == 1) {
    reinterpret_cast<UnretireFn>(g_gameBase + (0x140999990ULL - kImageBase))(vehicle);
    std::snprintf(vbuf, sizeof(vbuf),
                  "TerminalDamage: carro reativado (0x140999990): +0x368=%d.", vehicle[0x368]);
    Logger::Info(vbuf);
  }
  char buf[220];
  std::snprintf(buf, sizeof(buf),
                "TerminalDamage: apos reiniciar: controlador=%s +0x74=%d +0x75=%d motivo=%d "
                "host+0x2350=%d ffb+0xe1=%d.",
                controller != nullptr ? "ok" : "nao", controller ? controller[0x74] : -1,
                controller ? controller[0x75] : -1, comp ? comp[kReasonByte] : -1,
                host ? host[0x2350] : -1, ffb ? ffb[0xe1] : -1);
  Logger::Info(buf);
  if (controller != nullptr) {
    controller[0x74] = 0;
    controller[0x75] = 0;
    controller[0x77] = 0;
    controller[0x78] = 0;
  }
  if (comp != nullptr && comp[kReasonByte] != kIntact) comp[kReasonByte] = kIntact;
  if (host != nullptr && host[0x2350] == 0) {
    reinterpret_cast<GateFn>(g_gameBase + (0x1404069b0ULL - kImageBase))(host);
    Logger::Info("TerminalDamage: gate dos componentes religado (0x1404069b0).");
  }
  if (ffb != nullptr && ffb[0xe1] == 1) {
    reinterpret_cast<FfbSetFn>(g_gameBase + (0x140c2fcb0ULL - kImageBase))(ffb, 0);
    reinterpret_cast<FfbApplyFn>(g_gameBase + (0x140c2fb40ULL - kImageBase))(ffb);
    Logger::Info("TerminalDamage: force feedback restaurado (0x140c2fcb0).");
  }
}

void DetourTick(uint8_t *controller, void *arg) {
  g_controller.store(controller);
  g_lastTickMs.store(GetTickCount64());
  g_originalTick(controller, arg);
}

#endif

} // namespace

bool TerminalDamage::Install(uintptr_t gameBase) {
#if defined(_WIN32)
  if (gameBase == 0) return false;
  g_gameBase = gameBase;
  const MH_STATUS init = MH_Initialize();
  if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
    Logger::Error("TerminalDamage: MH_Initialize falhou.");
    return false;
  }
  void *fn = reinterpret_cast<void *>(gameBase + kTickRva);
  if (std::memcmp(fn, kTickPrologue, sizeof(kTickPrologue)) != 0) {
    Logger::Warn("TerminalDamage: prologo do tick diferente; hook abortado.");
    return false;
  }
  if (MH_CreateHook(fn, reinterpret_cast<void *>(&DetourTick),
                    reinterpret_cast<void **>(&g_originalTick)) != MH_OK) {
    Logger::Error("TerminalDamage: falha ao criar o hook do tick.");
    return false;
  }
  if (MH_EnableHook(fn) != MH_OK) {
    MH_RemoveHook(fn);
    Logger::Error("TerminalDamage: falha ao habilitar o hook do tick.");
    return false;
  }
  g_tickTarget = fn;
  Logger::Info("TerminalDamage: hook do tick do controlador ok.");
  void *poll = reinterpret_cast<void *>(gameBase + kPollRva);
  if (std::memcmp(poll, kPollPrologue, sizeof(kPollPrologue)) == 0 &&
      MH_CreateHook(poll, reinterpret_cast<void *>(&DetourPoll),
                    reinterpret_cast<void **>(&g_originalPoll)) == MH_OK &&
      MH_EnableHook(poll) == MH_OK) {
    g_pollTarget = poll;
    Logger::Info("TerminalDamage: hook do poll de StateCutscene ok.");
  } else {
    Logger::Warn("TerminalDamage: hook do poll de StateCutscene falhou.");
  }
  return true;
#else
  (void)gameBase;
  return false;
#endif
}

void TerminalDamage::Shutdown() {
#if defined(_WIN32)
  for (void **target : {&g_tickTarget, &g_pollTarget}) {
    if (*target != nullptr) MH_DisableHook(*target);
  }
  Sleep(50);
  for (void **target : {&g_tickTarget, &g_pollTarget}) {
    if (*target != nullptr) {
      MH_RemoveHook(*target);
      *target = nullptr;
    }
  }
  g_controller.store(nullptr);
  SetRestartVisible(false);
#endif
}

TerminalDamage::Result TerminalDamage::Crash() {
#if defined(_WIN32)
  if (!SafetyGuard::CanWriteState()) return Result::Blocked;
  // O tick para depois do dano (gate host+0x2350 = 0), mas o controlador segue
  // na lista do host: procura la quando o hook nao viu o tick recentemente.
  uint8_t *controller = g_controller.load();
  if (controller == nullptr || GetTickCount64() - g_lastTickMs.load() > kFreshMs) {
    uint8_t *found = FindPlayerController();
    if (found != nullptr) controller = found;
  }
  if (controller == nullptr) return Result::NoController;
  if (Read<uintptr_t>(controller, 0) != g_gameBase + kControllerVtableRva) {
    return Result::BadChain;
  }
  const uintptr_t owner = Read<uintptr_t>(controller, kControllerOwner);
  if (!UsablePointer(owner)) return Result::BadChain;
  const uintptr_t a = Read<uintptr_t>(reinterpret_cast<uint8_t *>(owner), kOwnerA);
  if (!UsablePointer(a)) return Result::BadChain;
  const uintptr_t inner = Read<uintptr_t>(reinterpret_cast<uint8_t *>(a), kAInner);
  if (!UsablePointer(inner)) return Result::BadChain;
  const uintptr_t component =
      Read<uintptr_t>(reinterpret_cast<uint8_t *>(inner), kInnerComponent);
  if (!UsablePointer(component)) return Result::BadChain;
  uint8_t *reason = reinterpret_cast<uint8_t *>(component) + kReasonByte;
  if (*reason != kIntact) return Result::AlreadyDown;
  uint8_t *comp = reinterpret_cast<uint8_t *>(component);
  void *setter = reinterpret_cast<void *>(g_gameBase + kSetterRva);
  const bool posts = comp[kPostEnabled] != 0 &&
                     std::memcmp(setter, kSetterPrologue, sizeof(kSetterPrologue)) == 0;
  if (posts) {
    // Setter do jogo: grava o motivo e chama o poster de TerminalDamageMessage
    // (+0x438), que leva ao fluxo de dano terminal e ao reinicio.
    reinterpret_cast<SetterFn>(setter)(comp, kImpact, 0);
  } else {
    *reason = kImpact;
  }
  g_probeStartMs.store(GetTickCount64());
  g_lastStack.clear();
  SetRestartVisible(true);
  Logger::Info(std::string("TerminalDamage: ") +
               (posts ? "setter 0x140744460" : "gravacao direta") +
               " com motivo 4; +0x556=" + std::to_string(*reason) +
               " modo(+0x74)=" + std::to_string(controller[kControllerMode]) +
               " pedido(+0x77)=" + std::to_string(controller[kControllerRequested]) + ".");
  return Result::Done;
#else
  return Result::NoController;
#endif
}

} // namespace dr2hook

namespace dr2hook {

void TerminalDamage::Update() {
#if defined(_WIN32)
  if (g_restartForced) {
    // Fim da sequencia: nenhum estado do dano terminal na pilha.
    const std::string stack = DescribeStack();
    char cutscene[24], freeze[24];
    std::snprintf(cutscene, sizeof(cutscene), "st=%llx ",
                  static_cast<unsigned long long>(kCutsceneVtableRva));
    std::snprintf(freeze, sizeof(freeze), "st=%llx ",
                  static_cast<unsigned long long>(kFreezeVtableRva));
    // Pausar troca a pilha pela cadeia do menu original (sem a cutscene), entao
    // o menu de pausa tambem conta como "ainda na sequencia".
    char pauseScreen[24];
    std::snprintf(pauseScreen, sizeof(pauseScreen), "st=%llx ",
                  static_cast<unsigned long long>(kPauseScreenVtableRva));
    const bool terminal = stack.find(cutscene) != std::string::npos ||
                          stack.find(freeze) != std::string::npos ||
                          (g_seenTerminal && stack.find(pauseScreen) != std::string::npos);
    if (terminal) {
      g_seenTerminal = true;
    } else if (g_seenTerminal) {
      SetRestartVisible(false);
      g_repairPending = true;
      g_repairSinceMs = GetTickCount64();
    }
  }
  if (g_repairPending) {
    uintptr_t top = 0;
    DescribeStack(&top);
    if (top == kRaceVtableRva) {
      g_repairPending = false;
      if (SafetyGuard::CanWriteState()) RepairAfterRestart();
    } else if (GetTickCount64() - g_repairSinceMs > 180000) {
      g_repairPending = false;
    }
  }
  const ULONGLONG start = g_probeStartMs.load();
  if (start == 0) return;
  if (GetTickCount64() - start > kProbeMs) {
    g_probeStartMs.store(0);
    Logger::Info("TerminalDamage: sonda da pilha encerrada.");
    return;
  }
  const std::string now = DescribeStack();
  if (now != g_lastStack) {
    g_lastStack = now;
    Logger::Info("TerminalDamage: pilha (base -> topo):" + now);
  }
#endif
}

} // namespace dr2hook

namespace dr2hook {

// Esc no dano terminal: so StateRace e StateOsdCountDown produzem `pause`, e os
// dois saem da pilha. O gancho em StateCutscene devolve o nome ao runner.
bool TerminalDamage::RequestPause() {
#if defined(_WIN32)
  uintptr_t top = 0;
  const std::string stack = DescribeStack(&top);
  if (top != kCutsceneVtableRva && top != kFreezeVtableRva) return false;
  const ULONGLONG now = GetTickCount64();
  if (now - g_lastPauseMs < 500) return false;
  g_lastPauseMs = now;
  if (top != kCutsceneVtableRva) return false; // so a cutscene tem o gancho
  if (!SafetyGuard::CanWriteState()) return false; // evento online: nada muda
  if (!g_restartForced) {
    SetRestartVisible(true);
    g_seenTerminal = true; // o topo ja e o estado terminal
  }
  g_pauseRequestMs.store(now);
  Logger::Info("TerminalDamage: pedido de pausa; o runner pergunta no proximo quadro.");
  return true;
#else
  return false;
#endif
}

} // namespace dr2hook

namespace dr2hook {

std::string TerminalDamage::FlowStack() {
#if defined(_WIN32)
  return DescribeStack();
#else
  return "indisponivel";
#endif
}

// Estados de tela (menus) guardam a transicao pendente em +0xc8 e a devolvem no
// slot +0x70 (0x1402d5730); 0x140215a80 grava o nome. O runner abre o link no
// proximo tick (ui_data.md, "Do evento a tela").
std::string TerminalDamage::PostLink(const std::string &name) {
#if defined(_WIN32)
  if (name.empty() || name.size() > 63) return "nome invalido";
  uintptr_t topRva = 0;
  uintptr_t state = 0;
  DescribeStack(&topRva, &state);
  if (state == 0 || !UsablePointer(state)) return "sem estado no topo";
  const uintptr_t vtable = Read<uintptr_t>(reinterpret_cast<uint8_t *>(state), 0);
  if (!UsablePointer(vtable)) return "vtable invalida";
  const uintptr_t slot = Read<uintptr_t>(reinterpret_cast<uint8_t *>(vtable), 0x70);
  if (slot != g_gameBase + (0x1402d5730ULL - kImageBase)) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "topo st=%llx nao e uma tela (slot +0x70 diferente)",
                  static_cast<unsigned long long>(topRva));
    return buf;
  }
  using PostFn = void (*)(void *state, const char *name);
  reinterpret_cast<PostFn>(g_gameBase + (0x140215a80ULL - kImageBase))(
      reinterpret_cast<void *>(state), name.c_str());
  char buf[96];
  std::snprintf(buf, sizeof(buf), "ok: '%s' postado no topo st=%llx", name.c_str(),
                static_cast<unsigned long long>(topRva));
  return buf;
#else
  return "indisponivel";
#endif
}

} // namespace dr2hook

namespace dr2hook {

// Pausa a corrida sem teclado: o Update de StateRace liga +0x58 quando a acao
// driving.pause esta ativa, e o slot +0x70 (0x1402815b0) devolve `pause` depois
// da contagem em +0x5c/+0x60. Ligar o byte faz o mesmo caminho.
std::string TerminalDamage::PostPause() {
#if defined(_WIN32)
  uintptr_t topRva = 0;
  uintptr_t state = 0;
  DescribeStack(&topRva, &state);
  if (state == 0 || topRva != kRaceVtableRva) return "o topo nao e a corrida (use: stack)";
  Read<uint8_t>(reinterpret_cast<uint8_t *>(state), 0x58);
  *reinterpret_cast<uint8_t *>(state + 0x58) = 1;
  return "ok: pausa pedida";
#else
  return "indisponivel";
#endif
}

} // namespace dr2hook
