#include "dr2hook/pause_menu.h"
#include "dr2hook/host.h"
#include "dr2hook/native_screen.h"

#include <MinHook.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>

namespace dr2hook {
namespace {

constexpr uintptr_t kImageBase = 0x140000000;
constexpr uintptr_t kPredicateRva = 0x140d09380 - kImageBase;
constexpr uintptr_t kDispatchRva = 0x140285640 - kImageBase;
constexpr uintptr_t kApplyTextRva = 0x140d040c0 - kImageBase;
constexpr uintptr_t kPauseVtableRva = 0x141250b50 - kImageBase;
constexpr uintptr_t kItemVtableRva = 0x1413d8058 - kImageBase;
constexpr uintptr_t kConditionVtableRva = 0x1413d3100 - kImageBase;
constexpr uintptr_t kStaticTextVtableRva = 0x1413d2e58 - kImageBase;
constexpr uint8_t kPredicateBytes[] = {0x80, 0x79, 0x28, 0x00,
                                       0x0f, 0x94, 0xc0, 0xc3};
constexpr uint8_t kDispatchBytes[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x55,
                                      0x56, 0x57, 0x48, 0x83, 0xec, 0x30,
                                      0x48, 0x8b, 0xfa, 0x48, 0x8b, 0xe9};
constexpr uint8_t kApplyTextBytes[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x55,
                                       0x48, 0x8d, 0x6c, 0x24, 0xa9, 0x48,
                                       0x81, 0xec, 0xc0, 0x00, 0x00, 0x00};

constexpr const char kVisibilityPath[] = "ui.pause_menu.reset_view_available";
// Helper que grava restart_available e restart_label (0x1402a4bd0). Com o
// pedido ligado ele roda com dl = 0, o caminho completo de disponibilidade e
// de rotulo (so ai o texto e montado).
constexpr uintptr_t kRestartHelperRva = 0x1402a4bd0 - kImageBase;
constexpr uint8_t kRestartHelperBytes[] = {0x48, 0x89, 0x5c, 0x24, 0x10,
                                           0x48, 0x89, 0x74, 0x24, 0x18};
using RestartHelperFn = void (*)(void *screen, uint8_t flag);
RestartHelperFn g_originalRestartHelper = nullptr;

constexpr const char kRestartPath[] = "ui.pause_menu.restart_available";
constexpr const char kEventName[] = "reset_view";
constexpr const char kOriginalKey[] = "lng_vr_reset_view";
constexpr const char kLabel[] = "DR2 Hook";
constexpr size_t kMaxChildren = 16;

using PredicateFn = bool (*)(void *condition);
using DispatchFn = bool (*)(void *screen, const char *eventName);
using ApplyTextFn = void (*)(void *textBinding);

uintptr_t g_base = 0;
uintptr_t g_imageEnd = 0;
std::atomic<uintptr_t> g_visibilityPath{0};
PredicateFn g_originalPredicate = nullptr;
DispatchFn g_originalDispatch = nullptr;
ApplyTextFn g_originalApplyText = nullptr;
std::atomic<uintptr_t> g_ownItem{0};
std::atomic<bool> g_hijacked{false};
std::atomic<bool> g_activationPending{false};

template <typename T> T Field(uintptr_t object, uintptr_t offset) {
  T value{};
  std::memcpy(&value, reinterpret_cast<const void *>(object + offset),
              sizeof(T));
  return value;
}

bool IsReadable(uintptr_t address, size_t size) {
  if (address < 0x10000 || address + size < address) {
    return false;
  }
  MEMORY_BASIC_INFORMATION info{};
  if (VirtualQuery(reinterpret_cast<const void *>(address), &info,
                   sizeof(info)) == 0) {
    return false;
  }
  constexpr DWORD kReadable = PAGE_READONLY | PAGE_READWRITE |
                              PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                              PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
  const auto regionEnd =
      reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
  return info.State == MEM_COMMIT && (info.Protect & kReadable) != 0 &&
         (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) == 0 &&
         address + size <= regionEnd;
}

// +0xa0 aponta para um pool de caminhos que o jogo monta em runtime dentro da
// própria imagem, e o endereço muda a cada sessão. Só se lê o texto quando o
// ponteiro cai dentro da imagem; ponteiros de heap não são seguidos.
bool IsOwnCondition(uintptr_t condition) {
  if (Field<uintptr_t>(condition, 0) != g_base + kConditionVtableRva) {
    return false;
  }
  const auto path = Field<uintptr_t>(condition, 0xa0);
  if (path == 0) {
    return false;
  }
  if (path == g_visibilityPath.load(std::memory_order_relaxed)) {
    return true;
  }
  if (path < g_base || path + sizeof(kVisibilityPath) > g_imageEnd) {
    return false;
  }
  if (std::memcmp(reinterpret_cast<const void *>(path), kVisibilityPath,
                  sizeof(kVisibilityPath)) != 0) {
    return false;
  }
  g_visibilityPath.store(path, std::memory_order_relaxed);
  return true;
}

// Reiniciar no menu de pausa durante o dano terminal (o jogo o esconde ali).
// O core liga o pedido so em sessao offline, ver terminal_damage.cpp.
std::atomic<bool> g_restartVisible{false};
std::atomic<uintptr_t> g_restartPath{0};

bool IsRestartCondition(uintptr_t condition) {
  if (Field<uintptr_t>(condition, 0) != g_base + kConditionVtableRva) {
    return false;
  }
  const auto path = Field<uintptr_t>(condition, 0xa0);
  if (path == 0) {
    return false;
  }
  if (path == g_restartPath.load(std::memory_order_relaxed)) {
    return true;
  }
  if (path < g_base || path + sizeof(kRestartPath) > g_imageEnd ||
      std::memcmp(reinterpret_cast<const void *>(path), kRestartPath,
                  sizeof(kRestartPath)) != 0) {
    return false;
  }
  g_restartPath.store(path, std::memory_order_relaxed);
  return true;
}

bool ItemHasOwnCondition(uintptr_t item) {
  if (!IsReadable(item, 0x1d0) ||
      Field<uintptr_t>(item, 0) != g_base + kItemVtableRva) {
    return false;
  }
  const auto begin = Field<uintptr_t>(item, 0x1c0);
  const auto end = Field<uintptr_t>(item, 0x1c8);
  if (end <= begin || (end - begin) / 8 > kMaxChildren ||
      !IsReadable(begin, end - begin)) {
    return false;
  }
  for (uintptr_t slot = begin; slot < end; slot += 8) {
    const auto child = Field<uintptr_t>(slot, 0);
    if (IsReadable(child, 0xa8) && IsOwnCondition(child)) {
      return true;
    }
  }
  return false;
}

// Troca a chave pelo rótulo no próprio buffer da string: o jogo aloca
// len+0x11 bytes e guarda (len+1)<<8 | refcount em +0x08.
void RelabelInPlace(uintptr_t textBinding) {
  auto *header = Field<uint8_t *>(textBinding, 0x28);
  if (!IsReadable(reinterpret_cast<uintptr_t>(header), 0x10)) {
    return;
  }
  const auto word = Field<uint32_t>(reinterpret_cast<uintptr_t>(header), 0x08);
  if (!IsReadable(reinterpret_cast<uintptr_t>(header) + 0x10, word >> 8)) {
    return;
  }
  char *text = reinterpret_cast<char *>(header + 0x10);
  auto *explicitFlag = reinterpret_cast<uint8_t *>(textBinding + 0x30);
  if (std::strcmp(text, kLabel) == 0) {
    *explicitFlag = 1;
    return;
  }
  if (std::strcmp(text, kOriginalKey) != 0) {
    return;
  }
  const uint32_t refcount = word & 0xffu;
  const uint32_t capacity = word >> 8;
  if (refcount != 1 || capacity < sizeof(kLabel)) {
    return;
  }
  std::memcpy(text, kLabel, sizeof(kLabel));
  const uint32_t relabeled =
      (static_cast<uint32_t>(sizeof(kLabel)) << 8) | refcount;
  std::memcpy(header + 0x08, &relabeled, sizeof(relabeled));
  *explicitFlag = 1;
}

// Condições das posições das telas dr2hook: o nó de dados não existe, então
// quem decide é native_screen.cpp. Só se lê o caminho dentro da imagem.
bool NativeSlotVisibility(uintptr_t condition, bool &hidden) {
  if (Field<uintptr_t>(condition, 0) != g_base + kConditionVtableRva) {
    return false;
  }
  const auto path = Field<uintptr_t>(condition, 0xa0);
  if (path < g_base || path >= g_imageEnd) {
    return false;
  }
  return NativeScreenVisibility(reinterpret_cast<const char *>(path),
                                g_imageEnd - path,
                                Field<uintptr_t>(condition, 0x10), hidden);
}

// Diagnostico temporario: registra cada condicao ui.pause_menu.* e o resultado
// original (uma vez por caminho e valor), para achar o que esconde o Reiniciar.
void TraceCondition(uintptr_t condition, bool hidden) {
  if (Field<uintptr_t>(condition, 0) != g_base + kConditionVtableRva) return;
  const auto path = Field<uintptr_t>(condition, 0xa0);
  constexpr char kPrefix[] = "ui.pause_menu.";
  if (path < g_base || path + 64 > g_imageEnd ||
      std::memcmp(reinterpret_cast<const void *>(path), kPrefix, sizeof(kPrefix) - 1) != 0) {
    return;
  }
  static std::atomic<uint64_t> seen[48];
  const uint64_t key = (static_cast<uint64_t>(path) << 1) | (hidden ? 1u : 0u);
  for (auto &slot : seen) {
    uint64_t cur = slot.load(std::memory_order_relaxed);
    if (cur == key) return;
    if (cur == 0 && slot.compare_exchange_strong(cur, key)) {
      char buf[160];
      std::snprintf(buf, sizeof(buf), "PauseMenu: condicao '%.60s' item=%llx escondido=%d",
                    reinterpret_cast<const char *>(path),
                    static_cast<unsigned long long>(Field<uintptr_t>(condition, 0x10)),
                    hidden ? 1 : 0);
      HostLog(buf);
      return;
    }
  }
}

void DetourRestartHelper(void *screen, uint8_t flag) {
  const bool forced = g_restartVisible.load(std::memory_order_relaxed);
  if (forced) HostLog("PauseMenu: helper do Reiniciar com dl=0.");
  g_originalRestartHelper(screen, forced ? 0 : flag);
}

bool DetourPredicate(void *condition) {
  NativeScreenTick();
  bool hidden = g_originalPredicate(condition);
  const auto self = reinterpret_cast<uintptr_t>(condition);
  TraceCondition(self, hidden);
  if (NativeSlotVisibility(self, hidden)) {
    return hidden;
  }
  if (hidden && g_restartVisible.load(std::memory_order_relaxed) &&
      IsRestartCondition(self)) {
    HostLog("PauseMenu: Reiniciar liberado (dano terminal).");
    return false;
  }
  if (!IsOwnCondition(self)) {
    return hidden;
  }
  g_ownItem.store(Field<uintptr_t>(self, 0x10), std::memory_order_relaxed);
  g_hijacked.store(hidden, std::memory_order_relaxed);
  return false;
}

bool DetourDispatch(void *screen, const char *eventName) {
  if (eventName != nullptr) {
    HostLog((std::string("PauseMenu: evento '") + eventName + "'.").c_str());
  }
  if (eventName != nullptr && g_hijacked.load(std::memory_order_relaxed) &&
      std::strcmp(eventName, kEventName) == 0) {
    RequestPauseMenuActivation();
    return true;
  }
  return g_originalDispatch(screen, eventName);
}

void DetourApplyText(void *textBinding) {
  const auto binding = reinterpret_cast<uintptr_t>(textBinding);
  if (Field<uintptr_t>(binding, 0) == g_base + kStaticTextVtableRva) {
    const auto item = Field<uintptr_t>(binding, 0x10);
    const bool own = item != 0 &&
                     (item == g_ownItem.load(std::memory_order_relaxed) ||
                      ItemHasOwnCondition(item));
    if (own) {
      RelabelInPlace(binding);
    }
  }
  g_originalApplyText(textBinding);
}

bool Matches(uintptr_t rva, const uint8_t *expected, size_t size) {
  return std::memcmp(reinterpret_cast<const void *>(g_base + rva), expected,
                     size) == 0;
}

bool Hook(uintptr_t rva, void *detour, void **original, const char *name) {
  void *target = reinterpret_cast<void *>(g_base + rva);
  if (MH_CreateHook(target, detour, original) != MH_OK ||
      MH_EnableHook(target) != MH_OK) {
    HostLog((std::string("PauseMenu: falha ao instalar hook ") + name).c_str());
    return false;
  }
  return true;
}

} // namespace

bool InstallPauseMenuHooks() {
  g_base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
  if (g_base == 0) {
    return false;
  }
  const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(g_base);
  const auto *nt =
      reinterpret_cast<const IMAGE_NT_HEADERS *>(g_base + dos->e_lfanew);
  g_imageEnd = g_base + nt->OptionalHeader.SizeOfImage;

  const bool dispatchInVtable =
      Field<uintptr_t>(g_base + kPauseVtableRva, 0x88) ==
      g_base + kDispatchRva;
  if (!dispatchInVtable ||
      !Matches(kPredicateRva, kPredicateBytes, sizeof(kPredicateBytes)) ||
      !Matches(kDispatchRva, kDispatchBytes, sizeof(kDispatchBytes)) ||
      !Matches(kApplyTextRva, kApplyTextBytes, sizeof(kApplyTextBytes))) {
    HostLog("PauseMenu: executavel diferente do esperado; item DR2 Hook "
            "desativado.");
    return false;
  }

  if (!Hook(kApplyTextRva, reinterpret_cast<void *>(&DetourApplyText),
            reinterpret_cast<void **>(&g_originalApplyText), "ApplyText") ||
      !Hook(kDispatchRva, reinterpret_cast<void *>(&DetourDispatch),
            reinterpret_cast<void **>(&g_originalDispatch), "Dispatch") ||
      !Hook(kPredicateRva, reinterpret_cast<void *>(&DetourPredicate),
            reinterpret_cast<void **>(&g_originalPredicate), "Predicate")) {
    return false;
  }

  if (Matches(kRestartHelperRva, kRestartHelperBytes, sizeof(kRestartHelperBytes))) {
    Hook(kRestartHelperRva, reinterpret_cast<void *>(&DetourRestartHelper),
         reinterpret_cast<void **>(&g_originalRestartHelper), "RestartHelper");
  }

  HostLog("PauseMenu: item DR2 Hook instalado no menu de pausa.");
  return true;
}

void RequestPauseMenuActivation() { g_activationPending.store(true); }

void RefreshItemText(uintptr_t item) {
  if (g_originalApplyText == nullptr || !IsReadable(item, 0x1d0) ||
      Field<uintptr_t>(item, 0) != g_base + kItemVtableRva) {
    return;
  }
  const auto begin = Field<uintptr_t>(item, 0x1c0);
  const auto end = Field<uintptr_t>(item, 0x1c8);
  if (end <= begin || (end - begin) / 8 > kMaxChildren ||
      !IsReadable(begin, end - begin)) {
    return;
  }
  for (uintptr_t slot = begin; slot < end; slot += 8) {
    const auto child = Field<uintptr_t>(slot, 0);
    if (IsReadable(child, 0x38) &&
        Field<uintptr_t>(child, 0) == g_base + kStaticTextVtableRva) {
      g_originalApplyText(reinterpret_cast<void *>(child));
    }
  }
}

void SetRestartVisible(bool visible) {
  g_restartVisible.store(visible, std::memory_order_relaxed);
}

bool ConsumePauseMenuActivation() {
  return g_activationPending.exchange(false);
}

} // namespace dr2hook

int Dr2Host_SetRestartVisible(int visible) {
  dr2hook::HostLog(visible != 0 ? "PauseMenu: pedido para liberar Reiniciar."
                       : "PauseMenu: Reiniciar volta ao normal.");
  dr2hook::SetRestartVisible(visible != 0);
  return 1;
}

int Dr2Host_ConsumePauseMenuRequest() {
  return dr2hook::ConsumePauseMenuActivation() ? 1 : 0;
}
