#include "dr2hook/native_screen.h"
#include "dr2hook/host.h"
#include "dr2hook/pause_menu.h"
#include "dr2hook/ui_patch.h"

#include <MinHook.h>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace dr2hook {
namespace {

namespace up = ui_patch;

constexpr uintptr_t kImageBase = 0x140000000;
constexpr uintptr_t kFeCoreVtableRva = 0x141256e98 - kImageBase;
constexpr uintptr_t kDispatchSlot = 0x88;
constexpr uintptr_t kEnterDataSlot = 0x80;
// Os dois slots apontam para stubs compartilhados com outras classes
// (`xor al, al; ret` e `ret 0`): por isso se troca o ponteiro na vtable.
constexpr uintptr_t kDispatchStubRva = 0x140ebb300 - kImageBase;
constexpr uint8_t kDispatchStubBytes[] = {0x32, 0xc0, 0xc3};
constexpr uintptr_t kEnterDataStubRva = 0x1406bfe70 - kImageBase;
constexpr uint8_t kEnterDataStubBytes[] = {0xc2, 0x00, 0x00};
// StateScreenFECore: id, store, Path de ui.<tela> (depois do Enter).
constexpr uintptr_t kStateIdOffset = 0x08;
constexpr uintptr_t kStateStoreOffset = 0x38;
constexpr uintptr_t kStateRootOffset = 0x40;
// neon::NeLanguageStringHandler, vtable 0x14126a2f0 slot +8: chave -> texto
// UTF-8 da tabela, ou nulo. Quem chama copia o texto na hora.
constexpr uintptr_t kLanguageLookupRva = 0x1403a8820 - kImageBase;
constexpr uint8_t kLanguageLookupBytes[] = {
    0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c, 0x24, 0x18, 0x56, 0x48,
    0x83, 0xec, 0x20, 0x48, 0x8b, 0x99, 0x88, 0x00, 0x00, 0x00, 0x48, 0x8b};
constexpr char kCapsSuffix[] = "_caps";

// TabController::Setup(ctrl, vec<{screen, label, bool}>*, índice, prefixo)
// (docs/reverse_engineering/ui_tabs.md). O menu principal (0x1402ff0b0) monta
// a lista dele e chama este Setup com ctrl = estado + 0x128; o estado guarda um
// código por aba em +0x2a0 (ptr u32), +0x2a8 (capacidade), +0x2b0 (contagem),
// lido pelo índice da aba atual (0x140312490).
constexpr uintptr_t kTabSetupRva = 0x140369320 - kImageBase;
constexpr uint8_t kTabSetupBytes[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x55, 0x56,
                                      0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56};
constexpr uintptr_t kTabControllerOffset = 0x128;
constexpr uintptr_t kMainMenuCodesOffset = 0x2a0;
constexpr char kMainMenuMarkerTab[] = "options_extras";
// Código de options_extras: a aba nossa é tratada como página de opções.
constexpr uint32_t kMainMenuTabCode = 2;

struct TabEntry {
  const char *screen;
  const char *label;
  uint8_t enabled;
};
static_assert(sizeof(TabEntry) == 0x18);

struct TabVector {
  TabEntry *data;
  uint64_t capacity;
  uint64_t count;
};

struct CodeArray {
  uint32_t *data;
  uint64_t capacity;
  uint64_t count;
};
constexpr size_t kMaxQueuedEvents = 64;

// Data store da UI (docs/reverse_engineering/ui_tabs.md). O spinlock em
// store+0x98 não é recursivo: as funções que travam sozinhas nunca são
// chamadas com ele na mão.
constexpr uintptr_t kStoreGlobalRva = 0x142016ca0 - kImageBase;
constexpr uintptr_t kStoreLockOffset = 0x98;
constexpr uintptr_t kNodeValueOffset = 0x30;
constexpr uintptr_t kNodeTypeOffset = 0x38;
constexpr uint8_t kNodeInt = 5;
constexpr uint8_t kNodeIntAlt = 6;

struct DsPath {
  uint64_t hash;
  uint64_t reserved;
  uint64_t kind;
};
static_assert(sizeof(DsPath) == 0x18);

using PathFromStringFn = DsPath *(*)(DsPath *, const char *);
using PathEmptyFn = DsPath *(*)(DsPath *);
using ChildFn = int (*)(void *, const DsPath *, const DsPath *, DsPath *);
using AppendFn = int (*)(void *, const DsPath *, DsPath *);
using FindOrCreateFn = int (*)(void *, const DsPath *, const DsPath *, DsPath *,
                               void **);
using LockFn = void (*)(void *, uint32_t *);
using SetStringFn = int (*)(void *, const DsPath *, const char *);
using SetIntFn = int (*)(void *, const DsPath *, const int32_t *);
using FindFn = void *(*)(void *, uint64_t);

struct GameFunction {
  uintptr_t address;
  std::vector<uint8_t> prologue;
  void **target;
};

struct DataStoreApi {
  PathFromStringFn pathFromString = nullptr;
  PathEmptyFn pathEmpty = nullptr;
  ChildFn child = nullptr;          // trava sozinha
  ChildFn array = nullptr;          // trava sozinha
  AppendFn append = nullptr;        // trava sozinha
  FindOrCreateFn findOrCreate = nullptr;
  LockFn lock = nullptr;
  LockFn unlock = nullptr;
  SetStringFn setString = nullptr;  // tipo 0xe
  SetIntFn setInt = nullptr;        // tipo 5
  FindFn find = nullptr;
  bool ready = false;
} g_ds;

using DispatchFn = bool (*)(void *state, const char *eventName);
using EnterDataFn = void (*)(void *state);
using LookupFn = const char *(*)(void *handler, const char *key);
using TabSetupFn = void (*)(void *controller, TabVector *tabs, uint32_t index,
                            const char *prefix);

struct MenuOption {
  std::string label;
  std::vector<std::string> values;
  int index = 0;
  int kind = kDr2MenuButton;
  std::string description;
  bool operator==(const MenuOption &other) const {
    return label == other.label && values == other.values &&
           index == other.index && kind == other.kind &&
           description == other.description;
  }
};

struct MenuMod {
  std::string name;
  std::string description;
  std::vector<MenuOption> options;
  bool operator==(const MenuMod &other) const {
    return name == other.name && description == other.description &&
           options == other.options;
  }
};

struct MenuEvent {
  int mod;
  int option;
  int value;
};

// Combo da tela de mod observado a cada quadro.
struct ComboWatch {
  uint64_t hash = 0;
  int32_t value = 0;
  bool active = false;
};

uintptr_t g_base = 0;
DispatchFn g_originalDispatch = nullptr;
EnterDataFn g_originalEnterData = nullptr;
LookupFn g_originalLookup = nullptr;
TabSetupFn g_originalTabSetup = nullptr;
std::atomic<bool> g_reloadModsPending{false};

// Escrito pelo core no Present; lido pela busca de idioma, cuja thread não
// foi identificada, e pelos hooks da UI.
std::mutex g_menuMutex;
std::vector<MenuMod> g_menu;
std::deque<MenuEvent> g_events;
std::atomic<uint32_t> g_menuVersion{1};
std::atomic<int> g_selectedMod{-1};
// Aba aberta no próximo Enter do hub: Mods quando se volta da tela de um mod.
std::atomic<unsigned> g_returnTab{0};

// Só a thread da UI toca: Enter do estado e predicado de visibilidade.
ComboWatch g_combos[up::kListSlots];
int g_comboMod = -1;
std::atomic<bool> g_combosActive{false};
bool g_rootHashLogged = false;
// Painel da direita da tela de mod (sidebar.title/description, BTextData).
// Cada linha grava o índice em foco em selected_index (IBItemFlowIndex).
DsPath g_sidebarPath{};
bool g_sidebarReady = false;
int g_sidebarIndex = -1;
uint32_t g_sidebarVersion = 0;
constexpr char kModHint[] =
    "Use left and right to change a value, and A to run an action.";

template <size_t N> bool StartsWith(const char *text, const char (&prefix)[N]) {
  return std::strncmp(text, prefix, N - 1) == 0;
}

// Índice de linha num sufixo decimal seguido de fim.
int SlotIndex(const char *suffix) {
  if (suffix[0] < '0' || suffix[0] > '9') {
    return -1;
  }
  char *end = nullptr;
  const unsigned long index = std::strtoul(suffix, &end, 10);
  if (*end != '\0' || (suffix[0] == '0' && suffix[1] != '\0') ||
      index >= up::kListSlots) {
    return -1;
  }
  return static_cast<int>(index);
}

size_t ModCount() {
  std::lock_guard<std::mutex> lock(g_menuMutex);
  return g_menu.size();
}

size_t OptionCount(int mod) {
  std::lock_guard<std::mutex> lock(g_menuMutex);
  return mod >= 0 && static_cast<size_t>(mod) < g_menu.size()
             ? g_menu[mod].options.size()
             : 0;
}

void Log(const std::string &message) {
  HostLog(("NativeScreen: " + message).c_str());
}

void QueueEvent(int mod, int option, int value) {
  std::lock_guard<std::mutex> lock(g_menuMutex);
  if (g_events.size() < kMaxQueuedEvents) {
    g_events.push_back({mod, option, value});
  }
}

// ---------------------------------------------------------------------------
// Data store
// ---------------------------------------------------------------------------

constexpr uint64_t kFnvOffset = 0xcbf29ce484222325ull;
constexpr uint64_t kFnvPrime = 0x100000001b3ull;

uint64_t Fnv(uint64_t hash, const void *data, size_t size) {
  const auto *bytes = static_cast<const uint8_t *>(data);
  for (size_t i = 0; i < size; ++i) {
    hash ^= bytes[i];
    hash *= kFnvPrime;
  }
  return hash;
}

// Hash de caminho do store (0x140c817c0): FNV-1a por segmento, encadeado
// pelos 8 bytes de cada hash seguinte. Só segmentos separados por ponto.
uint64_t PathHash(std::string_view path) {
  uint64_t accumulated = 0;
  bool first = true;
  while (!path.empty()) {
    const size_t dot = path.find('.');
    const std::string_view segment = path.substr(0, dot);
    const uint64_t hash = Fnv(kFnvOffset, segment.data(), segment.size());
    accumulated = first ? hash : Fnv(accumulated, &hash, sizeof(hash));
    first = false;
    path = dot == std::string_view::npos ? std::string_view{} : path.substr(dot + 1);
  }
  return accumulated;
}

bool ResolveDataStore(uintptr_t base) {
  const std::vector<GameFunction> functions = {
      {0x140c7e320, {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x4c, 0x8b, 0xc2, 0x48, 0x8b, 0xd9},
       reinterpret_cast<void **>(&g_ds.pathFromString)},
      {0x140c7e340, {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0x48, 0x8d, 0x4c},
       reinterpret_cast<void **>(&g_ds.pathEmpty)},
      {0x140c7fd70, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89},
       reinterpret_cast<void **>(&g_ds.child)},
      {0x140c7fee0, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89},
       reinterpret_cast<void **>(&g_ds.array)},
      {0x140c80070, {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48},
       reinterpret_cast<void **>(&g_ds.append)},
      {0x140c80850, {0x4c, 0x89, 0x4c, 0x24, 0x20, 0x4c, 0x89, 0x44, 0x24, 0x18, 0x48, 0x89},
       reinterpret_cast<void **>(&g_ds.findOrCreate)},
      {0x140c81e70, {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xda, 0xb8, 0x00, 0x01},
       reinterpret_cast<void **>(&g_ds.lock)},
      {0x140c83fc0, {0xc7, 0x02, 0x00, 0x00, 0x00, 0x00, 0xc3},
       reinterpret_cast<void **>(&g_ds.unlock)},
      {0x14011eba0, {0x48, 0x89, 0x5c, 0x24, 0x18, 0x56, 0x57, 0x41, 0x56, 0x48, 0x81, 0xec},
       reinterpret_cast<void **>(&g_ds.setString)},
      {0x1401ea8a0, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89},
       reinterpret_cast<void **>(&g_ds.setInt)},
      {0x140c815e0, {0x48, 0x89, 0x5c, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x40, 0x48, 0x8b},
       reinterpret_cast<void **>(&g_ds.find)},
  };
  for (const GameFunction &function : functions) {
    const auto *code =
        reinterpret_cast<const uint8_t *>(base + function.address - kImageBase);
    if (std::memcmp(code, function.prologue.data(), function.prologue.size()) != 0) {
      char text[96];
      std::snprintf(text, sizeof(text),
                    "funcao do data store diferente em 0x%llx",
                    static_cast<unsigned long long>(function.address));
      Log(text);
      return false;
    }
    *function.target = const_cast<uint8_t *>(code);
  }
  g_ds.ready = true;
  return true;
}

