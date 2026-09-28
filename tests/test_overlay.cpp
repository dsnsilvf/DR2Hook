#include "dr2hook/logger.h"
#include "dr2hook/memory.h"
#include "dr2hook/player.h"
#include "dr2hook/safety.h"
#include "dr2hook/savestate.h"
#include "dr2hook/script/lua_engine.h"
#include "dr2hook/script/mod_manager.h"
#include "dr2hook/ui/overlay.h"

#include "imgui.h"

#include <cmath>
#include <iostream>
#include <string>
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
// 1. Inicializacao e Encerramento Headless do ImGui / OverlayManager
// ---------------------------------------------------------------------------
void TestHeadlessInitAndShutdown() {
  std::cout << "[RUN] TestHeadlessInitAndShutdown..." << std::endl;

  ImGuiContext *ctx = ImGui::CreateContext();
  TEST_ASSERT(ctx != nullptr, "ImGui::CreateContext cria contexto headless");

  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(1920.0f, 1080.0f);
  io.DeltaTime = 1.0f / 60.0f;
  io.IniFilename = nullptr;

  unsigned char *pixels = nullptr;
  int width = 0, height = 0;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
  TEST_ASSERT(pixels != nullptr && width > 0 && height > 0,
              "Atlas de fontes gerado com sucesso para contexto headless");

  bool ok = dr2hook::OverlayManager::Initialize(nullptr, nullptr, nullptr);
  TEST_ASSERT(ok, "OverlayManager::Initialize tem sucesso em modo headless");
  TEST_ASSERT(dr2hook::OverlayManager::IsInitialized(),
              "OverlayManager reporta inicializado");

  dr2hook::OverlayManager::Shutdown();
  TEST_ASSERT(!dr2hook::OverlayManager::IsInitialized(),
              "OverlayManager reporta desinicializado apos Shutdown");
}

// ---------------------------------------------------------------------------
// 2. Alternancia de Visibilidade do Menu (ToggleMenu / IsMenuVisible)
// ---------------------------------------------------------------------------
void TestMenuVisibility() {
  std::cout << "[RUN] TestMenuVisibility..." << std::endl;

  dr2hook::OverlayManager::Initialize(nullptr, nullptr, nullptr);

  TEST_ASSERT(!dr2hook::OverlayManager::IsMenuVisible(),
              "Menu inicia oculto por padrao");

  dr2hook::OverlayManager::ToggleMenu();
  TEST_ASSERT(dr2hook::OverlayManager::IsMenuVisible(),
              "ToggleMenu alternou para visivel");

  dr2hook::OverlayManager::ToggleMenu();
  TEST_ASSERT(!dr2hook::OverlayManager::IsMenuVisible(),
              "ToggleMenu alternou para oculto");

  dr2hook::OverlayManager::SetMenuVisible(true);
  TEST_ASSERT(dr2hook::OverlayManager::IsMenuVisible(),
              "SetMenuVisible(true) define menu como visivel");

  dr2hook::OverlayManager::SetMenuVisible(false);
  TEST_ASSERT(!dr2hook::OverlayManager::IsMenuVisible(),
              "SetMenuVisible(false) define menu como oculto");

  dr2hook::OverlayManager::Shutdown();
}

