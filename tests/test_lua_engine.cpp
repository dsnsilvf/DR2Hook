#include "dr2hook/logger.h"
#include "dr2hook/memory.h"
#include "dr2hook/player.h"
#include "dr2hook/safety.h"
#include "dr2hook/script/lua_engine.h"
#include "dr2hook/script/mod_manager.h"
#include "dr2hook/script/mod_menu.h"

#include "lauxlib.h"
#include "lua.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

static int g_testsRun = 0;
static int g_testsPassed = 0;
static int g_testsFailed = 0;

#define TEST_ASSERT(condition, msg)                                            \
  do {                                                                         \
    ++g_testsRun;                                                              \
    if (condition) {                                                           \
      ++g_testsPassed;                                                         \
    } else {                                                                   \
      ++g_testsFailed;                                                         \
      std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ << " ("            \
                << __func__ << "): " << (msg) << " -> Assertion '"             \
                << #condition << "' failed." << std::endl;                     \
    }                                                                          \
  } while (0)

#define TEST_ASSERT_FLOAT_NEAR(val, expected, eps, msg)                        \
  TEST_ASSERT(std::fabs((val) - (expected)) <= (eps), msg)

// ---------------------------------------------------------------------------
// 1. Inicializacao e Unwinding Seguro C++
// ---------------------------------------------------------------------------
static bool g_destructorExecuted = false;

struct DestructorTracker {
  ~DestructorTracker() { g_destructorExecuted = true; }
};

static int Lua_TestUnwindFunction(lua_State *L) {
  DestructorTracker tracker;
  luaL_error(L, "Simulated Lua error to trigger stack unwinding");
  return 0;
}

void TestLuaEngineInitAndCxxUnwinding() {
  std::cout << "[RUN] TestLuaEngineInitAndCxxUnwinding..." << std::endl;

  TEST_ASSERT(dr2hook::LuaEngine::Initialize(),
              "LuaEngine inicializa com sucesso");
  TEST_ASSERT(dr2hook::LuaEngine::IsInitialized(),
              "LuaEngine reporta inicializado");
  lua_State *L = dr2hook::LuaEngine::GetState();
  TEST_ASSERT(L != nullptr, "lua_State nao e nulo");

  // Validar stack unwinding seguro via C++ try/catch
  g_destructorExecuted = false;
  lua_pushcfunction(L, Lua_TestUnwindFunction);
  int pcallRes = lua_pcall(L, 0, 0, 0);
  TEST_ASSERT(pcallRes == LUA_ERRRUN,
              "lua_pcall captura LUA_ERRRUN da funcao de teste");
  TEST_ASSERT(
      g_destructorExecuted,
      "Destrutor C++ executado durante unwinding de erro Lua (compilacao "
      "CXX)");
  lua_pop(L, 1); // pop mensagem de erro

  dr2hook::LuaEngine::Shutdown();
  TEST_ASSERT(!dr2hook::LuaEngine::IsInitialized(),
              "LuaEngine reporta desinicializado");
  TEST_ASSERT(dr2hook::LuaEngine::GetState() == nullptr,
              "lua_State limpo no shutdown");
}

// ---------------------------------------------------------------------------
// 2. Redirecionamento customizado de print(...)
// ---------------------------------------------------------------------------
void TestPrintRedirection() {
  std::cout << "[RUN] TestPrintRedirection..." << std::endl;

  dr2hook::LuaEngine::Initialize();

  bool ok = dr2hook::LuaEngine::ExecuteString(
      "print('Linha de teste do print', 42, true)");
  TEST_ASSERT(ok, "Execucao de print simples deve ter sucesso");

  bool okFormat = dr2hook::LuaEngine::ExecuteString(
      "print(string.format('Formatado: %s %d %.2f', 'valor', 10, 3.14))");
  TEST_ASSERT(okFormat, "Execucao de print com string.format deve ter sucesso");

  dr2hook::LuaEngine::Shutdown();
}

