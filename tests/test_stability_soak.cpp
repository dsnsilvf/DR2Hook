#include "dr2hook/logger.h"
#include "dr2hook/memory.h"
#include "dr2hook/player.h"
#include "dr2hook/safety.h"
#include "dr2hook/savestate.h"
#include "dr2hook/script/lua_engine.h"
#include "dr2hook/script/mod_manager.h"
#include "dr2hook/ui/overlay.h"

#include "imgui.h"
#include "lauxlib.h"
#include "lua.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>
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

static size_t GetProcessMemoryRSSBytes() {
  std::ifstream statm("/proc/self/statm");
  if (!statm.is_open()) {
    return 0;
  }
  size_t size = 0, resident = 0;
  statm >> size >> resident;
  long pageSize = sysconf(_SC_PAGESIZE);
  if (pageSize <= 0) {
    pageSize = 4096;
  }
  return resident * static_cast<size_t>(pageSize);
}

// ---------------------------------------------------------------------------
// 1. Soak Testing Determinístico: 10.000 Ciclos de Sessão Acelerada
// ---------------------------------------------------------------------------
void TestSoakLongSessionAccelerated10k() {
  std::cout << "[RUN] TestSoakLongSessionAccelerated10k (10.000 iterações)..."
            << std::endl;

  dr2hook::MockMemoryAccessor mock;
  dr2hook::MemoryScanner scanner(&mock);

  constexpr uintptr_t vehicleAddr = 0x140800000;
  constexpr uintptr_t sessionModeAddr = 0x140900000;

  dr2hook::CarState initialCarState{};
  initialCarState.position = {150.0f, 25.0f, 400.0f};
  initialCarState.linearVelocity = {22.0f, 0.0f, 35.0f};
  initialCarState.angularVelocity = {0.0f, 0.0f, 0.0f};
  initialCarState.rotationMatrix.m[0][0] = 1.0f;
  initialCarState.rotationMatrix.m[1][1] = 1.0f;
  initialCarState.rotationMatrix.m[2][2] = 1.0f;
  for (int i = 0; i < 4; ++i) {
    initialCarState.wheels[i].suspensionCompression =
        dr2hook::SUSPENSION_STATIC_SAG_RATIO;
    initialCarState.wheels[i].angularVelocity = 10.0f;
    initialCarState.wheels[i].inContact = true;
  }

  mock.SetValue(vehicleAddr, initialCarState);
  mock.SetValue(
      sessionModeAddr,
      static_cast<uint32_t>(dr2hook::GameSessionMode::TimeTrialOffline));

  dr2hook::Player::Configure(&scanner, vehicleAddr);
  dr2hook::SafetyGuard::Configure(&scanner, sessionModeAddr);

  TEST_ASSERT(dr2hook::SavestateManager::Initialize(),
              "SavestateManager inicializado com sucesso para soak test");

  TEST_ASSERT(dr2hook::OverlayManager::Initialize(nullptr, nullptr, nullptr),
              "OverlayManager inicializado em modo headless");

  TEST_ASSERT(dr2hook::ModManager::Initialize("mods"),
              "ModManager inicializado carregando mods/practice_mode");

  const auto &loadedMods = dr2hook::ModManager::GetLoadedMods();
  TEST_ASSERT(!loadedMods.empty(),
              "Mod de treino oficial (practice_mode) carregado");
  if (!loadedMods.empty()) {
    TEST_ASSERT(loadedMods[0].enabled, "Mod practice_mode ativo");
  }

  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(1920.0f, 1080.0f);
  constexpr float kDeltaTime = 0.016f;
  io.DeltaTime = kDeltaTime;

  constexpr int kTotalIterations = 10000;
  constexpr int kWarmupIterations = 200;

  size_t memoryWarmupBytes = 0;
  int saveCount = 0;
  int restoreCount = 0;
  dr2hook::Vector3 lastSavedPosition{0.0f, 0.0f, 0.0f};

  for (int frame = 0; frame < kTotalIterations; ++frame) {
    io.DeltaTime = kDeltaTime;

    // 1. Despacho de tick para a engine Lua
    dr2hook::ModManager::DispatchTick(kDeltaTime);

    // 2. Simulação de movimento do veículo no espaço
    dr2hook::CarState curState{};
    mock.Read(vehicleAddr, &curState, sizeof(curState));
    curState.position.x += 0.02f;
    curState.position.z += 0.05f;
    mock.Write(vehicleAddr, &curState, sizeof(curState));

    // 3. Ciclos periódicos de salvamento de savestate (a cada 100 frames)
    if (frame % 100 == 0) {
      lastSavedPosition = curState.position;
      dr2hook::ModManager::DispatchKeyDown(0x74); // F5 no script Lua
      dr2hook::SavestateManager::OnKeyAction(VK_F5, true);
      dr2hook::SavestateManager::OnKeyAction(VK_F5, false);
      ++saveCount;

      // Perturbar posição e velocidade angular simulando descontrole
      curState.position.x += 80.0f;
      curState.position.y += 10.0f;
      curState.angularVelocity = {2.5f, 4.0f, -1.8f};
      for (int i = 0; i < 4; ++i) {
        curState.wheels[i].suspensionCompression = 0.95f;
        curState.wheels[i].inContact = false;
      }
      mock.Write(vehicleAddr, &curState, sizeof(curState));
    }

    // 4. Ciclos periódicos de restauração de savestate (a cada 100 frames,
    // offset 50)
    if (frame % 100 == 50 && saveCount > 0) {
      dr2hook::ModManager::DispatchKeyDown(0x75); // F6 no script Lua
      dr2hook::SavestateManager::OnKeyAction(VK_F6, true);
      dr2hook::SavestateManager::OnKeyAction(VK_F6, false);
      ++restoreCount;

      dr2hook::CarState restoredState{};
      mock.Read(vehicleAddr, &restoredState, sizeof(restoredState));

      // Validar restauração e estabilização de física
      TEST_ASSERT_FLOAT_NEAR(restoredState.position.x, lastSavedPosition.x,
                             0.01f, "Posição X restaurada com fidelidade");
      TEST_ASSERT_FLOAT_NEAR(restoredState.angularVelocity.x, 0.0f, 0.001f,
                             "Amortecimento angular X zerado");
      TEST_ASSERT_FLOAT_NEAR(restoredState.angularVelocity.y, 0.0f, 0.001f,
                             "Amortecimento angular Y zerado");
      TEST_ASSERT_FLOAT_NEAR(restoredState.angularVelocity.z, 0.0f, 0.001f,
                             "Amortecimento angular Z zerado");
      for (int i = 0; i < 4; ++i) {
        TEST_ASSERT_FLOAT_NEAR(
            restoredState.wheels[i].suspensionCompression,
            dr2hook::SUSPENSION_STATIC_SAG_RATIO, 0.001f,
            "Compressão estática de suspensão sag 0.35 após restauração");
        TEST_ASSERT(restoredState.wheels[i].inContact,
                    "Rodas em contato com o solo após restauração");
      }
    }

    // 5. Enfileiramento contínuo de notificações ImGui HUD
    if (frame % 30 == 0) {
      dr2hook::OverlayManager::AddNotification(
          "Soak Test Notification #" + std::to_string(frame), 0.45f,
          (frame % 60 == 0) ? dr2hook::ToastType::Info
                            : dr2hook::ToastType::Warning);
    }
    if (frame % 150 == 0) {
      dr2hook::LuaEngine::ExecuteString(
          "UI.notify('Notificacao disparada via Lua no ciclo " +
          std::to_string(frame) + "', 0.40)");
    }

    // 6. Execução do frame de renderização gráfica headless
    dr2hook::OverlayManager::Render(nullptr);

    // 7. Checkpoints periódicos de integridade
    if (frame == kWarmupIterations) {
      memoryWarmupBytes = GetProcessMemoryRSSBytes();
    }

    if (frame % 1000 == 0 && frame > 0) {
      // Fila de notificações deve permanecer controlada e limitada por
      // expiração
      size_t activeNotifs = dr2hook::OverlayManager::GetNotificationCount();
      TEST_ASSERT(activeNotifs < 50,
                  "Fila de notificações mantida limitada e não cumulativa");

      // Pilha do interpretador Lua não pode acumular resíduos
      lua_State *L = dr2hook::LuaEngine::GetState();
      TEST_ASSERT(L != nullptr && lua_gettop(L) == 0,
                  "Pilha Lua equilibrada (top == 0)");
    }
  }

  // 8. Pós-condições: expiração completa de notificações ativas
  dr2hook::OverlayManager::UpdateNotifications(2.0f);
  TEST_ASSERT(
      dr2hook::OverlayManager::GetNotificationCount() == 0,
      "Todas as notificações expiraram completamente após avanço de tempo");

  // 9. Validação de estabilidade de memória
  size_t memoryFinalBytes = GetProcessMemoryRSSBytes();
  if (memoryWarmupBytes > 0 && memoryFinalBytes > memoryWarmupBytes) {
    size_t diffBytes = memoryFinalBytes - memoryWarmupBytes;
    constexpr size_t maxAllowedGrowthBytes = 35 * 1024 * 1024; // 35 MB máximo
    TEST_ASSERT(
        diffBytes < maxAllowedGrowthBytes,
        "Crescimento de memória RSS dentro do limite seguro (sem leak)");
    std::cout << "  [INFO] Consumo de memória pós-warmup: "
              << (memoryWarmupBytes / 1024)
              << " KB, final: " << (memoryFinalBytes / 1024)
              << " KB (delta: " << (diffBytes / 1024) << " KB)" << std::endl;
  }

  TEST_ASSERT(saveCount == 100, "100 ciclos de gravação completados");
  TEST_ASSERT(restoreCount == 100, "100 ciclos de restauração completados");

  lua_State *L = dr2hook::LuaEngine::GetState();
  TEST_ASSERT(L != nullptr && lua_gettop(L) == 0,
              "Pilha Lua final sem corrupção residual");

  dr2hook::ModManager::Shutdown();
  dr2hook::OverlayManager::Shutdown();
  dr2hook::SavestateManager::Shutdown();

  TEST_ASSERT(dr2hook::ModManager::GetLoadedMods().empty(),
              "ModManager encerrado limpo e mods limpos");
  TEST_ASSERT(!dr2hook::LuaEngine::IsInitialized(),
              "LuaEngine encerrado limpo");
  TEST_ASSERT(!dr2hook::OverlayManager::IsInitialized(),
              "OverlayManager encerrado limpo");
  TEST_ASSERT(!dr2hook::SavestateManager::HasSavedState(),
              "SavestateManager resetado limpo");
}

