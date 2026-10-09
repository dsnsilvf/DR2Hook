#include "dr2hook/core_api.h"
#include "dr2hook/cutscene_probe.h"
#include "dr2hook/free_camera.h"
#include "dr2hook/ghost_lab.h"
#include "dr2hook/ghost_trace.h"
#include "dr2hook/hotkeys.h"
#include "dr2hook/terminal_damage.h"
#include "dr2hook/logger.h"
#include "dr2hook/memory.h"
#include "dr2hook/net_dialog.h"
#include "dr2hook/remote_commands.h"
#include "dr2hook/physics_tick_harness.h"
#include "dr2hook/player.h"
#include "dr2hook/safety.h"
#include "dr2hook/savestate.h"
#include "dr2hook/load_view.h"
#include "dr2hook/intro_skip.h"
#include "dr2hook/live_link.h"
#include "dr2hook/session_audio.h"
#include "dr2hook/script/mod_manager.h"
#include "dr2hook/script/mod_menu.h"
#include "dr2hook/ui/overlay.h"

#include <d3d11.h>

#include <chrono>
#include <filesystem>
#include <fstream>

namespace {

dr2hook::DirectMemoryAccessor g_directAccessor;
dr2hook::MemoryScanner g_memoryScanner(&g_directAccessor);
bool g_notifyReload = false;

using ConsumeFn = int (*)();

// Inicialização rápida do editor (scripts/research/ring_deploy.py --quick):
// com dr2hook_quickload.ini recente na pasta do jogo, a tela fica preta do
// boot até a largada, e sem som (música, efeitos, o bipe da pista pronta). O
// arquivo é apagado ao ler; um velho (o jogo não abriu
// daquela vez) é ignorado para não escurecer um boot normal. Com
// "fundo=<caminho>" o terminal fica sobre a foto da pista.
constexpr auto kQuickLoadMaxAge = std::chrono::minutes(10);
constexpr ULONGLONG kLoadCoverTimeoutMs = 180000;
ULONGLONG g_loadCoverSince = 0;

void StartLoadCover() {
  namespace fs = std::filesystem;
  const fs::path marker = "dr2hook_quickload.ini";
  std::error_code ec;
  if (!fs::exists(marker, ec)) {
    return;
  }
  const auto age = fs::file_time_type::clock::now() -
                   fs::last_write_time(marker, ec);
  // "fundo=<caminho>": foto da pista para trás do terminal.
  std::string image;
  {
    std::ifstream in(marker);
    for (std::string line; std::getline(in, line);) {
      if (line.rfind("fundo=", 0) == 0) {
        image = line.substr(6);
        while (!image.empty() && (image.back() == '\r' || image.back() == ' ')) {
          image.pop_back();
        }
      }
    }
  }
  fs::remove(marker, ec);
  if (age > kQuickLoadMaxAge) {
    dr2hook::Logger::Info(
        "LoadCover: dr2hook_quickload.ini velho; ignorado e apagado.");
    return;
  }
  dr2hook::OverlayManager::SetLoadCoverImage(image);
  dr2hook::OverlayManager::SetLoadCover(true);
  dr2hook::LoadView::SetActive(true);
  dr2hook::SessionAudio::SetMuted(true);
  g_loadCoverSince = GetTickCount64();
  dr2hook::Logger::Info("LoadCover: tela preta ate a largada.");
}

void StopLoadCover(const char *why) {
  if (!dr2hook::OverlayManager::IsLoadCoverOn()) {
    return;
  }
  dr2hook::OverlayManager::SetLoadCover(false);
  dr2hook::LoadView::SetActive(false);
  dr2hook::SessionAudio::SetMuted(false);
  dr2hook::Logger::Info(std::string("LoadCover: tela liberada (") + why +
                        ").");
}

// O overlay por cima do quadro do jogo. Na tela preta, a LoadView mede o que o
// jogo desenhou neste quadro e entrega a cena (se houver) para o fundo do
// terminal; os draws do próprio overlay não contam.
void RenderOverlay(IDXGISwapChain *swapChain) {
  static bool tried = false;
  if (!tried && dr2hook::OverlayManager::IsLoadCoverOn() && swapChain != nullptr) {
    tried = true;
    ID3D11Device *device = nullptr;
    if (SUCCEEDED(swapChain->GetDevice(__uuidof(ID3D11Device),
                                       reinterpret_cast<void **>(&device))) &&
        device != nullptr) {
      ID3D11DeviceContext *context = nullptr;
      device->GetImmediateContext(&context);
      if (context != nullptr) {
        dr2hook::LoadView::Install(context);
        context->Release();
      }
      device->Release();
    }
  }
  dr2hook::LoadView::OnFrame(swapChain);
  float aspect = 1.0f;
  ID3D11ShaderResourceView *scene = dr2hook::LoadView::SceneView(&aspect);
  dr2hook::OverlayManager::SetLoadCoverScene(scene, aspect,
                                             dr2hook::LoadView::StatusLine(),
                                             dr2hook::LoadView::SceneExposure());
  dr2hook::LoadView::SetOwnDraws(true);
  dr2hook::OverlayManager::Render(swapChain);
  dr2hook::LoadView::SetOwnDraws(false);
}

template <typename Fn> Fn HostExport(const char *name) {
  const HMODULE host = GetModuleHandleA("dxgi.dll");
  return host == nullptr ? nullptr
                         : reinterpret_cast<Fn>(GetProcAddress(host, name));
}

bool ConsumePauseMenuRequest() {
  static const auto consume =
      HostExport<ConsumeFn>("Dr2Host_ConsumePauseMenuRequest");
  return consume != nullptr && consume() != 0;
}

bool ConsumeReloadModsRequest() {
  static const auto consume =
      HostExport<ConsumeFn>("Dr2Host_ConsumeReloadModsRequest");
  return consume != nullptr && consume() != 0;
}

// Eventos de especial enfileirados pela proxy (hooks rodam fora da thread de
// render); o Lua so e chamado daqui.
void DispatchStageEvents() {
  static const auto consume =
      HostExport<Dr2HostStageConsumeEventFn>("Dr2Host_StageConsumeEvent");
  dr2hook::Dr2StageEvent event{};
  while (consume != nullptr && consume(&event) != 0) {
    event.name[sizeof(event.name) - 1] = '\0';
    switch (event.kind) {
    case dr2hook::kDr2StageLoad:
      dr2hook::GhostLab::OnStageLoad();
      dr2hook::ModManager::DispatchStageLoad(event.name);
      break;
    case dr2hook::kDr2StageRoute:
      dr2hook::LoadView::NoteRoute(event.name);
      break;
    case dr2hook::kDr2StageCountdown:
      dr2hook::ModManager::DispatchCountdown(event.value);
      break;
    case dr2hook::kDr2StageStart:
      StopLoadCover("largada");
      dr2hook::LiveLink::OnStageStart();
      dr2hook::GhostLab::OnStageStart();
      dr2hook::ModManager::DispatchStageStart(event.name, event.value != 0);
      break;
    default:
      break;
    }
  }
}

// Cliques da tela nativa de mod vão para o Lua; o menu só é republicado
// quando uma opção ou a lista de mods muda.
void SyncNativeMenu() {
  static const auto publish =
      HostExport<Dr2HostMenuPublishFn>("Dr2Host_NativeMenuPublish");
  static const auto consume =
      HostExport<Dr2HostMenuConsumeEventFn>("Dr2Host_NativeMenuConsumeEvent");
  int modIndex = 0;
  int optionIndex = 0;
  int value = -1;
  while (consume != nullptr && consume(&modIndex, &optionIndex, &value) != 0) {
    if (modIndex >= 0 && optionIndex >= 0) {
      dr2hook::ModManager::DispatchMenuEvent(static_cast<size_t>(modIndex),
                                             static_cast<size_t>(optionIndex),
                                             value);
    }
  }
  if (publish == nullptr || !dr2hook::ModMenu::TakeDirty()) {
    return;
  }
  const std::vector<dr2hook::ModMenuEntry> entries =
      dr2hook::ModManager::MenuSnapshot();
  std::vector<std::vector<std::string>> names;
  std::vector<std::vector<const char *>> values;
  std::vector<std::vector<dr2hook::Dr2MenuOption>> options(entries.size());
  std::vector<dr2hook::Dr2MenuMod> mods(entries.size());
  for (const dr2hook::ModMenuEntry &entry : entries) {
    for (const dr2hook::ModOption &option : entry.options) {
      names.push_back(option.ValueNames());
    }
  }
  values.reserve(names.size());
  size_t next = 0;
  for (size_t i = 0; i < entries.size(); ++i) {
    for (const dr2hook::ModOption &option : entries[i].options) {
      std::vector<const char *> &texts = values.emplace_back();
      for (const std::string &name : names[next++]) {
        texts.push_back(name.c_str());
      }
      const int kind = option.type == dr2hook::ModOption::Type::Toggle
                           ? dr2hook::kDr2MenuToggle
                       : option.type == dr2hook::ModOption::Type::Choice
                           ? dr2hook::kDr2MenuChoice
                           : dr2hook::kDr2MenuButton;
      options[i].push_back({option.label.c_str(), texts.data(),
                            static_cast<int>(texts.size()),
                            static_cast<int>(option.ValueIndex()), kind,
                            option.description.c_str()});
    }
    mods[i] = {entries[i].name.c_str(), entries[i].description.c_str(),
               options[i].data(), static_cast<int>(options[i].size())};
  }
  publish(mods.data(), static_cast<int>(mods.size()));
}

int Core_Initialize(int truncateLog) {
  try {
    dr2hook::Logger::Init("dr2hook.log", truncateLog != 0);
    dr2hook::Logger::Info(truncateLog != 0
                              ? "DR2Hook Core v0.1.0 inicializando..."
                              : "DR2Hook Core recarregado em runtime.");

    dr2hook::SavestateManager::Initialize();
    dr2hook::SafetyGuard::Configure(&g_memoryScanner, 0);
    dr2hook::SafetyGuard::SetPermissiveMode(true);
    dr2hook::Player::Configure(&g_memoryScanner, 0);

    if (dr2hook::ModManager::Initialize()) {
      dr2hook::Logger::Info("ModManager inicializado com sucesso.");
    } else {
      dr2hook::Logger::Error("Falha ao inicializar ModManager.");
    }

    dr2hook::CutsceneProbe::Install(
        reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr)));
    dr2hook::FreeCamera::Install(
        reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr)));
    dr2hook::GhostLab::Install(
        reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr)));
    dr2hook::GhostTrace::Install(
        reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr)));
    dr2hook::TerminalDamage::Install(
        reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr)));
    dr2hook::NetDialog::Install(
        reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr)));
    dr2hook::IntroSkip::Install();
    dr2hook::LiveLink::Install();

    dr2hook::PhysicsTickHarness::LoadConfiguration();
    if (dr2hook::PhysicsTickHarness::IsInstrumentationEnabled()) {
      const uintptr_t gameBase = reinterpret_cast<uintptr_t>(
          GetModuleHandleA(nullptr));
      if (!dr2hook::PhysicsTickHarness::TryInstall(gameBase)) {
        dr2hook::Logger::Warn(
            "PhysicsTickHarness: nao instalado (prologo/endereco invalido).");
      }
    }

    g_notifyReload = truncateLog == 0;
    if (truncateLog != 0) {
      StartLoadCover();
    }
    return 1;
  } catch (...) {
    return 0;
  }
}