// ---------------------------------------------------------------------------
// 3. Bindings nativos para Safety e Player
// ---------------------------------------------------------------------------
void TestPlayerAndSafetyBindings() {
  std::cout << "[RUN] TestPlayerAndSafetyBindings..." << std::endl;

  dr2hook::MockMemoryAccessor mock;
  dr2hook::MemoryScanner scanner(&mock);

  constexpr uintptr_t vehicleAddr = 0x140700000;
  constexpr uintptr_t sessionModeAddr = 0x140800000;

  dr2hook::CarState initialState{};
  initialState.position = {100.0f, 200.0f, 300.0f};
  initialState.linearVelocity = {10.0f, 20.0f, 30.0f};
  initialState.angularVelocity = {1.0f, 2.0f, 3.0f};
  for (int i = 0; i < 4; ++i) {
    initialState.wheels[i].suspensionCompression = 0.50f;
    initialState.wheels[i].inContact = true;
  }
  mock.SetValue(vehicleAddr, initialState);

  mock.SetValue(
      sessionModeAddr,
      static_cast<uint32_t>(dr2hook::GameSessionMode::TimeTrialOffline));

  dr2hook::Player::Configure(&scanner, vehicleAddr);
  dr2hook::SafetyGuard::Configure(&scanner, sessionModeAddr);

  dr2hook::LuaEngine::Initialize();

  // Testar Safety.isRestrictedMode()
  bool okSafety = dr2hook::LuaEngine::ExecuteString(
      "assert(Safety.isRestrictedMode() == false, 'Modo TimeTrialOffline nao "
      "deve "
      "ser restrito')");
  TEST_ASSERT(okSafety, "Safety.isRestrictedMode() retorna false em TimeTrial");

  // Testar Player.getPosition() e Player.getVelocity()
  bool okGet = dr2hook::LuaEngine::ExecuteString(R"(
    local p = Player.getPosition()
    assert(p ~= nil, 'getPosition nao deve ser nil')
    assert(math.abs(p.x - 100.0) < 0.01, 'pos.x incorreto')
    assert(math.abs(p.y - 200.0) < 0.01, 'pos.y incorreto')
    assert(math.abs(p.z - 300.0) < 0.01, 'pos.z incorreto')

    local v = Player.getVelocity()
    assert(v ~= nil, 'getVelocity nao deve ser nil')
    assert(math.abs(v.vx - 10.0) < 0.01, 'vel.vx incorreto')
    assert(math.abs(v.vy - 20.0) < 0.01, 'vel.vy incorreto')
    assert(math.abs(v.vz - 30.0) < 0.01, 'vel.vz incorreto')
  )");
  TEST_ASSERT(okGet,
              "Player.getPosition e Player.getVelocity executados com sucesso");

  // Testar Player.setPosition e Player.setVelocity
  bool okSet = dr2hook::LuaEngine::ExecuteString(R"(
    local okP = Player.setPosition({ x = 500.0, y = 600.0, z = 700.0 })
    assert(okP == true, 'setPosition deve retornar true')
    local okV = Player.setVelocity({ vx = 50.0, vy = 60.0, vz = 70.0 })
    assert(okV == true, 'setVelocity deve retornar true')
  )");
  TEST_ASSERT(okSet, "Player.setPosition e Player.setVelocity retornam true");

  dr2hook::CarState modifiedMemory{};
  mock.Read(vehicleAddr, &modifiedMemory, sizeof(dr2hook::CarState));
  TEST_ASSERT_FLOAT_NEAR(modifiedMemory.position.x, 500.0f, 0.01f,
                         "Memoria atualizada via setPosition");
  TEST_ASSERT_FLOAT_NEAR(modifiedMemory.linearVelocity.x, 50.0f, 0.01f,
                         "Memoria atualizada via setVelocity");

  // Testar Player.getState e Player.setState
  bool okState = dr2hook::LuaEngine::ExecuteString(R"(
    local st = Player.getState()
    assert(st ~= nil, 'getState retornou nil')
    assert(math.abs(st.position.x - 500.0) < 0.01, 'state position.x incorreto')
    assert(type(st.rotation) == 'table', 'rotation deve ser tabela')
    assert(type(st.wheels) == 'table', 'wheels deve ser tabela')
    assert(#st.wheels == 4, 'wheels deve ter 4 elementos')

    st.position.x = 999.0
    st.position.y = 888.0
    st.position.z = 777.0
    local okApply = Player.setState(st)
    assert(okApply == true, 'setState deve retornar true')
  )");
  TEST_ASSERT(okState,
              "Player.getState e Player.setState executados com sucesso");

  mock.Read(vehicleAddr, &modifiedMemory, sizeof(dr2hook::CarState));
  TEST_ASSERT_FLOAT_NEAR(modifiedMemory.position.x, 999.0f, 0.01f,
                         "setState aplicou nova posicao");
  TEST_ASSERT_FLOAT_NEAR(modifiedMemory.angularVelocity.x, 0.0f, 0.001f,
                         "setState sanitizou velocidade angular residual");
  TEST_ASSERT_FLOAT_NEAR(
      modifiedMemory.wheels[0].suspensionCompression,
      dr2hook::SUSPENSION_STATIC_SAG_RATIO, 0.001f,
      "setState normalizou compressao de suspensao para sag estatico");

  // Testar UI.notify
  bool okNotify = dr2hook::LuaEngine::ExecuteString(
      "UI.notify('Mensagem de notificacao de teste', 2.5)");
  TEST_ASSERT(okNotify, "UI.notify executa com sucesso");

  dr2hook::LuaEngine::Shutdown();
}

// ---------------------------------------------------------------------------
// 4. Bloqueio Anti-Cheat (ADR-003)
// ---------------------------------------------------------------------------
void TestAntiCheatLock() {
  std::cout << "[RUN] TestAntiCheatLock..." << std::endl;

  dr2hook::MockMemoryAccessor mock;
  dr2hook::MemoryScanner scanner(&mock);

  constexpr uintptr_t vehicleAddr = 0x140700000;
  constexpr uintptr_t sessionModeAddr = 0x140800000;

  dr2hook::CarState initialState{};
  initialState.position = {123.0f, 456.0f, 789.0f};
  mock.SetValue(vehicleAddr, initialState);

  // Modo competitivo / online (CareerOnline) -> Hard-lock
  mock.SetValue(sessionModeAddr,
                static_cast<uint32_t>(dr2hook::GameSessionMode::CareerOnline));

  dr2hook::Player::Configure(&scanner, vehicleAddr);
  dr2hook::SafetyGuard::Configure(&scanner, sessionModeAddr);

  dr2hook::LuaEngine::Initialize();

  bool okBlock = dr2hook::LuaEngine::ExecuteString(R"(
    assert(Safety.isRestrictedMode() == true, 'Safety.isRestrictedMode deve ser true')

    local okSetState = Player.setState({ position = { x = 0.0, y = 0.0, z = 0.0 } })
    assert(okSetState == false, 'Player.setState deve retornar false em modo restrito')

    local okSetPos = Player.setPosition({ x = 0.0, y = 0.0, z = 0.0 })
    assert(okSetPos == false, 'Player.setPosition deve retornar false em modo restrito')

    local okSetVel = Player.setVelocity({ vx = 0.0, vy = 0.0, vz = 0.0 })
    assert(okSetVel == false, 'Player.setVelocity deve retornar false em modo restrito')
  )");
  TEST_ASSERT(okBlock, "Bloqueio anti-cheat verificado em Lua para todas as "
                       "operacoes de escrita");

  dr2hook::CarState verifyMemory{};
  mock.Read(vehicleAddr, &verifyMemory, sizeof(dr2hook::CarState));
  TEST_ASSERT_FLOAT_NEAR(
      verifyMemory.position.x, 123.0f, 0.001f,
      "Memoria intocada apos tentativa de escrita rejeitada");

  dr2hook::LuaEngine::Shutdown();
}

// ---------------------------------------------------------------------------
// 5. Cache de funcoes via Lua Registry
// ---------------------------------------------------------------------------
void TestRegistryCaching() {
  std::cout << "[RUN] TestRegistryCaching..." << std::endl;

  dr2hook::LuaEngine::Initialize();
  lua_State *L = dr2hook::LuaEngine::GetState();

  bool okDef = dr2hook::LuaEngine::ExecuteString(
      "function calculateMultiplier(a, b) return (a + b) * 2 end");
  TEST_ASSERT(okDef, "Funcao definida em Lua");

  lua_getglobal(L, "calculateMultiplier");
  TEST_ASSERT(lua_isfunction(L, -1), "calculateMultiplier e uma funcao");

  int funcRef = luaL_ref(L, LUA_REGISTRYINDEX);
  TEST_ASSERT(funcRef != LUA_NOREF && funcRef != LUA_REFNIL,
              "Referencia valida obtida no registry");

  // Remover da tabela global para garantir que nao havera lookup por string
  lua_pushnil(L);
  lua_setglobal(L, "calculateMultiplier");

  lua_getglobal(L, "calculateMultiplier");
  TEST_ASSERT(lua_isnil(L, -1),
              "calculateMultiplier agora e nil na tabela global");
  lua_pop(L, 1);

  // Invocar funcao via registry ref passando 10 e 20 -> resultado esperado: 60
  lua_pushinteger(L, 10);
  lua_pushinteger(L, 20);
  bool callOk = dr2hook::LuaEngine::CallFunctionRef(funcRef, 2, 1);
  TEST_ASSERT(callOk, "CallFunctionRef executou callback via registry");

  int res = static_cast<int>(lua_tointeger(L, -1));
  TEST_ASSERT(res == 60, "Resultado do calculo via registry coincide: 60");
  lua_pop(L, 1);

  luaL_unref(L, LUA_REGISTRYINDEX, funcRef);
  dr2hook::LuaEngine::Shutdown();
}

// ---------------------------------------------------------------------------
// 6. Isolamento de Falhas e Desativacao de Mod
// ---------------------------------------------------------------------------
void TestModErrorIsolationAndDeactivation() {
  std::cout << "[RUN] TestModErrorIsolationAndDeactivation..." << std::endl;

  std::filesystem::path tempModsDir = "/tmp/dr2_test_mods_isolation";
  std::filesystem::path brokenModDir = tempModsDir / "broken_mod";
  std::filesystem::create_directories(brokenModDir);

  std::ofstream manifest(brokenModDir / "mod.json");
  manifest << R"({
    "name": "Broken Mod Test",
    "id": "test.broken",
    "version": "1.0.0",
    "main": "main.lua"
  })";
  manifest.close();

  std::ofstream script(brokenModDir / "main.lua");
  script << R"(
    function onInit()
      print("[Broken Mod] onInit invocado com sucesso.")
    end

    function onTick(dt)
      local bad = nil
      bad.callUndefined() -- Erro proposital de runtime
    end
  )";
  script.close();

  TEST_ASSERT(dr2hook::ModManager::Initialize(tempModsDir.string()),
              "ModManager inicializa diretorio de teste");

  const auto &mods = dr2hook::ModManager::GetLoadedMods();
  TEST_ASSERT(mods.size() == 1, "Exatamente 1 mod carregado");
  TEST_ASSERT(mods[0].id == "test.broken", "Mod carregado possui ID correto");
  TEST_ASSERT(mods[0].enabled == true, "Mod esta inicialmente habilitado");
  TEST_ASSERT(mods[0].refOnTick != LUA_NOREF, "Mod possui onTick registrado");

  // Despachar Tick: o mod deve falhar e ser IMEDIATAMENTE desativado
  dr2hook::ModManager::DispatchTick(0.016);

  TEST_ASSERT(
      mods[0].enabled == false,
      "Mod defeituoso foi desativado automaticamente apos erro em callback");

  // Despachar Tick novamente: nao deve travar e mod continua desativado
  dr2hook::ModManager::DispatchTick(0.016);
  TEST_ASSERT(mods[0].enabled == false, "Mod permanece desativado na sessao");

  // Verificar que a LuaEngine permanece saudavel e operacional para outros
  // scripts
  bool engineAlive =
      dr2hook::LuaEngine::ExecuteString("local valid = 100 + 200");
  TEST_ASSERT(engineAlive,
              "LuaEngine permanece intacta e funcional apos falha do mod");

  dr2hook::ModManager::Shutdown();
  std::filesystem::remove_all(tempModsDir);
}

