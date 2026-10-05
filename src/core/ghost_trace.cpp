#include "dr2hook/ghost_trace.h"
#include "dr2hook/logger.h"
#include "dr2hook/terminal_damage.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#if defined(_WIN32)
#include <MinHook.h>
#include <windows.h>
#endif

namespace dr2hook {

std::atomic<bool> GhostTrace::s_enabled{false};

#if defined(_WIN32)
namespace {

constexpr uintptr_t kImageBase = 0x140000000;

// Atualizador do sistema de fantasmas (0x140518bb0): percorre o mapa de
// controladores em this+0x18 e chama o atualizador de cada um. this+0x20 =
// tamanho, this+0x30 = ligado.
constexpr uintptr_t kSystemUpdateRva = 0x140518bb0 - kImageBase;
constexpr uint8_t kSystemUpdatePrologue[] = {0x40, 0x56, 0x48, 0x83, 0xec, 0x40,
                                             0x48, 0x8b, 0xf1, 0x48, 0x8b, 0x49,
                                             0x28, 0xe8};
// Atualizador de um controlador (0x140518400): (ctrl, double relogio, r8, r9).
// +0x58 tempo passado a EvaluateGhostState, +0x60/+0x63 bytes de controle,
// +0x40 velocidade, +0x48 buffer de saida (posicao em +0x30).
constexpr uintptr_t kControllerUpdateRva = 0x140518400 - kImageBase;
constexpr uint8_t kControllerUpdatePrologue[] = {0x40, 0x55, 0x57, 0x41, 0x56, 0x48,
                                                 0x81, 0xec, 0xf0, 0x00, 0x00, 0x00,
                                                 0x80, 0x79, 0x60, 0x00};
// Aplica o buffer de saida ao corpo do veiculo (0x1409cdaa0): (obj = ctrl+8,
// buffer, veiculo, double dt). Devolve 2 se nao aplicou.
constexpr uintptr_t kApplyRva = 0x1409cdaa0 - kImageBase;
constexpr uint8_t kApplyPrologue[] = {0x40, 0x53, 0x48, 0x81, 0xec, 0x90, 0x00, 0x00,
                                      0x00, 0x48, 0x8b, 0x41, 0x20, 0x48, 0x8b, 0xda};

constexpr uintptr_t kTimeOffsetRva = 0x141f593e0 - kImageBase; // float
constexpr uintptr_t kTimeFlagsRva = 0x141f593e4 - kImageBase;  // 2 bytes

constexpr size_t kCtlVehicle = 0x00;
constexpr size_t kCtlOwner = 0x08;
constexpr size_t kCtlSlot = 0x28;
constexpr size_t kCtlSpeed = 0x40;
constexpr size_t kCtlBuffer = 0x48;
constexpr size_t kCtlTime = 0x58;
constexpr size_t kBufPos = 0x30;
constexpr size_t kVehicleBody = 0x30;
constexpr size_t kBodyPos = 0x2d0;
constexpr size_t kSlotState = 0x1a8;
constexpr size_t kCtlDumpSize = 0x100;
constexpr size_t kBodyDumpSize = 0x340;
constexpr size_t kMaxFileBytes = 256u * 1024 * 1024;

using SystemUpdateFn = uintptr_t (*)(uint8_t *);
using ControllerUpdateFn = uintptr_t (*)(uint8_t *, double, void *, void *);
using ApplyFn = int (*)(uint8_t *, uint8_t *, uint8_t *, double);

uintptr_t g_base = 0;
SystemUpdateFn g_origSystem = nullptr;
ControllerUpdateFn g_origController = nullptr;
ApplyFn g_origApply = nullptr;
void *g_sysTarget = nullptr;
void *g_ctlTarget = nullptr;
void *g_applyTarget = nullptr;

std::atomic<int> g_inFlight{0};
struct InFlight {
  InFlight() { g_inFlight.fetch_add(1); }
  ~InFlight() { g_inFlight.fetch_sub(1); }
};

std::mutex g_mutex; // protege g_buf e g_file
std::string g_buf;
std::FILE *g_file = nullptr;
size_t g_written = 0;
std::atomic<bool> g_running{false};
std::thread g_beat;

std::atomic<uint64_t> g_frame{0}; // chamadas do sistema
std::atomic<uint64_t> g_cSys{0}, g_cCtl{0}, g_cEval{0}, g_cApply{0};
std::atomic<uint32_t> g_epoch{0};      // sobe a cada troca de estado da pilha
std::atomic<uint32_t> g_chainEpoch{~0u}; // epoca da ultima cadeia registrada
std::atomic<const uint8_t *> g_lastSystem{nullptr};

// Controladores vistos (para os dumps do batimento).
constexpr int kMaxCtl = 12;
std::atomic<const uint8_t *> g_ctls[kMaxCtl];

double NowMs() {
  using namespace std::chrono;
  static const auto start = steady_clock::now();
  return duration<double, std::milli>(steady_clock::now() - start).count();
}

void Line(const char *fmt, ...) {
  char text[900];
  int n = std::snprintf(text, sizeof(text), "%.3f ", NowMs());
  va_list args;
  va_start(args, fmt);
  const int m = std::vsnprintf(text + n, sizeof(text) - n - 2, fmt, args);
  va_end(args);
  if (m < 0) return;
  n = std::min<int>(n + m, static_cast<int>(sizeof(text)) - 2);
  text[n++] = '\n';
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_written + g_buf.size() < kMaxFileBytes) g_buf.append(text, n);
}

void Flush() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_file == nullptr || g_buf.empty()) return;
  std::fwrite(g_buf.data(), 1, g_buf.size(), g_file);
  std::fflush(g_file);
  g_written += g_buf.size();
  g_buf.clear();
}