// ---------------------------------------------------------------------------
// 2. Estresse de Reentrância: 100 Ciclos Rápidos de Initialize / Shutdown
// ---------------------------------------------------------------------------
void TestRepeatedInitShutdownStress() {
  std::cout << "[RUN] TestRepeatedInitShutdownStress (100 ciclos rápidos)..."
            << std::endl;

  for (int cycle = 0; cycle < 100; ++cycle) {
    bool okInitOverlay =
        dr2hook::OverlayManager::Initialize(nullptr, nullptr, nullptr);
    bool okInitMods = dr2hook::ModManager::Initialize("mods");
    bool okInitSave = dr2hook::SavestateManager::Initialize();

    TEST_ASSERT(okInitOverlay && okInitMods && okInitSave,
                "Inicialização conjunta bem-sucedida no ciclo");

    dr2hook::OverlayManager::AddNotification("Stress Ciclo", 0.5f);
    dr2hook::OverlayManager::Render(nullptr);
    dr2hook::ModManager::DispatchTick(0.016);

    dr2hook::ModManager::Shutdown();
    dr2hook::OverlayManager::Shutdown();
    dr2hook::SavestateManager::Shutdown();

    TEST_ASSERT(!dr2hook::OverlayManager::IsInitialized(),
                "OverlayManager desligado no ciclo");
    TEST_ASSERT(dr2hook::ModManager::GetLoadedMods().empty(),
                "ModManager desligado no ciclo");
  }
}

