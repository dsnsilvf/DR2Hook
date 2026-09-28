#include "dr2hook/script/mod_manager.h"
#include "dr2hook/logger.h"
#include "dr2hook/script/lua_engine.h"

#include "lauxlib.h"
#include "lua.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>

namespace dr2hook {

std::vector<ModInstance> ModManager::s_mods;
bool ModManager::s_initialized = false;
std::string ModManager::s_lastModsDirectory = "mods";

// ---------------------------------------------------------------------------
// Helper simples para extrair strings de JSON sem dependencias externas
// ---------------------------------------------------------------------------
static std::string ExtractJsonString(const std::string &json,
                                     const std::string &key) {
  std::string pattern = "\"" + key + "\"";
  size_t pos = json.find(pattern);
  if (pos == std::string::npos) {
    return "";
  }
  pos += pattern.length();
  pos = json.find(':', pos);
  if (pos == std::string::npos) {
    return "";
  }
  pos = json.find('"', pos + 1);
  if (pos == std::string::npos) {
    return "";
  }
  size_t start = pos + 1;
  std::string value;
  for (size_t i = start; i < json.length(); ++i) {
    if (json[i] == '\\' && i + 1 < json.length()) {
      value += json[i + 1];
      ++i;
    } else if (json[i] == '"') {
      return value;
    } else {
      value += json[i];
    }
  }
  return value;
}

// ---------------------------------------------------------------------------
// ModManager Implementation
// ---------------------------------------------------------------------------
bool ModManager::Initialize(const std::string &modsDirectory) {
  if (s_initialized) {
    Shutdown();
  }

  s_lastModsDirectory = modsDirectory;

  if (!LuaEngine::Initialize()) {
    Logger::Error("ModManager::Initialize: falha ao inicializar LuaEngine.");
    return false;
  }

  s_initialized = true;

  std::error_code ec;
  if (!std::filesystem::exists(modsDirectory, ec)) {
    Logger::Warn("ModManager: diretorio de mods '" + modsDirectory +
                 "' nao encontrado.");
    return true;
  }

  std::vector<std::filesystem::path> subdirs;
  for (const auto &entry :
       std::filesystem::directory_iterator(modsDirectory, ec)) {
    if (entry.is_directory(ec)) {
      subdirs.push_back(entry.path());
    }
  }

  std::sort(subdirs.begin(), subdirs.end());

  for (const auto &dir : subdirs) {
    LoadModFromDirectory(dir.string());
  }

  return true;
}

void ModManager::Shutdown() {
  if (!s_initialized) {
    return;
  }

  lua_State *L = LuaEngine::GetState();
  if (L != nullptr) {
    for (auto &mod : s_mods) {
      if (mod.refOnInit != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, mod.refOnInit);
        mod.refOnInit = LUA_NOREF;
      }
      if (mod.refOnTick != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, mod.refOnTick);
        mod.refOnTick = LUA_NOREF;
      }
      if (mod.refOnKeyDown != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, mod.refOnKeyDown);
        mod.refOnKeyDown = LUA_NOREF;
      }
      if (mod.refOnStageStart != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, mod.refOnStageStart);
        mod.refOnStageStart = LUA_NOREF;
      }
      if (mod.refOnRenderUI != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, mod.refOnRenderUI);
        mod.refOnRenderUI = LUA_NOREF;
      }
      mod.enabled = false;
    }
  }

  s_mods.clear();
  LuaEngine::Shutdown();
  s_initialized = false;
  Logger::Info("ModManager encerrado com sucesso.");
}

void ModManager::ReloadMods(const std::string &modsDirectory) {
  std::string dir = modsDirectory.empty() ? s_lastModsDirectory : modsDirectory;
  Shutdown();
  Initialize(dir);
  Logger::Info("ModManager: todos os mods foram recarregados com sucesso a partir de: " + dir);
}

