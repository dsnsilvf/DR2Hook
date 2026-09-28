#pragma once

#include "dr2hook/common.h"
#include <mutex>
#include <string>
#include <string_view>

struct lua_State;

namespace dr2hook {

class LuaEngine {
public:
  static bool Initialize();
  static void Shutdown();
  static bool IsInitialized();

  static lua_State *GetState();
  static std::recursive_mutex &GetMutex();
  static bool ExecuteString(const std::string &code);
  static bool ExecuteFile(const std::string &filePath);

  static bool CallFunctionRef(int funcRef, int nargs = 0, int nresults = 0);

private:
  static void LoadSafeLibraries();
  static void RegisterBindings();
  static int TracebackHandler(lua_State *L);

  static lua_State *s_L;
  static bool s_initialized;
  static std::recursive_mutex s_mutex;
};

} // namespace dr2hook

using dr2hook::LuaEngine;
