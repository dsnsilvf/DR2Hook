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
// Mesma tela de opções do mod, aberta direto do menu principal: ao entrar, a
// DLL escolhe o mod interno (kBuiltinModName) em vez do selecionado no hub.
inline constexpr ScreenDef kModDirect{"dr2hook_mod", "1146224642", 0x44520002u};
// Hub aberto pelo bloco MODS do menu principal: já entra na aba Mods.
inline constexpr ScreenDef kHubMods{"dr2hook_hub", "1146224643", 0x44520003u};
inline constexpr ScreenDef kScreens[] = {kHub, kMod, kModDirect, kHubMods};
inline constexpr char kBuiltinModName[] = "Practice Mode";

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

// Aba "DR2 Hook" do menu principal: página copiada da grade de options_extras
// com os blocos abaixo. A aba entra em tabs.info pelo hook do Setup do
// TabController (native_screen.cpp); os eventos caem no nó do menu principal
// (jump_id kMainMenuJumpId), que ganha os links para o hub e para o mod
// interno.
// Disabled: sem ação e "Coming soon". Não usa IBDataEnabled: a navegação por
// teclado pula itens desabilitados e, sem outro bloco ativo na direção, o
// cursor não anda (a seta para baixo travava a partir de MODS).
// Todo bloco mantém o IBSelectableSimple: a grade só para em itens com um
// behaviour IB* (nenhuma das 82 grades do jogo tem item sem). Info e Disabled
// disparam kMainMenuIdleEvent, que não tem link no fluxo e por isso não faz
// nada.
enum class TileState { Action, Info, Disabled };
inline constexpr char kMainMenuIdleEvent[] = "dr2hook_idle";
struct MainMenuTile {
  const char *item;        // id do Item do modelo (posição fixa na cena)
  const char *titleKey;
  const char *subtitleKey; // nulo nos blocos pequenos (não têm subtítulo)
  const char *texture;     // marca d'água; nulo mantém a do modelo
  TileState state;
  const char *event;       // só em Action
};
inline constexpr char kMainMenuPracticeEvent[] = "dr2hook_practice";
inline constexpr char kMainMenuModsEvent[] = "dr2hook_mods";
inline constexpr MainMenuTile kMainMenuTiles[] = {
    {"preferences", "lng_dr2hook_mm_mods_title", "lng_dr2hook_mm_mods_subtitle", nullptr,
     TileState::Action, kMainMenuModsEvent},
    {"input", "lng_dr2hook_mm_mp_title", "lng_dr2hook_mm_soon", "tile_watermark_join",
     TileState::Disabled, nullptr},
    {"profile", "lng_dr2hook_mm_overlay_title", nullptr, nullptr, TileState::Disabled, nullptr},
    {"racenet", "lng_dr2hook_mm_reload_title", nullptr, nullptr, TileState::Disabled, nullptr},
    {"graphics", "lng_dr2hook_mm_world_title", "lng_dr2hook_mm_soon",
     "tile_watermark_rally_lead", TileState::Disabled, nullptr},
    {"audio", "lng_dr2hook_mm_vehicle_title", "lng_dr2hook_mm_soon",
     "tile_watermark_performance", TileState::Disabled, nullptr},
    // Ícone original da posição: os de bloco grande ficam cortados no pequeno.
    {"legal", "lng_dr2hook_mm_practice_title", nullptr, nullptr, TileState::Action,
     kMainMenuPracticeEvent},
    {"credits", "lng_dr2hook_mm_about_title", nullptr, nullptr, TileState::Info, nullptr},
};
// Navegação da grade: a mesma do SBGridItemFlow de options_extras (3 colunas x
// 4 linhas; `[id]` é a célula a mais de um bloco grande). Precisa das 12
// células; com menos, as linhas desalinham e a seta para baixo não anda.
inline constexpr char kMainMenuGrid[] =
    "preferences input profile [preferences] [input] racenet "
    "graphics audio legal [graphics] [audio] credits";
struct MainMenuPageDef {
  const char *name;
  const char *templateScreen;
  const char *titleKey;  // bloco do hub (compatibilidade com testes/docs)
  const char *tabLabel;  // texto já pronto em tabs.info[i].label
};
inline constexpr MainMenuPageDef kMainMenuPage{"dr2hook_mm", "options_extras",
                                               "lng_dr2hook_mm_title", "DR2 Hook"};
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