// ---------------------------------------------------------------------------
// 3. Fila de Notificacoes HUD (AddNotification, Progressao e Expiracao)
// ---------------------------------------------------------------------------
void TestNotificationQueueAndExpiration() {
  std::cout << "[RUN] TestNotificationQueueAndExpiration..." << std::endl;

  dr2hook::OverlayManager::Initialize(nullptr, nullptr, nullptr);

  TEST_ASSERT(dr2hook::OverlayManager::GetNotificationCount() == 0,
              "Fila de notificacoes inicia vazia");

  dr2hook::OverlayManager::AddNotification("Notificacao Info", 1.0f,
                                           dr2hook::ToastType::Info);
  dr2hook::OverlayManager::AddNotification("Notificacao Aviso", 2.0f,
                                           dr2hook::ToastType::Warning);
  dr2hook::OverlayManager::AddNotification("Notificacao Erro", 0.5f,
                                           dr2hook::ToastType::Error);

  TEST_ASSERT(dr2hook::OverlayManager::GetNotificationCount() == 3,
              "3 notificacoes enfileiradas");

  auto toasts = dr2hook::OverlayManager::GetActiveNotifications();
  TEST_ASSERT(toasts.size() == 3,
              "Vetor de notificacoes ativas contem 3 itens");
  TEST_ASSERT(toasts[0].message == "Notificacao Info",
              "Mensagem da notificacao 0 correta");
  TEST_ASSERT(toasts[0].type == dr2hook::ToastType::Info,
              "Tipo da notificacao 0 e Info");
  TEST_ASSERT(toasts[1].message == "Notificacao Aviso",
              "Mensagem da notificacao 1 correta");
  TEST_ASSERT(toasts[1].type == dr2hook::ToastType::Warning,
              "Tipo da notificacao 1 e Warning");
  TEST_ASSERT(toasts[2].message == "Notificacao Erro",
              "Mensagem da notificacao 2 correta");
  TEST_ASSERT(toasts[2].type == dr2hook::ToastType::Error,
              "Tipo da notificacao 2 e Error");

  // Avancar 0.6s: o toast de 0.5s deve expirar
  dr2hook::OverlayManager::UpdateNotifications(0.6f);
  TEST_ASSERT(dr2hook::OverlayManager::GetNotificationCount() == 2,
              "Toast de 0.5s expirou e foi removido apos 0.6s");

  // Avancar mais 0.5s (total 1.1s): o toast de 1.0s deve expirar
  dr2hook::OverlayManager::UpdateNotifications(0.5f);
  TEST_ASSERT(dr2hook::OverlayManager::GetNotificationCount() == 1,
              "Toast de 1.0s expirou e foi removido apos 1.1s total");

  toasts = dr2hook::OverlayManager::GetActiveNotifications();
  TEST_ASSERT(toasts.size() == 1 && toasts[0].message == "Notificacao Aviso",
              "Apenas o toast de 2.0s permanece na fila");

  // Avancar mais 1.0s (total 2.1s): todos devem expirar
  dr2hook::OverlayManager::UpdateNotifications(1.0f);
  TEST_ASSERT(dr2hook::OverlayManager::GetNotificationCount() == 0,
              "Todos os toasts expiraram e a fila esta vazia");

  dr2hook::OverlayManager::Shutdown();
}

// ---------------------------------------------------------------------------
// 4. Renderizacao Headless (Loop Grafico sem Frame Drops)
// ---------------------------------------------------------------------------
void TestHeadlessRenderLoop() {
  std::cout << "[RUN] TestHeadlessRenderLoop..." << std::endl;

  dr2hook::OverlayManager::Initialize(nullptr, nullptr, nullptr);

  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(1920.0f, 1080.0f);
  io.DeltaTime = 1.0f / 60.0f;

  dr2hook::OverlayManager::AddNotification("Toast Ativo", 3.0f,
                                           dr2hook::ToastType::Info);
  dr2hook::OverlayManager::SetMenuVisible(true);

  // Executar frames simulados de renderizacao para permitir calculo de layout e
  // desenho
  for (int frame = 0; frame < 5; ++frame) {
    dr2hook::OverlayManager::Render(nullptr);
  }

  ImDrawData *drawData = ImGui::GetDrawData();
  TEST_ASSERT(drawData != nullptr,
              "ImGui::GetDrawData retorna ponteiro valido no frame headless");
  TEST_ASSERT(drawData->CmdListsCount > 0,
              "Listas de comandos de desenho geradas no frame headless");
  TEST_ASSERT(drawData->TotalVtxCount > 0,
              "Vertices de geometria gerados para overlay e menu");

  dr2hook::OverlayManager::Shutdown();
}

