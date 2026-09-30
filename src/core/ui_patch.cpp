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
constexpr char kTemplateScreen[] = "options_ingame";
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

std::string FreshId(const std::set<uint32_t> &used, uint32_t first,
                    uint32_t limit) {
  for (uint32_t id = first; id < limit; ++id) {
    if (used.count(id) == 0) {
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

void RelabelItem(Node &item, const char *event) {
  for (Node &behaviour : item.children) {
    if (behaviour.name == "BTextStatic") {
      behaviour.SetAttribute("string", kLabel);
      behaviour.SetAttribute("explicit", "true");
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

} // namespace

bool PatchStates(Node &root, std::string &error) {
  for (const Node &state : root.children) {
    if (AttributeIs(state, "screen_name", kScreenName)) {
      error = "estado dr2modloader ja existe";
      return false;
    }
    if (AttributeIs(state, "id", kStateId)) {
      error = std::string("id de estado ") + kStateId + " ja usado por " +
              state.name;
      return false;
    }
  }
  root.children.push_back(MakeNode(
      "StateScreenFECore", {{"id", kStateId}, {"screen_name", kScreenName}}));
  return true;
}

bool PatchFlow(Node &root, size_t &linkedNodes, std::string &error) {
  struct Origin {
    Path path;
    std::string id;
    std::string optionsTarget;
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
    if (AttributeIs(node, "state", kStateId)) {
      stateInUse = true;
    }
    if (!path.empty()) {
      parentOf[*id] = Path(path.begin(), path.end() - 1);
    }
    const std::string *target = nullptr;
    for (const Node &child : node.children) {
      if (child.name == "link" && AttributeIs(child, "id", kPauseEvent)) {
        return;
      }
      if (child.name == "link" && AttributeIs(child, "id", kTemplateLink)) {
        target = child.Attribute("target");
      }
    }
    if (target != nullptr) {
      origins.push_back({path, *id, *target});
    }
  });
  if (stateInUse) {
    error = std::string("estado ") + kStateId + " ja usado no fluxo";
    return false;
  }
  if (origins.empty()) {
    error = "nenhum no com link options";
    return false;
  }

  // Acrescentar no fim de children não muda os índices, então os caminhos
  // continuam válidos durante as alterações.
  Node patched = root;
  std::set<uint32_t> used = NumericIds(root);
  for (const Origin &origin : origins) {
    const auto parent = parentOf.find(origin.optionsTarget);
    if (parent == parentOf.end()) {
      error = "alvo de options sem pai: " + origin.optionsTarget;
      return false;
    }
    const std::string nodeId = FreshId(used, kFirstNodeId, kNodeIdLimit);
    if (nodeId.empty()) {
      error = "sem id de no livre";
      return false;
    }
    used.insert(static_cast<uint32_t>(std::stoul(nodeId)));

    Node screenNode = MakeNode("node", {{"id", nodeId}, {"state", kStateId}});
    screenNode.children.push_back(MakeNode(
        "link", {{"id", "back"}, {"target", origin.id}, {"type", "back"}}));
    At(patched, origin.path)
        .children.push_back(
            MakeNode("link", {{"id", kPauseEvent}, {"target", nodeId}}));
    At(patched, parent->second).children.push_back(std::move(screenNode));
  }
  root = std::move(patched);
  linkedNodes = origins.size();
  return true;
}

bool PatchScreens(Node &root, std::string &error) {
  Path templatePath, pausePath;
  bool exists = false, foundTemplate = false, foundPause = false;
  Walk(root, [&](const Node &node, const Path &path) {
    if (node.name != "Screen") {
      return;
    }
    if (AttributeIs(node, "id", kScreenName)) {
      exists = true;
    } else if (AttributeIs(node, "id", kTemplateScreen)) {
      templatePath = path;
      foundTemplate = true;
    } else if (AttributeIs(node, "id", kPauseScreen)) {
      pausePath = path;
      foundPause = true;
    }
  });
  if (exists || !foundTemplate || !foundPause || templatePath.empty()) {
    error = exists ? "tela dr2modloader ja existe"
                   : "tela options_ingame ou pause_menu ausente";
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
  RelabelItem(*pauseItem, kPauseEvent);
  pauseItem->RemoveChild("BVisibilityControlData");

  Node screen = At(patched, templatePath);
  screen.SetAttribute("id", kScreenName);
  Node *items = screen.Child("items");
  Node *behaviours = screen.Child("behaviours");
  Node *flow = behaviours != nullptr ? behaviours->Child("SBGridItemFlow")
                                     : nullptr;
  if (items == nullptr || items->children.empty() || flow == nullptr ||
      !HasChild(items->children.front(), "BTextStatic") ||
      !HasChild(items->children.front(), "IBSelectableSimple")) {
    error = "options_ingame sem o layout esperado";
    return false;
  }
  items->children.resize(1);
  const std::string *firstItem = items->children.front().Attribute("id");
  if (firstItem == nullptr) {
    error = "item de options_ingame sem id";
    return false;
  }
  RelabelItem(items->children.front(), kOverlayEvent);
  flow->text = " " + *firstItem + " ";

  const Path parentPath(templatePath.begin(), templatePath.end() - 1);
  At(patched, parentPath).children.push_back(std::move(screen));
  root = std::move(patched);
  return true;
}

} // namespace dr2hook::ui_patch
