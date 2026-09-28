#pragma once

#include "dr2hook/common.h"
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
  int refOnStageStart = LUA_NOREF;
  int refOnRenderUI = LUA_NOREF;
};

class ModManager {
public:
  static bool Initialize(const std::string &modsDirectory = "mods");
  static void Shutdown();
  static void ReloadMods(const std::string &modsDirectory = "mods");
  static void DispatchTick(double deltaTime);
  static void DispatchKeyDown(UINT vkCode);
  static void DispatchStageStart(const std::string &stageName);
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