// ---------------------------------------------------------------------------
// 5. Integracao com UI.notify a partir de scripts Lua
// ---------------------------------------------------------------------------
void TestLuaUINotifyIntegration() {
  std::cout << "[RUN] TestLuaUINotifyIntegration..." << std::endl;

  dr2hook::OverlayManager::Initialize(nullptr, nullptr, nullptr);
  dr2hook::LuaEngine::Initialize();

  TEST_ASSERT(dr2hook::OverlayManager::GetNotificationCount() == 0,
              "Fila de notificacoes vazia antes do teste de script");

  // Disparar notificacao a partir de codigo Lua
  bool ok = dr2hook::LuaEngine::ExecuteString(
      "UI.notify('Mensagem de Alerta do Script Lua', 4.5)");
  TEST_ASSERT(ok, "Execucao de UI.notify via Lua foi bem-sucedida");

  TEST_ASSERT(dr2hook::OverlayManager::GetNotificationCount() == 1,
              "OverlayManager recebeu a notificacao disparada pelo script Lua");

  auto toasts = dr2hook::OverlayManager::GetActiveNotifications();
  TEST_ASSERT(!toasts.empty(), "Lista de notificacoes ativas contem item");
  if (!toasts.empty()) {
    TEST_ASSERT(toasts[0].message == "Mensagem de Alerta do Script Lua",
                "Mensagem recebida corresponde ao enviado pelo script Lua");
    TEST_ASSERT_FLOAT_NEAR(toasts[0].duration, 4.5f, 0.01f,
                           "Duracao configurada via Lua corresponde");
    TEST_ASSERT(toasts[0].type == dr2hook::ToastType::Info,
                "Tipo padrao de notificacao Lua e Info");
  }

  dr2hook::LuaEngine::Shutdown();
  dr2hook::OverlayManager::Shutdown();
}

// ---------------------------------------------------------------------------
// 6. Captura Seletiva de Input no WndProc (HandleWndProc)
// ---------------------------------------------------------------------------
void TestInputCaptureFiltering() {
  std::cout << "[RUN] TestInputCaptureFiltering..." << std::endl;

  dr2hook::OverlayManager::Initialize(nullptr, nullptr, nullptr);
  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(1920.0f, 1080.0f);
  io.DeltaTime = 1.0f / 60.0f;

  constexpr UINT MSG_KEYDOWN = 0x0100;   // WM_KEYDOWN
  constexpr UINT MSG_MOUSEMOVE = 0x0200; // WM_MOUSEMOVE

  // Caso 1: Menu fechado -> nunca consome input
  dr2hook::OverlayManager::SetMenuVisible(false);
  io.WantCaptureKeyboard = true;
  io.WantCaptureMouse = true;
  TEST_ASSERT(
      !dr2hook::OverlayManager::HandleWndProc(nullptr, MSG_KEYDOWN, 0x41, 0),
      "Com menu fechado, HandleWndProc retorna false para teclado");
  TEST_ASSERT(
      !dr2hook::OverlayManager::HandleWndProc(nullptr, MSG_MOUSEMOVE, 0, 0),
      "Com menu fechado, HandleWndProc retorna false para mouse");

  // Caso 2: Menu aberto com WantCaptureKeyboard = true
  dr2hook::OverlayManager::SetMenuVisible(true);
  io.WantCaptureKeyboard = true;
  io.WantCaptureMouse = false;
  TEST_ASSERT(
      dr2hook::OverlayManager::HandleWndProc(nullptr, MSG_KEYDOWN, 0x41, 0),
      "Com menu aberto e WantCaptureKeyboard, consome tecla");
  TEST_ASSERT(
      !dr2hook::OverlayManager::HandleWndProc(nullptr, MSG_MOUSEMOVE, 0, 0),
      "Com menu aberto mas sem WantCaptureMouse, nao consome mouse");

  // Caso 3: Menu aberto com WantCaptureMouse = true
  io.WantCaptureKeyboard = false;
  io.WantCaptureMouse = true;
  TEST_ASSERT(
      !dr2hook::OverlayManager::HandleWndProc(nullptr, MSG_KEYDOWN, 0x41, 0),
      "Com menu aberto mas sem WantCaptureKeyboard, nao consome tecla");
  TEST_ASSERT(
      dr2hook::OverlayManager::HandleWndProc(nullptr, MSG_MOUSEMOVE, 0, 0),
      "Com menu aberto e WantCaptureMouse, consome mouse");

  // Caso 4: Menu aberto mas ImGui nao quer captura
  io.WantCaptureKeyboard = false;
  io.WantCaptureMouse = false;
  TEST_ASSERT(
      !dr2hook::OverlayManager::HandleWndProc(nullptr, MSG_KEYDOWN, 0x41, 0),
      "Com menu aberto sem foco de captura, nao consome tecla");
  TEST_ASSERT(
      !dr2hook::OverlayManager::HandleWndProc(nullptr, MSG_MOUSEMOVE, 0, 0),
      "Com menu aberto sem foco de captura, nao consome mouse");

  dr2hook::OverlayManager::Shutdown();
}