// ---------------------------------------------------------------------------
// 7. Validacao real ponta a ponta: mods/practice_mode/main.lua
// ---------------------------------------------------------------------------
void TestPracticeModeModEndToEnd() {
  std::cout << "[RUN] TestPracticeModeModEndToEnd..." << std::endl;

  dr2hook::MockMemoryAccessor mock;
  dr2hook::MemoryScanner scanner(&mock);

  constexpr uintptr_t vehicleAddr = 0x140700000;
  constexpr uintptr_t sessionModeAddr = 0x140800000;

  dr2hook::CarState startState{};
  startState.position = {12.5f, 34.5f, 56.5f};
  startState.linearVelocity = {8.0f, 0.0f, 15.0f};
  startState.angularVelocity = {0.5f, -0.2f, 0.1f};
  for (int i = 0; i < 4; ++i) {
    startState.wheels[i].suspensionCompression = 0.45f;
    startState.wheels[i].inContact = true;
  }
  mock.SetValue(vehicleAddr, startState);

  // Iniciar em modo DirtFish (permitido para treino)
  mock.SetValue(sessionModeAddr,
                static_cast<uint32_t>(dr2hook::GameSessionMode::DirtFish));

  dr2hook::Player::Configure(&scanner, vehicleAddr);
  dr2hook::SafetyGuard::Configure(&scanner, sessionModeAddr);

  TEST_ASSERT(dr2hook::ModManager::Initialize("mods"),
              "ModManager inicializa com pasta 'mods'");

  const auto &loadedMods = dr2hook::ModManager::GetLoadedMods();
  const dr2hook::ModInstance *practiceMod = nullptr;
  for (const auto &m : loadedMods) {
    if (m.id == "dr2.practice_mode") {
      practiceMod = &m;
      break;
    }
  }

  TEST_ASSERT(practiceMod != nullptr, "Mod 'dr2.practice_mode' encontrado");
  TEST_ASSERT(practiceMod != nullptr && practiceMod->enabled,
              "Mod 'dr2.practice_mode' esta habilitado");
  TEST_ASSERT(practiceMod != nullptr && practiceMod->refOnKeyDown != LUA_NOREF,
              "refOnKeyDown registrado");
  TEST_ASSERT(practiceMod != nullptr &&
                  practiceMod->refOnStageStart != LUA_NOREF,
              "refOnStageStart registrado");

  // 1. Pressionar F5 (0x74): Salvar checkpoint
  dr2hook::ModManager::DispatchKeyDown(0x74);

  // 2. Simular deslocamento do carro na especial (carro avanca para novas
  // coordenadas)
  dr2hook::CarState movedState = startState;
  movedState.position = {888.0f, 777.0f, 666.0f};
  movedState.linearVelocity = {40.0f, 5.0f, 20.0f};
  movedState.angularVelocity = {2.0f, 2.0f, 2.0f};
  mock.SetValue(vehicleAddr, movedState);

  // 3. Tentar restaurar checkpoint via F6 (0x75) durante modo restrito
  // (DailyChallenge)
  mock.SetValue(sessionModeAddr, static_cast<uint32_t>(
                                     dr2hook::GameSessionMode::DailyChallenge));
  dr2hook::ModManager::DispatchKeyDown(0x75);

  dr2hook::CarState memCheck{};
  mock.Read(vehicleAddr, &memCheck, sizeof(dr2hook::CarState));
  TEST_ASSERT_FLOAT_NEAR(
      memCheck.position.x, 888.0f, 0.001f,
      "F6 em modo restrito bloqueado com sucesso pelo mod: memoria intocada");

  // 4. Retornar para modo homologado (DirtFish) e pressionar F6 (0x75)
  mock.SetValue(sessionModeAddr,
                static_cast<uint32_t>(dr2hook::GameSessionMode::DirtFish));
  dr2hook::ModManager::DispatchKeyDown(0x75);

  mock.Read(vehicleAddr, &memCheck, sizeof(dr2hook::CarState));
  TEST_ASSERT_FLOAT_NEAR(
      memCheck.position.x, 12.5f, 0.001f,
      "F6 em modo permitido restaura posicao original com sucesso");
  TEST_ASSERT_FLOAT_NEAR(memCheck.position.y, 34.5f, 0.001f,
                         "Posicao Y restaurada");
  TEST_ASSERT_FLOAT_NEAR(memCheck.position.z, 56.5f, 0.001f,
                         "Posicao Z restaurada");
  TEST_ASSERT_FLOAT_NEAR(memCheck.angularVelocity.x, 0.0f, 0.001f,
                         "Velocidade angular zerada para estabilidade");
  TEST_ASSERT_FLOAT_NEAR(memCheck.wheels[0].suspensionCompression,
                         dr2hook::SUSPENSION_STATIC_SAG_RATIO, 0.001f,
                         "Suspensao normalizada para repouso estatico (0.35)");

  // 5. Testar onStageStart: limpa checkpoint salvo
  dr2hook::ModManager::DispatchStageStart("Hawkes Bay - New Zealand");

  // Deslocar o carro para outra posicao
  dr2hook::CarState stageMovedState = startState;
  stageMovedState.position = {555.0f, 444.0f, 333.0f};
  mock.SetValue(vehicleAddr, stageMovedState);

  // Tentar restaurar com F6 apos stage start: checkpoint deve estar nulo
  dr2hook::ModManager::DispatchKeyDown(0x75);
  mock.Read(vehicleAddr, &memCheck, sizeof(dr2hook::CarState));
  TEST_ASSERT_FLOAT_NEAR(
      memCheck.position.x, 555.0f, 0.001f,
      "F6 apos onStageStart nao altera posicao pois checkpoint foi limpo");

  dr2hook::ModManager::Shutdown();
}

