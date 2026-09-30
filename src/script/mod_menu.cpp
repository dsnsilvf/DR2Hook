#include "dr2hook/script/mod_menu.h"

#include <algorithm>
#include <atomic>
#include <map>

namespace dr2hook {
namespace {

std::map<std::string, std::vector<ModOption>> g_options;
std::string g_currentMod;
// Lido no Present, gravado também por callbacks Lua da thread da WndProc.
std::atomic<bool> g_dirty{true};

} // namespace

std::string ModOption::DisplayLabel() const {
  switch (type) {
  case Type::Toggle:
    return label + (enabled ? ": On" : ": Off");
  case Type::Choice:
    return index < values.size() ? label + ": " + values[index] : label;
  case Type::Button:
    break;
  }
  return label;
}

void ModOption::Advance() {
  if (type == Type::Toggle) {
    enabled = !enabled;
  } else if (type == Type::Choice && !values.empty()) {
    index = (index + 1) % values.size();
  }
}

std::vector<std::string> ModOption::ValueNames() const {
  switch (type) {
  case Type::Toggle:
    return {"Off", "On"};
  case Type::Choice:
    return values;
  case Type::Button:
    break;
  }
  return {};
}

size_t ModOption::ValueIndex() const {
  switch (type) {
  case Type::Toggle:
    return enabled ? 1 : 0;
  case Type::Choice:
    return index;
  case Type::Button:
    break;
  }
  return 0;
}

bool ModOption::SetValueIndex(size_t newIndex) {
  if (newIndex >= ValueNames().size() || newIndex == ValueIndex()) {
    return false;
  }
  if (type == Type::Toggle) {
    enabled = newIndex == 1;
  } else {
    index = newIndex;
  }
  return true;
}

void ModMenu::SetCurrentMod(const std::string &modId) { g_currentMod = modId; }

const std::string &ModMenu::CurrentMod() { return g_currentMod; }

bool ModMenu::Add(const std::string &modId, ModOption option) {
  std::vector<ModOption> &options = g_options[modId];
  const auto same = std::find_if(
      options.begin(), options.end(),
      [&](const ModOption &existing) { return existing.id == option.id; });
  if (same != options.end()) {
    *same = std::move(option);
  } else if (options.size() >= kMaxOptions) {
    return false;
  } else {
    options.push_back(std::move(option));
  }
  g_dirty = true;
  return true;
}

ModOption *ModMenu::Find(const std::string &modId,
                         const std::string &optionId) {
  std::vector<ModOption> *options = OptionsOf(modId);
  if (options == nullptr) {
    return nullptr;
  }
  const auto found = std::find_if(
      options->begin(), options->end(),
      [&](const ModOption &option) { return option.id == optionId; });
  return found != options->end() ? &*found : nullptr;
}

std::vector<ModOption> *ModMenu::OptionsOf(const std::string &modId) {
  const auto found = g_options.find(modId);
  return found != g_options.end() ? &found->second : nullptr;
}

void ModMenu::Clear() {
  g_options.clear();
  g_currentMod.clear();
  g_dirty = true;
}

void ModMenu::MarkDirty() { g_dirty = true; }

bool ModMenu::TakeDirty() {
  return g_dirty.exchange(false);
}

} // namespace dr2hook
