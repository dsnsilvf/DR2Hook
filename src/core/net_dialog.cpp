#include "dr2hook/net_dialog.h"
#include "dr2hook/logger.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>

#if defined(_WIN32)
#include <MinHook.h>
#include <windows.h>
#endif

namespace dr2hook {
namespace {

#if defined(_WIN32)

// StateEgoNetSignIn: login na RaceNet a cada reinicio da especial. Na entrada
// (OnEnter) abre net_egonet_signin_wait_msg ("CONTATANDO SERVIDOR"); no Update
// tenta a rede e, sem ela (NetworkGuard), abre net_egonet_error_generic_error_msg
// ("FALHA DE CONEXAO"). Os dois so fazem isso com o byte "online" ligado
// ([0x141695200]+0x1b52); desligado, o Update sai direto pelo link "failed",
// o mesmo destino do OK no popup. O byte e desligado so durante essas chamadas.
constexpr uintptr_t kSignInEnterRva = 0x61d260;
constexpr uint8_t kSignInEnterPrologue[] = {
    0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x74,
    0x24, 0x18, 0x57, 0x48, 0x83, 0xec, 0x50};
constexpr uintptr_t kSignInUpdateRva = 0x623010;
constexpr uint8_t kSignInUpdatePrologue[] = {
    0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c, 0x24, 0x18, 0x56,
    0x57, 0x41, 0x56, 0x48, 0x81, 0xec, 0x80, 0x00, 0x00, 0x00};
constexpr uintptr_t kNetManagerPtrRva = 0x1695200;
constexpr size_t kOnlineFlagOffset = 0x1b52;

// StateNetWaitForNetwork::Update(state, &link). No reinicio da especial, com
// a flag de rede ligada, segura o fluxo ate 5000 ms ("CONTATANDO SERVIDOR").
// Offline a espera nao leva a nada: sai logo pelo link "next".
constexpr uintptr_t kWaitForNetworkUpdateRva = 0x6253a0;
constexpr uint8_t kWaitForNetworkUpdatePrologue[] = {
    0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74,
    0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20};
// Hash global do link "next", lido pelo proprio Update.
constexpr uintptr_t kNextLinkRva = 0x15b9458;

using EnterFn = void (*)(void *);
using UpdateFn = void *(*)(void *, void *);
EnterFn g_originalSignInEnter = nullptr;
UpdateFn g_originalSignInUpdate = nullptr;
UpdateFn g_originalWaitUpdate = nullptr;
void *g_signInEnterTarget = nullptr;
void *g_signInUpdateTarget = nullptr;
void *g_waitUpdateTarget = nullptr;
uintptr_t g_gameBase = 0;
std::atomic<int> g_inFlight{0};
std::atomic<void *> g_lastSilenced{nullptr};
std::atomic<void *> g_lastSkipped{nullptr};

// Desliga o byte "online" enquanto vive; restaura o valor anterior.
class OfflineScope {
public:
  OfflineScope() {
    const uintptr_t manager =
        *reinterpret_cast<const uintptr_t *>(g_gameBase + kNetManagerPtrRva);
    if (manager == 0) return;
    flag_ = reinterpret_cast<uint8_t *>(manager + kOnlineFlagOffset);
    saved_ = *flag_;
    *flag_ = 0;
  }
  ~OfflineScope() {
    if (flag_ != nullptr) *flag_ = saved_;
  }
  OfflineScope(const OfflineScope &) = delete;
  OfflineScope &operator=(const OfflineScope &) = delete;

private:
  uint8_t *flag_ = nullptr;
  uint8_t saved_ = 0;
};

void DetourSignInEnter(void *state) {
  ++g_inFlight;
  {
    OfflineScope offline;
    g_originalSignInEnter(state);
  }
  if (g_lastSilenced.exchange(state) != state) {
    Logger::Info("NetDialog: login na RaceNet pulado (offline).");
  }
  --g_inFlight;
}

void *DetourSignInUpdate(void *state, void *link) {
  ++g_inFlight;
  void *result;
  {
    OfflineScope offline;
    result = g_originalSignInUpdate(state, link);
  }
  --g_inFlight;
  return result;
}

void *DetourWaitUpdate(void *state, void *link) {
  ++g_inFlight;
  void *result = g_originalWaitUpdate(state, link);
  *static_cast<uint64_t *>(link) =
      *reinterpret_cast<const uint64_t *>(g_gameBase + kNextLinkRva);
  if (g_lastSkipped.exchange(state) != state) {
    Logger::Info("NetDialog: espera pela rede pulada (offline).");
  }
  --g_inFlight;
  return result;
}

bool Hook(uintptr_t rva, const uint8_t *prologue, size_t size, void *detour,
          void **original, void **target, const char *name) {
  void *fn = reinterpret_cast<void *>(g_gameBase + rva);
  if (std::memcmp(fn, prologue, size) != 0) {
    Logger::Warn(std::string("NetDialog: prologo de ") + name +
                 " diferente do esperado; hook abortado.");
    return false;
  }
  if (MH_CreateHook(fn, detour, original) != MH_OK) {
    Logger::Error(std::string("NetDialog: falha ao criar hook de ") + name);
    return false;
  }
  if (MH_EnableHook(fn) != MH_OK) {
    MH_RemoveHook(fn);
    Logger::Error(std::string("NetDialog: falha ao habilitar hook de ") + name);
    return false;
  }
  *target = fn;
  return true;
}

#endif

} // namespace

bool NetDialog::Install(uintptr_t gameBase) {
#if defined(_WIN32)
  if (gameBase == 0) return false;
  g_gameBase = gameBase;
  const MH_STATUS init = MH_Initialize();
  if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) return false;
  const bool signIn =
      Hook(kSignInEnterRva, kSignInEnterPrologue,
           sizeof(kSignInEnterPrologue),
           reinterpret_cast<void *>(&DetourSignInEnter),
           reinterpret_cast<void **>(&g_originalSignInEnter),
           &g_signInEnterTarget, "StateEgoNetSignIn::OnEnter") &&
      Hook(kSignInUpdateRva, kSignInUpdatePrologue,
           sizeof(kSignInUpdatePrologue),
           reinterpret_cast<void *>(&DetourSignInUpdate),
           reinterpret_cast<void **>(&g_originalSignInUpdate),
           &g_signInUpdateTarget, "StateEgoNetSignIn::Update");
  const bool wait =
      Hook(kWaitForNetworkUpdateRva, kWaitForNetworkUpdatePrologue,
           sizeof(kWaitForNetworkUpdatePrologue),
           reinterpret_cast<void *>(&DetourWaitUpdate),
           reinterpret_cast<void **>(&g_originalWaitUpdate),
           &g_waitUpdateTarget, "StateNetWaitForNetwork");
  Logger::Info(std::string("NetDialog: hooks (login pulado ") +
               (signIn ? "ok" : "FALHOU") + ", espera pela rede " +
               (wait ? "ok" : "FALHOU") + ").");
  return signIn || wait;
#else
  (void)gameBase;
  return false;
#endif
}

void NetDialog::Shutdown() {
#if defined(_WIN32)
  for (void *target :
       {g_signInEnterTarget, g_signInUpdateTarget, g_waitUpdateTarget}) {
    if (target != nullptr) MH_DisableHook(target);
  }
  for (int waited = 0; g_inFlight.load() > 0 && waited < 2000; ++waited) {
    Sleep(1);
  }
  for (void **target :
       {&g_signInEnterTarget, &g_signInUpdateTarget, &g_waitUpdateTarget}) {
    if (*target != nullptr) {
      MH_RemoveHook(*target);
      *target = nullptr;
    }
  }
  g_lastSilenced = nullptr;
  g_lastSkipped = nullptr;
#endif
}

} // namespace dr2hook
