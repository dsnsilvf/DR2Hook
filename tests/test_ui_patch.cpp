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
            N("behaviours", {}, {std::move(flow)})});
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

Node Screens() {
  return N("xml", {},
           {N("World", {{"glyph", "P"}},
              {Screen("options_ingame",
                      {Item("item_0", "lng_game_settings_label", "game_settings"),
                       Item("item_1", "lng_input_calibration_label", "input_calibration")},
                      "\r\n item_0 item_1\r\n"),
               Screen("pause_menu",
                      {Item("item_0", "lng_button_continue", "continue"),
                       Item("item_9", "lng_vr_reset_view", "reset_view", true)},
                      "\r\n item_0 item_9\r\n")})});
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
  TEST_ASSERT(flow != nullptr && flow->text && *flow->text == "\r\n item_0 item_1\r\n",
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

void TestPatchChain() {
  std::cout << "[RUN] TestPatchChain..." << std::endl;
  std::string error;
  Node states = States(), flow = Flow(), screens = Screens();

  TEST_ASSERT(ui_patch::PatchStates(states, error), error);
  const Node *state = Find(states, [](const Node &n) {
    return Is(n, "StateScreenFECore", "screen_name", ui_patch::kScreenName);
  });
  TEST_ASSERT(state != nullptr && *state->Attribute("id") == ui_patch::kStateId,
              "estado novo com o id fixo");

  size_t linked = 0;
  TEST_ASSERT(ui_patch::PatchFlow(flow, linked, error), error);
  TEST_ASSERT(linked == 1, "so o no com link options");
  const Node *pause = Find(flow, [](const Node &n) { return Is(n, "node", "id", "231066979"); });
  const Node *link = pause == nullptr ? nullptr : Find(*pause, [](const Node &n) {
    return Is(n, "link", "id", ui_patch::kPauseEvent);
  });
  TEST_ASSERT(link != nullptr, "link novo no no de pausa");
  const std::string target = link != nullptr ? *link->Attribute("target") : "";
  TEST_ASSERT(!target.empty() && std::stoul(target) < (1ul << 28), "id de no abaixo de 2^28");
  const Node *statePause = Find(flow, [](const Node &n) { return Is(n, "node", "id", "129712173"); });
  const Node *created = statePause == nullptr ? nullptr : Find(*statePause, [&](const Node &n) {
    return Is(n, "node", "id", target.c_str());
  });
  TEST_ASSERT(created != nullptr && Is(*created, "node", "state", ui_patch::kStateId),
              "no novo no mesmo pai do alvo de options");
  TEST_ASSERT(created != nullptr && created->children.size() == 1 &&
                  Is(created->children[0], "link", "target", "231066979") &&
                  Is(created->children[0], "link", "type", "back"),
              "back volta para a pausa");

  TEST_ASSERT(ui_patch::PatchScreens(screens, error), error);
  const Node *own = Find(screens, [](const Node &n) {
    return Is(n, "Screen", "id", ui_patch::kScreenName);
  });
  TEST_ASSERT(own != nullptr && own->children[0].children.size() == 1, "tela nova com um item");
  const Node *pauseItem = Find(screens, [](const Node &n) { return Is(n, "Item", "id", "item_9"); });
  TEST_ASSERT(pauseItem != nullptr &&
                  Find(*pauseItem, [](const Node &n) {
                    return Is(n, "IBSelectableSimple", "select_value", ui_patch::kPauseEvent);
                  }) != nullptr,
              "item 9 dispara o evento do link");
  TEST_ASSERT(pauseItem != nullptr &&
                  Find(*pauseItem, [](const Node &n) {
                    return Is(n, "BTextStatic", "explicit", "true") &&
                           Is(n, "BTextStatic", "string", ui_patch::kLabel);
                  }) != nullptr,
              "rotulo literal");
  TEST_ASSERT(pauseItem != nullptr &&
                  Find(*pauseItem, [](const Node &n) { return n.name == "BVisibilityControlData"; }) == nullptr,
              "item 9 sem condicao de visibilidade");
  const Node *template_ = Find(screens, [](const Node &n) { return Is(n, "Screen", "id", "options_ingame"); });
  TEST_ASSERT(template_ != nullptr && template_->children[0].children.size() == 2,
              "options_ingame intacta");
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
  states.children.push_back(N("StateOther", {{"id", ui_patch::kStateId}}));
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
    if (out != nullptr) {
      const std::vector<uint8_t> patched = dr2hook::bxml::Encode(root);
      std::ofstream(std::string(out) + "/" + name + ".bin", std::ios::binary)
          .write(reinterpret_cast<const char *>(patched.data()),
                 static_cast<std::streamsize>(patched.size()));
    }
  }
  std::cout << "  estado " << ui_patch::kStateId << ", link em " << linked
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
  TestRealFiles();
  std::cout << g_testsRun - g_testsFailed << "/" << g_testsRun << " asserções OK"
            << std::endl;
  return g_testsFailed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
