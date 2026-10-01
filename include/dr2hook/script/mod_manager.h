#pragma once

#include "dr2hook/common.h"
#include "dr2hook/script/mod_menu.h"
#include <string>
#include <vector>

#ifndef LUA_NOREF
#define LUA_NOREF (-2)
#endif

namespace dr2hook {

struct ModInstance {
  std::string id;
  std::string name;
  std::string version;
  std::string author;
  std::string description;
  std::string mainScriptPath;
  std::string directoryPath;
  bool enabled = true;
  int refOnInit = LUA_NOREF;
  int refOnTick = LUA_NOREF;
  int refOnKeyDown = LUA_NOREF;
  int refOnStageLoad = LUA_NOREF;
  int refOnCountdown = LUA_NOREF;
  int refOnStageStart = LUA_NOREF;
  int refOnRenderUI = LUA_NOREF;
};

// Um mod como a tela nativa mostra: nome, descrição e cópia das opções.
struct ModMenuEntry {
  std::string name;
  std::string description;
  std::vector<ModOption> options;
};

class ModManager {
public:
  // Opção `optionIndex` do mod `modIndex` (ordem de GetLoadedMods) na tela
  // nativa. `value` < 0 é a linha selecionada: o botão chama o callback, e
  // toggle e choice avançam. `value` >= 0 é o índice novo do combo. O callback
  // Lua só é chamado quando algo muda ou num botão.
  static void DispatchMenuEvent(size_t modIndex, size_t optionIndex, int value = -1);
  static std::vector<ModMenuEntry> MenuSnapshot();
  static bool Initialize(const std::string &modsDirectory = "mods");
  static void Shutdown();
  static void ReloadMods(const std::string &modsDirectory = "mods");
  static void DispatchTick(double deltaTime);
  static void DispatchKeyDown(UINT vkCode);
  // Ciclo de vida da especial (eventos do jogo via proxy).
  static void DispatchStageLoad(const std::string &stageName);
  static void DispatchCountdown(int light);
  static void DispatchStageStart(const std::string &stageName,
                                 bool restart = false);
  static void DispatchRenderUI(ModInstance &mod);
  static const std::vector<ModInstance> &GetLoadedMods();

private:
  static bool LoadModFromDirectory(const std::string &modDirPath);
  static void CallModCallback(ModInstance &mod, int funcRef, int nargs = 0,
                              int nresults = 0);

  static std::vector<ModInstance> s_mods;
  static bool s_initialized;
  static std::string s_lastModsDirectory;
};

} // namespace dr2hook

using dr2hook::ModInstance;
using dr2hook::ModManager;