class StoreLock {
public:
  explicit StoreLock(void *store)
      : m_store(store),
        m_word(reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(store) +
                                            kStoreLockOffset)) {
    g_ds.lock(m_store, m_word);
  }
  ~StoreLock() { g_ds.unlock(m_store, m_word); }
  StoreLock(const StoreLock &) = delete;
  StoreLock &operator=(const StoreLock &) = delete;

private:
  void *m_store;
  uint32_t *m_word;
};

DsPath EmptyPath() {
  DsPath path{};
  g_ds.pathEmpty(&path);
  return path;
}

DsPath NamePath(const char *name) {
  DsPath path{};
  g_ds.pathFromString(&path, name);
  return path;
}

// Contêiner ou array filho de `parent`; as duas funções travam sozinhas.
bool Container(void *store, const DsPath &parent, const char *name, bool array,
               DsPath &out) {
  const DsPath key = NamePath(name);
  out = EmptyPath();
  return (array ? g_ds.array : g_ds.child)(store, &parent, &key, &out) == 0;
}

bool Append(void *store, const DsPath &array, DsPath &element) {
  element = EmptyPath();
  return g_ds.append(store, &array, &element) == 0;
}

bool SetString(void *store, const DsPath &parent, const char *key,
               const char *value) {
  const DsPath name = NamePath(key);
  DsPath out = EmptyPath();
  StoreLock lock(store);
  return g_ds.findOrCreate(store, &parent, &name, &out, nullptr) == 0 &&
         g_ds.setString(store, &out, value) == 0;
}