// ---------------------------------------------------------------------------
// 3. Teste de Drenagem de Alta Frequência de Notificações
// ---------------------------------------------------------------------------
void TestHighFrequencyNotificationDrain() {
  std::cout << "[RUN] TestHighFrequencyNotificationDrain (500 notificações)..."
            << std::endl;

  dr2hook::OverlayManager::Initialize(nullptr, nullptr, nullptr);
  TEST_ASSERT(dr2hook::OverlayManager::GetNotificationCount() == 0,
              "Fila inicialmente vazia");

  // Injetar 500 notificações com durações escalonadas
  for (int i = 0; i < 500; ++i) {
    float dur = 0.1f + static_cast<float>(i % 10) * 0.05f;
    dr2hook::OverlayManager::AddNotification("Toast #" + std::to_string(i), dur,
                                             dr2hook::ToastType::Info);
  }

  TEST_ASSERT(dr2hook::OverlayManager::GetNotificationCount() == 500,
              "500 notificações enfileiradas");

  // Executar renderização consumindo pequenos passos de tempo
  for (int step = 0; step < 20; ++step) {
    ImGuiIO &io = ImGui::GetIO();
    io.DeltaTime = 0.05f;
    dr2hook::OverlayManager::Render(nullptr);
  }

  // Avançar tempo restante para drenar todos os toasts
  dr2hook::OverlayManager::UpdateNotifications(2.0f);
  TEST_ASSERT(dr2hook::OverlayManager::GetNotificationCount() == 0,
              "Todas as 500 notificações drenadas com sucesso sem corrupção");

  dr2hook::OverlayManager::Shutdown();
}

// ---------------------------------------------------------------------------
// Main Test Runner
// ---------------------------------------------------------------------------
int main() {
  std::cout << "====================================================="
            << std::endl;
  std::cout << "DR2Hook - Suíte de Estresse e Soak Testing (Fase 6)"
            << std::endl;
  std::cout << "====================================================="
            << std::endl;

  TestSoakLongSessionAccelerated10k();
  TestRepeatedInitShutdownStress();
  TestHighFrequencyNotificationDrain();

  std::cout << "====================================================="
            << std::endl;
  std::cout << "Resultados da Suíte de Soak Testing:" << std::endl;
  std::cout << "  Total de asserções executadas: " << g_testsRun << std::endl;
  std::cout << "  Passaram: " << g_testsPassed << std::endl;
  std::cout << "  Falharam: " << g_testsFailed << std::endl;
  std::cout << "====================================================="
            << std::endl;

  return (g_testsFailed == 0) ? 0 : 1;
}