void Core_Shutdown() {
  try {
    StopLoadCover("core descarregado");
    dr2hook::IntroSkip::Shutdown();
    dr2hook::LiveLink::Shutdown();
    dr2hook::SessionAudio::Shutdown();
    dr2hook::LoadView::Shutdown();
    dr2hook::OverlayManager::Shutdown();
    dr2hook::FreeCamera::Shutdown();
    dr2hook::PhysicsTickHarness::Shutdown();
    dr2hook::CutsceneProbe::Shutdown();
    dr2hook::GhostTrace::Shutdown();
    dr2hook::GhostLab::Shutdown();
    dr2hook::TerminalDamage::Shutdown();
    dr2hook::NetDialog::Shutdown();
    dr2hook::SavestateManager::Shutdown();
    dr2hook::ModManager::Shutdown();
    dr2hook::Logger::Info("DR2Hook Core descarregado.");
    dr2hook::Logger::Shutdown();
  } catch (...) {
    dr2hook::Logger::Shutdown();
  }
}

void Core_OnFrame(IDXGISwapChain *swapChain, HWND hwnd, double deltaTime) {
  try {
    if (!dr2hook::OverlayManager::IsInitialized() && swapChain != nullptr) {
      ID3D11Device *device = nullptr;
      const HRESULT hr = swapChain->GetDevice(
          __uuidof(ID3D11Device), reinterpret_cast<void **>(&device));
      if (SUCCEEDED(hr) && device != nullptr) {
        ID3D11DeviceContext *context = nullptr;
        device->GetImmediateContext(&context);
        if (context != nullptr) {
          dr2hook::OverlayManager::Initialize(hwnd, device, context);
          context->Release();
        }
        device->Release();
      }
    }

    if (g_notifyReload && dr2hook::OverlayManager::IsInitialized()) {
      dr2hook::OverlayManager::AddNotification(
          "Native core reloaded. In-memory checkpoint was cleared.", 4.0f,
          dr2hook::ToastType::Info);
      g_notifyReload = false;
    }

    if (dr2hook::Player::GetVehicleAddress() == 0) {
      dr2hook::Player::ResolveVehicleAddress(
          reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr)));
    }
    dr2hook::GhostLab::Update();
    {
      std::string notice;
      if (dr2hook::GhostLab::TakeSpawnNotice(notice) &&
          dr2hook::OverlayManager::IsInitialized()) {
        dr2hook::OverlayManager::AddNotification(notice, 3.0f,
                                                 dr2hook::ToastType::Info);
      }
    }
    dr2hook::TerminalDamage::Update();
    dr2hook::RemoteCommands::Poll(hwnd);
    DispatchStageEvents();
    if (dr2hook::OverlayManager::IsLoadCoverOn() &&
        GetTickCount64() - g_loadCoverSince > kLoadCoverTimeoutMs) {
      StopLoadCover("3 min sem largada");
    }
    dr2hook::ModManager::DispatchTick(deltaTime);

    if (ConsumePauseMenuRequest()) {
      dr2hook::OverlayManager::SetMenuVisible(true);
    }
    if (ConsumeReloadModsRequest()) {
      dr2hook::ModManager::ReloadMods();
      if (dr2hook::OverlayManager::IsInitialized()) {
        dr2hook::OverlayManager::AddNotification("Lua mods reloaded.", 3.0f,
                                                 dr2hook::ToastType::Info);
      }
    }
    SyncNativeMenu();

    dr2hook::FreeCamera::OnFrame(hwnd);
    dr2hook::LiveLink::OnFrame();
    if (dr2hook::OverlayManager::IsInitialized()) {
      RenderOverlay(swapChain);
    }
  } catch (...) {
    dr2hook::Logger::Error("Excecao em Dr2Core::OnFrame.");
  }
}

