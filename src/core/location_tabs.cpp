#include "dr2hook/location_tabs.h"
#include "dr2hook/logger.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <MinHook.h>
#endif

namespace dr2hook {
namespace {

#if defined(_WIN32)

// Tela location_select (vtable 0x141256e98). O slot 34 (0x1402c7830) preenche o
// data store: monta a lista de locais em this+0x170 (ponteiros, contagem em
// +0x180), chama 0x1402cdc00 para montar as abas e então grava cada local
// (location_list[].discipline = 0x1400ecc40(disciplina)) e a barra
// (quick_nav.list[].icon por 0x1400ecc50(aba.disc): 0 rally, 1 rallycross,
// resto "mixed"). O F4/F3 (0x1402d61c0) só troca quick_nav.index (nas pontas
// põe selected_index no primeiro/último local); quem move a seleção é
// 0x1402dc000: se ela ficou antes da aba nova vai para aba.first, se ficou
// depois vai para aba.last. Ao mudar a seleção, 0x1402d36b0 faz o caminho
// inverso: quick_nav.index = primeira aba com a disciplina do local.
//
// Local = ponteiro para o identificador da linha: [[local]+8] = id da location,
// [[local]+0x30] = identificador da disciplina, [[disc]+8] = id da disciplina
// (1 rally, 2 rallycross, 3 landrush...).
constexpr uintptr_t kBuildTabsRva = 0x2cdc00;
constexpr uint8_t kBuildTabsPrologue[] = {0x40, 0x53, 0x48, 0x83, 0xec, 0x30};
constexpr uintptr_t kListOffset = 0x170;
constexpr uintptr_t kListCount = 0x180;
constexpr uintptr_t kTabs = 0x290;
constexpr uintptr_t kTabCapacity = 0x298;
constexpr uintptr_t kTabCount = 0x2a0;
// A fábrica da tela (0x1402d96f0) aloca 0x2d0 bytes e guarda as abas dentro do
// objeto, em +0x2b0, com capacidade 2: não cabe a terceira. As abas passam para
// uma área nossa que nunca é liberada (o destrutor 0x1402c3250 só zera a contagem).
constexpr uint64_t kOwnTabCapacity = 8;

// call 0x1400ecc40 ao gravar location_list[].discipline; r12 aponta o local.
constexpr uintptr_t kDisciplineCallRva = 0x2c7aaa;
constexpr uintptr_t kDisciplineIndexRva = 0xecc40;
// lea r8,'icon_dis_mixed_small' (ícone da aba que não é rally nem rallycross).
constexpr uintptr_t kTabIconLeaRva = 0x2c808b;
constexpr uint8_t kTabIconLea[] = {0x4c, 0x8d, 0x05, 0x86, 0x41, 0xf6, 0x00};
// Selo do card (0x140155950): discipline 0 rally, 1 rallycross, senão
// lea rdx,'icon_dis_mixed' e salta para 0x140155a59. O valor lido fica em [rbp+0x67].
constexpr uintptr_t kCardIconLeaRva = 0x155a3c;
constexpr uint8_t kCardIconLea[] = {0x48, 0x8d, 0x15, 0x8d, 0x70, 0x0d, 0x01};
constexpr uintptr_t kCardIconJoinRva = 0x155a59;
// call 0x140100840 (local -> disciplina) no laço de 0x1402d36b0 que acha a aba do
// local selecionado (rcx = local). Sem isto o Ring (rallycross no catálogo) volta
// a marcar a aba RALLYCROSS e o 0x1402dc000 recua a seleção para o fim dela.
constexpr uintptr_t kSyncCallRva = 0x2d3d2a;
constexpr uintptr_t kLocationDisciplineRva = 0x100840;

// call 0x140100850 (local -> nome interno, [[local]+0x48]) ao gravar
// location_list[].location_name, que o card (0x140155950) junta a "s_" para achar
// a foto. As locations novas copiam o nome da hospedeira (as pastas vêm dele):
// aqui ganham dr2hook_loc<id>, e a dxgi enxerta s_dr2hook_loc<id> no bundle
// (src/trackpkg/builder.cpp, CardLocationName).
constexpr uintptr_t kCardNameCallRva = 0x2c7bf3;
constexpr uintptr_t kLocationNameRva = 0x100850;
// O mapa-múndi (BWorldMap, 0x14016fbb0) acha o estado de cada location no data store
// world_map/<nome> (0 escondido, 1 inativo, 2 ativo, 3 selecionado) e desenha o nó
// map_marker_<nome> do persistentDB.pssg (0x140170cd0). O provedor world_map (0x140288150 e
// 0x14029b840/0x14029c830/0x14029e920/0x14029f1d0) cria e atualiza world_map/<nome> pelo mesmo
// 0x140100850: com o nome da hospedeira as duas linhas caem na mesma chave e o alfinete acende
// nela. Nesses calls as locations novas usam a chave dr2hook_loc<id> (CardLocationName).
constexpr uintptr_t kMapKeyCallRvas[] = {0x16fd89, 0x29b89e, 0x29b9a7, 0x29c893,
                                         0x29ec09, 0x29ecf4, 0x29f4bf, 0x29f595};
constexpr size_t kMapKeyCalls = sizeof kMapKeyCallRvas / sizeof kMapKeyCallRvas[0];
// call 0x140d78a00 ("%s%s", "map_marker_", nome) em 0x140170cd0: dr2hook_loc<id> vira um dos
// marcadores que o jogo não usa (sobras do DR1), que a dxgi move para o lat/lon do pacote
// (src/trackpkg/builder.cpp, MapMarkerName). O jogo não aceita um persistentDB maior que o
// original, então não dá para acrescentar marcadores.
constexpr uintptr_t kMarkerFormatCallRva = 0x170d9f;
constexpr uintptr_t kFormatRva = 0xd78a00;
constexpr size_t kCardNameSlots = 64;
constexpr size_t kCardNameSize = 24;

// Locais do jogo vão até o id 46; custom_track.py cria os novos a partir do 47.
constexpr uint32_t kFirstCustomLocation = 47;
// A aba do DR2Hook usa a disciplina 3 (landrush, que nenhum local do jogo tem):
// 0x1400ecc50(3) = 2, o caminho "mixed" da barra e do card.
constexpr int32_t kCustomTabDiscipline = 3;
constexpr int32_t kCustomDisciplineIndex = 2;

constexpr char kTabIcon[] = "icon_dis_dr2hook_small";
constexpr char kCardIcon[] = "icon_dis_dr2hook";

struct Tab {
  int32_t discipline;
  int32_t first;
  int32_t last;
};
static_assert(sizeof(Tab) == 12, "aba = 12 bytes");

using BuildTabsFn = void (*)(uint8_t *self);
using DisciplineIndexFn = int32_t (*)(void *discipline);

uintptr_t g_base = 0;
void *g_buildTabs = nullptr;
BuildTabsFn g_origBuildTabs = nullptr;
DisciplineIndexFn g_disciplineIndex = nullptr;
uint8_t *g_cave = nullptr;
void *g_tabStorage = nullptr;
bool g_tabIconPatched = false;
bool g_cardIconPatched = false;
bool g_disciplinePatched = false;
uint8_t g_disciplineCall[5] = {};
bool g_syncPatched = false;
uint8_t g_syncCall[5] = {};
bool g_cardNamePatched = false;
uint8_t g_cardNameCall[5] = {};
bool g_mapKeyPatched[kMapKeyCalls] = {};
uint8_t g_mapKeyCall[kMapKeyCalls][5] = {};
bool g_markerPatched = false;
uint8_t g_markerCall[5] = {};
bool g_cardNameLogged = false;
// Identificador falso de disciplina com id kCustomTabDiscipline (mesma forma que
// os do catálogo: [ref] = linha, [linha+8] = id), na página perto do exe.
void *g_customDisciplineRef = nullptr;
std::string g_lastLog;

bool Readable(const void *p, size_t size) {
  MEMORY_BASIC_INFORMATION mbi{};
  if (p == nullptr || VirtualQuery(p, &mbi, sizeof mbi) == 0) return false;
  if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) return false;
  const auto end = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
  return reinterpret_cast<uintptr_t>(p) + size <= end;
}

uint32_t LocationId(void *loc) {
  auto *row = *static_cast<uint8_t **>(loc);
  return *reinterpret_cast<uint32_t *>(row + 8);
}

int32_t DisciplineId(void *loc) {
  auto *row = *static_cast<uint8_t **>(loc);
  auto *disc = *reinterpret_cast<uint8_t **>(row + 0x30);
  return *reinterpret_cast<int32_t *>(*reinterpret_cast<uint8_t **>(disc) + 8);
}

bool IsCustom(void *loc) { return loc != nullptr && LocationId(loc) >= kFirstCustomLocation; }

// Mesmo que 0x1402cdc00, com os locais do DR2Hook no fim (ordem relativa mantida)
// e numa aba só deles.
void DetourBuildTabs(uint8_t *self) {
  auto *list = *reinterpret_cast<void ***>(self + kListOffset);
  const uint32_t count = *reinterpret_cast<uint32_t *>(self + kListCount);
  std::vector<void *> custom;
  uint32_t kept = 0;
  for (uint32_t i = 0; i < count; ++i) {
    if (IsCustom(list[i])) {
      custom.push_back(list[i]);
    } else {
      list[kept++] = list[i];
    }
  }
  for (void *loc : custom) list[kept++] = loc;

  if (g_tabStorage != nullptr &&
      *reinterpret_cast<uint64_t *>(self + kTabCapacity) < kOwnTabCapacity) {
    *reinterpret_cast<void **>(self + kTabs) = g_tabStorage;
    *reinterpret_cast<uint64_t *>(self + kTabCapacity) = kOwnTabCapacity;
  }
  auto *tabs = *reinterpret_cast<Tab **>(self + kTabs);
  const uint64_t capacity = *reinterpret_cast<uint64_t *>(self + kTabCapacity);
  auto &tabCount = *reinterpret_cast<uint64_t *>(self + kTabCount);
  tabCount = 0;
  uint32_t dropped = 0;
  for (uint32_t i = 0; i < count; ++i) {
    void *loc = list[i];
    if (loc == nullptr) continue;
    const int32_t disc = IsCustom(loc) ? kCustomTabDiscipline : DisciplineId(loc);
    if (tabCount != 0 && tabs[tabCount - 1].discipline == disc) continue;
    if (tabCount >= capacity) {
      ++dropped;
      continue;
    }
    tabs[tabCount++] = Tab{disc, static_cast<int32_t>(i), static_cast<int32_t>(i)};
  }
  for (uint64_t k = 0; k + 1 < tabCount; ++k) tabs[k].last = tabs[k + 1].first - 1;
  if (tabCount != 0) tabs[tabCount - 1].last = static_cast<int32_t>(count) - 1;

  std::string msg = "LocationTabs: " + std::to_string(count) + " locais (" +
                    std::to_string(custom.size()) + " do DR2Hook), abas " +
                    std::to_string(tabCount) + "/" + std::to_string(capacity) + ":";
  for (uint64_t k = 0; k < tabCount; ++k) {
    char part[48];
    std::snprintf(part, sizeof part, " [disc %d, %d..%d]", tabs[k].discipline, tabs[k].first,
                  tabs[k].last);
    msg += part;
  }
  if (dropped != 0) msg += " (" + std::to_string(dropped) + " sem espaco)";
  if (msg != g_lastLog) {
    Logger::Info(msg);
    g_lastLog = msg;
  }
}

// Chamada no lugar de 0x1400ecc40 ao gravar location_list[].discipline.
int32_t DisciplineIndex(void *discipline, void *loc) {
  if (IsCustom(loc)) return kCustomDisciplineIndex;
  return g_disciplineIndex(discipline);
}

// Chamada no lugar de 0x140100840 ao procurar a aba do local selecionado.
void *TabDisciplineRef(void *loc) {
  if (IsCustom(loc)) return g_customDisciplineRef;
  return *reinterpret_cast<void **>(*static_cast<uint8_t **>(loc) + 0x30);
}

// Chamada no lugar de 0x140100850 ao gravar location_list[].location_name.
const char *CardLocationName(void *loc) {
  const char *name = *reinterpret_cast<const char **>(*static_cast<uint8_t **>(loc) + 0x48);
  if (!IsCustom(loc) || g_cave == nullptr) return name;
  const uint32_t id = LocationId(loc);
  // Na página perto do exe: fica depois de um F8 (o data store pode guardar o ponteiro).
  char *slot = reinterpret_cast<char *>(g_cave + 0x400 + (id % kCardNameSlots) * kCardNameSize);
  std::snprintf(slot, kCardNameSize, "dr2hook_loc%u", id);
  if (!g_cardNameLogged) {
    g_cardNameLogged = true;
    Logger::Info(std::string("LocationTabs: card da location ") + std::to_string(id) + " (" +
                 (Readable(name, 1) ? name : "?") + ") usa s_" + slot);
  }
  return slot;
}

// Chamada (pela ponte em g_cave+0x160) no lugar de 0x140d78a00 com r9 = nome da location.
const char *MarkerNodeName(const char *name) {
  static const char *const kSpare[] = {"argentina", "la_figuera", "baumholder", "argolis"};
  static constexpr char kPrefix[] = "dr2hook_loc";
  if (name == nullptr || std::strncmp(name, kPrefix, sizeof kPrefix - 1) != 0) return name;
  const unsigned long id = std::strtoul(name + sizeof kPrefix - 1, nullptr, 10);
  if (id < kFirstCustomLocation) return name;
  return kSpare[(id - kFirstCustomLocation) % 4];
}

bool Patch(uintptr_t va, const void *bytes, size_t size) {
  DWORD old = 0;
  void *p = reinterpret_cast<void *>(va);
  if (!VirtualProtect(p, size, PAGE_EXECUTE_READWRITE, &old)) return false;
  std::memcpy(p, bytes, size);
  FlushInstructionCache(GetCurrentProcess(), p, size);
  VirtualProtect(p, size, old, &old);
  return true;
}

uint8_t *AllocNear(size_t size) {
  for (uintptr_t hint = g_base + 0x10000000; hint < g_base + 0x70000000; hint += 0x1000000) {
    void *p = VirtualAlloc(reinterpret_cast<void *>(hint), size, MEM_COMMIT | MEM_RESERVE,
                           PAGE_EXECUTE_READWRITE);
    if (p != nullptr) return static_cast<uint8_t *>(p);
  }
  return nullptr;
}

int32_t Rel32(uintptr_t from, uintptr_t to) { return static_cast<int32_t>(to - from); }

// lea reg,[rip+disp32] de 7 bytes apontando para `target`.
void LeaTo(uint8_t *lea, uintptr_t at, uintptr_t target) {
  const int32_t disp = Rel32(at + 7, target);
  std::memcpy(lea + 3, &disp, 4);
}

#endif

} // namespace