bool ModManager::LoadModFromDirectory(const std::string &modDirPath) {
  std::filesystem::path dirPath(modDirPath);
  std::filesystem::path manifestPath = dirPath / "mod.json";

  std::error_code ec;
  if (!std::filesystem::exists(manifestPath, ec)) {
    return false;
  }

  std::ifstream manifestFile(manifestPath);
  if (!manifestFile.is_open()) {
    Logger::Error("ModManager: falha ao abrir manifesto: " +
                  manifestPath.string());
    return false;
  }

  std::stringstream buffer;
  buffer << manifestFile.rdbuf();
  std::string jsonContent = buffer.str();

  std::string id = ExtractJsonString(jsonContent, "id");
  if (id.empty()) {
    id = dirPath.filename().string();
  }

  std::string name = ExtractJsonString(jsonContent, "name");
  if (name.empty()) {
    name = id;
  }

  std::string version = ExtractJsonString(jsonContent, "version");
  if (version.empty()) {
    version = "1.0.0";
  }

  std::string author = ExtractJsonString(jsonContent, "author");
  if (author.empty()) {
    author = "Desconhecido";
  }

  std::string description = ExtractJsonString(jsonContent, "description");
  if (description.empty()) {
    description = "Sem descrição fornecida.";
  }

  std::string mainFile = ExtractJsonString(jsonContent, "main");
  if (mainFile.empty()) {
    mainFile = "main.lua";
  }

  std::filesystem::path mainScriptPath = dirPath / mainFile;
  if (!std::filesystem::exists(mainScriptPath, ec)) {
    Logger::Error("ModManager: script principal '" + mainScriptPath.string() +
                  "' nao encontrado para o mod: " + id);
    return false;
  }

  ModInstance mod;
  mod.id = id;
  mod.name = name;
  mod.version = version;
  mod.author = author;
  mod.description = description;
  mod.directoryPath = dirPath.string();
  mod.mainScriptPath = mainScriptPath.string();
  mod.enabled = true;
  mod.refOnInit = LUA_NOREF;
  mod.refOnTick = LUA_NOREF;
  mod.refOnKeyDown = LUA_NOREF;
  mod.refOnStageStart = LUA_NOREF;
  mod.refOnRenderUI = LUA_NOREF;

  lua_State *L = LuaEngine::GetState();
  if (L == nullptr) {
    Logger::Error("ModManager: lua_State nulo ao carregar mod: " + id);
    return false;
  }

  // Carregamento protegido do script
  int errHandler = lua_gettop(L) + 1;
  lua_pushcfunction(L, [](lua_State *state) -> int {
    const char *msg = lua_tostring(state, 1);
    luaL_traceback(state, state, msg, 1);
    return 1;
  });

  int loadRes = luaL_loadfile(L, mainScriptPath.string().c_str());
  if (loadRes != LUA_OK) {
    const char *err = lua_tostring(L, -1);
    Logger::Error("[Mod: " + mod.id + "] Erro de sintaxe: " + (err ? err : ""));
    lua_pop(L, 2); // pop erro e errHandler
    mod.enabled = false;
    s_mods.push_back(mod);
    return false;
  }

  int pcallRes = lua_pcall(L, 0, 0, errHandler);
  if (pcallRes != LUA_OK) {
    const char *err = lua_tostring(L, -1);
    Logger::Error("[Mod: " + mod.id +
                  "] Erro ao carregar script: " + (err ? err : ""));
    lua_pop(L, 2); // pop erro e errHandler
    mod.enabled = false;
    s_mods.push_back(mod);
    return false;
  }

  lua_pop(L, 1); // pop errHandler

  // Cache das funcoes no Lua Registry (Zero String Lookup em Frame)
  auto cacheCallback = [&](const char *funcName) -> int {
    lua_getglobal(L, funcName);
    int ref = LUA_NOREF;
    if (lua_isfunction(L, -1)) {
      ref = luaL_ref(L, LUA_REGISTRYINDEX);
    } else {
      lua_pop(L, 1);
    }
    lua_pushnil(L);
    lua_setglobal(L, funcName);
    return ref;
  };

  mod.refOnInit = cacheCallback("onInit");
  mod.refOnTick = cacheCallback("onTick");
  mod.refOnKeyDown = cacheCallback("onKeyDown");
  mod.refOnStageStart = cacheCallback("onStageStart");
  mod.refOnRenderUI = cacheCallback("onRenderUI");

  // Invocacao de ciclo de vida onInit
  if (mod.enabled && mod.refOnInit != LUA_NOREF) {
    CallModCallback(mod, mod.refOnInit, 0, 0);
  }

  s_mods.push_back(mod);
  Logger::Info("Mod carregado com sucesso: " + mod.name + " (" + mod.id +
               ") v" + mod.version);
  return true;
}

void ModManager::CallModCallback(ModInstance &mod, int funcRef, int nargs,
                                 int nresults) {
  if (!mod.enabled || funcRef == LUA_NOREF) {
    if (nargs > 0) {
      lua_pop(LuaEngine::GetState(), nargs);
    }
    return;
  }

  bool success = LuaEngine::CallFunctionRef(funcRef, nargs, nresults);
  if (!success) {
    mod.enabled = false;
    Logger::Error("[ModManager] Mod '" + mod.id +
                  "' desativado permanentemente para o resto da sessao devido "
                  "a erro de execucao.");
  }
}

void ModManager::DispatchTick(double deltaTime) {
  if (!s_initialized) {
    return;
  }

  std::lock_guard<std::recursive_mutex> lock(LuaEngine::GetMutex());

  lua_State *L = LuaEngine::GetState();
  if (L == nullptr) {
    return;
  }

  for (auto &mod : s_mods) {
    if (mod.enabled && mod.refOnTick != LUA_NOREF) {
      lua_pushnumber(L, deltaTime);
      CallModCallback(mod, mod.refOnTick, 1, 0);
    }
  }
}

void ModManager::DispatchKeyDown(UINT vkCode) {
  if (!s_initialized) {
    return;
  }

  std::lock_guard<std::recursive_mutex> lock(LuaEngine::GetMutex());

  lua_State *L = LuaEngine::GetState();
  if (L == nullptr) {
    return;
  }

  for (auto &mod : s_mods) {
    if (mod.enabled && mod.refOnKeyDown != LUA_NOREF) {
      lua_pushinteger(L, static_cast<lua_Integer>(vkCode));
      CallModCallback(mod, mod.refOnKeyDown, 1, 0);
    }
  }
}

void ModManager::DispatchStageStart(const std::string &stageName) {
  if (!s_initialized) {
    return;
  }

  std::lock_guard<std::recursive_mutex> lock(LuaEngine::GetMutex());

  lua_State *L = LuaEngine::GetState();
  if (L == nullptr) {
    return;
  }

  for (auto &mod : s_mods) {
    if (mod.enabled && mod.refOnStageStart != LUA_NOREF) {
      lua_createtable(L, 0, 1);
      lua_pushstring(L, stageName.c_str());
      lua_setfield(L, -2, "name");
      CallModCallback(mod, mod.refOnStageStart, 1, 0);
    }
  }
}

void ModManager::DispatchRenderUI(ModInstance &mod) {
  if (!s_initialized || !mod.enabled || mod.refOnRenderUI == LUA_NOREF) {
    return;
  }

  std::lock_guard<std::recursive_mutex> lock(LuaEngine::GetMutex());
  CallModCallback(mod, mod.refOnRenderUI, 0, 0);
}

const std::vector<ModInstance> &ModManager::GetLoadedMods() { return s_mods; }

} // namespace dr2hook
