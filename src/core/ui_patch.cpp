#include "dr2hook/ui_patch.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <set>

namespace dr2hook::ui_patch {
namespace {

using bxml::Node;
using Path = std::vector<size_t>;

constexpr char kPauseScreen[] = "pause_menu";
constexpr char kHostTemplate[] = "graphics_calibration";
constexpr char kPageTemplate[] = "basic_graphics";
constexpr char kPauseItemEvent[] = "reset_view";
constexpr char kTemplateLink[] = "options";
// O runner do fluxo usa os bits acima de 28 do id de nó.
constexpr uint32_t kFirstNodeId = 0x0d520000;
constexpr uint32_t kNodeIdLimit = 1u << 28;

bool AttributeIs(const Node &node, const char *key, const char *value) {
  const std::string *found = node.Attribute(key);
  return found != nullptr && *found == value;
}

Node &At(Node &root, const Path &path) {
  Node *node = &root;
  for (size_t index : path) {
    node = &node->children[index];
  }
  return *node;
}

void Walk(const Node &node, Path &path,
          const std::function<void(const Node &, const Path &)> &visit) {
  visit(node, path);
  for (size_t i = 0; i < node.children.size(); ++i) {
    path.push_back(i);
    Walk(node.children[i], path, visit);
    path.pop_back();
  }
}

void Walk(const Node &root,
          const std::function<void(const Node &, const Path &)> &visit) {
  Path path;
  Walk(root, path, visit);
}

std::string FreshId(std::set<uint32_t> &used) {
  for (uint32_t id = kFirstNodeId; id < kNodeIdLimit; ++id) {
    if (used.insert(id).second) {
      return std::to_string(id);
    }
  }
  return {};
}

std::set<uint32_t> NumericIds(const Node &root) {
  std::set<uint32_t> used;
  Walk(root, [&](const Node &node, const Path &) {
    if (const std::string *id = node.Attribute("id")) {
      char *end = nullptr;
      const unsigned long long value = std::strtoull(id->c_str(), &end, 10);
      if (!id->empty() && *end == '\0' && value <= UINT32_MAX) {
        used.insert(static_cast<uint32_t>(value));
      }
    }
  });
  return used;
}

Node MakeNode(std::string name,
              std::vector<std::pair<std::string, std::string>> attributes) {
  Node node;
  node.name = std::move(name);
  node.attributes = std::move(attributes);
  return node;
}

Node BackLink(const std::string &target) {
  return MakeNode("link", {{"id", "back"}, {"target", target}, {"type", "back"}});
}

bool IsOwnState(const std::string &id) {
  return std::any_of(std::begin(kScreens), std::end(kScreens),
                     [&](const ScreenDef &def) { return id == def.stateId; });
}

// `literal` grava o rótulo como texto; senão ele é uma chave que a DLL
// responde na busca de idioma.
void RelabelItem(Node &item, const std::string &label, const std::string &event,
                 bool literal) {
  for (Node &behaviour : item.children) {
    if (behaviour.name == "BTextStatic") {
      behaviour.SetAttribute("string", label);
      if (literal) {
        behaviour.SetAttribute("explicit", "true");
      } else {
        behaviour.attributes.erase(
            std::remove_if(behaviour.attributes.begin(),
                           behaviour.attributes.end(),
                           [](const auto &a) { return a.first == "explicit"; }),
            behaviour.attributes.end());
      }
    } else if (behaviour.name == "IBSelectableSimple") {
      behaviour.SetAttribute("select_value", event);
    }
  }
}

bool ItemHasEvent(const Node &item, const char *event) {
  return std::any_of(item.children.begin(), item.children.end(),
                     [&](const Node &behaviour) {
                       return behaviour.name == "IBSelectableSimple" &&
                              AttributeIs(behaviour, "select_value", event);
                     });
}

bool HasChild(const Node &node, const char *name) {
  return std::any_of(node.children.begin(), node.children.end(),
                     [&](const Node &child) { return child.name == name; });
}

void RemoveAttribute(Node &node, const char *key) {
  node.attributes.erase(
      std::remove_if(node.attributes.begin(), node.attributes.end(),
                     [&](const auto &a) { return a.first == key; }),
      node.attributes.end());
}

Node *FindChild(Node &parent, const char *name, const char *key,
                const char *value) {
  for (Node &child : parent.children) {
    if (child.name == name && AttributeIs(child, key, value)) {
      return &child;
    }
  }
  return nullptr;
}

struct Row {
  std::string label;
  bool literal = false;
  std::string event;
  std::string visibilityPath;
  // Posição do combo (nós <kOptionDataPrefix>N.*); -1 é um botão.
  int comboSlot = -1;
  // Grava o índice em foco em selected_index, como em
  // profile_save_management; a DLL troca o painel da direita.
  bool reportsFocus = false;
};

// Linha de smart_screen no formato de profile_save_management (botão) e de
// basic_graphics (combo). O combo também tem IBSelectableSimple para o A.
Node RowItem(size_t index, const Row &row) {
  const std::string i = std::to_string(index);
  Node item = MakeNode("Item", {{"id", "item_" + i},
                                {"glyph", "smart_set." + i + ".switch"},
                                {"enable_cursor", "true"}});
  const bool combo = row.comboSlot >= 0;
  item.children.push_back(MakeNode("IBStackItem", {}));
  item.children.push_back(
      MakeNode("BSwitchStatic", {{"object", combo ? "toggle_15col" : "button"}}));
  item.children.push_back(MakeNode(
      "BVisibilityControlStatic", {{"glyph", "save_dot"}, {"visible", "false"}}));
  Node text = MakeNode("BTextStatic", {{"string", row.label}, {"glyph", "text_title"}});
  if (row.literal) {
    text.SetAttribute("explicit", "true");
  }
  item.children.push_back(std::move(text));
  if (combo) {
    const std::string data = kOptionDataPrefix + std::to_string(row.comboSlot);
    item.children.push_back(MakeNode(
        "IBComboTextData", {{"data_path", data + ".index"},
                            {"list_value_data_path", data + ".list[%u]"},
                            {"watch_data_list", "true"},
                            {"list_value_format_id", "explicit"},
                            {"text_glyph", "text_value"}}));
  }
  item.children.push_back(MakeNode("IBSelectableSimple",
                                   {{"out_data_path", "event"},
                                    {"select_value", row.event},
                                    {"help_text", "lng_select"}}));
  if (row.reportsFocus) {
    item.children.push_back(MakeNode(
        "IBItemFlowIndex", {{"index", i}, {"index_data_path", "selected_index"}}));
  }
  if (!row.visibilityPath.empty()) {
    item.children.push_back(MakeNode("BVisibilityControlData",
                                     {{"data_path", row.visibilityPath},
                                      {"watch_data", "false"}}));
  }
  item.children.push_back(MakeNode("IBAnimated", {}));
  return item;
}

// Cópia de basic_graphics (smart_screen com rolagem) com as linhas dadas, sem
// o rodapé apply. O texto do SBScrollableItemFlow é um id por linha, em CRLF,
// como nos dados originais.
bool BuildPage(const Node &templateScreen, const PageDef &page,
               const char *dataParent, const std::vector<Row> &rows, Node &out,
               std::string &error) {
  out = templateScreen;
  out.SetAttribute("id", page.name);
  if (dataParent != nullptr) {
    out.SetAttribute("data_parent_override", dataParent);
  }
  Node *items = out.Child("items");
  Node *behaviours = out.Child("behaviours");
  Node *titleItem =
      items != nullptr ? FindChild(*items, "Item", "id", "title") : nullptr;
  Node *titleText = titleItem != nullptr ? titleItem->Child("BTextStatic") : nullptr;
  Node *flow = behaviours != nullptr ? behaviours->Child("SBScrollableItemFlow")
                                     : nullptr;
  Node *title = behaviours != nullptr ? behaviours->Child("SBScreenTitle") : nullptr;
  Node *audio = behaviours != nullptr ? behaviours->Child("SBAudioNotification")
                                      : nullptr;
  Node *infoTitle = behaviours != nullptr
                        ? FindChild(*behaviours, "BTextStatic", "glyph",
                                    "smart_contextual_info.text_title")
                        : nullptr;
  Node *infoText = behaviours != nullptr
                       ? FindChild(*behaviours, "BTextStatic", "glyph",
                                   "smart_contextual_info.text")
                       : nullptr;
  if (titleText == nullptr || flow == nullptr || title == nullptr ||
      audio == nullptr || infoTitle == nullptr || infoText == nullptr) {
    error = std::string(kPageTemplate) + " sem o layout esperado";
    return false;
  }
  if (rows.empty() || rows.size() > kListSlots) {
    error = std::string("numero de linhas invalido em ") + page.name;
    return false;
  }

  titleText->SetAttribute("string", page.titleKey);
  title->SetAttribute("string_id",
                      page.breadcrumbKey != nullptr ? page.breadcrumbKey : page.titleKey);
  audio->SetAttribute("screen_name", page.name);
  if (page.infoTitleKey != nullptr) {
    infoTitle->SetAttribute("string", page.infoTitleKey);
    infoText->SetAttribute("string", page.infoTextKey);
  } else {
    // Painel ligado a dados (sidebar.*), escrito pela DLL conforme o foco.
    *infoTitle = MakeNode("BTextData", {{"glyph", "smart_contextual_info.text_title"},
                                        {"data_path", "sidebar.title"},
                                        {"format_id", "explicit"},
                                        {"watch_data", "true"}});
    *infoText = MakeNode("BTextData", {{"glyph", "smart_contextual_info.text"},
                                       {"data_path", "sidebar.description"},
                                       {"format_id", "explicit"},
                                       {"watch_data", "true"}});
  }

  Node header = *titleItem;
  items->children.clear();
  items->children.push_back(std::move(header));
  std::string flowText = "\r\n";
  for (size_t i = 0; i < rows.size(); ++i) {
    items->children.push_back(RowItem(i, rows[i]));
    flowText += "        item_" + std::to_string(i) + "\r\n";
  }
  flow->text = flowText + "      ";
  return true;
}

// Cópia de graphics_calibration. Sem requested_index_check o SBTabGroup troca
// de aba sem perguntar ao estado; os nomes de tabs.* são os que a DLL cria.
bool BuildHost(const Node &templateScreen, Node &out, std::string &error) {
  out = templateScreen;
  out.SetAttribute("id", kHub.name);
  Node *behaviours = out.Child("behaviours");
  Node *tabs = behaviours != nullptr ? behaviours->Child("SBTabGroup") : nullptr;
  if (tabs == nullptr ||
      !AttributeIs(*tabs, "current_index_data_path", "tabs.current_index") ||
      !AttributeIs(*tabs, "tab_info_data_path", "tabs.info[%u]")) {
    error = std::string(kHostTemplate) + " sem o SBTabGroup esperado";
    return false;
  }
  RemoveAttribute(*tabs, "requested_index_check_data_path");
  return true;
}

} // namespace

bool PatchStates(Node &root, std::string &error) {
  for (const Node &state : root.children) {
    for (const ScreenDef &def : kScreens) {
      if (AttributeIs(state, "screen_name", def.name)) {
        error = std::string("estado ") + def.name + " ja existe";
        return false;
      }
      if (AttributeIs(state, "id", def.stateId)) {
        error = std::string("id de estado ") + def.stateId + " ja usado por " +
                state.name;
        return false;
      }
    }
  }
  for (const ScreenDef &def : kScreens) {
    root.children.push_back(MakeNode(
        "StateScreenFECore", {{"id", def.stateId}, {"screen_name", def.name}}));
  }
  return true;
}

bool PatchFlow(Node &root, size_t &linkedNodes, std::string &error) {
  struct Origin {
    Path path;
    std::string id;
    std::string optionsTarget;
    bool mainMenu = false;
  };
  std::vector<Origin> origins;
  std::map<std::string, Path> parentOf;
  bool stateInUse = false;
  Walk(root, [&](const Node &node, const Path &path) {
    if (node.name != "node") {
      return;
    }
    const std::string *id = node.Attribute("id");
    if (id == nullptr) {
      return;
    }
    if (const std::string *state = node.Attribute("state");
        state != nullptr && IsOwnState(*state)) {
      stateInUse = true;
    }
    if (!path.empty()) {
      parentOf[*id] = Path(path.begin(), path.end() - 1);
    }
    const std::string *target = nullptr;
    const bool mainMenu = AttributeIs(node, "jump_id", kMainMenuJumpId);
    for (const Node &child : node.children) {
      if (child.name == "link" && AttributeIs(child, "id", kPauseEvent)) {
        return;
      }
      if (child.name == "link" &&
          AttributeIs(child, "id", mainMenu ? kMainMenuAnchorLink : kTemplateLink)) {
        target = child.Attribute("target");
      }
    }
    if (target != nullptr) {
      origins.push_back({path, *id, *target, mainMenu});
    }
  });
  if (stateInUse) {
    error = "estado dr2hook ja usado no fluxo";
    return false;
  }
  if (origins.empty()) {
    error = "nenhum no com link options";
    return false;
  }

  // Um par hub/mod por origem, para que cada `back` volte à pausa de onde
  // veio. Acrescentar no fim de children não muda os índices, então os
  // caminhos continuam válidos durante as alterações.
  Node patched = root;
  std::set<uint32_t> used = NumericIds(root);
  for (const Origin &origin : origins) {
    const auto parent = parentOf.find(origin.optionsTarget);
    if (parent == parentOf.end()) {
      error = "alvo de options sem pai: " + origin.optionsTarget;
      return false;
    }
    const std::string hubId = FreshId(used);
    const std::string modId = FreshId(used);
    if (hubId.empty() || modId.empty()) {
      error = "sem id de no livre";
      return false;
    }

    Node hub = MakeNode("node", {{"id", hubId}, {"state", kHub.stateId}});
    for (size_t i = 0; i < kListSlots; ++i) {
      hub.children.push_back(MakeNode(
          "link", {{"id", kNavModPrefix + std::to_string(i)}, {"target", modId}}));
    }
    hub.children.push_back(BackLink(origin.id));

    Node mod = MakeNode("node", {{"id", modId}, {"state", kMod.stateId}});
    mod.children.push_back(BackLink(hubId));

    At(patched, origin.path)
        .children.push_back(
            MakeNode("link", {{"id", kPauseEvent}, {"target", hubId}}));
    Node &siblings = At(patched, parent->second);
    siblings.children.push_back(std::move(hub));
    siblings.children.push_back(std::move(mod));

    // Menu principal: o mod interno abre direto, e MODS abre o hub na aba
    // Mods (estado próprio; mesmos links do hub).
    if (origin.mainMenu) {
      const std::string directId = FreshId(used);
      const std::string modsHubId = FreshId(used);
      if (directId.empty() || modsHubId.empty()) {
        error = "sem id de no livre";
        return false;
      }
      Node direct = MakeNode("node", {{"id", directId}, {"state", kModDirect.stateId}});
      direct.children.push_back(BackLink(origin.id));
      Node modsHub = MakeNode("node", {{"id", modsHubId}, {"state", kHubMods.stateId}});
      for (size_t i = 0; i < kListSlots; ++i) {
        modsHub.children.push_back(MakeNode(
            "link", {{"id", kNavModPrefix + std::to_string(i)}, {"target", modId}}));
      }
      modsHub.children.push_back(BackLink(origin.id));
      Node &menu = At(patched, origin.path);
      menu.children.push_back(
          MakeNode("link", {{"id", kMainMenuPracticeEvent}, {"target", directId}}));
      menu.children.push_back(
          MakeNode("link", {{"id", kMainMenuModsEvent}, {"target", modsHubId}}));
      Node &parentNode = At(patched, parent->second);
      parentNode.children.push_back(std::move(direct));
      parentNode.children.push_back(std::move(modsHub));
    }
  }
  root = std::move(patched);
  linkedNodes = origins.size();
  return true;
}

// Cópia de options_extras só com os blocos de kMainMenuTiles, textos e eventos
// nossos. Falso se o modelo não tiver o layout esperado.
bool BuildMainMenuPage(const Node &templateScreen, Node &out) {
  out = templateScreen;
  out.SetAttribute("id", kMainMenuPage.name);
  Node *items = out.Child("items");
  Node *behaviours = out.Child("behaviours");
  Node *flow = behaviours != nullptr ? behaviours->Child("SBGridItemFlow") : nullptr;
  if (items == nullptr || flow == nullptr) {
    return false;
  }
  std::vector<Node> kept;
  for (const MainMenuTile &def : kMainMenuTiles) {
    Node *source = FindChild(*items, "Item", "id", def.item);
    if (source == nullptr) {
      return false;
    }
    Node tile = *source;
    Node *title = FindChild(tile, "BTextStatic", "glyph", "text_title");
    Node *subtitle = FindChild(tile, "BTextStatic", "glyph", "text_subtitle");
    Node *texture = tile.Child("BTextureStatic");
    if (title == nullptr || texture == nullptr || tile.Child("IBSelectableSimple") == nullptr ||
        (def.subtitleKey != nullptr && subtitle == nullptr)) {
      return false;
    }
    title->SetAttribute("string", def.titleKey);
    if (subtitle != nullptr && def.subtitleKey != nullptr) {
      subtitle->SetAttribute("string", def.subtitleKey);
    }
    if (def.texture != nullptr) {
      texture->SetAttribute("texture", def.texture);
    }
    if (def.state == TileState::Action) {
      tile.Child("IBSelectableSimple")->SetAttribute("select_value", def.event);
    } else {
      tile.RemoveChild("IBSelectableSimple");
    }
    kept.push_back(std::move(tile));
  }
  items->children = std::move(kept);
  flow->text = std::string("\r\n          ") + kMainMenuGrid + "\r\n        ";
  // O atalho de "trial upsell" é da tela original.
  behaviours->RemoveChild("SBHotButtonScreenEvent");
  return true;
}

bool PatchScreens(Node &root, std::string &error) {
  Path hostPath, pagePath, pausePath, mainMenuTemplatePath;
  bool exists = false, foundHost = false, foundPage = false, foundPause = false;
  bool foundMainMenuTemplate = false;
  Walk(root, [&](const Node &node, const Path &path) {
    if (node.name != "Screen") {
      return;
    }
    const std::string *id = node.Attribute("id");
    if (id == nullptr) {
      return;
    }
    const bool own =
        *id == kHub.name || *id == kMod.name || *id == kMainMenuPage.name ||
        std::any_of(std::begin(kPages), std::end(kPages),
                    [&](const PageDef &page) { return *id == page.name; });
    if (own) {
      exists = true;
    } else if (*id == kHostTemplate) {
      hostPath = path;
      foundHost = true;
    } else if (*id == kPageTemplate) {
      pagePath = path;
      foundPage = true;
    } else if (*id == kPauseScreen) {
      pausePath = path;
      foundPause = true;
    } else if (*id == kMainMenuPage.templateScreen) {
      mainMenuTemplatePath = path;
      foundMainMenuTemplate = true;
    }
  });
  if (exists || !foundHost || !foundPage || !foundPause || hostPath.empty()) {
    error = exists ? "tela dr2hook ja existe"
                   : "tela graphics_calibration, basic_graphics ou pause_menu "
                     "ausente";
    return false;
  }

  Node patched = root;

  Node *pauseItems = At(patched, pausePath).Child("items");
  Node *pauseItem = nullptr;
  if (pauseItems != nullptr) {
    for (Node &item : pauseItems->children) {
      if (item.name == "Item" && ItemHasEvent(item, kPauseItemEvent)) {
        pauseItem = &item;
      }
    }
  }
  if (pauseItem == nullptr || !HasChild(*pauseItem, "BTextStatic")) {
    error = "item reset_view do pause_menu ausente";
    return false;
  }
  RelabelItem(*pauseItem, kLabel, kPauseEvent, true);
  pauseItem->RemoveChild("BVisibilityControlData");

  std::vector<Row> mainRows, modRows, optionRows;
  for (const Button &button : kMainButtons) {
    mainRows.push_back({button.label, true, button.event, {}, -1});
  }
  for (size_t i = 0; i < kListSlots; ++i) {
    const std::string index = std::to_string(i);
    modRows.push_back(
        {kModKeyPrefix + index, false, kNavModPrefix + index, kModSlotPath + index, -1});
    optionRows.push_back({kOptionKeyPrefix + index, false, kOptionEventPrefix + index,
                          kOptionSlotPath + index, static_cast<int>(i), true});
  }

  const Node &pageTemplate = At(root, pagePath);
  Node host, mainPage, modsPage, modPage;
  if (!BuildHost(At(root, hostPath), host, error) ||
      !BuildPage(pageTemplate, kMainPage, kHub.name, mainRows, mainPage, error) ||
      !BuildPage(pageTemplate, kModsPage, kHub.name, modRows, modsPage, error) ||
      !BuildPage(pageTemplate, kModPage, nullptr, optionRows, modPage, error)) {
    return false;
  }

  // As páginas das abas ficam no World do host, como as de graphics_calibration.
  const Path worldPath(hostPath.begin(), hostPath.end() - 1);
  Node &world = At(patched, worldPath);
  world.children.push_back(std::move(host));
  world.children.push_back(std::move(mainPage));
  world.children.push_back(std::move(modsPage));
  world.children.push_back(std::move(modPage));

  // Opcional: sem o modelo, a pausa continua funcionando sem a aba.
  Node mainMenuPage;
  if (foundMainMenuTemplate &&
      BuildMainMenuPage(At(root, mainMenuTemplatePath), mainMenuPage)) {
    const Path parent(mainMenuTemplatePath.begin(), mainMenuTemplatePath.end() - 1);
    At(patched, parent).children.push_back(std::move(mainMenuPage));
  }
  root = std::move(patched);
  return true;
}

} // namespace dr2hook::ui_patch