bool SetStringAt(void *store, const DsPath &path, const char *value) {
  StoreLock lock(store);
  return g_ds.setString(store, &path, value) == 0;
}

bool SetInt(void *store, const DsPath &parent, const char *key, int32_t value) {
  const DsPath name = NamePath(key);
  DsPath out = EmptyPath();
  StoreLock lock(store);
  return g_ds.findOrCreate(store, &parent, &name, &out, nullptr) == 0 &&
         g_ds.setInt(store, &out, &value) == 0;
}

template <typename T> T Field(const void *base, uintptr_t offset) {
  T value;
  std::memcpy(&value, static_cast<const uint8_t *>(base) + offset, sizeof(T));
  return value;
}

// O Path de ui.<tela> em estado+0x40 tem de bater com o hash reimplementado,
// senão a leitura dos combos por hash fica desligada.
bool RootHashMatches(const void *state, const char *screen) {
  const uint64_t expected = PathHash(std::string("ui.") + screen);
  const uint64_t actual = Field<uint64_t>(state, kStateRootOffset);
  if (expected != actual && !g_rootHashLogged) {
    g_rootHashLogged = true;
    char text[128];
    std::snprintf(text, sizeof(text),
                  "hash de ui.%s nao bate (0x%llx, jogo 0x%llx); combos sem leitura",
                  screen, static_cast<unsigned long long>(expected),
                  static_cast<unsigned long long>(actual));
    Log(text);
  }
  return expected == actual;
}