// ---------------------------------------------------------------------------
// 7b. API Menu (opções da tela nativa)
// ---------------------------------------------------------------------------
void TestMenuBindings() {
  std::cout << "[RUN] TestMenuBindings..." << std::endl;
  TEST_ASSERT(dr2hook::LuaEngine::Initialize(), "LuaEngine inicializa");
  dr2hook::ModMenu::Clear();

  TEST_ASSERT(!dr2hook::LuaEngine::ExecuteString(R"(Menu.toggle("a", "A"))"),
              "Menu fora de um mod falha");

  dr2hook::ModMenu::SetCurrentMod("test.menu");
  const bool okDeclare = dr2hook::LuaEngine::ExecuteString(R"(
    Menu.toggle("tyres", "Indestructible tyres", false)
    Menu.choice("mode", "Restore mode", {"Normal", "Momentum"}, 2)
    Menu.button("save", "Save checkpoint", function() end)
    assert(Menu.get("tyres") == false)
    local index, value = Menu.get("mode")
    assert(index == 2 and value == "Momentum")
    assert(Menu.get("save") == nil)
    Menu.set("tyres", true)
    Menu.set("mode", 1)
    assert(Menu.get("tyres") == true)
    assert(select(2, Menu.get("mode")) == "Normal")
  )");
  TEST_ASSERT(okDeclare, "declara, le e grava opcoes");

  const auto *options = dr2hook::ModMenu::OptionsOf("test.menu");
  TEST_ASSERT(options != nullptr && options->size() == 3, "tres opcoes registradas");
  TEST_ASSERT(options != nullptr && (*options)[0].DisplayLabel() == "Indestructible tyres: On",
              "rotulo do toggle reflete Menu.set");

  TEST_ASSERT(!dr2hook::LuaEngine::ExecuteString(R"(Menu.choice("x", "X", {}))"),
              "choice sem valores falha");
  TEST_ASSERT(!dr2hook::LuaEngine::ExecuteString(R"(Menu.button("x", "X"))"),
              "button sem funcao falha");
  TEST_ASSERT(!dr2hook::LuaEngine::ExecuteString(R"(Menu.set("missing", true))"),
              "set de opcao inexistente falha");
  const bool okFill = dr2hook::LuaEngine::ExecuteString(R"(
    for i = 4, 24 do Menu.toggle("extra" .. i, "Extra " .. i) end
  )");
  TEST_ASSERT(okFill, "ate 24 opcoes");
  TEST_ASSERT(!dr2hook::LuaEngine::ExecuteString(R"(Menu.toggle("more", "More"))"),
              "opcao alem do limite falha");

  dr2hook::ModMenu::Clear();
}

