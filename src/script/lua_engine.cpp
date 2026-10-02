#include "dr2hook/script/lua_engine.h"
#include "dr2hook/cutscene_probe.h"
#include "dr2hook/logger.h"
#include "dr2hook/script/mod_manager.h"
#include "dr2hook/script/mod_menu.h"
#include "dr2hook/player.h"
#include "dr2hook/safety.h"
#include "dr2hook/ui/overlay.h"

#include <cstring>

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

namespace dr2hook {

lua_State *LuaEngine::s_L = nullptr;
bool LuaEngine::s_initialized = false;
std::recursive_mutex LuaEngine::s_mutex;

// ---------------------------------------------------------------------------
// Redirecionamento customizado de print(...)
// ---------------------------------------------------------------------------
static int Lua_CustomPrint(lua_State *L) {
  int n = lua_gettop(L);
  std::string msg;
  for (int i = 1; i <= n; ++i) {
    if (i > 1) {
      msg += "\t";
    }
    size_t len = 0;
    const char *s = luaL_tolstring(L, i, &len);
    if (s != nullptr) {
      msg.append(s, len);
    }
    lua_pop(L, 1);
  }
  Logger::Info("[Lua] " + msg);
  return 0;
}

// ---------------------------------------------------------------------------
// Bindings nativos para Player
// ---------------------------------------------------------------------------
static int Lua_Player_getPosition(lua_State *L) {
  CarState state{};
  if (!Player::CaptureState(state)) {
    lua_pushnil(L);
    return 1;
  }
  lua_createtable(L, 0, 3);
  lua_pushnumber(L, state.position.x);
  lua_setfield(L, -2, "x");
  lua_pushnumber(L, state.position.y);
  lua_setfield(L, -2, "y");
  lua_pushnumber(L, state.position.z);
  lua_setfield(L, -2, "z");
  return 1;
}

static int Lua_Player_setPosition(lua_State *L) {
  if (!SafetyGuard::CanWriteState()) {
    Logger::Warn(
        "Player.setPosition: bloqueado pelo SafetyGuard em modo restrito.");
    lua_pushboolean(L, 0);
    return 1;
  }

  if (!lua_istable(L, 1)) {
    Logger::Warn("Player.setPosition: argumento invalido (tabela esperada).");
    lua_pushboolean(L, 0);
    return 1;
  }

  CarState state{};
  Player::CaptureState(state);

  lua_getfield(L, 1, "x");
  if (lua_isnumber(L, -1)) {
    state.position.x = static_cast<float>(lua_tonumber(L, -1));
  } else {
    lua_pop(L, 1);
    lua_rawgeti(L, 1, 1);
    if (lua_isnumber(L, -1)) {
      state.position.x = static_cast<float>(lua_tonumber(L, -1));
    }
  }
  lua_pop(L, 1);

  lua_getfield(L, 1, "y");
  if (lua_isnumber(L, -1)) {
    state.position.y = static_cast<float>(lua_tonumber(L, -1));
  } else {
    lua_pop(L, 1);
    lua_rawgeti(L, 1, 2);
    if (lua_isnumber(L, -1)) {
      state.position.y = static_cast<float>(lua_tonumber(L, -1));
    }
  }
  lua_pop(L, 1);

  lua_getfield(L, 1, "z");
  if (lua_isnumber(L, -1)) {
    state.position.z = static_cast<float>(lua_tonumber(L, -1));
  } else {
    lua_pop(L, 1);
    lua_rawgeti(L, 1, 3);
    if (lua_isnumber(L, -1)) {
      state.position.z = static_cast<float>(lua_tonumber(L, -1));
    }
  }
  lua_pop(L, 1);

  bool ok = Player::ApplyState(state, RestoreMode::WithMomentum);
  lua_pushboolean(L, ok ? 1 : 0);
  return 1;
}

static int Lua_Player_getVelocity(lua_State *L) {
  CarState state{};
  if (!Player::CaptureState(state)) {
    lua_pushnil(L);
    return 1;
  }
  lua_createtable(L, 0, 6);
  lua_pushnumber(L, state.linearVelocity.x);
  lua_setfield(L, -2, "vx");
  lua_pushnumber(L, state.linearVelocity.y);
  lua_setfield(L, -2, "vy");
  lua_pushnumber(L, state.linearVelocity.z);
  lua_setfield(L, -2, "vz");
  lua_pushnumber(L, state.linearVelocity.x);
  lua_setfield(L, -2, "x");
  lua_pushnumber(L, state.linearVelocity.y);
  lua_setfield(L, -2, "y");
  lua_pushnumber(L, state.linearVelocity.z);
  lua_setfield(L, -2, "z");
  return 1;
}

static int Lua_Player_setVelocity(lua_State *L) {
  if (!SafetyGuard::CanWriteState()) {
    Logger::Warn(
        "Player.setVelocity: bloqueado pelo SafetyGuard em modo restrito.");
    lua_pushboolean(L, 0);
    return 1;
  }

  if (!lua_istable(L, 1)) {
    Logger::Warn("Player.setVelocity: argumento invalido (tabela esperada).");
    lua_pushboolean(L, 0);
    return 1;
  }

  CarState state{};
  Player::CaptureState(state);

  lua_getfield(L, 1, "vx");
  if (lua_isnumber(L, -1)) {
    state.linearVelocity.x = static_cast<float>(lua_tonumber(L, -1));
  } else {
    lua_pop(L, 1);
    lua_getfield(L, 1, "x");
    if (lua_isnumber(L, -1)) {
      state.linearVelocity.x = static_cast<float>(lua_tonumber(L, -1));
    } else {
      lua_pop(L, 1);
      lua_rawgeti(L, 1, 1);
      if (lua_isnumber(L, -1)) {
        state.linearVelocity.x = static_cast<float>(lua_tonumber(L, -1));
      }
    }
  }
  lua_pop(L, 1);

  lua_getfield(L, 1, "vy");
  if (lua_isnumber(L, -1)) {
    state.linearVelocity.y = static_cast<float>(lua_tonumber(L, -1));
  } else {
    lua_pop(L, 1);
    lua_getfield(L, 1, "y");
    if (lua_isnumber(L, -1)) {
      state.linearVelocity.y = static_cast<float>(lua_tonumber(L, -1));
    } else {
      lua_pop(L, 1);
      lua_rawgeti(L, 1, 2);
      if (lua_isnumber(L, -1)) {
        state.linearVelocity.y = static_cast<float>(lua_tonumber(L, -1));
      }
    }
  }
  lua_pop(L, 1);

  lua_getfield(L, 1, "vz");
  if (lua_isnumber(L, -1)) {
    state.linearVelocity.z = static_cast<float>(lua_tonumber(L, -1));
  } else {
    lua_pop(L, 1);
    lua_getfield(L, 1, "z");
    if (lua_isnumber(L, -1)) {
      state.linearVelocity.z = static_cast<float>(lua_tonumber(L, -1));
    } else {
      lua_pop(L, 1);
      lua_rawgeti(L, 1, 3);
      if (lua_isnumber(L, -1)) {
        state.linearVelocity.z = static_cast<float>(lua_tonumber(L, -1));
      }
    }
  }
  lua_pop(L, 1);

  bool ok = Player::ApplyState(state, RestoreMode::WithMomentum);
  lua_pushboolean(L, ok ? 1 : 0);
  return 1;
}

static int Lua_Player_getState(lua_State *L) {
  CarState state{};
  if (!Player::CaptureState(state)) {
    lua_pushnil(L);
    return 1;
  }

  lua_createtable(L, 0, 5);

  // position
  lua_createtable(L, 0, 3);
  lua_pushnumber(L, state.position.x);
  lua_setfield(L, -2, "x");
  lua_pushnumber(L, state.position.y);
  lua_setfield(L, -2, "y");
  lua_pushnumber(L, state.position.z);
  lua_setfield(L, -2, "z");
  lua_setfield(L, -2, "position");

  // linearVelocity
  lua_createtable(L, 0, 6);
  lua_pushnumber(L, state.linearVelocity.x);
  lua_setfield(L, -2, "x");
  lua_pushnumber(L, state.linearVelocity.y);
  lua_setfield(L, -2, "y");
  lua_pushnumber(L, state.linearVelocity.z);
  lua_setfield(L, -2, "z");
  lua_pushnumber(L, state.linearVelocity.x);
  lua_setfield(L, -2, "vx");
  lua_pushnumber(L, state.linearVelocity.y);
  lua_setfield(L, -2, "vy");
  lua_pushnumber(L, state.linearVelocity.z);
  lua_setfield(L, -2, "vz");
  lua_setfield(L, -2, "linearVelocity");

  // angularVelocity
  lua_createtable(L, 0, 3);
  lua_pushnumber(L, state.angularVelocity.x);
  lua_setfield(L, -2, "x");
  lua_pushnumber(L, state.angularVelocity.y);
  lua_setfield(L, -2, "y");
  lua_pushnumber(L, state.angularVelocity.z);
  lua_setfield(L, -2, "z");
  lua_setfield(L, -2, "angularVelocity");

  // rotation
  lua_createtable(L, 3, 0);
  for (int r = 0; r < 3; ++r) {
    lua_createtable(L, 3, 0);
    for (int c = 0; c < 3; ++c) {
      lua_pushnumber(L, state.rotationMatrix.m[r][c]);
      lua_rawseti(L, -2, c + 1);
    }
    lua_rawseti(L, -2, r + 1);
  }
  lua_setfield(L, -2, "rotation");

  // wheels
  lua_createtable(L, 4, 0);
  for (int i = 0; i < 4; ++i) {
    lua_createtable(L, 0, 3);
    lua_pushnumber(L, state.wheels[i].suspensionCompression);
    lua_setfield(L, -2, "suspensionCompression");
    lua_pushnumber(L, state.wheels[i].angularVelocity);
    lua_setfield(L, -2, "angularVelocity");
    lua_pushboolean(L, state.wheels[i].inContact ? 1 : 0);
    lua_setfield(L, -2, "inContact");
    lua_rawseti(L, -2, i + 1);
  }
  lua_setfield(L, -2, "wheels");

  return 1;
}

static int Lua_Player_setState(lua_State *L) {
  if (!SafetyGuard::CanWriteState()) {
    Logger::Warn(
        "Player.setState: bloqueado pelo SafetyGuard em modo restrito.");
    lua_pushboolean(L, 0);
    return 1;
  }

  if (!lua_istable(L, 1)) {
    Logger::Warn("Player.setState: argumento invalido (tabela esperada).");
    lua_pushboolean(L, 0);
    return 1;
  }

  CarState state{};
  Player::CaptureState(state);

  // position
  lua_getfield(L, 1, "position");
  if (lua_istable(L, -1)) {
    lua_getfield(L, -1, "x");
    if (lua_isnumber(L, -1)) {
      state.position.x = static_cast<float>(lua_tonumber(L, -1));
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "y");
    if (lua_isnumber(L, -1)) {
      state.position.y = static_cast<float>(lua_tonumber(L, -1));
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "z");
    if (lua_isnumber(L, -1)) {
      state.position.z = static_cast<float>(lua_tonumber(L, -1));
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);

  // linearVelocity
  lua_getfield(L, 1, "linearVelocity");
  if (lua_istable(L, -1)) {
    lua_getfield(L, -1, "x");
    if (lua_isnumber(L, -1)) {
      state.linearVelocity.x = static_cast<float>(lua_tonumber(L, -1));
    } else {
      lua_pop(L, 1);
      lua_getfield(L, -1, "vx");
      if (lua_isnumber(L, -1)) {
        state.linearVelocity.x = static_cast<float>(lua_tonumber(L, -1));
      }
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "y");
    if (lua_isnumber(L, -1)) {
      state.linearVelocity.y = static_cast<float>(lua_tonumber(L, -1));
    } else {
      lua_pop(L, 1);
      lua_getfield(L, -1, "vy");
      if (lua_isnumber(L, -1)) {
        state.linearVelocity.y = static_cast<float>(lua_tonumber(L, -1));
      }
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "z");
    if (lua_isnumber(L, -1)) {
      state.linearVelocity.z = static_cast<float>(lua_tonumber(L, -1));
    } else {
      lua_pop(L, 1);
      lua_getfield(L, -1, "vz");
      if (lua_isnumber(L, -1)) {
        state.linearVelocity.z = static_cast<float>(lua_tonumber(L, -1));
      }
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);

  // angularVelocity
  lua_getfield(L, 1, "angularVelocity");
  if (lua_istable(L, -1)) {
    lua_getfield(L, -1, "x");
    if (lua_isnumber(L, -1)) {
      state.angularVelocity.x = static_cast<float>(lua_tonumber(L, -1));
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "y");
    if (lua_isnumber(L, -1)) {
      state.angularVelocity.y = static_cast<float>(lua_tonumber(L, -1));
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "z");
    if (lua_isnumber(L, -1)) {
      state.angularVelocity.z = static_cast<float>(lua_tonumber(L, -1));
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);

  // rotation
  lua_getfield(L, 1, "rotation");
  if (lua_istable(L, -1)) {
    for (int r = 0; r < 3; ++r) {
      lua_rawgeti(L, -1, r + 1);
      if (lua_istable(L, -1)) {
        for (int c = 0; c < 3; ++c) {
          lua_rawgeti(L, -1, c + 1);
          if (lua_isnumber(L, -1)) {
            state.rotationMatrix.m[r][c] =
                static_cast<float>(lua_tonumber(L, -1));
          }
          lua_pop(L, 1);
        }
      }
      lua_pop(L, 1);
    }
  }
  lua_pop(L, 1);

  // wheels
  lua_getfield(L, 1, "wheels");
  if (lua_istable(L, -1)) {
    for (int i = 0; i < 4; ++i) {
      lua_rawgeti(L, -1, i + 1);
      if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "suspensionCompression");
        if (lua_isnumber(L, -1)) {
          state.wheels[i].suspensionCompression =
              static_cast<float>(lua_tonumber(L, -1));
        }
        lua_pop(L, 1);

        lua_getfield(L, -1, "angularVelocity");
        if (lua_isnumber(L, -1)) {
          state.wheels[i].angularVelocity =
              static_cast<float>(lua_tonumber(L, -1));
        }
        lua_pop(L, 1);

        lua_getfield(L, -1, "inContact");
        if (lua_isboolean(L, -1)) {
          state.wheels[i].inContact = (lua_toboolean(L, -1) != 0);
        }
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
    }
  }
  RestoreMode mode = RestoreMode::Normal;
  if (lua_isstring(L, 2)) {
    const char *m = lua_tostring(L, 2);
    if (std::strcmp(m, "momentum") == 0 ||
        std::strcmp(m, "with_momentum") == 0) {
      mode = RestoreMode::WithMomentum;
    }
  } else if (lua_isboolean(L, 2) && lua_toboolean(L, 2)) {
    mode = RestoreMode::WithMomentum;
  }

  bool ok = Player::ApplyState(state, mode);
  lua_pushboolean(L, ok ? 1 : 0);
  return 1;
}

// ---------------------------------------------------------------------------
// Bindings nativos para Safety
// ---------------------------------------------------------------------------
static int Lua_Safety_isRestrictedMode(lua_State *L) {
  bool restricted = !SafetyGuard::CanWriteState();
  lua_pushboolean(L, restricted ? 1 : 0);
  return 1;
}

// ---------------------------------------------------------------------------
// Bindings nativos para Race (largada; hooks em CutsceneProbe)
// ---------------------------------------------------------------------------
// Race.setStartMode("normal" | "no_countdown" | "automatic" | "on_throttle")
static int Lua_Race_setStartMode(lua_State *L) {
  const char *mode = luaL_checkstring(L, 1);
  if (!CutsceneProbe::SetStartMode(mode)) {
    return luaL_error(L, "Race.setStartMode: modo invalido '%s'", mode);
  }
#if defined(_WIN32)
  lua_pushboolean(L, 1);
#else
  lua_pushboolean(L, 0); // sem jogo, sem hook
#endif
  return 1;
}

// ---------------------------------------------------------------------------
// Bindings nativos para UI
// ---------------------------------------------------------------------------
static int Lua_UI_notify(lua_State *L) {
  const char *text = luaL_optstring(L, 1, "");
  lua_Number duration = luaL_optnumber(L, 2, 3.0);
  Logger::Info("[UI Notify] " + std::string(text));
  OverlayManager::AddNotification(text, static_cast<float>(duration),
                                  ToastType::Info);
  return 0;
}

// ---------------------------------------------------------------------------
// Menu: opções do mod em execução na tela nativa (ModMenu::CurrentMod)
// ---------------------------------------------------------------------------
static int Lua_Menu_add(lua_State *L, ModOption option, int callbackIndex) {
  const std::string &modId = ModMenu::CurrentMod();
  if (modId.empty()) {
    return luaL_error(L, "Menu: no mod is running");
  }
  if (const ModOption *existing = ModMenu::Find(modId, option.id);
      existing != nullptr && existing->callbackRef != LUA_NOREF) {
    luaL_unref(L, LUA_REGISTRYINDEX, existing->callbackRef);
  }
  if (callbackIndex > 0 && lua_isfunction(L, callbackIndex)) {
    lua_pushvalue(L, callbackIndex);
    option.callbackRef = luaL_ref(L, LUA_REGISTRYINDEX);
  }
  const int callbackRef = option.callbackRef;
  if (!ModMenu::Add(modId, std::move(option))) {
    luaL_unref(L, LUA_REGISTRYINDEX, callbackRef);
    return luaL_error(L, "Menu: at most %d options per mod",
                      static_cast<int>(ModMenu::kMaxOptions));
  }
  return 0;
}

// Menu.toggle(id, label, [default], [onChange(enabled)])
static int Lua_Menu_toggle(lua_State *L) {
  ModOption option;
  option.type = ModOption::Type::Toggle;
  option.id = luaL_checkstring(L, 1);
  option.label = luaL_checkstring(L, 2);
  option.enabled = lua_toboolean(L, 3) != 0;
  return Lua_Menu_add(L, std::move(option), 4);
}

// Menu.choice(id, label, {values}, [defaultIndex], [onChange(index, value)])
static int Lua_Menu_choice(lua_State *L) {
  ModOption option;
  option.type = ModOption::Type::Choice;
  option.id = luaL_checkstring(L, 1);
  option.label = luaL_checkstring(L, 2);
  luaL_checktype(L, 3, LUA_TTABLE);
  const lua_Integer count = static_cast<lua_Integer>(lua_rawlen(L, 3));
  for (lua_Integer i = 1; i <= count; ++i) {
    lua_rawgeti(L, 3, i);
    const char *value = lua_tostring(L, -1);
    option.values.emplace_back(value != nullptr ? value : "");
    lua_pop(L, 1);
  }
  if (option.values.empty()) {
    return luaL_error(L, "Menu.choice: values must not be empty");
  }
  const lua_Integer initial = luaL_optinteger(L, 4, 1);
  option.index = initial >= 1 && initial <= count
                     ? static_cast<size_t>(initial - 1)
                     : 0;
  return Lua_Menu_add(L, std::move(option), 5);
}

// Menu.button(id, label, onClick())
static int Lua_Menu_button(lua_State *L) {
  ModOption option;
  option.type = ModOption::Type::Button;
  option.id = luaL_checkstring(L, 1);
  option.label = luaL_checkstring(L, 2);
  luaL_checktype(L, 3, LUA_TFUNCTION);
  return Lua_Menu_add(L, std::move(option), 3);
}

// Menu.get(id): toggle -> boolean; choice -> index, value; senão nil.
static int Lua_Menu_get(lua_State *L) {
  const ModOption *option =
      ModMenu::Find(ModMenu::CurrentMod(), luaL_checkstring(L, 1));
  if (option == nullptr || option->type == ModOption::Type::Button) {
    lua_pushnil(L);
    return 1;
  }
  if (option->type == ModOption::Type::Toggle) {
    lua_pushboolean(L, option->enabled ? 1 : 0);
    return 1;
  }
  lua_pushinteger(L, static_cast<lua_Integer>(option->index + 1));
  lua_pushstring(L, option->index < option->values.size()
                        ? option->values[option->index].c_str()
                        : "");
  return 2;
}

// Menu.set(id, value): toggle recebe boolean; choice recebe o índice. Não
// chama o callback.
static int Lua_Menu_set(lua_State *L) {
  ModOption *option =
      ModMenu::Find(ModMenu::CurrentMod(), luaL_checkstring(L, 1));
  if (option == nullptr) {
    return luaL_error(L, "Menu.set: unknown option");
  }
  if (option->type == ModOption::Type::Toggle) {
    option->enabled = lua_toboolean(L, 2) != 0;
  } else if (option->type == ModOption::Type::Choice) {
    const lua_Integer index = luaL_checkinteger(L, 2);
    if (index < 1 || index > static_cast<lua_Integer>(option->values.size())) {
      return luaL_error(L, "Menu.set: index out of range");
    }
    option->index = static_cast<size_t>(index - 1);
  }
  ModMenu::MarkDirty();
  ModManager::SaveModSettings(ModMenu::CurrentMod());
  return 0;
}

// Menu.describe(id, text): descrição da opção no painel da direita da tela
// nativa. Sem ela, o painel mostra o nome da opção e a descrição do mod.
static int Lua_Menu_describe(lua_State *L) {
  ModOption *option =
      ModMenu::Find(ModMenu::CurrentMod(), luaL_checkstring(L, 1));
  if (option == nullptr) {
    return luaL_error(L, "Menu.describe: unknown option");
  }
  // O texto do jogo desenha um \n no começo de linha como caractere
  // desconhecido: linha vazia vira um espaço. \r sai por precaução.
  std::string text;
  for (const char *c = luaL_optstring(L, 2, ""); *c != '\0'; ++c) {
    if (*c == '\r') {
      continue;
    }
    if (*c == '\n' && !text.empty() && text.back() == '\n') {
      text += ' ';
    }
    text += *c;
  }
  option->description = std::move(text);
  ModMenu::MarkDirty();
  return 0;
}

// ---------------------------------------------------------------------------
// LuaEngine Implementation
// ---------------------------------------------------------------------------
bool LuaEngine::Initialize() {
  std::lock_guard<std::recursive_mutex> lock(s_mutex);
  if (s_initialized) {
    return true;
  }

  s_L = luaL_newstate();
  if (s_L == nullptr) {
    Logger::Error("LuaEngine::Initialize: falha ao alocar lua_State.");
    return false;
  }

  LoadSafeLibraries();
  RegisterBindings();

  s_initialized = true;
  Logger::Info("LuaEngine inicializado com sucesso.");
  return true;
}

void LuaEngine::Shutdown() {
  std::lock_guard<std::recursive_mutex> lock(s_mutex);
  if (s_L != nullptr) {
    lua_close(s_L);
    s_L = nullptr;
  }
  s_initialized = false;
  Logger::Info("LuaEngine encerrado com sucesso.");
}

bool LuaEngine::IsInitialized() { return s_initialized; }

lua_State *LuaEngine::GetState() { return s_L; }

std::recursive_mutex &LuaEngine::GetMutex() { return s_mutex; }

void LuaEngine::LoadSafeLibraries() {
  if (s_L == nullptr) {
    return;
  }

  static const luaL_Reg safe_libs[] = {{"_G", luaopen_base},
                                       {LUA_LOADLIBNAME, luaopen_package},
                                       {LUA_TABLIBNAME, luaopen_table},
                                       {LUA_STRLIBNAME, luaopen_string},
                                       {LUA_MATHLIBNAME, luaopen_math},
                                       {nullptr, nullptr}};

  for (const luaL_Reg *lib = safe_libs; lib->func != nullptr; ++lib) {
    luaL_requiref(s_L, lib->name, lib->func, 1);
    lua_pop(s_L, 1);
  }
}

void LuaEngine::RegisterBindings() {
  if (s_L == nullptr) {
    return;
  }

  // Redirecionamento customizado de print(...)
  lua_pushcfunction(s_L, Lua_CustomPrint);
  lua_setglobal(s_L, "print");

  // Tabela Player
  lua_createtable(s_L, 0, 6);
  lua_pushcfunction(s_L, Lua_Player_getPosition);
  lua_setfield(s_L, -2, "getPosition");
  lua_pushcfunction(s_L, Lua_Player_setPosition);
  lua_setfield(s_L, -2, "setPosition");
  lua_pushcfunction(s_L, Lua_Player_getVelocity);
  lua_setfield(s_L, -2, "getVelocity");
  lua_pushcfunction(s_L, Lua_Player_setVelocity);
  lua_setfield(s_L, -2, "setVelocity");
  lua_pushcfunction(s_L, Lua_Player_getState);
  lua_setfield(s_L, -2, "getState");
  lua_pushcfunction(s_L, Lua_Player_setState);
  lua_setfield(s_L, -2, "setState");
  lua_setglobal(s_L, "Player");

  // Tabela Safety
  lua_createtable(s_L, 0, 1);
  lua_pushcfunction(s_L, Lua_Safety_isRestrictedMode);
  lua_setfield(s_L, -2, "isRestrictedMode");
  lua_setglobal(s_L, "Safety");

  // Tabela Race
  lua_createtable(s_L, 0, 1);
  lua_pushcfunction(s_L, Lua_Race_setStartMode);
  lua_setfield(s_L, -2, "setStartMode");
  lua_setglobal(s_L, "Race");

  // Tabela UI
  lua_createtable(s_L, 0, 1);
  lua_pushcfunction(s_L, Lua_UI_notify);
  lua_setfield(s_L, -2, "notify");
  lua_setglobal(s_L, "UI");

  // Tabela Menu
  lua_createtable(s_L, 0, 6);
  lua_pushcfunction(s_L, Lua_Menu_toggle);
  lua_setfield(s_L, -2, "toggle");
  lua_pushcfunction(s_L, Lua_Menu_choice);
  lua_setfield(s_L, -2, "choice");
  lua_pushcfunction(s_L, Lua_Menu_button);
  lua_setfield(s_L, -2, "button");
  lua_pushcfunction(s_L, Lua_Menu_get);
  lua_setfield(s_L, -2, "get");
  lua_pushcfunction(s_L, Lua_Menu_set);
  lua_setfield(s_L, -2, "set");
  lua_pushcfunction(s_L, Lua_Menu_describe);
  lua_setfield(s_L, -2, "describe");
  lua_setglobal(s_L, "Menu");
}

int LuaEngine::TracebackHandler(lua_State *L) {
  const char *msg = lua_tostring(L, 1);
  if (msg == nullptr) {
    if (luaL_callmeta(L, 1, "__tostring") && lua_type(L, -1) == LUA_TSTRING) {
      msg = lua_tostring(L, -1);
    } else {
      msg = "(error object is not a string)";
    }
  }
  luaL_traceback(L, L, msg, 1);
  return 1;
}

bool LuaEngine::CallFunctionRef(int funcRef, int nargs, int nresults) {
  std::lock_guard<std::recursive_mutex> lock(s_mutex);
  if (s_L == nullptr || !s_initialized) {
    Logger::Error("LuaEngine::CallFunctionRef: engine nao inicializada.");
    return false;
  }

  if (funcRef == LUA_NOREF || funcRef == LUA_REFNIL) {
    if (nargs > 0) {
      lua_pop(s_L, nargs);
    }
    return false;
  }

  int top = lua_gettop(s_L);
  int funcIdx = top - nargs + 1;

  lua_pushcfunction(s_L, TracebackHandler);
  lua_insert(s_L, funcIdx);
  int errFuncIdx = funcIdx;

  lua_rawgeti(s_L, LUA_REGISTRYINDEX, funcRef);
  lua_insert(s_L, funcIdx + 1);

  int status = lua_pcall(s_L, nargs, nresults, errFuncIdx);
  if (status != LUA_OK) {
    const char *err = lua_tostring(s_L, -1);
    Logger::Error(err ? err : "Unknown Lua error");
    lua_pop(s_L, 2); // pop error message and errFunc
    return false;
  }

  lua_remove(s_L, errFuncIdx);
  return true;
}

bool LuaEngine::ExecuteString(const std::string &code) {
  std::lock_guard<std::recursive_mutex> lock(s_mutex);
  if (s_L == nullptr || !s_initialized) {
    Logger::Error("LuaEngine::ExecuteString: engine nao inicializada.");
    return false;
  }

  int errHandler = lua_gettop(s_L) + 1;
  lua_pushcfunction(s_L, TracebackHandler);

  int loadRes = luaL_loadstring(s_L, code.c_str());
  if (loadRes != LUA_OK) {
    const char *err = lua_tostring(s_L, -1);
    Logger::Error("[Lua Syntax Error] " + std::string(err ? err : ""));
    lua_pop(s_L, 2);
    return false;
  }

  int pcallRes = lua_pcall(s_L, 0, 0, errHandler);
  if (pcallRes != LUA_OK) {
    const char *err = lua_tostring(s_L, -1);
    Logger::Error("[Lua Runtime Error] " + std::string(err ? err : ""));
    lua_pop(s_L, 2);
    return false;
  }

  lua_pop(s_L, 1); // remove errHandler
  return true;
}

bool LuaEngine::ExecuteFile(const std::string &filePath) {
  std::lock_guard<std::recursive_mutex> lock(s_mutex);
  if (s_L == nullptr || !s_initialized) {
    Logger::Error("LuaEngine::ExecuteFile: engine nao inicializada.");
    return false;
  }

  int errHandler = lua_gettop(s_L) + 1;
  lua_pushcfunction(s_L, TracebackHandler);

  int loadRes = luaL_loadfile(s_L, filePath.c_str());
  if (loadRes != LUA_OK) {
    const char *err = lua_tostring(s_L, -1);
    Logger::Error("[Lua Load Error] " + std::string(err ? err : ""));
    lua_pop(s_L, 2);
    return false;
  }

  int pcallRes = lua_pcall(s_L, 0, 0, errHandler);
  if (pcallRes != LUA_OK) {
    const char *err = lua_tostring(s_L, -1);
    Logger::Error("[Lua Runtime Error] " + std::string(err ? err : ""));
    lua_pop(s_L, 2);
    return false;
  }

  lua_pop(s_L, 1); // remove errHandler
  return true;
}

} // namespace dr2hook