bool Readable(const void *p, size_t n) {
  if (p == nullptr || reinterpret_cast<uintptr_t>(p) < 0x10000) return false;
  MEMORY_BASIC_INFORMATION mbi;
  if (!VirtualQuery(p, &mbi, sizeof(mbi))) return false;
  if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
  return static_cast<const uint8_t *>(p) + n <=
         static_cast<const uint8_t *>(mbi.BaseAddress) + mbi.RegionSize;
}

template <typename T> T Rd(const uint8_t *base, size_t offset) {
  T value{};
  if (Readable(base + offset, sizeof(T))) std::memcpy(&value, base + offset, sizeof(T));
  return value;
}

struct Vec3 {
  float x = 0, y = 0, z = 0;
};
Vec3 ReadVec3(const uint8_t *p) {
  Vec3 v;
  if (p != nullptr && Readable(p, sizeof(float) * 3)) std::memcpy(&v, p, sizeof(float) * 3);
  return v;
}

const uint8_t *BodyOf(const uint8_t *vehicle) {
  return Readable(vehicle, 0x38) ? Rd<const uint8_t *>(vehicle, kVehicleBody) : nullptr;
}
Vec3 BodyPos(const uint8_t *vehicle) {
  const uint8_t *body = BodyOf(vehicle);
  return body != nullptr ? ReadVec3(body + kBodyPos) : Vec3{};
}

unsigned long long Rva(const void *p) {
  const uintptr_t v = reinterpret_cast<uintptr_t>(p);
  return v >= g_base ? static_cast<unsigned long long>(v - g_base) : 0;
}

// Retornos no exe na pilha atual (cadeia de chamadas, heuristica de crash).
std::string Chain(const void *frame) {
  const NT_TIB *tib = reinterpret_cast<const NT_TIB *>(NtCurrentTeb());
  const uintptr_t top = reinterpret_cast<uintptr_t>(tib->StackBase);
  std::string out;
  int found = 0;
  for (uintptr_t sp = reinterpret_cast<uintptr_t>(frame);
       sp + 8 <= top && sp < reinterpret_cast<uintptr_t>(frame) + 0x4000 && found < 20; sp += 8) {
    const uintptr_t v = *reinterpret_cast<const uintptr_t *>(sp);
    if (v < g_base + 0x1000 || v >= g_base + 0x1099000) continue;
    const uint8_t *ret = reinterpret_cast<const uint8_t *>(v);
    if (ret[-5] != 0xe8 && !(ret[-6] == 0xff && ret[-5] == 0x15) && ret[-2] != 0xff) continue;
    char one[24];
    std::snprintf(one, sizeof(one), " +%llx", Rva(ret));
    out += one;
    ++found;
  }
  return out;
}

void Dump(const char *who, const uint8_t *p, size_t size) {
  if (!Readable(p, size)) {
    Line("DUMP %s ptr=%p ilegivel", who, p);
    return;
  }
  for (size_t off = 0; off < size; off += 32) {
    char hex[100];
    int n = 0;
    for (size_t i = 0; i < 32 && off + i < size; ++i) {
      n += std::snprintf(hex + n, sizeof(hex) - n, "%02x", p[off + i]);
      if (i % 4 == 3) hex[n++] = ' ';
    }
    hex[n] = 0;
    Line("DUMP %s +%03zx %s", who, off, hex);
  }
}

void NoteController(const uint8_t *ctl) {
  for (auto &slot : g_ctls) {
    if (slot.load() == ctl) return;
  }
  for (auto &slot : g_ctls) {
    const uint8_t *expected = nullptr;
    if (slot.compare_exchange_strong(expected, ctl)) return;
  }
}