// Título e texto do painel para a linha `option` do mod `mod`: a descrição da
// opção, ou o nome dela com a descrição do mod. Fora das opções, o mod.
void SidebarText(int mod, int option, std::string &title, std::string &text) {
  std::lock_guard<std::mutex> lock(g_menuMutex);
  if (mod < 0 || static_cast<size_t>(mod) >= g_menu.size()) {
    title = "Mod";
    text = kModHint;
    return;
  }
  const MenuMod &entry = g_menu[mod];
  const std::string modText =
      entry.description.empty() ? kModHint : entry.description + " " + kModHint;
  if (option < 0 || static_cast<size_t>(option) >= entry.options.size()) {
    title = entry.name;
    text = modText;
    return;
  }
  const MenuOption &selected = entry.options[option];
  title = selected.label;
  text = selected.description.empty() ? modText : selected.description;
}

void WriteSidebar(void *store, int option) {
  std::string title, text;
  SidebarText(g_comboMod, option, title, text);
  SetString(store, g_sidebarPath, "title", title.c_str());
  SetString(store, g_sidebarPath, "description", text.c_str());
  g_sidebarIndex = option;
  g_sidebarVersion = g_menuVersion.load();
}

// Enter do hub: tabs.info[i].{screen,label}, tabs.current_index e a
// visibilidade de reserva das linhas da lista de mods.
void PopulateHub(void *store, const DsPath &root) {
  DsPath tabs, info;
  if (!Container(store, root, "tabs", false, tabs) ||
      !Container(store, tabs, "info", true, info)) {
    Log("falha ao criar tabs.info no hub");
    return;
  }
  for (const up::PageDef &page : up::kPages) {
    DsPath element;
    if (!Append(store, info, element) ||
        !SetString(store, element, "screen", page.name) ||
        !SetString(store, element, "label", page.tabLabel)) {
      Log(std::string("falha ao criar a aba ") + page.name);
      return;
    }
  }
  const unsigned tab = g_returnTab.exchange(0);
  SetInt(store, tabs, "current_index", static_cast<int32_t>(tab));

  const size_t mods = ModCount();
  for (size_t i = 0; i < up::kListSlots; ++i) {
    const std::string key = up::kModSlotPath + std::to_string(i);
    SetInt(store, root, key.c_str(), i == 0 || i < mods ? 1 : 0);
  }
}

// Enter da tela de mod: <opt>N.list[] com os textos do combo, <opt>N.index e a
// visibilidade de reserva. Todas as linhas ganham ao menos um valor, para o
// combo nunca ter lista vazia.
void PopulateMod(void *store, const DsPath &root, bool hashOk) {
  const int selected = g_selectedMod.load();
  std::vector<MenuOption> options;
  {
    std::lock_guard<std::mutex> lock(g_menuMutex);
    if (selected >= 0 && static_cast<size_t>(selected) < g_menu.size()) {
      options = g_menu[selected].options;
    }
  }

  g_combosActive.store(false);
  g_comboMod = selected;
  size_t created = 0;
  for (size_t i = 0; i < up::kListSlots; ++i) {
    const MenuOption *option = i < options.size() ? &options[i] : nullptr;
    const std::string name = up::kOptionDataPrefix + std::to_string(i);
    DsPath node, list;
    if (!Container(store, root, name.c_str(), false, node) ||
        !Container(store, node, "list", true, list)) {
      Log("falha ao criar " + name);
      break;
    }
    std::vector<std::string> values =
        option != nullptr ? option->values : std::vector<std::string>{};
    if (values.empty()) {
      values.emplace_back();
    }
    for (const std::string &value : values) {
      DsPath element;
      if (!Append(store, list, element) ||
          !SetStringAt(store, element, value.c_str())) {
        break;
      }
    }
    const int32_t index = option != nullptr ? option->index : 0;
    SetInt(store, node, "index", index);
    const std::string visibility = up::kOptionSlotPath + std::to_string(i);
    SetInt(store, root, visibility.c_str(), i == 0 || option != nullptr ? 1 : 0);

    ComboWatch &watch = g_combos[i];
    watch.active = option != nullptr && option->kind != kDr2MenuButton;
    watch.value = index;
    watch.hash = PathHash(std::string("ui.") + up::kMod.name + "." + name + ".index");
    ++created;
  }
  // IBItemFlowIndex só grava num nó que já existe (em profile_save_management
  // quem cria é o estado do jogo); sem ele o painel não acompanha o foco.
  SetInt(store, root, "selected_index", 0);
  g_sidebarReady = Container(store, root, "sidebar", false, g_sidebarPath);
  if (g_sidebarReady) {
    WriteSidebar(store, 0);
  }
  g_combosActive.store(created == up::kListSlots && hashOk);
}