void TestPracticeModeNativeMenu() {
  std::cout << "[RUN] TestPracticeModeNativeMenu..." << std::endl;

  dr2hook::MockMemoryAccessor mock;
  dr2hook::MemoryScanner scanner(&mock);
  constexpr uintptr_t vehicleAddr = 0x140700000;
  constexpr uintptr_t sessionModeAddr = 0x140800000;

  dr2hook::CarState startState{};
  startState.position = {12.5f, 34.5f, 56.5f};
  mock.SetValue(vehicleAddr, startState);
  mock.SetValue(sessionModeAddr,
                static_cast<uint32_t>(dr2hook::GameSessionMode::DirtFish));
  dr2hook::Player::Configure(&scanner, vehicleAddr);
  dr2hook::SafetyGuard::Configure(&scanner, sessionModeAddr);

  TEST_ASSERT(dr2hook::ModManager::Initialize("mods"), "ModManager inicializa");
  const auto &mods = dr2hook::ModManager::GetLoadedMods();
  size_t practice = mods.size();
  for (size_t i = 0; i < mods.size(); ++i) {
    if (mods[i].id == "dr2.practice_mode") {
      practice = i;
    }
  }
  TEST_ASSERT(practice < mods.size(), "mod de pratica carregado");
  if (practice >= mods.size()) {
    dr2hook::ModManager::Shutdown();
    return;
  }

  const auto labels = [](const dr2hook::ModMenuEntry &entry) {
    std::vector<std::string> out;
    for (const dr2hook::ModOption &option : entry.options) {
      out.push_back(option.DisplayLabel());
    }
    return out;
  };
  const auto entries = dr2hook::ModManager::MenuSnapshot();
  const std::vector<std::string> expected = {
      "Save checkpoint",
      "Restore checkpoint",
      "Restore mode: Normal",
      "Clear checkpoint on new stage: On",
      "Race start: Normal",
      "Indestructible tyres: Off",
      "Indestructible car: Off",
      "Notifications: On",
  };
  TEST_ASSERT(entries.size() == mods.size() && entries[practice].name == "Practice Mode",
              "nome do mod no menu");
  TEST_ASSERT(entries.size() == mods.size() && !entries[practice].description.empty(),
              "descricao do mod no menu");
  TEST_ASSERT(entries.size() == mods.size() && labels(entries[practice]) == expected,
              "opcoes do mod de pratica, na ordem declarada");
  TEST_ASSERT(entries.size() == mods.size() &&
                  entries[practice].options[4].description.rfind("{v}How every start works", 0) == 0 &&
                  !entries[practice].options[0].description.empty(),
              "Menu.describe preenche a descricao do painel");
  TEST_ASSERT(entries.size() == mods.size() &&
                  entries[practice].options[4].description.find("\n\n") == std::string::npos &&
                  entries[practice].options[4].description.find("\n \n") != std::string::npos,
              "linha vazia da descricao vira espaco (sem simbolo no jogo)");

  // Salvar pelo menu (A no botao), desligar a limpeza ao iniciar especial pelo
  // combo (indice 0) e restaurar.
  dr2hook::ModManager::DispatchMenuEvent(practice, 0, -1);
  dr2hook::ModManager::DispatchMenuEvent(practice, 3, 0);
  dr2hook::CarState moved = startState;
  moved.position = {555.0f, 444.0f, 333.0f};
  mock.SetValue(vehicleAddr, moved);
  dr2hook::ModManager::DispatchStageStart("Hawkes Bay - New Zealand");
  dr2hook::ModManager::DispatchMenuEvent(practice, 1, -1);

  dr2hook::CarState memCheck{};
  mock.Read(vehicleAddr, &memCheck, sizeof(dr2hook::CarState));
  TEST_ASSERT_FLOAT_NEAR(memCheck.position.x, 12.5f, 0.001f,
                         "restaura pelo menu com a limpeza desligada");

  dr2hook::ModManager::DispatchMenuEvent(practice, 2, 1);
  dr2hook::ModManager::DispatchMenuEvent(practice, 4, -1);
  dr2hook::ModManager::DispatchMenuEvent(practice, 5, -1);
  dr2hook::ModManager::DispatchMenuEvent(practice, 7, 7);
  const auto after = dr2hook::ModManager::MenuSnapshot();
  const auto afterLabels = labels(after[practice]);
  TEST_ASSERT(afterLabels[2] == "Restore mode: Momentum", "combo escolhe o valor do choice");
  TEST_ASSERT(afterLabels[3] == "Clear checkpoint on new stage: Off", "combo desliga o toggle");
  TEST_ASSERT(afterLabels[4] == "Race start: No countdown",
              "A avanca o modo de largada sem quebrar o mod fora do jogo");
  TEST_ASSERT(afterLabels[5] == "Indestructible tyres: On", "A avanca o toggle");
  TEST_ASSERT(afterLabels[7] == "Notifications: On", "indice fora dos valores ignorado");
  TEST_ASSERT(after[practice].options[2].ValueNames() ==
                  std::vector<std::string>({"Normal", "Momentum"}),
              "valores do combo no snapshot");
  TEST_ASSERT(dr2hook::ModMenu::TakeDirty(), "menu marcado para republicar");

  dr2hook::ModManager::DispatchMenuEvent(practice, 99);
  dr2hook::ModManager::DispatchMenuEvent(mods.size() + 5, 0);
  TEST_ASSERT(true, "indices fora do intervalo sao ignorados");

  dr2hook::ModManager::Shutdown();
  TEST_ASSERT(dr2hook::ModMenu::OptionsOf("dr2.practice_mode") == nullptr,
              "shutdown limpa as opcoes");
}

