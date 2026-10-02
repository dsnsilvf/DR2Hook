#include "dr2hook/bxml.h"
#include "dr2hook/ui_patch.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

using dr2hook::bxml::Node;
namespace ui_patch = dr2hook::ui_patch;

static int g_testsRun = 0;
static int g_testsFailed = 0;

#define TEST_ASSERT(condition, msg)                                            \
  do {                                                                         \
    ++g_testsRun;                                                              \
    if (!(condition)) {                                                        \
      ++g_testsFailed;                                                         \
      std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ << " ("            \
                << __func__ << "): " << (msg) << " -> Assertion '"             \
                << #condition << "' failed." << std::endl;                     \
    }                                                                          \
  } while (0)

namespace {

Node N(std::string name,
       std::vector<std::pair<std::string, std::string>> attributes = {},
       std::vector<Node> children = {}) {
  Node node;
  node.name = std::move(name);
  node.attributes = std::move(attributes);
  node.children = std::move(children);
  return node;
}

Node Item(const std::string &id, const std::string &text,
          const std::string &event, bool withVisibility = false) {
  Node item = N("Item", {{"id", id}, {"glyph", "smart_set.0.switch"}},
                {N("IBStackItem"),
                 N("BTextStatic", {{"string", text}, {"glyph", "text_title"}}),
                 N("IBSelectableSimple",
                   {{"out_data_path", "event"}, {"select_value", event}})});
  if (withVisibility) {
    item.children.push_back(N("BVisibilityControlData",
                              {{"data_path", "reset_view_available"}}));
  }
  return item;
}

Node Screen(const std::string &id, std::vector<Node> items,
            const std::string &flowText) {
  Node flow = N("SBGridItemFlow", {{"wrapV", "true"}});
  flow.text = flowText;
  return N("Screen", {{"id", id}, {"object", "smart_hub"}},
           {N("items", {}, std::move(items)),
            N("behaviours", {},
              {N("SBScreenTitle", {{"string_id", "lng_" + id + "_title"},
                                   {"override_format_id", "localise"}}),
               std::move(flow)})});
}

Node States() {
  return N("xml", {},
           {N("StatePauseScreen", {{"id", "314506569"}, {"screen_name", "pause_menu"}}),
            N("StatePauseScreen", {{"id", "589096572"}, {"screen_name", "pause_menu"}, {"pre_race", "true"}}),
            N("StateScreenFECore", {{"id", "3411174356"}, {"screen_name", "options_ingame"}}),
            N("TaskDisplayDialog", {{"id", "eula_declined"}})});
}

Node Flow() {
  return N("flow", {},
           {N("node", {{"id", "129712173"}, {"state", "2192346083"}},
              {N("node", {{"id", "135287375"}, {"state", "1837755473"}},
                 {N("node", {{"id", "231066979"}, {"state", "314506569"}},
                    {N("link", {{"id", "continue"}, {"target", "228227392"}}),
                     N("link", {{"id", "options"}, {"target", "254662489"}}),
                     N("link", {{"id", "back"}, {"target", "228227392"}, {"type", "back"}})})}),
               N("node", {{"id", "254662489"}, {"state", "3411174356"}},
                 {N("link", {{"id", "back"}, {"target", "231066979"}, {"type", "back"}})})}),
            N("node", {{"id", "999"}, {"state", "589096572"}},
              {N("link", {{"id", "quit"}, {"target", "1"}})})});
}

Node BasicGraphics() {
  Node flow = N("SBScrollableItemFlow", {{"wrapV", "true"}, {"scroller_glyph", "smart_set.scroller"}});
  flow.text = "\r\n        item_0\r\n        item_1\r\n        apply\r\n      ";
  return N(
      "Screen",
      {{"id", "basic_graphics"}, {"glyph", "screen_directory.screen_choice"}, {"object", "smart_screen"}},
      {N("items", {},
         {N("Item", {{"id", "title"}, {"glyph", "screen_header"}},
            {N("BTextStatic", {{"string", "lng_video_mode_title"}, {"glyph", "screen_header_text"}})}),
          N("Item", {{"id", "item_0"}, {"glyph", "smart_set.0.switch"}, {"enable_cursor", "true"}},
            {N("IBStackItem"), N("BSwitchStatic", {{"object", "toggle_15col"}}),
             N("BTextStatic", {{"string", "lng_vsync_toggle_header"}, {"glyph", "text_title"}}),
             N("IBComboTextStatic", {{"data_path", "vsync.index"}, {"value_list", "lng_off;lng_on"},
                                     {"text_glyph", "text_value"}})}),
          N("Item", {{"id", "apply"}, {"glyph", "smart_set.switch_footer"}, {"enable_cursor", "true"}},
            {N("BSwitchStatic", {{"object", "apply_button"}}),
             N("IBSelectableSimple", {{"out_data_path", "event"}, {"select_value", "confirm"}})})}),
       N("behaviours", {},
         {N("SBAudioNotification", {{"screen_name", "basic_graphics"}}),
          N("SBScreenTitle", {{"string_id", "lng_basic_graphics_title"}, {"override_format_id", "localise"}}),
          N("SBHotButtonScreenEvent", {{"action", "Back"}, {"event_primary", "back"}, {"data_path", "event"}}),
          N("SBItemStack", {{"glyph", "smart_set.stacker"}}),
          N("BTextStatic", {{"string", "lng_basic_graph_settings_context_title"},
                            {"glyph", "smart_contextual_info.text_title"}}),
          N("BTextStatic", {{"string", "lng_basic_graph_settings_context_description"},
                            {"glyph", "smart_contextual_info.text"}}),
          std::move(flow)})});
}

Node GraphicsCalibration() {
  return N("Screen",
           {{"id", "graphics_calibration"}, {"glyph", "screen_directory.screen_choice"},
            {"object", "smart_screen_tabbed"}},
           {N("items"),
            N("behaviours", {},
              {N("SBTabGroup", {{"current_index_data_path", "tabs.current_index"},
                                {"tab_info_data_path", "tabs.info[%u]"},
                                {"requested_index_check_data_path", "tabs.requested_index"},
                                {"stacker_glyph", "smart_tabs.stacker_tabs"},
                                {"hide_single_tab", "true"}}),
               N("SBAnimated", {{"open", "open"}})})});
}

Node Screens() {
  return N("xml", {},
           {N("World", {{"glyph", "P"}},
              {Screen("options_ingame",
                      {Item("item_0", "lng_game_settings_label", "game_settings"),
                       Item("item_1", "lng_input_calibration_label", "input_calibration"),
                       Item("item_2", "lng_graphics_calibration_label", "graphics_calibration"),
                       Item("item_3", "lng_audio_calibration_label", "audio_calibration")},
                      "\r\n item_0\r\n item_1\r\n item_2\r\n item_3\r\n")}),
            N("World", {{"glyph", "P"}}, {BasicGraphics(), GraphicsCalibration()}),
            N("World", {{"glyph", "P"}},
              {Screen("pause_menu",
                      {Item("item_0", "lng_button_continue", "continue"),
                       Item("item_9", "lng_vr_reset_view", "reset_view", true)},
                      "\r\n item_0\r\n item_9\r\n")})});
}

const Node *Find(const Node &root, const std::function<bool(const Node &)> &match) {
  if (match(root)) {
    return &root;
  }
  for (const Node &child : root.children) {
    if (const Node *found = Find(child, match)) {
      return found;
    }
  }
  return nullptr;
}

bool Is(const Node &node, const char *name, const char *key, const char *value) {
  const std::string *found = node.Attribute(key);
  return node.name == name && found != nullptr && *found == value;
}

std::vector<uint8_t> ReadFile(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void TestRoundtrip() {
  std::cout << "[RUN] TestRoundtrip..." << std::endl;
  const Node original = Screens();
  const std::vector<uint8_t> encoded = dr2hook::bxml::Encode(original);
  Node decoded;
  TEST_ASSERT(dr2hook::bxml::Decode(encoded.data(), encoded.size(), decoded),
              "Decode do proprio Encode");
  TEST_ASSERT(dr2hook::bxml::Encode(decoded) == encoded, "ida e volta estavel");
  const Node *flow = Find(decoded, [](const Node &n) { return n.name == "SBGridItemFlow"; });
  TEST_ASSERT(flow != nullptr && flow->text &&
                  *flow->text == "\r\n item_0\r\n item_1\r\n item_2\r\n item_3\r\n",
              "texto do no preservado");
}

void TestDecodeRejectsGarbage() {
  std::cout << "[RUN] TestDecodeRejectsGarbage..." << std::endl;
  Node root;
  const uint8_t pssg[] = {'P', 'S', 'S', 'G', 0, 0, 0, 0};
  TEST_ASSERT(!dr2hook::bxml::Decode(pssg, sizeof(pssg), root), "rejeita outra magia");
  std::vector<uint8_t> truncated = dr2hook::bxml::Encode(States());
  truncated.resize(truncated.size() / 2);
  TEST_ASSERT(!dr2hook::bxml::Decode(truncated.data(), truncated.size(), root),
              "rejeita arquivo truncado");
}

const Node *ChildNode(const Node &parent, const std::string &id) {
  for (const Node &child : parent.children) {
    if (Is(child, "node", "id", id.c_str())) {
      return &child;
    }
  }
  return nullptr;
}

std::string LinkTarget(const Node &node, const std::string &linkId) {
  for (const Node &child : node.children) {
    if (Is(child, "link", "id", linkId.c_str())) {
      return *child.Attribute("target");
    }
  }
  return {};
}

bool IsBackTo(const Node &node, const std::string &target) {
  for (const Node &child : node.children) {
    if (Is(child, "link", "id", "back")) {
      return Is(child, "link", "target", target.c_str()) && Is(child, "link", "type", "back");
    }
  }
  return false;
}

const Node *ScreenOf(const Node &screens, const char *id) {
  return Find(screens, [&](const Node &n) { return Is(n, "Screen", "id", id); });
}

const Node *WorldOf(const Node &screens, const char *id) {
  for (const Node &world : screens.children) {
    for (const Node &screen : world.children) {
      if (Is(screen, "Screen", "id", id)) {
        return &world;
      }
    }
  }
  return nullptr;
}

std::string ListText(size_t count) {
  std::string text = "\r\n";
  for (size_t i = 0; i < count; ++i) {
    text += "        item_" + std::to_string(i) + "\r\n";
  }
  return text + "      ";
}

const Node *Behaviour(const Node &screen, const char *name, const char *key = nullptr,
                      const char *value = nullptr) {
  return Find(screen, [&](const Node &n) {
    return n.name == name && (key == nullptr || Is(n, name, key, value));
  });
}

// Página smart_screen: título, painel, lista com rolagem e sem rodapé.
void CheckPage(const Node *screen, const ui_patch::PageDef &page, const char *dataParent,
               size_t rows) {
  const std::string name = page.name;
  TEST_ASSERT(screen != nullptr, name + ": tela criada");
  if (screen == nullptr) {
    return;
  }
  TEST_ASSERT(Is(*screen, "Screen", "object", "smart_screen"), name + ": smart_screen");
  const std::string *parent = screen->Attribute("data_parent_override");
  TEST_ASSERT(dataParent == nullptr ? parent == nullptr
                                    : parent != nullptr && *parent == dataParent,
              name + ": raiz de dados");
  const Node &items = screen->children[0];
  TEST_ASSERT(items.children.size() == rows + 1 && Is(items.children[0], "Item", "id", "title"),
              name + ": cabecalho e uma linha por posicao, sem apply");
  TEST_ASSERT(Find(items, [](const Node &n) { return Is(n, "Item", "id", "apply"); }) == nullptr,
              name + ": sem rodape");
  for (size_t i = 0; i < rows && i + 1 < items.children.size(); ++i) {
    const Node &item = items.children[i + 1];
    const std::string index = std::to_string(i);
    TEST_ASSERT(Is(item, "Item", "id", ("item_" + index).c_str()) &&
                    Is(item, "Item", "glyph", ("smart_set." + index + ".switch").c_str()) &&
                    Is(item, "Item", "enable_cursor", "true"),
                name + ": id e glyph da linha " + index);
  }
  const Node *flow = Behaviour(*screen, "SBScrollableItemFlow");
  TEST_ASSERT(flow != nullptr && flow->text && *flow->text == ListText(rows),
              name + ": lista com um id por linha em CRLF");
  TEST_ASSERT(Behaviour(*screen, "SBScreenTitle", "string_id",
                        page.breadcrumbKey != nullptr ? page.breadcrumbKey
                                                      : page.titleKey) != nullptr,
              name + ": titulo pela chave propria");
  TEST_ASSERT(Behaviour(*screen, "SBAudioNotification", "screen_name", page.name) != nullptr,
              name + ": audio da propria tela");
  if (page.infoTitleKey != nullptr) {
    TEST_ASSERT(Behaviour(*screen, "BTextStatic", "string", page.infoTitleKey) != nullptr &&
                    Behaviour(*screen, "BTextStatic", "string", page.infoTextKey) != nullptr,
                name + ": painel de descricao");
  } else {
    TEST_ASSERT(Behaviour(*screen, "BTextData", "data_path", "sidebar.title") != nullptr &&
                    Behaviour(*screen, "BTextData", "data_path", "sidebar.description") != nullptr &&
                    Behaviour(*screen, "BTextStatic", "glyph",
                              "smart_contextual_info.text_title") == nullptr,
                name + ": painel ligado a sidebar.* (muda com o foco)");
    for (size_t i = 0; i < rows && i + 1 < items.children.size(); ++i) {
      const Node &item = items.children[i + 1];
      const std::string index = std::to_string(i);
      TEST_ASSERT(Find(item, [&](const Node &n) {
                    return Is(n, "IBItemFlowIndex", "index", index.c_str()) &&
                           Is(n, "IBItemFlowIndex", "index_data_path", "selected_index");
                  }),
                  name + ": linha " + index + " grava o foco em selected_index");
    }
  }
  TEST_ASSERT(Behaviour(*screen, "SBHotButtonScreenEvent", "event_primary", "back") != nullptr,
              name + ": B volta pelo evento back");
}

bool RowIs(const Node &item, const char *object, const char *label, const char *event) {
  return Find(item, [&](const Node &n) { return Is(n, "BSwitchStatic", "object", object); }) &&
         Find(item, [&](const Node &n) { return Is(n, "BTextStatic", "string", label); }) &&
         Find(item, [&](const Node &n) {
           return Is(n, "IBSelectableSimple", "select_value", event) &&
                  Is(n, "IBSelectableSimple", "out_data_path", "event");
         });
}

void TestPatchChain() {
  std::cout << "[RUN] TestPatchChain..." << std::endl;
  std::string error;
  Node states = States(), flow = Flow(), screens = Screens();

  TEST_ASSERT(ui_patch::PatchStates(states, error), error);
  for (const ui_patch::ScreenDef &def : ui_patch::kScreens) {
    const Node *state = Find(states, [&](const Node &n) {
      return Is(n, "StateScreenFECore", "screen_name", def.name);
    });
    TEST_ASSERT(state != nullptr && *state->Attribute("id") == def.stateId,
                std::string("estado com id fixo: ") + def.name);
    TEST_ASSERT(std::stoul(def.stateId) == def.stateIdValue,
                std::string("id em texto e em numero: ") + def.name);
  }
  for (const ui_patch::PageDef &page : ui_patch::kPages) {
    TEST_ASSERT(Find(states, [&](const Node &n) { return Is(n, "StateScreenFECore", "screen_name", page.name); }) == nullptr,
                std::string("pagina de aba sem estado: ") + page.name);
  }

  size_t linked = 0;
  TEST_ASSERT(ui_patch::PatchFlow(flow, linked, error), error);
  TEST_ASSERT(linked == 1, "so o no com link options");
  const Node *pause = Find(flow, [](const Node &n) { return Is(n, "node", "id", "231066979"); });
  const std::string hubId = pause != nullptr ? LinkTarget(*pause, ui_patch::kPauseEvent) : "";
  TEST_ASSERT(!hubId.empty() && std::stoul(hubId) < (1ul << 28), "link novo no no de pausa");

  const Node *siblings = Find(flow, [](const Node &n) { return Is(n, "node", "id", "129712173"); });
  const Node *hub = siblings != nullptr ? ChildNode(*siblings, hubId) : nullptr;
  TEST_ASSERT(hub != nullptr && Is(*hub, "node", "state", ui_patch::kHub.stateId),
              "hub no mesmo pai do alvo de options");
  TEST_ASSERT(hub != nullptr && IsBackTo(*hub, "231066979"), "back do hub volta a pausa");

  std::string modId;
  for (size_t i = 0; hub != nullptr && i < ui_patch::kListSlots; ++i) {
    const std::string target = LinkTarget(*hub, ui_patch::kNavModPrefix + std::to_string(i));
    TEST_ASSERT(!target.empty() && (modId.empty() || target == modId),
                "toda linha da aba Mods leva a mesma tela de mod");
    modId = target;
  }
  const Node *mod = siblings != nullptr ? ChildNode(*siblings, modId) : nullptr;
  TEST_ASSERT(mod != nullptr && Is(*mod, "node", "state", ui_patch::kMod.stateId),
              "tela de mod irma do hub");
  TEST_ASSERT(mod != nullptr && IsBackTo(*mod, hubId), "back do mod volta ao hub");
  TEST_ASSERT(hubId != modId, "ids de no distintos");

  TEST_ASSERT(ui_patch::PatchScreens(screens, error), error);
  const Node *host = ScreenOf(screens, ui_patch::kHub.name);
  TEST_ASSERT(host != nullptr && Is(*host, "Screen", "object", "smart_screen_tabbed"),
              "host com abas");
  const Node *tabs = host != nullptr ? Behaviour(*host, "SBTabGroup") : nullptr;
  TEST_ASSERT(tabs != nullptr && Is(*tabs, "SBTabGroup", "tab_info_data_path", "tabs.info[%u]") &&
                  Is(*tabs, "SBTabGroup", "current_index_data_path", "tabs.current_index"),
              "SBTabGroup com os caminhos que a DLL cria");
  TEST_ASSERT(tabs != nullptr && tabs->Attribute("requested_index_check_data_path") == nullptr,
              "sem pergunta ao estado antes de trocar de aba");

  const Node *graphicsWorld = WorldOf(screens, "graphics_calibration");
  for (const char *id : {ui_patch::kHub.name, ui_patch::kMainPage.name,
                         ui_patch::kModsPage.name, ui_patch::kMod.name}) {
    TEST_ASSERT(WorldOf(screens, id) == graphicsWorld, std::string("no World de graficos: ") + id);
  }

  constexpr size_t kMainCount = sizeof(ui_patch::kMainButtons) / sizeof(ui_patch::kMainButtons[0]);
  const Node *mainPage = ScreenOf(screens, ui_patch::kMainPage.name);
  const Node *modsPage = ScreenOf(screens, ui_patch::kModsPage.name);
  const Node *modPage = ScreenOf(screens, ui_patch::kMod.name);
  CheckPage(mainPage, ui_patch::kMainPage, ui_patch::kHub.name, kMainCount);
  CheckPage(modsPage, ui_patch::kModsPage, ui_patch::kHub.name, ui_patch::kListSlots);
  CheckPage(modPage, ui_patch::kModPage, nullptr, ui_patch::kListSlots);

  for (size_t i = 0; mainPage != nullptr && i < kMainCount; ++i) {
    const Node &item = mainPage->children[0].children[i + 1];
    const ui_patch::Button &button = ui_patch::kMainButtons[i];
    TEST_ASSERT(RowIs(item, "button", button.label, button.event),
                std::string("botao ") + button.label);
    TEST_ASSERT(Find(item, [](const Node &n) { return Is(n, "BTextStatic", "explicit", "true"); }),
                std::string("rotulo literal ") + button.label);
    TEST_ASSERT(!Find(item, [](const Node &n) { return n.name == "BVisibilityControlData"; }),
                "botao da aba DR2 Hook sempre visivel");
  }

  for (size_t i = 0; modsPage != nullptr && modPage != nullptr && i < ui_patch::kListSlots; ++i) {
    const std::string index = std::to_string(i);
    const Node &modRow = modsPage->children[0].children[i + 1];
    TEST_ASSERT(RowIs(modRow, "button", (ui_patch::kModKeyPrefix + index).c_str(),
                      (ui_patch::kNavModPrefix + index).c_str()),
                "linha de mod " + index);
    const Node &optionRow = modPage->children[0].children[i + 1];
    TEST_ASSERT(RowIs(optionRow, "toggle_15col", (ui_patch::kOptionKeyPrefix + index).c_str(),
                      (ui_patch::kOptionEventPrefix + index).c_str()),
                "linha de opcao " + index);
    const std::string data = ui_patch::kOptionDataPrefix + index;
    TEST_ASSERT(Find(optionRow, [&](const Node &n) {
                  return Is(n, "IBComboTextData", "data_path", (data + ".index").c_str()) &&
                         Is(n, "IBComboTextData", "list_value_data_path", (data + ".list[%u]").c_str()) &&
                         Is(n, "IBComboTextData", "list_value_format_id", "explicit") &&
                         Is(n, "IBComboTextData", "text_glyph", "text_value");
                }),
                "combo da opcao " + index);
    for (const auto &[row, path] : {std::pair{&modRow, ui_patch::kModSlotPath},
                                    std::pair{&optionRow, ui_patch::kOptionSlotPath}}) {
      const std::string expected = path + index;
      const Node *visibility = Find(*row, [](const Node &n) { return n.name == "BVisibilityControlData"; });
      TEST_ASSERT(visibility != nullptr &&
                      Is(*visibility, "BVisibilityControlData", "data_path", expected.c_str()) &&
                      visibility->Attribute("glyph") == nullptr,
                  "condicao do item inteiro " + expected);
      TEST_ASSERT(!Find(*row, [](const Node &n) { return Is(n, "BTextStatic", "explicit", "true"); }),
                  "rotulo dinamico sem explicit " + index);
    }
  }
  for (const Node *page : {mainPage, modsPage, modPage}) {
    TEST_ASSERT(page != nullptr &&
                    !Find(*page, [](const Node &n) {
                      return Is(n, "SBHotButtonScreenEvent", "action", "LeftShoulder") ||
                             Is(n, "SBHotButtonScreenEvent", "action", "RightShoulder");
                    }),
                "LB e RB ficam com o SBTabGroup");
  }

  const Node *pauseScreen = ScreenOf(screens, "pause_menu");
  const Node *pauseItem =
      pauseScreen != nullptr
          ? Find(*pauseScreen, [](const Node &n) { return Is(n, "Item", "id", "item_9"); })
          : nullptr;
  TEST_ASSERT(pauseItem != nullptr &&
                  Find(*pauseItem, [](const Node &n) {
                    return Is(n, "IBSelectableSimple", "select_value", ui_patch::kPauseEvent);
                  }),
              "item 9 dispara o evento do link");
  TEST_ASSERT(pauseItem != nullptr &&
                  Find(*pauseItem, [](const Node &n) {
                    return Is(n, "BTextStatic", "explicit", "true") &&
                           Is(n, "BTextStatic", "string", ui_patch::kLabel);
                  }),
              "rotulo literal");
  TEST_ASSERT(pauseItem != nullptr &&
                  !Find(*pauseItem, [](const Node &n) { return n.name == "BVisibilityControlData"; }),
              "item 9 sem condicao de visibilidade");
  const Node *basic = ScreenOf(screens, "basic_graphics");
  TEST_ASSERT(basic != nullptr && basic->children[0].children.size() == 3 &&
                  basic->Attribute("data_parent_override") == nullptr,
              "basic_graphics intacta");
  const Node *graphics = ScreenOf(screens, "graphics_calibration");
  TEST_ASSERT(graphics != nullptr && Behaviour(*graphics, "SBTabGroup", "requested_index_check_data_path",
                                               "tabs.requested_index"),
              "graphics_calibration intacta");
}

void TestEventPrefixes() {
  std::cout << "[RUN] TestEventPrefixes..." << std::endl;
  const std::string action = ui_patch::kActionPrefix;
  for (const ui_patch::Button &button : ui_patch::kMainButtons) {
    TEST_ASSERT(std::string(button.event).rfind(action, 0) == 0,
                std::string("botao da aba DR2 Hook tratado pela DLL: ") + button.event);
  }
  TEST_ASSERT(std::string(ui_patch::kOptionEventPrefix).rfind(action, 0) == 0,
              "opcoes de mod sao consumidas pela DLL");
  TEST_ASSERT(std::string(ui_patch::kNavModPrefix).rfind(action, 0) != 0,
              "navegacao segue para o fluxo");
  TEST_ASSERT(std::string(ui_patch::kPauseEvent).rfind(action, 0) != 0,
              "evento do item da pausa fica com o fluxo");
  TEST_ASSERT(std::string(ui_patch::kModKeyPrefix).rfind(ui_patch::kKeyPrefix, 0) == 0 &&
                  std::string(ui_patch::kOptionKeyPrefix).rfind(ui_patch::kKeyPrefix, 0) == 0,
              "chaves dinamicas com o prefixo da busca");
  for (const ui_patch::PageDef &page : {ui_patch::kMainPage, ui_patch::kModsPage, ui_patch::kModPage}) {
    for (const char *key : {page.titleKey, page.infoTitleKey, page.infoTextKey}) {
      if (key == nullptr) {
        continue; // painel ligado a dados
      }
      TEST_ASSERT(std::string(key).rfind(ui_patch::kKeyPrefix, 0) == 0,
                  std::string("chave respondida pela DLL: ") + key);
    }
  }
  TEST_ASSERT(ui_patch::kModsTab < sizeof(ui_patch::kPages) / sizeof(ui_patch::kPages[0]) &&
                  std::string(ui_patch::kPages[ui_patch::kModsTab].name) == ui_patch::kModsPage.name,
              "indice da aba Mods");
}

void TestTemplateWithoutItems() {
  std::cout << "[RUN] TestTemplateWithoutItems..." << std::endl;
  for (const char *broken : {"title", "flow", "tabs"}) {
    Node screens = Screens();
    Node &basic = screens.children[1].children[0];
    Node &graphics = screens.children[1].children[1];
    if (std::string(broken) == "title") {
      basic.children[0].children.erase(basic.children[0].children.begin());
    } else if (std::string(broken) == "flow") {
      basic.children[1].RemoveChild("SBScrollableItemFlow");
    } else {
      graphics.children[1].RemoveChild("SBTabGroup");
    }
    const auto before = dr2hook::bxml::Encode(screens);
    std::string error;
    TEST_ASSERT(!ui_patch::PatchScreens(screens, error), std::string("molde sem ") + broken);
    TEST_ASSERT(dr2hook::bxml::Encode(screens) == before, std::string("screens intacto sem ") + broken);
  }
}

void TestPatchesAreIndependent() {
  std::cout << "[RUN] TestPatchesAreIndependent..." << std::endl;
  // O jogo interpreta os documentos em ordem variavel: o resultado nao pode
  // depender dela.
  int order[] = {0, 1, 2};
  std::vector<std::vector<uint8_t>> first;
  do {
    Node docs[] = {States(), Flow(), Screens()};
    std::string error;
    size_t linked = 0;
    for (int which : order) {
      bool ok = which == 0   ? ui_patch::PatchStates(docs[0], error)
                : which == 1 ? ui_patch::PatchFlow(docs[1], linked, error)
                             : ui_patch::PatchScreens(docs[2], error);
      TEST_ASSERT(ok, error);
    }
    std::vector<std::vector<uint8_t>> encoded;
    for (const Node &doc : docs) {
      encoded.push_back(dr2hook::bxml::Encode(doc));
    }
    if (first.empty()) {
      first = encoded;
    }
    TEST_ASSERT(encoded == first, "mesmo resultado em qualquer ordem");
  } while (std::next_permutation(order, order + 3));
}

void TestPatchRefusesTwice() {
  std::cout << "[RUN] TestPatchRefusesTwice..." << std::endl;
  std::string error;
  size_t linked = 0;
  Node states = States(), flow = Flow(), screens = Screens();
  TEST_ASSERT(ui_patch::PatchStates(states, error), error);
  TEST_ASSERT(ui_patch::PatchFlow(flow, linked, error), error);
  TEST_ASSERT(ui_patch::PatchScreens(screens, error), error);
  const auto before = dr2hook::bxml::Encode(flow);
  TEST_ASSERT(!ui_patch::PatchStates(states, error), "states duas vezes");
  TEST_ASSERT(!ui_patch::PatchFlow(flow, linked, error), "flow duas vezes");
  TEST_ASSERT(!ui_patch::PatchScreens(screens, error), "screens duas vezes");
  TEST_ASSERT(dr2hook::bxml::Encode(flow) == before, "flow intacto na recusa");
}

void TestStateIdCollision() {
  std::cout << "[RUN] TestStateIdCollision..." << std::endl;
  std::string error;
  Node states = States();
  states.children.push_back(N("StateOther", {{"id", ui_patch::kMod.stateId}}));
  const auto before = dr2hook::bxml::Encode(states);
  TEST_ASSERT(!ui_patch::PatchStates(states, error), "id fixo ocupado");
  TEST_ASSERT(dr2hook::bxml::Encode(states) == before, "states intacto");
}

void TestRealFiles() {
  const char *dir = std::getenv("DR2_UI_DIR");
  if (dir == nullptr) {
    std::cout << "[SKIP] TestRealFiles (defina DR2_UI_DIR)" << std::endl;
    return;
  }
  std::cout << "[RUN] TestRealFiles em " << dir << "..." << std::endl;
  const char *out = std::getenv("DR2_UI_OUT");
  size_t linked = 0;
  const char *names[] = {"states", "flow", "screens"};
  for (const char *name : names) {
    const std::vector<uint8_t> data = ReadFile(std::string(dir) + "/" + name + ".bin");
    Node root;
    TEST_ASSERT(dr2hook::bxml::Decode(data.data(), data.size(), root),
                std::string("decode ") + name);
    TEST_ASSERT(dr2hook::bxml::Encode(root) == data, std::string("byte a byte ") + name);
    std::string error;
    bool ok = false;
    if (std::string(name) == "states") ok = ui_patch::PatchStates(root, error);
    if (std::string(name) == "flow") ok = ui_patch::PatchFlow(root, linked, error);
    if (std::string(name) == "screens") ok = ui_patch::PatchScreens(root, error);
    TEST_ASSERT(ok, std::string(name) + ": " + error);
    if (ok && std::string(name) == "screens") {
      const Node *page = Find(root, [](const Node &n) {
        return Is(n, "Screen", "id", ui_patch::kMainMenuPage.name);
      });
      TEST_ASSERT(page != nullptr && page->children.size() == 2 &&
                      page->children[0].children.size() == 1 &&
                      Find(*page, [](const Node &n) {
                        return Is(n, "IBSelectableSimple", "select_value", ui_patch::kPauseEvent);
                      }) != nullptr &&
                      Find(*page, [](const Node &n) {
                        return n.name == "SBHotButtonScreenEvent";
                      }) == nullptr,
                  "aba do menu principal: um bloco que abre o DR2 Hook");
    }
    if (ok && std::string(name) == "flow") {
      const Node *mainMenu = Find(root, [](const Node &n) {
        return Is(n, "node", "jump_id", ui_patch::kMainMenuJumpId);
      });
      TEST_ASSERT(mainMenu != nullptr &&
                      Find(*mainMenu, [](const Node &n) {
                        return Is(n, "link", "id", ui_patch::kPauseEvent);
                      }) != nullptr,
                  "menu principal ligado ao hub");
    }
    if (out != nullptr) {
      const std::vector<uint8_t> patched = dr2hook::bxml::Encode(root);
      std::ofstream(std::string(out) + "/" + name + ".bin", std::ios::binary)
          .write(reinterpret_cast<const char *>(patched.data()),
                 static_cast<std::streamsize>(patched.size()));
    }
  }
  std::cout << "  estados " << ui_patch::kHub.stateId << ".." << ui_patch::kMod.stateId
            << ", link em " << linked
            << " no(s) com options" << std::endl;
}

} // namespace

int main() {
  std::cout << "DR2Hook - Testes do XML binario e do patch de UI" << std::endl;
  TestRoundtrip();
  TestDecodeRejectsGarbage();
  TestPatchChain();
  TestPatchesAreIndependent();
  TestPatchRefusesTwice();
  TestStateIdCollision();
  TestEventPrefixes();
  TestTemplateWithoutItems();
  TestRealFiles();
  std::cout << g_testsRun - g_testsFailed << "/" << g_testsRun << " asserções OK"
            << std::endl;
  return g_testsFailed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