void DetourEnterData(void *state) {
  g_originalEnterData(state);
  const uint32_t id = Field<uint32_t>(state, kStateIdOffset);
  if (id != up::kHub.stateIdValue && id != up::kMod.stateIdValue) {
    return;
  }
  void *store = Field<void *>(state, kStateStoreOffset);
  if (!g_ds.ready || store == nullptr) {
    return;
  }
  const auto *root =
      reinterpret_cast<const DsPath *>(static_cast<const uint8_t *>(state) +
                                       kStateRootOffset);
  if (id == up::kHub.stateIdValue) {
    PopulateHub(store, *root);
  } else {
    PopulateMod(store, *root, RootHashMatches(state, up::kMod.name));
  }
}

// ---------------------------------------------------------------------------
// Eventos
// ---------------------------------------------------------------------------

bool HandleAction(const char *eventName) {
  if (StartsWith(eventName, up::kOptionEventPrefix)) {
    const int option = SlotIndex(eventName + sizeof(up::kOptionEventPrefix) - 1);
    const int mod = g_selectedMod.load();
    bool button = false;
    {
      std::lock_guard<std::mutex> lock(g_menuMutex);
      button = mod >= 0 && static_cast<size_t>(mod) < g_menu.size() && option >= 0 &&
               static_cast<size_t>(option) < g_menu[mod].options.size() &&
               g_menu[mod].options[option].kind == kDr2MenuButton;
    }
    // Toggle e choice mudam só pelo combo (< >), que mostra o valor.
    if (!button) {
      return true;
    }
    QueueEvent(mod, option, -1);
  } else if (std::strcmp(eventName, up::kOpenOverlayEvent) == 0) {
    RequestPauseMenuActivation();
  } else if (std::strcmp(eventName, up::kReloadModsEvent) == 0) {
    g_reloadModsPending.store(true);
  } else if (std::strcmp(eventName, up::kReloadCoreEvent) == 0) {
    RequestCoreReload();
  } else {
    Log(std::string("evento sem tratador: ") + eventName);
    return true;
  }
  Log(std::string("evento ") + eventName);
  return true;
}

bool IsOwnState(uint32_t stateId) {
  for (const up::ScreenDef &def : up::kScreens) {
    if (stateId == def.stateIdValue) {
      return true;
    }
  }
  return false;
}

bool DetourDispatch(void *state, const char *eventName) {
  const uint32_t stateId = Field<uint32_t>(state, kStateIdOffset);
  if (eventName == nullptr || !IsOwnState(stateId)) {
    return g_originalDispatch(state, eventName);
  }
  if (StartsWith(eventName, up::kActionPrefix)) {
    return HandleAction(eventName);
  }
  if (StartsWith(eventName, up::kNavModPrefix)) {
    const int mod = SlotIndex(eventName + sizeof(up::kNavModPrefix) - 1);
    if (mod < 0 || static_cast<size_t>(mod) >= ModCount()) {
      return true;
    }
    // Falso deixa o runner seguir o link até a tela do mod.
    g_selectedMod.store(mod);
    g_returnTab.store(up::kModsTab);
    Log(std::string("mod selecionado ") + eventName);
  }
  return g_originalDispatch(state, eventName);
}

// ---------------------------------------------------------------------------
// Textos
// ---------------------------------------------------------------------------

std::string Upper(std::string text) {
  for (char &c : text) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  return text;
}