int ControllerIndex(const uint8_t *ctl) {
  for (int i = 0; i < kMaxCtl; ++i) {
    if (g_ctls[i].load() == ctl) return i;
  }
  return -1;
}

void DumpWorld(const char *why) {
  Line("DUMPSET why=%s epoch=%u", why, g_epoch.load());
  if (const uint8_t *sys = g_lastSystem.load()) Dump("sys", sys, 0x80);
  for (int i = 0; i < kMaxCtl; ++i) {
    const uint8_t *ctl = g_ctls[i].load();
    if (ctl == nullptr || !Readable(ctl, kCtlDumpSize)) continue;
    char who[32];
    std::snprintf(who, sizeof(who), "ctl%d@%p", i, ctl);
    Dump(who, ctl, kCtlDumpSize);
    const uint8_t *vehicle = Rd<const uint8_t *>(ctl, kCtlVehicle);
    if (const uint8_t *body = BodyOf(vehicle)) {
      std::snprintf(who, sizeof(who), "body%d@%p", i, body);
      Dump(who, body, kBodyDumpSize);
    }
    const uint8_t *slot = Rd<const uint8_t *>(ctl, kCtlSlot);
    if (Readable(slot, 0x260)) {
      std::snprintf(who, sizeof(who), "slot%d@%p", i, slot);
      Dump(who, slot, 0x260);
    }
  }
}

void BeatLoop() {
  unsigned long long lastTop = ~0ull;
  uint64_t lastSys = 0, lastCtl = 0, lastEval = 0, lastApply = 0;
  std::string lastStack;
  double dumpLateAt = 0;
  while (g_running.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (!g_running.load()) break;
    const std::string stack = TerminalDamage::FlowStack();
    const size_t at = stack.rfind("st=");
    const unsigned long long top =
        at == std::string::npos ? 0 : std::strtoull(stack.c_str() + at + 3, nullptr, 16);
    const uint64_t sys = g_cSys.load(), ctl = g_cCtl.load(), eval = g_cEval.load(),
                   apply = g_cApply.load();
    const float offset = *reinterpret_cast<const float *>(g_base + kTimeOffsetRva);
    const uint8_t *flags = reinterpret_cast<const uint8_t *>(g_base + kTimeFlagsRva);
    Line("BEAT sys=%llu ctl=%llu eval=%llu apply=%llu top=%llx off=%.4f flags=%d,%d epoch=%u",
         static_cast<unsigned long long>(sys - lastSys),
         static_cast<unsigned long long>(ctl - lastCtl),
         static_cast<unsigned long long>(eval - lastEval),
         static_cast<unsigned long long>(apply - lastApply), top, offset, flags[0], flags[1],
         g_epoch.load());
    lastSys = sys;
    lastCtl = ctl;
    lastEval = eval;
    lastApply = apply;
    if (stack != lastStack) {
      Line("STACK %s", stack.c_str());
      lastStack = stack;
    }
    if (top != lastTop) {
      if (lastTop != ~0ull) {
        g_epoch.fetch_add(1);
        Line("STATE %llx -> %llx (epoca %u)", lastTop, top, g_epoch.load());
        DumpWorld("troca de estado");
        dumpLateAt = NowMs() + 600;
      }
      lastTop = top;
    } else if (dumpLateAt > 0 && NowMs() >= dumpLateAt) {
      DumpWorld("600ms depois");
      dumpLateAt = 0;
    }
    Flush();
  }
}

uintptr_t DetourSystem(uint8_t *self) {
  InFlight guard;
  g_cSys.fetch_add(1);
  const uint64_t frame = g_frame.fetch_add(1) + 1;
  g_lastSystem.store(self);
  Line("SYS f=%llu this=%p en=%u size=%llu ret=+%llx", static_cast<unsigned long long>(frame),
       self, static_cast<unsigned>(Rd<uint8_t>(self, 0x30)),
       static_cast<unsigned long long>(Rd<uint64_t>(self, 0x20)),
       Rva(__builtin_return_address(0)));
  const uint32_t epoch = g_epoch.load();
  if (g_chainEpoch.exchange(epoch) != epoch) {
    Line("CHAIN epoca=%u:%s", epoch, Chain(__builtin_frame_address(0)).c_str());
  }
  return g_origSystem(self);
}