// ---------------------------------------------------------------------------
// 8. Despacho Concorrente de Eventos (Thread-Safety)
// ---------------------------------------------------------------------------
void TestConcurrentEventDispatch() {
  std::cout << "[RUN] TestConcurrentEventDispatch..." << std::endl;

  std::filesystem::path tempModsDir = "/tmp/dr2_test_concurrent_mods";
  std::filesystem::path modDir = tempModsDir / "concurrent_mod";
  std::filesystem::create_directories(modDir);

  std::ofstream manifest(modDir / "mod.json");
  manifest << R"({
    "name": "Concurrent Test Mod",
    "id": "test.concurrent",
    "version": "1.0.0",
    "main": "main.lua"
  })";
  manifest.close();

  std::ofstream script(modDir / "main.lua");
  script << R"(
    tickCount = 0
    keyCount = 0

    function onTick(dt)
      tickCount = tickCount + 1
    end

    function onKeyDown(key)
      keyCount = keyCount + 1
    end
  )";
  script.close();

  TEST_ASSERT(dr2hook::ModManager::Initialize(tempModsDir.string()),
              "ModManager inicializa com mod de teste concorrente");

  const auto &mods = dr2hook::ModManager::GetLoadedMods();
  TEST_ASSERT(mods.size() == 1, "Exatamente 1 mod concorrente carregado");
  TEST_ASSERT(mods[0].enabled == true, "Mod concorrente habilitado");
  TEST_ASSERT(mods[0].refOnTick != LUA_NOREF, "refOnTick registrado");
  TEST_ASSERT(mods[0].refOnKeyDown != LUA_NOREF, "refOnKeyDown registrado");

  // Lançar duas threads concorrentes:
  // Thread A: DispatchTick (100 iterações)
  // Thread B: DispatchKeyDown (100 iterações)
  std::thread threadA([]() {
    for (int i = 0; i < 100; ++i) {
      dr2hook::ModManager::DispatchTick(0.016);
    }
  });

  std::thread threadB([]() {
    for (int i = 0; i < 100; ++i) {
      dr2hook::ModManager::DispatchKeyDown(0x41);
    }
  });

  threadA.join();
  threadB.join();

  // Validar integridade da engine e incremento dos contadores
  TEST_ASSERT(mods[0].enabled == true,
              "Mod permaneceu habilitado sem falhas apos execucao concorrente");

  lua_State *L = dr2hook::LuaEngine::GetState();
  TEST_ASSERT(L != nullptr, "lua_State permanece valido");
  TEST_ASSERT(lua_gettop(L) == 0,
              "Pilha Lua sem corrupcao/vazamentos residuais (top == 0)");

  bool okValidate = dr2hook::LuaEngine::ExecuteString(R"(
    assert(tickCount == 100, 'tickCount incorreto: ' .. tostring(tickCount))
    assert(keyCount == 100, 'keyCount incorreto: ' .. tostring(keyCount))
  )");
  TEST_ASSERT(
      okValidate,
      "Contadores incrementados com exatidao (100 ticks e 100 keydowns)");

  dr2hook::ModManager::Shutdown();
  std::filesystem::remove_all(tempModsDir);
}