// Texto de uma chave lng_dr2hook_*, sem o sufixo _caps. Falso se não for nossa.
bool MenuText(const std::string &key, std::string &text) {
  std::lock_guard<std::mutex> lock(g_menuMutex);
  const int selected = g_selectedMod.load();
  const MenuMod *mod = selected >= 0 && static_cast<size_t>(selected) < g_menu.size()
                           ? &g_menu[selected]
                           : nullptr;
  if (key == up::kMainPage.titleKey || key == up::kMainPage.infoTitleKey) {
    text = "DR2 HOOK";
  } else if (key == up::kMainPage.infoTextKey) {
    text = "Native menu of DR2 Hook. Use LB and RB to switch between tabs.";
  } else if (key == up::kModsPage.titleKey || key == up::kModsPage.infoTitleKey) {
    text = "MODS";
  } else if (key == up::kModsPage.infoTextKey) {
    text = g_menu.empty()
               ? "No mods loaded. Put mods in the mods folder and use Reload Lua mods."
               : std::to_string(g_menu.size()) +
                     (g_menu.size() == 1 ? " mod loaded." : " mods loaded.") +
                     " Select a mod to open its options.";
  } else if (key == up::kMainMenuPage.titleKey) {
    text = "DR2 HOOK";
  } else if (key == up::kMainMenuPage.subtitleKey) {
    text = "Mods, Practice Mode and tools";
  } else if (key == up::kModPage.titleKey) {
    text = "OPTIONS";
  } else if (key == up::kModPage.breadcrumbKey) {
    text = mod != nullptr ? Upper(mod->name) : "MOD";
  } else if (key.rfind(up::kModKeyPrefix, 0) == 0) {
    const int slot = SlotIndex(key.c_str() + sizeof(up::kModKeyPrefix) - 1);
    if (slot < 0) {
      return false;
    }
    if (static_cast<size_t>(slot) < g_menu.size()) {
      text = g_menu[slot].name;
    } else {
      text = slot == 0 ? "No mods loaded" : "";
    }
  } else if (key.rfind(up::kOptionKeyPrefix, 0) == 0) {
    const int slot = SlotIndex(key.c_str() + sizeof(up::kOptionKeyPrefix) - 1);
    if (slot < 0) {
      return false;
    }
    if (mod != nullptr && static_cast<size_t>(slot) < mod->options.size()) {
      text = mod->options[slot].label;
    } else {
      text = slot == 0 ? "This mod has no options" : "";
    }
  } else {
    return false;
  }
  return true;
}

const char *DetourLookup(void *handler, const char *key) {
  if (key != nullptr && key[0] == 'l' && StartsWith(key, up::kKeyPrefix)) {
    std::string name = key;
    constexpr size_t kSuffix = sizeof(kCapsSuffix) - 1;
    if (name.size() > kSuffix &&
        name.compare(name.size() - kSuffix, kSuffix, kCapsSuffix) == 0) {
      name.resize(name.size() - kSuffix);
    }
    // O jogo copia o texto antes de voltar a chamar a busca nesta thread.
    thread_local std::string text;
    if (MenuText(name, text)) {
      return text.c_str();
    }
  }
  return g_originalLookup(handler, key);
}

bool InstallTitleHook(uintptr_t base) {
  void *target = reinterpret_cast<void *>(base + kLanguageLookupRva);
  if (std::memcmp(target, kLanguageLookupBytes,
                  sizeof(kLanguageLookupBytes)) != 0) {
    HostLog("NativeScreen: busca de idioma diferente do esperado; textos das "
            "telas dr2hook desativados.");
    return false;
  }
  if (MH_CreateHook(target, reinterpret_cast<void *>(&DetourLookup),
                    reinterpret_cast<void **>(&g_originalLookup)) != MH_OK ||
      MH_EnableHook(target) != MH_OK) {
    HostLog("NativeScreen: falha ao instalar hook da busca de idioma.");
    return false;
  }
  return true;
}

// Acrescenta a aba DR2 Hook à lista do menu principal antes do Setup gravar
// tabs.info. Só mexe se a lista tem options_extras, cabe mais uma aba e os
// códigos do estado estão alinhados com as abas.
void DetourTabSetup(void *controller, TabVector *tabs, uint32_t index,
                    const char *prefix) {
  bool mainMenu = false, present = false;
  for (uint64_t i = 0; tabs != nullptr && i < tabs->count; ++i) {
    const char *screen = tabs->data[i].screen;
    mainMenu = mainMenu || (screen != nullptr && std::strcmp(screen, kMainMenuMarkerTab) == 0);
    present = present || (screen != nullptr && std::strcmp(screen, up::kMainMenuPage.name) == 0);
  }
  if (mainMenu && !present && tabs->count < tabs->capacity) {
    auto *codes = reinterpret_cast<CodeArray *>(static_cast<uint8_t *>(controller) -
                                                kTabControllerOffset + kMainMenuCodesOffset);
    if (codes->data != nullptr && codes->count == tabs->count &&
        codes->count < codes->capacity) {
      tabs->data[tabs->count++] = {up::kMainMenuPage.name, up::kMainMenuPage.tabLabel, 1};
      codes->data[codes->count++] = kMainMenuTabCode;
    } else {
      Log("menu principal sem espaco para a aba DR2 Hook");
    }
  }
  g_originalTabSetup(controller, tabs, index, prefix);
}