void LocationTabs::Install(uintptr_t gameBase) {
#if defined(_WIN32)
  if (g_buildTabs != nullptr) return;
  g_base = gameBase;
  const uintptr_t buildTabs = gameBase + kBuildTabsRva;
  const uintptr_t discCall = gameBase + kDisciplineCallRva;
  const uintptr_t tabLea = gameBase + kTabIconLeaRva;
  const uintptr_t cardLea = gameBase + kCardIconLeaRva;
  const uintptr_t syncCall = gameBase + kSyncCallRva;
  uint8_t expectCall[5] = {0xe8};
  const int32_t callRel = Rel32(discCall + 5, gameBase + kDisciplineIndexRva);
  std::memcpy(expectCall + 1, &callRel, 4);
  const uintptr_t nameCall = gameBase + kCardNameCallRva;
  uint8_t expectName[5] = {0xe8};
  const int32_t nameRel = Rel32(nameCall + 5, gameBase + kLocationNameRva);
  std::memcpy(expectName + 1, &nameRel, 4);
  uint8_t expectSync[5] = {0xe8};
  const int32_t syncRel = Rel32(syncCall + 5, gameBase + kLocationDisciplineRva);
  std::memcpy(expectSync + 1, &syncRel, 4);
  if (!Readable(reinterpret_cast<void *>(buildTabs), sizeof kBuildTabsPrologue) ||
      std::memcmp(reinterpret_cast<void *>(buildTabs), kBuildTabsPrologue,
                  sizeof kBuildTabsPrologue) != 0 ||
      std::memcmp(reinterpret_cast<void *>(discCall), expectCall, 5) != 0 ||
      std::memcmp(reinterpret_cast<void *>(syncCall), expectSync, 5) != 0 ||
      std::memcmp(reinterpret_cast<void *>(tabLea), kTabIconLea, sizeof kTabIconLea) != 0 ||
      std::memcmp(reinterpret_cast<void *>(cardLea), kCardIconLea, sizeof kCardIconLea) != 0) {
    Logger::Warn("LocationTabs: bytes do exe nao conferem; aba do DR2Hook desligada.");
    return;
  }
  g_cave = AllocNear(0x1000);
  if (g_cave == nullptr) {
    Logger::Warn("LocationTabs: sem memoria perto do exe.");
    return;
  }

  // Strings, a ponte do ecc40 e o desvio do selo do card na mesma página.
  uint8_t *tabIcon = g_cave;
  uint8_t *cardIcon = g_cave + 0x40;
  std::memcpy(tabIcon, kTabIcon, sizeof kTabIcon);
  std::memcpy(cardIcon, kCardIcon, sizeof kCardIcon);

  g_tabStorage = g_cave + 0x200;
  auto *fakeRow = g_cave + 0x310;
  *reinterpret_cast<int32_t *>(fakeRow + 8) = kCustomTabDiscipline;
  *reinterpret_cast<uint8_t **>(g_cave + 0x300) = fakeRow;
  g_customDisciplineRef = g_cave + 0x300;

  // mov rax,TabDisciplineRef ; jmp rax (rcx já é o local)
  uint8_t *syncBridge = g_cave + 0x120;
  syncBridge[0] = 0x48;
  syncBridge[1] = 0xb8;
  const auto syncFn = reinterpret_cast<uint64_t>(&TabDisciplineRef);
  std::memcpy(syncBridge + 2, &syncFn, 8);
  syncBridge[10] = 0xff;
  syncBridge[11] = 0xe0;

  // push rcx/rdx/r8 ; sub rsp,0x20 ; mov rcx,r9 ; mov rax,MarkerNodeName ; call rax ;
  // add rsp,0x20 ; mov r9,rax ; pop r8/rdx/rcx ; mov rax,0x140d78a00 ; jmp rax
  uint8_t *markerBridge = g_cave + 0x160;
  {
    const uint8_t head[] = {0x51, 0x52, 0x41, 0x50, 0x48, 0x83, 0xec, 0x20, 0x4c, 0x89, 0xc9, 0x48, 0xb8};
    const uint8_t mid[] = {0xff, 0xd0, 0x48, 0x83, 0xc4, 0x20, 0x49, 0x89, 0xc1, 0x41, 0x58, 0x5a, 0x59, 0x48, 0xb8};
    const uint8_t tail[] = {0xff, 0xe0};
    const auto fnMarker = reinterpret_cast<uint64_t>(&MarkerNodeName);
    const uint64_t fnFormat = gameBase + kFormatRva;
    size_t at = 0;
    std::memcpy(markerBridge + at, head, sizeof head);
    at += sizeof head;
    std::memcpy(markerBridge + at, &fnMarker, 8);
    at += 8;
    std::memcpy(markerBridge + at, mid, sizeof mid);
    at += sizeof mid;
    std::memcpy(markerBridge + at, &fnFormat, 8);
    at += 8;
    std::memcpy(markerBridge + at, tail, sizeof tail);
  }

  // mov rax,CardLocationName ; jmp rax (rcx já é o local)
  uint8_t *nameBridge = g_cave + 0x140;
  nameBridge[0] = 0x48;
  nameBridge[1] = 0xb8;
  const auto nameFn = reinterpret_cast<uint64_t>(&CardLocationName);
  std::memcpy(nameBridge + 2, &nameFn, 8);
  nameBridge[10] = 0xff;
  nameBridge[11] = 0xe0;

  // mov rdx,[r12] ; mov rax,DisciplineIndex ; jmp rax
  uint8_t *bridge = g_cave + 0x80;
  const uint8_t bridgeHead[] = {0x49, 0x8b, 0x14, 0x24, 0x48, 0xb8};
  std::memcpy(bridge, bridgeHead, sizeof bridgeHead);
  const auto fn = reinterpret_cast<uint64_t>(&DisciplineIndex);
  std::memcpy(bridge + 6, &fn, 8);
  bridge[14] = 0xff;
  bridge[15] = 0xe0;

  // cmp dword [rbp+0x67],2 ; jne mixed ; lea rdx,icon_dis_dr2hook ; jmp volta
  // mixed: lea rdx,icon_dis_mixed ; jmp volta
  uint8_t *card = g_cave + 0xc0;
  const auto cardVa = reinterpret_cast<uintptr_t>(card);
  const uintptr_t join = gameBase + kCardIconJoinRva;
  int32_t mixedDisp = 0;
  std::memcpy(&mixedDisp, kCardIconLea + 3, 4);
  const uintptr_t mixed = cardLea + 7 + mixedDisp;
  size_t n = 0;
  const uint8_t cmp[] = {0x83, 0x7d, 0x67, static_cast<uint8_t>(kCustomDisciplineIndex), 0x75, 0x0c};
  std::memcpy(card, cmp, sizeof cmp);
  n += sizeof cmp;
  std::memcpy(card + n, kCardIconLea, 7);
  LeaTo(card + n, cardVa + n, reinterpret_cast<uintptr_t>(cardIcon));
  n += 7;
  card[n] = 0xe9;
  const int32_t back1 = Rel32(cardVa + n + 5, join);
  std::memcpy(card + n + 1, &back1, 4);
  n += 5;
  std::memcpy(card + n, kCardIconLea, 7);
  LeaTo(card + n, cardVa + n, mixed);
  n += 7;
  card[n] = 0xe9;
  const int32_t back2 = Rel32(cardVa + n + 5, join);
  std::memcpy(card + n + 1, &back2, 4);
  FlushInstructionCache(GetCurrentProcess(), g_cave, 0x1000);

  const MH_STATUS init = MH_Initialize();
  if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
    Logger::Warn("LocationTabs: MH_Initialize falhou.");
    return;
  }
  void *target = reinterpret_cast<void *>(buildTabs);
  if (MH_CreateHook(target, reinterpret_cast<void *>(&DetourBuildTabs),
                    reinterpret_cast<void **>(&g_origBuildTabs)) != MH_OK ||
      MH_EnableHook(target) != MH_OK) {
    MH_RemoveHook(target);
    Logger::Warn("LocationTabs: hook de 0x1402cdc00 falhou.");
    return;
  }
  g_buildTabs = target;
  g_disciplineIndex = reinterpret_cast<DisciplineIndexFn>(gameBase + kDisciplineIndexRva);

  std::memcpy(g_disciplineCall, expectCall, 5);
  uint8_t call[5] = {0xe8};
  const int32_t toBridge = Rel32(discCall + 5, reinterpret_cast<uintptr_t>(bridge));
  std::memcpy(call + 1, &toBridge, 4);
  g_disciplinePatched = Patch(discCall, call, 5);

  std::memcpy(g_syncCall, expectSync, 5);
  const int32_t toSync = Rel32(syncCall + 5, reinterpret_cast<uintptr_t>(syncBridge));
  std::memcpy(call + 1, &toSync, 4);
  g_syncPatched = Patch(syncCall, call, 5);

  // Só a ponte do nome: com bytes diferentes, a aba segue sem a foto própria.
  if (std::memcmp(reinterpret_cast<void *>(nameCall), expectName, 5) == 0) {
    std::memcpy(g_cardNameCall, expectName, 5);
    const int32_t toName = Rel32(nameCall + 5, reinterpret_cast<uintptr_t>(nameBridge));
    std::memcpy(call + 1, &toName, 4);
    g_cardNamePatched = Patch(nameCall, call, 5);
  }
  // Chave e nó do mapa só juntos: com a chave nova e o nó antigo o alfinete sumiria.
  const uintptr_t markerCall = gameBase + kMarkerFormatCallRva;
  uint8_t expectMarker[5] = {0xe8};
  const int32_t markerRel = Rel32(markerCall + 5, gameBase + kFormatRva);
  std::memcpy(expectMarker + 1, &markerRel, 4);
  bool mapOk = g_cardNamePatched && std::memcmp(reinterpret_cast<void *>(markerCall), expectMarker, 5) == 0;
  for (size_t i = 0; mapOk && i < kMapKeyCalls; ++i) {
    const uintptr_t at = gameBase + kMapKeyCallRvas[i];
    uint8_t expectKey[5] = {0xe8};
    const int32_t keyRel = Rel32(at + 5, gameBase + kLocationNameRva);
    std::memcpy(expectKey + 1, &keyRel, 4);
    mapOk = std::memcmp(reinterpret_cast<void *>(at), expectKey, 5) == 0;
  }
  if (mapOk) {
    std::memcpy(g_markerCall, expectMarker, 5);
    const int32_t toMarker = Rel32(markerCall + 5, reinterpret_cast<uintptr_t>(markerBridge));
    std::memcpy(call + 1, &toMarker, 4);
    g_markerPatched = Patch(markerCall, call, 5);
    for (size_t i = 0; g_markerPatched && i < kMapKeyCalls; ++i) {
      const uintptr_t at = gameBase + kMapKeyCallRvas[i];
      std::memcpy(g_mapKeyCall[i], reinterpret_cast<void *>(at), 5);
      const int32_t toKey = Rel32(at + 5, reinterpret_cast<uintptr_t>(nameBridge));
      std::memcpy(call + 1, &toKey, 4);
      g_mapKeyPatched[i] = Patch(at, call, 5);
    }
  }

  uint8_t lea[7];
  std::memcpy(lea, kTabIconLea, 7);
  LeaTo(lea, tabLea, reinterpret_cast<uintptr_t>(tabIcon));
  g_tabIconPatched = Patch(tabLea, lea, 7);

  uint8_t jmp[7] = {0xe9, 0, 0, 0, 0, 0x90, 0x90};
  const int32_t toCard = Rel32(cardLea + 5, cardVa);
  std::memcpy(jmp + 1, &toCard, 4);
  g_cardIconPatched = Patch(cardLea, jmp, 7);

  Logger::Info(std::string("LocationTabs: ativo (aba e selo do DR2Hook para location >= ") +
               std::to_string(kFirstCustomLocation) + "; patches " +
               (g_disciplinePatched ? "disc " : "") + (g_syncPatched ? "sync " : "") + (g_tabIconPatched ? "aba " : "") +
               (g_cardIconPatched ? "card " : "") + (g_cardNamePatched ? "foto " : "") + (g_markerPatched ? "mapa" : "") + ").");
