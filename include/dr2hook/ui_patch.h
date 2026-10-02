#pragma once

#include "dr2hook/bxml.h"

#include <cstddef>
#include <string>

// Acréscimos aos documentos de UI do boot para abrir telas próprias a partir
// do menu de pausa. O jogo interpreta states.bin, flow.bin e screens.bin em
// ordem variável, então cada função depende só do próprio documento: os ids
// dos estados são fixos e os links vão para todo nó com link `options`. Cada
// função só altera a árvore quando consegue aplicar tudo; em caso de falha a
// árvore fica como estava e `error` diz o motivo.
namespace dr2hook::ui_patch {

inline constexpr char kPauseEvent[] = "dr2hook";
inline constexpr char kLabel[] = "DR2 Hook";

// Linhas das listas com rolagem (smart_screen). advanced_graphics usa 29
// posições, smart_set.0 a .28; o teto da cena não foi medido.
inline constexpr size_t kListSlots = 24;

// Telas com estado próprio (StateScreenFECore).
struct ScreenDef {
  const char *name;
  const char *stateId;
  unsigned stateIdValue;
};

// Host com abas (smart_screen_tabbed + SBTabGroup). A DLL cria tabs.* no
// Enter do estado; as páginas vêm de tabs.info[i].screen.
inline constexpr ScreenDef kHub{"dr2hook_hub", "1146224640", 0x44520000u};
inline constexpr ScreenDef kMod{"dr2hook_mod", "1146224641", 0x44520001u};
inline constexpr ScreenDef kScreens[] = {kHub, kMod};

// Textos de uma tela smart_screen: título e painel de descrição à direita.
struct PageDef {
  const char *name;
  const char *titleKey;
  const char *infoTitleKey;
  const char *infoTextKey;
  const char *tabLabel;
  // Título pequeno em vermelho acima do cabeçalho (SBScreenTitle); nulo usa
  // titleKey, como nas telas do jogo.
  const char *breadcrumbKey = nullptr;
};

// Páginas das abas, na ordem das abas. Usam data_parent_override do host, e
// por isso os eventos caem em ui.dr2hook_hub.event.
inline constexpr PageDef kMainPage{"dr2hook_page_main", "lng_dr2hook_title_hub",
                                   "lng_dr2hook_info_hub_title",
                                   "lng_dr2hook_info_hub_text", "DR2 Hook"};
inline constexpr PageDef kModsPage{"dr2hook_page_mods", "lng_dr2hook_title_mods",
                                   "lng_dr2hook_info_mods_title",
                                   "lng_dr2hook_info_mods_text", "Mods"};
inline constexpr PageDef kPages[] = {kMainPage, kModsPage};
inline constexpr unsigned kModsTab = 1;

// Sem chaves de painel: o painel da tela de mod é ligado a sidebar.* e
// muda com a linha em foco (native_screen.cpp).
inline constexpr PageDef kModPage{kMod.name, "lng_dr2hook_title_mod", nullptr,
                                  nullptr, nullptr, "lng_dr2hook_crumb_mod"};

// Aba "DR2 Hook" do menu principal: página com um bloco, copiada da grade de
// options_extras. A aba entra em tabs.info pelo hook do Setup do TabController
// (native_screen.cpp); o bloco dispara kPauseEvent, ligado ao hub no nó do
// menu principal (jump_id kMainMenuJumpId).
struct MainMenuPageDef {
  const char *name;
  const char *templateScreen;
  const char *tile;      // id do Item mantido do modelo
  const char *titleKey;
  const char *subtitleKey;
  const char *tabLabel;  // texto já pronto em tabs.info[i].label
};
inline constexpr MainMenuPageDef kMainMenuPage{
    "dr2hook_mm", "options_extras", "preferences", "lng_dr2hook_mm_title",
    "lng_dr2hook_mm_subtitle", "DR2 Hook"};
inline constexpr char kMainMenuJumpId[] = "main_menu_hub";
// Link do nó do menu principal cujo alvo indica onde pôr os nós hub/mod.
inline constexpr char kMainMenuAnchorLink[] = "game_settings";

// `dr2hook_do_*` não tem link no fluxo: a DLL trata e consome
// (native_screen.cpp). `dr2hook_nav_*` e `back` são links do fluxo.
inline constexpr char kActionPrefix[] = "dr2hook_do_";
inline constexpr char kOpenOverlayEvent[] = "dr2hook_do_overlay";
inline constexpr char kReloadModsEvent[] = "dr2hook_do_reload_mods";
inline constexpr char kReloadCoreEvent[] = "dr2hook_do_reload_core";
inline constexpr char kOptionEventPrefix[] = "dr2hook_do_opt_";
inline constexpr char kNavModPrefix[] = "dr2hook_nav_mod_";

// Chaves ausentes da tabela de idioma; a DLL responde na busca. Sem o hook, o
// jogo mostra a chave entre < e >.
inline constexpr char kKeyPrefix[] = "lng_dr2hook_";
inline constexpr char kModKeyPrefix[] = "lng_dr2hook_mod_";
inline constexpr char kOptionKeyPrefix[] = "lng_dr2hook_opt_";

// data_path das condições de visibilidade por linha; o jogo guarda o caminho
// completo como ui.<raiz>.<data_path>.
inline constexpr char kModSlotPath[] = "dr2hook_slot_";
inline constexpr char kOptionSlotPath[] = "dr2hook_vis_";
// Nós dos combos da tela de mod, criados pela DLL: <prefixo>N.index e
// <prefixo>N.list[i].
inline constexpr char kOptionDataPrefix[] = "dr2hook_opt_";

struct Button {
  const char *label;
  const char *event;
};

inline constexpr Button kMainButtons[] = {
    {"Open overlay", kOpenOverlayEvent},
    {"Reload Lua mods", kReloadModsEvent},
    {"Reload native core", kReloadCoreEvent},
};

bool PatchStates(bxml::Node &root, std::string &error);
bool PatchFlow(bxml::Node &root, size_t &linkedNodes, std::string &error);
bool PatchScreens(bxml::Node &root, std::string &error);

} // namespace dr2hook::ui_patch