uintptr_t DetourController(uint8_t *ctl, double clock, void *r8, void *r9) {
  InFlight guard;
  g_cCtl.fetch_add(1);
  NoteController(ctl);
  const uint8_t b60 = Rd<uint8_t>(ctl, 0x60), b61 = Rd<uint8_t>(ctl, 0x61),
                b62 = Rd<uint8_t>(ctl, 0x62), b63 = Rd<uint8_t>(ctl, 0x63);
  const uint64_t t58 = Rd<uint64_t>(ctl, kCtlTime);
  const uint8_t *slot = Rd<const uint8_t *>(ctl, kCtlSlot);
  const uint32_t slotState = Readable(slot, kSlotState + 4) ? Rd<uint32_t>(slot, kSlotState) : 0xffff;
  const uintptr_t result = g_origController(ctl, clock, r8, r9);
  const uint8_t *vehicle = Rd<const uint8_t *>(ctl, kCtlVehicle);
  const uint8_t *buffer = Rd<const uint8_t *>(ctl, kCtlBuffer);
  const uint64_t t58After = Rd<uint64_t>(ctl, kCtlTime);
  double t58Double = 0;
  std::memcpy(&t58Double, &t58After, sizeof(t58Double));
  const Vec3 out = buffer != nullptr ? ReadVec3(buffer + kBufPos) : Vec3{};
  const Vec3 body = BodyPos(vehicle);
  Line("CTL f=%llu c=%d ctl=%p veh=%p slot=%p st=%u pre[60=%u 61=%u 62=%u 63=%u t58=%016llx] "
       "clock=%.6f post[60=%u 61=%u t58=%016llx d=%.6f spd=%.3f] out=%.2f,%.2f,%.2f "
       "body=%.2f,%.2f,%.2f",
       static_cast<unsigned long long>(g_frame.load()), ControllerIndex(ctl), ctl, vehicle,
       slot, slotState, b60, b61, b62, b63, static_cast<unsigned long long>(t58), clock,
       Rd<uint8_t>(ctl, 0x60), Rd<uint8_t>(ctl, 0x61), static_cast<unsigned long long>(t58After),
       t58Double, Rd<float>(ctl, kCtlSpeed), out.x, out.y, out.z, body.x, body.y, body.z);
  return result;
}

int DetourApply(uint8_t *obj, uint8_t *buffer, uint8_t *vehicle, double dt) {
  InFlight guard;
  g_cApply.fetch_add(1);
  const Vec3 before = BodyPos(vehicle);
  const int result = g_origApply(obj, buffer, vehicle, dt);
  const Vec3 after = BodyPos(vehicle);
  const Vec3 bufPos = buffer != nullptr ? ReadVec3(buffer + kBufPos) : Vec3{};
  Line("APPLY f=%llu obj=%p buf=%p veh=%p dt=%.6f res=%d buf60=%u bufpos=%.2f,%.2f,%.2f "
       "body_pre=%.2f,%.2f,%.2f body_post=%.2f,%.2f,%.2f",
       static_cast<unsigned long long>(g_frame.load()), obj, buffer, vehicle, dt, result,
       static_cast<unsigned>(Rd<uint8_t>(buffer, 0x60)), bufPos.x, bufPos.y, bufPos.z,
       before.x, before.y, before.z, after.x, after.y, after.z);
  return result;
}

bool HookAt(uintptr_t rva, const uint8_t *prologue, size_t size, void *detour, void **original,
            void **target, const char *name) {
  void *fn = reinterpret_cast<void *>(g_base + rva);
  if (std::memcmp(fn, prologue, size) != 0) {
    Logger::Warn(std::string("GhostTrace: prologo de ") + name + " diferente; hook abortado.");
    return false;
  }
  if (MH_CreateHook(fn, detour, original) != MH_OK) return false;
  if (MH_EnableHook(fn) != MH_OK) {
    MH_RemoveHook(fn);
    return false;
  }
  *target = fn;
  return true;
}

} // namespace
#endif