#else
  (void)gameBase;
#endif
}

void LocationTabs::Shutdown() {
#if defined(_WIN32)
  if (g_cardIconPatched) Patch(g_base + kCardIconLeaRva, kCardIconLea, sizeof kCardIconLea);
  if (g_tabIconPatched) Patch(g_base + kTabIconLeaRva, kTabIconLea, sizeof kTabIconLea);
  if (g_disciplinePatched) Patch(g_base + kDisciplineCallRva, g_disciplineCall, 5);
  if (g_syncPatched) Patch(g_base + kSyncCallRva, g_syncCall, 5);
  if (g_cardNamePatched) Patch(g_base + kCardNameCallRva, g_cardNameCall, 5);
  for (size_t i = 0; i < kMapKeyCalls; ++i) {
    if (g_mapKeyPatched[i]) Patch(g_base + kMapKeyCallRvas[i], g_mapKeyCall[i], 5);
    g_mapKeyPatched[i] = false;
  }
  if (g_markerPatched) Patch(g_base + kMarkerFormatCallRva, g_markerCall, 5);
  g_markerPatched = false;
  g_cardIconPatched = g_tabIconPatched = g_disciplinePatched = g_syncPatched = g_cardNamePatched = false;
  g_cardNameLogged = false;
  if (g_buildTabs != nullptr) {
    MH_DisableHook(g_buildTabs);
    MH_RemoveHook(g_buildTabs);
    g_buildTabs = nullptr;
  }
  // A página fica: o data store pode guardar ponteiros para as strings até a
  // tela ser refeita, e um F8 não deve deixar o jogo apontando para memória solta.
  g_cave = nullptr;
  g_tabStorage = nullptr;
  g_customDisciplineRef = nullptr;
  g_lastLog.clear();
#endif
}

} // namespace dr2hook