// ---------------------------------------------------------------------------
// 7. Encerramento Limpo e Tolerancia a Reentrancia
// ---------------------------------------------------------------------------
void TestCleanShutdownAndReentrance() {
  std::cout << "[RUN] TestCleanShutdownAndReentrance..." << std::endl;

  for (int cycle = 0; cycle < 3; ++cycle) {
    dr2hook::OverlayManager::Initialize(nullptr, nullptr, nullptr);
    dr2hook::OverlayManager::AddNotification("Ciclo", 1.0f);
    dr2hook::OverlayManager::SetMenuVisible(true);
    dr2hook::OverlayManager::Render(nullptr);
    dr2hook::OverlayManager::Shutdown();

    TEST_ASSERT(!dr2hook::OverlayManager::IsInitialized(),
                "Desinicializado corretamente no ciclo");
    TEST_ASSERT(dr2hook::OverlayManager::GetNotificationCount() == 0,
                "Fila limpa no encerramento do ciclo");
    TEST_ASSERT(!dr2hook::OverlayManager::IsMenuVisible(),
                "Visibilidade redefinida no encerramento do ciclo");
  }
}

// ---------------------------------------------------------------------------
// 8. Renderização das Abas de Diagnóstico, Mods e Abas Dinâmicas
// ---------------------------------------------------------------------------
void TestTabbedUIRenderingAndTelemetry() {
  std::cout << "[RUN] TestTabbedUIRenderingAndTelemetry..." << std::endl;

  uintptr_t mockRig = 0x50000000;
  dr2hook::MockMemoryAccessor mockMem(0x4000, mockRig);
  dr2hook::MemoryScanner scanner(&mockMem);

  // Layout real da EGO Engine no rig simulado
  dr2hook::Vector3 pos{100.0f, 25.0f, -50.0f};
  dr2hook::Vector3 linVel{15.0f, 0.5f, 20.0f}; // ~25 m/s = 90 km/h
  dr2hook::Vector3 angVel{0.02f, 0.05f, -0.01f};
  dr2hook::Vector4 quat{0.0f, 0.7071f, 0.0f, 0.7071f}; // ~90 deg yaw
  float idleRpm = 1080.0f;
  float maxRpm = 5500.0f;
  float gears = 5.0f;

  mockMem.SetValue(mockRig + 0x2d0, pos);
  mockMem.SetValue(mockRig + 0x2e0, quat);
  mockMem.SetValue(mockRig + 0x320, linVel);
  mockMem.SetValue(mockRig + 0x330, angVel);
  mockMem.SetValue(mockRig + 0x8e8, idleRpm);
  mockMem.SetValue(mockRig + 0x918, maxRpm);
  mockMem.SetValue(mockRig + 0x8f4, gears);

  dr2hook::Player::Configure(&scanner, mockRig);

  dr2hook::VehicleTelemetryInfo vInfo;
  bool okVeh = dr2hook::Player::GetVehicleTelemetry(vInfo);
  TEST_ASSERT(okVeh, "GetVehicleTelemetry executado com sucesso");
  TEST_ASSERT(vInfo.isAnchored, "Veículo reportado como ancorado");
  TEST_ASSERT(vInfo.speedKmh > 80.0f && vInfo.speedKmh < 100.0f,
              "Velocidade calculada em ~90 km/h");
  TEST_ASSERT(vInfo.gear >= 1 && vInfo.gear <= 5, "Marcha ativa entre 1 e 5");
  TEST_ASSERT(vInfo.rpm >= vInfo.idleRpm, "RPM acima ou igual a marcha lenta");

  dr2hook::TrackTelemetryInfo tInfo;
  bool okTrack = dr2hook::Player::GetTrackTelemetry(tInfo);
  TEST_ASSERT(okTrack, "GetTrackTelemetry executado com sucesso");
  TEST_ASSERT(!tInfo.trackName.empty(), "Nome da pista preenchido");
  TEST_ASSERT(!tInfo.location.empty(), "Localização da pista preenchida");

  // Inicializar ModManager e carregar mods reais para teste da aba dinâmica
  dr2hook::ModManager::Initialize("mods");
  const auto &loadedMods = dr2hook::ModManager::GetLoadedMods();
  TEST_ASSERT(!loadedMods.empty(), "Mods carregados a partir de 'mods/'");

  dr2hook::OverlayManager::Initialize(nullptr, nullptr, nullptr);
  dr2hook::OverlayManager::SetMenuVisible(true);

  // Renderizar múltiplos frames para exercitar todo o pipeline do TabBar e abas
  for (int f = 0; f < 3; ++f) {
    dr2hook::OverlayManager::Render(nullptr);
  }

  ImDrawData *drawData = ImGui::GetDrawData();
  TEST_ASSERT(drawData != nullptr && drawData->CmdListsCount > 0,
              "Estruturas ImDrawData geradas para abas Diagnóstico, Mods e Dinâmicas");

  // Testar Hot-Reload de mods
  dr2hook::ModManager::ReloadMods("mods");
  TEST_ASSERT(!dr2hook::ModManager::GetLoadedMods().empty(),
              "ReloadMods preserva mods carregados");

  dr2hook::OverlayManager::Shutdown();
  dr2hook::ModManager::Shutdown();
  dr2hook::Player::Configure(nullptr, 0);
}

// ---------------------------------------------------------------------------
// Main Test Runner
// ---------------------------------------------------------------------------
int main() {
  std::cout << "====================================================="
            << std::endl;
  std::cout << "DR2Hook - Suíte de Testes do Overlay Dear ImGui (Fase 5)"
            << std::endl;
  std::cout << "====================================================="
            << std::endl;

  TestHeadlessInitAndShutdown();
  TestMenuVisibility();
  TestNotificationQueueAndExpiration();
  TestHeadlessRenderLoop();
  TestLuaUINotifyIntegration();
  TestInputCaptureFiltering();
  TestCleanShutdownAndReentrance();
  TestTabbedUIRenderingAndTelemetry();

  std::cout << "====================================================="
            << std::endl;
  std::cout << "Resultados dos Testes de Overlay:" << std::endl;
  std::cout << "  Total de asserções executadas: " << g_testsRun << std::endl;
  std::cout << "  Passaram: " << g_testsPassed << std::endl;
  std::cout << "  Falharam: " << g_testsFailed << std::endl;
  std::cout << "====================================================="
            << std::endl;

  return (g_testsFailed == 0) ? 0 : 1;
}
