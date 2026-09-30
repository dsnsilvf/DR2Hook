#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace dr2hook {

// Opção que um mod declara para a tela nativa (Menu.toggle, Menu.choice e
// Menu.button no Lua). Toggle e choice viram um combo "< valor >".
struct ModOption {
  enum class Type { Toggle, Choice, Button };

  std::string id;
  std::string label;
  Type type = Type::Button;
  bool enabled = false;
  size_t index = 0;
  std::vector<std::string> values;
  int callbackRef = -2; // LUA_NOREF

  // "Indestructible tyres: Off", "Restore mode: Momentum" ou só o rótulo.
  std::string DisplayLabel() const;
  // Toggle inverte; choice avança e volta ao primeiro depois do último.
  void Advance();
  // Valores do combo: {"Off", "On"}, os valores do choice, ou vazio.
  std::vector<std::string> ValueNames() const;
  size_t ValueIndex() const;
  // Falso se o índice está fora dos valores ou não muda nada.
  bool SetValueIndex(size_t newIndex);
};

// Opções por mod, na ordem de declaração. Vive no core e é limpo no reload dos
// mods. Acesso sob o mutex do LuaEngine.
class ModMenu {
public:
  // Linhas da tela nativa de mod (ui_patch::kListSlots).
  static constexpr size_t kMaxOptions = 24;

  static void SetCurrentMod(const std::string &modId);
  static const std::string &CurrentMod();
  // Mesmo id substitui a opção. Falso quando o mod já tem kMaxOptions.
  static bool Add(const std::string &modId, ModOption option);
  static ModOption *Find(const std::string &modId, const std::string &optionId);
  static std::vector<ModOption> *OptionsOf(const std::string &modId);
  static void Clear();
  static void MarkDirty();
  static bool TakeDirty();
};

} // namespace dr2hook