bool InstallMainMenuTab(uintptr_t base) {
  void *target = reinterpret_cast<void *>(base + kTabSetupRva);
  if (std::memcmp(target, kTabSetupBytes, sizeof(kTabSetupBytes)) != 0) {
    Log("Setup de abas diferente do esperado; sem aba no menu principal.");
    return false;
  }
  if (MH_CreateHook(target, reinterpret_cast<void *>(&DetourTabSetup),
                    reinterpret_cast<void **>(&g_originalTabSetup)) != MH_OK ||
      MH_EnableHook(target) != MH_OK) {
    Log("falha ao instalar o hook do Setup de abas.");
    return false;
  }
  return true;
}

enum class SlotKind { None, Mod, Option };

struct SlotPath {
  SlotKind kind = SlotKind::None;
  int index = -1;
};

SlotPath ParseSlotPath(const char *path, size_t available) {
  const std::string mods = std::string("ui.") + up::kHub.name + "." + up::kModSlotPath;
  const std::string options =
      std::string("ui.") + up::kMod.name + "." + up::kOptionSlotPath;
  for (const auto &[prefix, kind] :
       {std::pair{mods, SlotKind::Mod}, std::pair{options, SlotKind::Option}}) {
    if (available < prefix.size() + 2 ||
        std::memcmp(path, prefix.data(), prefix.size()) != 0) {
      continue;
    }
    const size_t rest = strnlen(path + prefix.size(), available - prefix.size());
    if (rest == available - prefix.size()) {
      continue;
    }
    const int index = SlotIndex(path + prefix.size());
    if (index >= 0) {
      return {kind, index};
    }
  }
  return {};
}

// Vtable trocada só depois de conferir o valor atual e os bytes do stub.
bool SwapVtableSlot(uintptr_t base, uintptr_t slotOffset, uintptr_t stubRva,
                    const uint8_t *stubBytes, size_t stubSize, void *detour,
                    void **original, const char *name) {
  auto *slot = reinterpret_cast<uintptr_t *>(base + kFeCoreVtableRva + slotOffset);
  const uintptr_t stub = base + stubRva;
  if (*slot != stub ||
      std::memcmp(reinterpret_cast<const void *>(stub), stubBytes, stubSize) != 0) {
    Log(std::string("slot ") + name + " diferente do esperado.");
    return false;
  }
  DWORD oldProtect = 0;
  if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &oldProtect)) {
    Log(std::string("VirtualProtect falhou no slot ") + name);
    return false;
  }
  *original = reinterpret_cast<void *>(stub);
  *slot = reinterpret_cast<uintptr_t>(detour);
  VirtualProtect(slot, sizeof(*slot), oldProtect, &oldProtect);
  return true;
}

} // namespace

bool NativeScreenVisibility(const char *path, size_t available, uintptr_t item,
                            bool &hidden) {
  // Só a thread da UI chama: os caches não precisam de trava.
  static std::unordered_map<uintptr_t, SlotPath> paths;
  static std::unordered_map<uintptr_t, uint32_t> appliedVersion;

  const auto key = reinterpret_cast<uintptr_t>(path);
  auto found = paths.find(key);
  if (found == paths.end()) {
    found = paths.emplace(key, ParseSlotPath(path, available)).first;
  }
  const SlotPath slot = found->second;
  if (slot.kind == SlotKind::None) {
    return false;
  }

  const size_t used = slot.kind == SlotKind::Mod
                          ? ModCount()
                          : OptionCount(g_selectedMod.load());
  hidden = slot.index != 0 && static_cast<size_t>(slot.index) >= used;

  const uint32_t version = g_menuVersion.load();
  const auto applied = appliedVersion.emplace(item, version);
  if (!applied.second && applied.first->second != version) {
    applied.first->second = version;
    RefreshItemText(item);
  }
  return true;
}

