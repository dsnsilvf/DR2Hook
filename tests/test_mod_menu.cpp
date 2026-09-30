#include "dr2hook/script/mod_menu.h"

#include <cstdlib>
#include <iostream>
#include <string>

using dr2hook::ModMenu;
using dr2hook::ModOption;

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

ModOption Toggle(const std::string &id, bool enabled) {
  ModOption option;
  option.type = ModOption::Type::Toggle;
  option.id = id;
  option.label = "Indestructible tyres";
  option.enabled = enabled;
  return option;
}

ModOption Choice() {
  ModOption option;
  option.type = ModOption::Type::Choice;
  option.id = "restore_mode";
  option.label = "Restore mode";
  option.values = {"Normal", "Momentum"};
  return option;
}

void TestLabels() {
  std::cout << "[RUN] TestLabels..." << std::endl;
  TEST_ASSERT(Toggle("t", false).DisplayLabel() == "Indestructible tyres: Off",
              "toggle desligado");
  TEST_ASSERT(Toggle("t", true).DisplayLabel() == "Indestructible tyres: On",
              "toggle ligado");
  TEST_ASSERT(Choice().DisplayLabel() == "Restore mode: Normal", "choice");
  ModOption button;
  button.label = "Save checkpoint";
  TEST_ASSERT(button.DisplayLabel() == "Save checkpoint", "botao so com rotulo");
  ModOption empty = Choice();
  empty.values.clear();
  TEST_ASSERT(empty.DisplayLabel() == "Restore mode", "choice sem valores");
}

void TestAdvance() {
  std::cout << "[RUN] TestAdvance..." << std::endl;
  ModOption toggle = Toggle("t", false);
  toggle.Advance();
  TEST_ASSERT(toggle.enabled, "toggle inverte");
  toggle.Advance();
  TEST_ASSERT(!toggle.enabled, "toggle volta");
  ModOption choice = Choice();
  choice.Advance();
  TEST_ASSERT(choice.DisplayLabel() == "Restore mode: Momentum", "choice avanca");
  choice.Advance();
  TEST_ASSERT(choice.index == 0, "choice volta ao primeiro");
  ModOption button;
  button.Advance();
  TEST_ASSERT(button.DisplayLabel().empty() && !button.enabled, "botao nao muda");
}

void TestComboValues() {
  std::cout << "[RUN] TestComboValues..." << std::endl;
  ModOption toggle = Toggle("t", true);
  TEST_ASSERT(toggle.ValueNames() == std::vector<std::string>({"Off", "On"}),
              "valores do toggle");
  TEST_ASSERT(toggle.ValueIndex() == 1, "toggle ligado no indice 1");
  TEST_ASSERT(toggle.SetValueIndex(0) && !toggle.enabled, "combo desliga o toggle");
  TEST_ASSERT(!toggle.SetValueIndex(0), "mesmo indice nao muda");
  TEST_ASSERT(!toggle.SetValueIndex(2), "indice fora dos valores");

  ModOption choice = Choice();
  TEST_ASSERT(choice.ValueNames() == choice.values, "valores do choice");
  TEST_ASSERT(choice.SetValueIndex(1) && choice.ValueIndex() == 1 &&
                  choice.DisplayLabel() == "Restore mode: Momentum",
              "combo escolhe o valor");
  TEST_ASSERT(!choice.SetValueIndex(5) && choice.index == 1, "choice fora do intervalo");

  ModOption button;
  TEST_ASSERT(button.ValueNames().empty() && button.ValueIndex() == 0 &&
                  !button.SetValueIndex(0),
              "botao sem valores");
}

void TestRegistry() {
  std::cout << "[RUN] TestRegistry..." << std::endl;
  ModMenu::Clear();
  TEST_ASSERT(ModMenu::TakeDirty(), "sujo depois de limpar");
  TEST_ASSERT(!ModMenu::TakeDirty(), "limpo depois de ler");
  TEST_ASSERT(ModMenu::OptionsOf("a") == nullptr, "mod sem opcoes");

  TEST_ASSERT(ModMenu::Add("a", Toggle("tyres", false)), "primeira opcao");
  TEST_ASSERT(ModMenu::TakeDirty(), "sujo depois de acrescentar");
  TEST_ASSERT(ModMenu::Add("a", Choice()), "segunda opcao");
  TEST_ASSERT(ModMenu::Add("a", Toggle("tyres", true)), "mesmo id substitui");
  TEST_ASSERT(ModMenu::OptionsOf("a")->size() == 2, "sem duplicar");
  TEST_ASSERT(ModMenu::OptionsOf("a")->front().enabled, "valor substituido");
  TEST_ASSERT(ModMenu::Find("a", "restore_mode") != nullptr, "acha por id");
  TEST_ASSERT(ModMenu::Find("b", "restore_mode") == nullptr, "opcoes por mod");

  for (size_t i = ModMenu::OptionsOf("a")->size(); i < ModMenu::kMaxOptions; ++i) {
    TEST_ASSERT(ModMenu::Add("a", Toggle("extra_" + std::to_string(i), false)),
                "ate o limite");
  }
  TEST_ASSERT(!ModMenu::Add("a", Toggle("overflow", false)), "recusa alem do limite");
  TEST_ASSERT(ModMenu::Add("a", Toggle("tyres", false)), "substituir no limite");

  ModMenu::SetCurrentMod("a");
  TEST_ASSERT(ModMenu::CurrentMod() == "a", "mod atual");
  ModMenu::Clear();
  TEST_ASSERT(ModMenu::CurrentMod().empty() && ModMenu::OptionsOf("a") == nullptr,
              "limpar zera tudo");
}

} // namespace

int main() {
  std::cout << "DR2Hook - Testes das opcoes de mod da tela nativa" << std::endl;
  TestLabels();
  TestAdvance();
  TestComboValues();
  TestRegistry();
  std::cout << g_testsRun - g_testsFailed << "/" << g_testsRun << " asserções OK"
            << std::endl;
  return g_testsFailed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