int Core_OnWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  try {
    if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) &&
        (wParam == VK_INSERT || wParam == 0x2D)) {
      dr2hook::OverlayManager::ToggleMenu();
      return 1;
    }

    // Tecla fisica tem scan code; o `key` remoto (PostMessage com lParam 1) nao.
    const bool physicalKey = ((lParam >> 16) & 0xFF) != 0;

    // F9 desligado no Debug Mode: a tecla fisica so desliga a camera, nunca liga.
    const bool f9Blocked = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && wParam == VK_F9 && physicalKey &&
                           !dr2hook::Hotkeys::freeCamera.load() && !dr2hook::FreeCamera::Enabled();
    if (!f9Blocked && dr2hook::FreeCamera::OnWndProc(hwnd, msg, wParam, lParam) != 0) {
      return 1;
    }

    // Esc no dano terminal: pede a pausa (nao consome a tecla).
    if (msg == WM_KEYDOWN && wParam == VK_ESCAPE && (lParam & (1 << 30)) == 0) {
      dr2hook::TerminalDamage::RequestPause();
    }

    // F7: teste de limite, mais uma copia do fantasma por toque (log em
    // "GhostLab[limite]"). So com dr2hook_ghost_cars.txt; senao vale o F7 do checkpoint.
    if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && wParam == VK_F7 &&
        (lParam & (1 << 30)) == 0 && dr2hook::GhostLab::TestModeActive()) {
      dr2hook::GhostLab::SpawnClone();
      return 1;
    }

    // F6: pausa/despausa so os clones do fantasma (teste de clones; so com
    // dr2hook_ghost_cars.txt, senao vale o F6 do checkpoint).
    if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && wParam == VK_F6 &&
        (lParam & (1 << 30)) == 0 && dr2hook::GhostLab::TestModeActive()) {
      const bool paused = dr2hook::GhostLab::ToggleClonePause();
      if (dr2hook::OverlayManager::IsInitialized()) {
        dr2hook::OverlayManager::AddNotification(
            paused ? "Ghost cars paused." : "Ghost cars resumed.", 2.0f,
            dr2hook::ToastType::Info);
      }
      return 1;
    }

    // F11: insta crash, destroi o carro na hora (dano terminal).
    if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && wParam == VK_F11 &&
        (lParam & (1 << 30)) == 0 && (!physicalKey || dr2hook::Hotkeys::instaCrash.load())) {
      using R = dr2hook::TerminalDamage::Result;
      const R r = dr2hook::TerminalDamage::Crash();
      if (dr2hook::OverlayManager::IsInitialized()) {
        const char *text = "Car destroyed.";
        auto type = dr2hook::ToastType::Info;
        if (r == R::Blocked) { text = "Crash blocked: online event."; type = dr2hook::ToastType::Error; }
        else if (r == R::NoController) { text = "Crash unavailable: not in a stage."; type = dr2hook::ToastType::Error; }
        else if (r == R::BadChain) { text = "Crash failed: unexpected game state."; type = dr2hook::ToastType::Error; }
        else if (r == R::AlreadyDown) { text = "Car already destroyed."; }
        dr2hook::OverlayManager::AddNotification(text, 2.0f, type);
      }
      return 1;
    }

    if (dr2hook::OverlayManager::HandleWndProc(hwnd, msg, wParam, lParam)) {
      return 1;
    }

    if (msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYDOWN ||
        msg == WM_SYSKEYUP) {
      const UINT vkCode = static_cast<UINT>(wParam);
      const bool isDown = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
      dr2hook::SavestateManager::OnKeyAction(vkCode, isDown);
      if (isDown) {
        dr2hook::ModManager::DispatchKeyDown(vkCode);
      }
    }
    return 0;
  } catch (...) {
    dr2hook::Logger::Error("Excecao em Dr2Core::OnWndProc.");
    return 0;
  }
}

dr2hook::Dr2CoreApi g_api = {
    dr2hook::kCoreAbiVersion, Core_Initialize, Core_Shutdown, Core_OnFrame,
    Core_OnWndProc,
};

} // namespace

DR2HOOK_API dr2hook::Dr2CoreApi *Dr2Core_GetApi() { return &g_api; }

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(hModule);
  }
  return TRUE;
}