void NativeScreenTick() {
  if (!g_combosActive.load() || g_base == 0) {
    return;
  }
  static ULONGLONG last = 0;
  const ULONGLONG now = GetTickCount64();
  if (now - last < 15) {
    return;
  }
  last = now;

  void *store = *reinterpret_cast<void **>(g_base + kStoreGlobalRva);
  if (store == nullptr) {
    return;
  }
  std::vector<std::pair<int, int32_t>> changed;
  bool gone = false;
  int focused = g_sidebarIndex;
  {
    StoreLock lock(store);
    static const uint64_t selectedHash =
        PathHash(std::string("ui.") + up::kMod.name + ".selected_index");
    if (const void *node = g_ds.find(store, selectedHash)) {
      const uint8_t type = Field<uint8_t>(node, kNodeTypeOffset);
      if (type == kNodeInt || type == kNodeIntAlt) {
        focused = Field<int32_t>(node, kNodeValueOffset);
      }
    }
    for (size_t i = 0; i < up::kListSlots; ++i) {
      ComboWatch &watch = g_combos[i];
      if (!watch.active) {
        continue;
      }
      const void *node = g_ds.find(store, watch.hash);
      if (node == nullptr) {
        gone = true;
        break;
      }
      const uint8_t type = Field<uint8_t>(node, kNodeTypeOffset);
      if (type != kNodeInt && type != kNodeIntAlt) {
        continue;
      }
      const int32_t value = Field<int32_t>(node, kNodeValueOffset);
      if (value != watch.value) {
        watch.value = value;
        changed.emplace_back(static_cast<int>(i), value);
      }
    }
  }
  // A raiz ui.dr2hook_mod some quando o estado sai.
  if (gone) {
    g_combosActive.store(false);
    g_sidebarReady = false;
    return;
  }
  for (const auto &[option, value] : changed) {
    QueueEvent(g_comboMod, option, value);
    Log("combo " + std::to_string(option) + " = " + std::to_string(value));
  }
  // Fora da trava do store: SetString trava sozinho (spinlock não recursivo).
  if (g_sidebarReady &&
      (focused != g_sidebarIndex || g_menuVersion.load() != g_sidebarVersion)) {
    WriteSidebar(store, focused);
  }
}

bool InstallNativeScreenHook() {
  const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
  if (base == 0) {
    return false;
  }
  g_base = base;
  if (InstallTitleHook(base)) {
    HostLog("NativeScreen: textos das telas dr2hook instalados.");
  }
  if (InstallMainMenuTab(base)) {
    HostLog("NativeScreen: aba DR2 Hook do menu principal instalada.");
  }

  if (!SwapVtableSlot(base, kDispatchSlot, kDispatchStubRva, kDispatchStubBytes,
                      sizeof(kDispatchStubBytes),
                      reinterpret_cast<void *>(&DetourDispatch),
                      reinterpret_cast<void **>(&g_originalDispatch), "+0x88")) {
    HostLog("NativeScreen: executavel diferente do esperado; eventos das "
            "telas dr2hook desativados.");
    return false;
  }
  HostLog("NativeScreen: eventos das telas dr2hook tratados pela DLL.");

  if (ResolveDataStore(base) &&
      SwapVtableSlot(base, kEnterDataSlot, kEnterDataStubRva, kEnterDataStubBytes,
                     sizeof(kEnterDataStubBytes),
                     reinterpret_cast<void *>(&DetourEnterData),
                     reinterpret_cast<void **>(&g_originalEnterData), "+0x80")) {
    HostLog("NativeScreen: abas e combos das telas dr2hook criados pela DLL.");
  } else {
    HostLog("NativeScreen: sem abas nem combos nas telas dr2hook.");
  }
  return true;
}

} // namespace dr2hook

int Dr2Host_ConsumeReloadModsRequest() {
  return dr2hook::g_reloadModsPending.exchange(false) ? 1 : 0;
}

void Dr2Host_NativeMenuPublish(const dr2hook::Dr2MenuMod *mods, int count) {
  namespace up = dr2hook::ui_patch;
  std::vector<dr2hook::MenuMod> menu;
  for (int i = 0; mods != nullptr && i < count && menu.size() < up::kListSlots; ++i) {
    dr2hook::MenuMod mod;
    mod.name = mods[i].name != nullptr ? mods[i].name : "";
    mod.description = mods[i].description != nullptr ? mods[i].description : "";
    for (int j = 0; mods[i].options != nullptr && j < mods[i].optionCount &&
                    mod.options.size() < up::kListSlots;
         ++j) {
      const dr2hook::Dr2MenuOption &source = mods[i].options[j];
      dr2hook::MenuOption option;
      option.label = source.label != nullptr ? source.label : "";
      for (int k = 0; source.values != nullptr && k < source.valueCount; ++k) {
        option.values.emplace_back(source.values[k] != nullptr ? source.values[k] : "");
      }
      option.index = source.index;
      option.kind = source.kind;
      option.description = source.description != nullptr ? source.description : "";
      mod.options.push_back(std::move(option));
    }
    menu.push_back(std::move(mod));
  }
  std::lock_guard<std::mutex> lock(dr2hook::g_menuMutex);
  if (!(menu == dr2hook::g_menu)) {
    dr2hook::g_menu = std::move(menu);
    dr2hook::g_menuVersion.fetch_add(1);
  }
}

int Dr2Host_NativeMenuConsumeEvent(int *modIndex, int *optionIndex, int *value) {
  std::lock_guard<std::mutex> lock(dr2hook::g_menuMutex);
  if (dr2hook::g_events.empty() || modIndex == nullptr ||
      optionIndex == nullptr || value == nullptr) {
    return 0;
  }
  const dr2hook::MenuEvent event = dr2hook::g_events.front();
  dr2hook::g_events.pop_front();
  *modIndex = event.mod;
  *optionIndex = event.option;
  *value = event.value;
  return 1;
}