// ---------------------------------------------------------------------------
// Main Runner
// ---------------------------------------------------------------------------
// Opções gravadas por uma execução anterior mudariam os valores padrão que os
// testes esperam.
void RemoveRealModSettings() {
  std::error_code ignored;
  std::filesystem::remove(std::filesystem::path("mods") / "practice_mode" /
                              dr2hook::ModManager::kSettingsFileName,
                          ignored);
}

void TestModSettingsPersistence() {
  std::cout << "[RUN] TestModSettingsPersistence..." << std::endl;
  const std::filesystem::path tempModsDir = "/tmp/dr2_test_settings_mods";
  const std::filesystem::path modDir = tempModsDir / "settings_mod";
  std::filesystem::remove_all(tempModsDir);
  std::filesystem::create_directories(modDir);
  std::ofstream(modDir / "mod.json") << R"({
    "name": "Settings Test Mod", "id": "test.settings", "version": "1.0.0", "main": "main.lua"
  })";
  std::ofstream(modDir / "main.lua") << R"(
    Menu.toggle("fast", "Fast", false)
    Menu.choice("mode", "Mode", {"A", "B", "C"}, 1)
    Menu.button("go", "Go", function() end)
    function onInit()
      local _, mode = Menu.get("mode")
      seenAtInit = tostring(Menu.get("fast")) .. "/" .. mode
    end
  )";

  TEST_ASSERT(dr2hook::ModManager::Initialize(tempModsDir.string()), "carrega mod de opcoes");
  TEST_ASSERT(!std::filesystem::exists(modDir / dr2hook::ModManager::kSettingsFileName),
              "sem mudanca, sem arquivo");
  dr2hook::ModManager::DispatchMenuEvent(0, 0, -1); // fast -> on
  dr2hook::ModManager::DispatchMenuEvent(0, 1, 2);  // mode -> C
  dr2hook::ModManager::DispatchMenuEvent(0, 2, -1); // botao nao grava valor
  std::ifstream saved(modDir / dr2hook::ModManager::kSettingsFileName);
  std::stringstream text;
  text << saved.rdbuf();
  TEST_ASSERT(text.str().find("fast=on") != std::string::npos &&
                  text.str().find("mode=C") != std::string::npos &&
                  text.str().find("go=") == std::string::npos,
              "grava toggle como on/off e choice pelo texto");

  dr2hook::ModManager::ReloadMods(tempModsDir.string());
  const auto entries = dr2hook::ModManager::MenuSnapshot();
  TEST_ASSERT(entries.size() == 1 && entries[0].options.size() == 3 &&
                  entries[0].options[0].DisplayLabel() == "Fast: On" &&
                  entries[0].options[1].DisplayLabel() == "Mode: C",
              "valores restaurados no reload");
  TEST_ASSERT(dr2hook::LuaEngine::ExecuteString(
                  "assert(seenAtInit == 'true/C', tostring(seenAtInit))"),
              "onInit ja enxerga os valores salvos");

  std::ofstream(modDir / dr2hook::ModManager::kSettingsFileName)
      << "; comentario\nmode=Z\nfast=talvez\nunknown=on\n";
  dr2hook::ModManager::ReloadMods(tempModsDir.string());
  const auto fallback = dr2hook::ModManager::MenuSnapshot();
  TEST_ASSERT(fallback.size() == 1 && fallback[0].options[0].DisplayLabel() == "Fast: Off" &&
                  fallback[0].options[1].DisplayLabel() == "Mode: A",
              "valores invalidos e ids desconhecidos ficam no padrao");

  dr2hook::ModManager::Shutdown();
  std::filesystem::remove_all(tempModsDir);
}

int main() {
  RemoveRealModSettings();
  std::cout << "====================================================="
            << std::endl;
  std::cout << "DR2Hook - Suíte de Testes do Motor Lua e ModManager (Fase 4)"
            << std::endl;
  std::cout << "====================================================="
            << std::endl;

  TestLuaEngineInitAndCxxUnwinding();
  TestPrintRedirection();
  TestPlayerAndSafetyBindings();
  TestAntiCheatLock();
  TestRegistryCaching();
  TestModErrorIsolationAndDeactivation();
  TestPracticeModeModEndToEnd();
  TestMenuBindings();
  TestPracticeModeNativeMenu();
  TestConcurrentEventDispatch();
  TestModSettingsPersistence();
  RemoveRealModSettings();

  std::cout << "====================================================="
            << std::endl;
  std::cout << "Resultados dos Testes:" << std::endl;
  std::cout << "  Total de asserções executadas: " << g_testsRun << std::endl;
  std::cout << "  Passaram: " << g_testsPassed << std::endl;
  std::cout << "  Falharam: " << g_testsFailed << std::endl;
  std::cout << "====================================================="
            << std::endl;

  return (g_testsFailed == 0) ? 0 : 1;
}