bool GhostTrace::Install(uintptr_t gameBase) {
#if defined(_WIN32)
  if (gameBase == 0) return false;
  std::FILE *marker = std::fopen("dr2hook_ghost_trace.txt", "r");
  if (marker == nullptr) {
    Logger::Info("GhostTrace: desligado (crie dr2hook_ghost_trace.txt ao lado do exe e F8).");
    return false;
  }
  std::fclose(marker);
  g_base = gameBase;
  const MH_STATUS init = MH_Initialize();
  if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) return false;
  g_file = std::fopen("dr2hook_ghost_trace.log", "wb");
  if (g_file == nullptr) {
    Logger::Error("GhostTrace: nao consegui abrir dr2hook_ghost_trace.log.");
    return false;
  }
  for (auto &slot : g_ctls) slot.store(nullptr);
  g_frame = g_cSys = g_cCtl = g_cEval = g_cApply = 0;
  g_epoch = 0;
  g_chainEpoch = ~0u;
  g_buf.clear();
  g_written = 0;
  Line("# GhostTrace base=%llx. Linhas: SYS (sistema, 1 por quadro), CTL (1 por fantasma, "
       "pre/pos do atualizador), EVAL (EvaluateGhostState), APPLY (aplica ao corpo), "
       "BEAT (100 ms, sobrevive a pausa), STATE/STACK/CHAIN/DUMP (troca de tela). "
       "Detalhes em docs/reverse_engineering/investigations/ghost-pause-trace-prompt.md",
       static_cast<unsigned long long>(gameBase));

  const bool sys = HookAt(kSystemUpdateRva, kSystemUpdatePrologue, sizeof(kSystemUpdatePrologue),
                          reinterpret_cast<void *>(&DetourSystem),
                          reinterpret_cast<void **>(&g_origSystem), &g_sysTarget, "SystemUpdate");
  const bool ctl = HookAt(kControllerUpdateRva, kControllerUpdatePrologue,
                          sizeof(kControllerUpdatePrologue),
                          reinterpret_cast<void *>(&DetourController),
                          reinterpret_cast<void **>(&g_origController), &g_ctlTarget,
                          "ControllerUpdate");
  const bool apply = HookAt(kApplyRva, kApplyPrologue, sizeof(kApplyPrologue),
                            reinterpret_cast<void *>(&DetourApply),
                            reinterpret_cast<void **>(&g_origApply), &g_applyTarget, "Apply");
  Line("# hooks: sistema=%d controlador=%d aplica=%d (EVAL vem do hook do GhostLab)", sys, ctl,
       apply);
  s_enabled.store(true);
  g_running.store(true);
  g_beat = std::thread(BeatLoop);
  Logger::Info(std::string("GhostTrace: ligado (sistema ") + (sys ? "ok" : "FALHOU") +
               ", controlador " + (ctl ? "ok" : "FALHOU") + ", aplica " +
               (apply ? "ok" : "FALHOU") + "); gravando dr2hook_ghost_trace.log.");
  return sys || ctl || apply;
#else
  (void)gameBase;
  return false;
#endif
}

void GhostTrace::Shutdown() {
#if defined(_WIN32)
  s_enabled.store(false);
  g_running.store(false);
  if (g_beat.joinable()) g_beat.join();
  for (void *target : {g_sysTarget, g_ctlTarget, g_applyTarget}) {
    if (target != nullptr) MH_DisableHook(target);
  }
  for (int waited = 0; g_inFlight.load() > 0 && waited < 2000; ++waited) Sleep(1);
  for (void **target : {&g_sysTarget, &g_ctlTarget, &g_applyTarget}) {
    if (*target != nullptr) {
      MH_RemoveHook(*target);
      *target = nullptr;
    }
  }
  Flush();
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_file != nullptr) {
    std::fclose(g_file);
    g_file = nullptr;
  }
#endif
}

void GhostTrace::OnEvaluate(const uint8_t *owner, const void *time, int result,
                            const uint8_t *out) {
#if defined(_WIN32)
  g_cEval.fetch_add(1);
  uint64_t raw = 0;
  double asDouble = 0;
  if (Readable(time, 8)) {
    std::memcpy(&raw, time, 8);
    std::memcpy(&asDouble, time, 8);
  }
  const bool good = out != nullptr && Readable(out, 0x78);
  const Vec3 pos = good ? ReadVec3(out + 0x30) : Vec3{};
  const Vec3 vel = good ? ReadVec3(out + 0x40) : Vec3{};
  const float offset = *reinterpret_cast<const float *>(g_base + kTimeOffsetRva);
  const uint8_t *flags = reinterpret_cast<const uint8_t *>(g_base + kTimeFlagsRva);
  Line("EVAL f=%llu own=%p t=%016llx d=%.6f off=%.4f fl=%d,%d res=%d valid=%u "
       "pos=%.2f,%.2f,%.2f vel=%.3f,%.3f,%.3f prog=%.5f t58=%08x",
       static_cast<unsigned long long>(g_frame.load()), owner, static_cast<unsigned long long>(raw),
       asDouble, offset, flags[0], flags[1], result, good ? out[0x60] : 0u, pos.x, pos.y, pos.z,
       vel.x, vel.y, vel.z, good ? Rd<float>(out, 0x54) : 0.f, good ? Rd<uint32_t>(out, 0x58) : 0u);
#else
  (void)owner;
  (void)time;
  (void)result;
  (void)out;
#endif
}

} // namespace dr2hook
