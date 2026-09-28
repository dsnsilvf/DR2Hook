#include "dr2hook/ui/overlay.h"
#include "dr2hook/logger.h"
#include "dr2hook/player.h"
#include "dr2hook/safety.h"
#include "dr2hook/savestate.h"
#include "dr2hook/script/mod_manager.h"

#include "imgui.h"

#if defined(_WIN32) || defined(DR2HOOK_USE_BACKENDS)
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#endif

#include <algorithm>
#include <cmath>
#include <mutex>
#include <string>
#include <vector>

namespace {

bool g_initialized = false;
bool g_showMenu = false;
HWND g_hWnd = nullptr;
ID3D11Device *g_device = nullptr;
ID3D11DeviceContext *g_deviceContext = nullptr;
ID3D11RenderTargetView *g_renderTargetView = nullptr;

std::mutex g_notificationMutex;
std::vector<dr2hook::ToastNotification> g_notifications;

} // namespace

namespace dr2hook {

#if defined(_WIN32) || defined(DR2HOOK_USE_BACKENDS)
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd,
                                                             UINT msg,
                                                             WPARAM wParam,
                                                             LPARAM lParam);
#endif

bool OverlayManager::Initialize(HWND hWnd, ID3D11Device *pDevice,
                                ID3D11DeviceContext *pContext) {
  if (g_initialized) {
    return true;
  }

  if (ImGui::GetCurrentContext() == nullptr) {
    ImGui::CreateContext();
  }

  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.IniFilename = nullptr;
  ImGui::StyleColorsDark();

  if (!io.Fonts->IsBuilt()) {
    unsigned char *pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
  }

  g_hWnd = hWnd;
  g_device = pDevice;
  g_deviceContext = pContext;
  if (g_device != nullptr) {
    g_device->AddRef();
  }
  if (g_deviceContext != nullptr) {
    g_deviceContext->AddRef();
  }

#if defined(_WIN32) || defined(DR2HOOK_USE_BACKENDS)
  if (g_hWnd != nullptr) {
    ImGui_ImplWin32_Init(g_hWnd);
  }
  if (g_device != nullptr && g_deviceContext != nullptr) {
    ImGui_ImplDX11_Init(g_device, g_deviceContext);
  }
#endif

  g_initialized = true;
  Logger::Info("OverlayManager inicializado com sucesso.");
  return true;
}

void OverlayManager::Shutdown() {
  if (!g_initialized) {
    return;
  }

#if defined(_WIN32) || defined(DR2HOOK_USE_BACKENDS)
  if (g_renderTargetView != nullptr) {
    g_renderTargetView->Release();
    g_renderTargetView = nullptr;
  }
  ImGui_ImplDX11_Shutdown();
  ImGui_ImplWin32_Shutdown();
#endif

  if (g_renderTargetView != nullptr) {
    g_renderTargetView->Release();
    g_renderTargetView = nullptr;
  }
  if (g_deviceContext != nullptr) {
    g_deviceContext->Release();
    g_deviceContext = nullptr;
  }
  if (g_device != nullptr) {
    g_device->Release();
    g_device = nullptr;
  }
  g_hWnd = nullptr;
  g_showMenu = false;

  {
    std::lock_guard<std::mutex> lock(g_notificationMutex);
    g_notifications.clear();
  }

  if (ImGui::GetCurrentContext() != nullptr) {
    ImGui::DestroyContext();
  }

  g_initialized = false;
  Logger::Info("OverlayManager encerrado com sucesso.");
}

bool OverlayManager::IsInitialized() { return g_initialized; }

void OverlayManager::ToggleMenu() { g_showMenu = !g_showMenu; }

bool OverlayManager::IsMenuVisible() { return g_showMenu; }

void OverlayManager::SetMenuVisible(bool visible) { g_showMenu = visible; }

void OverlayManager::AddNotification(std::string message, float duration,
                                     ToastType type) {
  std::lock_guard<std::mutex> lock(g_notificationMutex);
  g_notifications.push_back(
      ToastNotification{std::move(message), duration, 0.0f, type});
}

void OverlayManager::UpdateNotifications(float deltaTime) {
  std::lock_guard<std::mutex> lock(g_notificationMutex);
  for (auto &toast : g_notifications) {
    toast.elapsed += deltaTime;
  }
  g_notifications.erase(std::remove_if(g_notifications.begin(),
                                       g_notifications.end(),
                                       [](const ToastNotification &t) {
                                         return t.elapsed >= t.duration;
                                       }),
                        g_notifications.end());
}

size_t OverlayManager::GetNotificationCount() {
  std::lock_guard<std::mutex> lock(g_notificationMutex);
  return g_notifications.size();
}

std::vector<ToastNotification> OverlayManager::GetActiveNotifications() {
  std::lock_guard<std::mutex> lock(g_notificationMutex);
  return g_notifications;
}

bool OverlayManager::HandleWndProc(HWND hWnd, UINT msg, WPARAM wParam,
                                   LPARAM lParam) {
  if (!g_initialized || !g_showMenu) {
    return false;
  }

#if defined(_WIN32) || defined(DR2HOOK_USE_BACKENDS)
  ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
#endif

  ImGuiIO &io = ImGui::GetIO();
  if (io.WantCaptureMouse && (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST)) {
    return true;
  }
  if (io.WantCaptureKeyboard && (msg >= WM_KEYFIRST && msg <= WM_KEYLAST)) {
    return true;
  }
  return false;
}

void OverlayManager::RenderUI() {
  // 1. HUD Toast Overlay
  float dt = ImGui::GetIO().DeltaTime;
  if (dt > 0.0f) {
    UpdateNotifications(dt);
  }

  std::vector<ToastNotification> activeToasts;
  {
    std::lock_guard<std::mutex> lock(g_notificationMutex);
    activeToasts = g_notifications;
  }

  if (!activeToasts.empty()) {
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImVec2 workPos = viewport->WorkPos;
    ImVec2 workSize = viewport->WorkSize;
    float startX = workPos.x + workSize.x - 20.0f;
    float currentY = workPos.y + 20.0f;

    for (size_t i = 0; i < activeToasts.size(); ++i) {
      const auto &toast = activeToasts[i];

      float alpha = 1.0f;
      constexpr float fadeTime = 0.4f;
      if (toast.elapsed < fadeTime) {
        alpha = toast.elapsed / fadeTime;
      } else if (toast.duration - toast.elapsed < fadeTime) {
        alpha = (toast.duration - toast.elapsed) / fadeTime;
      }
      alpha = std::clamp(alpha, 0.05f, 1.0f);

      ImVec4 badgeColor;
      const char *badgeText = "[INFO]";
      if (toast.type == ToastType::Warning) {
        badgeColor = ImVec4(1.0f, 0.75f, 0.0f, alpha);
        badgeText = "[AVISO]";
      } else if (toast.type == ToastType::Error) {
        badgeColor = ImVec4(1.0f, 0.25f, 0.25f, alpha);
        badgeText = "[ERRO]";
      } else {
        badgeColor = ImVec4(0.2f, 0.7f, 1.0f, alpha);
        badgeText = "[INFO]";
      }

      ImGui::SetNextWindowPos(ImVec2(startX, currentY), ImGuiCond_Always,
                              ImVec2(1.0f, 0.0f));
      ImGui::SetNextWindowBgAlpha(0.85f * alpha);

      std::string winName = "##Toast_" + std::to_string(i);
      ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                               ImGuiWindowFlags_AlwaysAutoResize |
                               ImGuiWindowFlags_NoSavedSettings |
                               ImGuiWindowFlags_NoFocusOnAppearing |
                               ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;

      if (ImGui::Begin(winName.c_str(), nullptr, flags)) {
        ImGui::TextColored(badgeColor, "%s", badgeText);
        ImGui::SameLine();
        ImVec4 textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        textColor.w = alpha;
        ImGui::TextColored(textColor, "%s", toast.message.c_str());

        currentY += ImGui::GetWindowHeight() + 8.0f;
      }
      ImGui::End();
    }
  }

  // 2. Menu Principal In-Game (DR2Hook Overlay)
  if (g_showMenu) {
    ImGui::SetNextWindowSize(ImVec2(480, 420), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("DR2Hook Overlay", &g_showMenu)) {
      // Cabeçalho com logo/versão
      ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "DR2Hook v%s",
                         DR2HOOK_VERSION);
      ImGui::Separator();

      // Status do Fair Play / Anti-Cheat
      bool canWrite = SafetyGuard::CanWriteState();
      if (canWrite) {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f),
                           "● Status Fair Play: Treino Permitido");
      } else {
        ImGui::TextColored(
            ImVec4(1.0f, 0.25f, 0.25f, 1.0f),
            "● Status Fair Play: Sessão Competitiva / Escrita Bloqueada");
      }

      bool permissive = SafetyGuard::IsPermissiveMode();
      if (ImGui::Checkbox("Modo Treino (Liberar Savestate em Time Trial)",
                          &permissive)) {
        SafetyGuard::SetPermissiveMode(permissive);
      }
      ImGui::Separator();

      // Gerenciador de Mods
      if (ImGui::CollapsingHeader("Gerenciador de Mods",
                                  ImGuiTreeNodeFlags_DefaultOpen)) {
        const auto &mods = ModManager::GetLoadedMods();
        if (mods.empty()) {
          ImGui::TextDisabled("Nenhum mod carregado.");
        } else {
          if (ImGui::BeginTable("ModsTable", 4,
                                ImGuiTableFlags_Borders |
                                    ImGuiTableFlags_RowBg |
                                    ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Ativo", ImGuiTableColumnFlags_WidthFixed,
                                    45.0f);
            ImGui::TableSetupColumn("Nome", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed,
                                    110.0f);
            ImGui::TableSetupColumn("Versão", ImGuiTableColumnFlags_WidthFixed,
                                    65.0f);
            ImGui::TableHeadersRow();

            for (size_t i = 0; i < mods.size(); ++i) {
              const auto &mod = mods[i];
              ImGui::TableNextRow();

              ImGui::TableSetColumnIndex(0);
              ImGui::PushID(static_cast<int>(i));
              bool enabled = mod.enabled;
              if (ImGui::Checkbox("##enabled", &enabled)) {
                const_cast<ModInstance &>(mod).enabled = enabled;
              }
              ImGui::PopID();

              ImGui::TableSetColumnIndex(1);
              ImGui::TextUnformatted(mod.name.c_str());

              ImGui::TableSetColumnIndex(2);
              ImGui::TextDisabled("%s", mod.id.c_str());

              ImGui::TableSetColumnIndex(3);
              ImGui::TextDisabled("v%s", mod.version.c_str());
            }
            ImGui::EndTable();
          }
        }
      }

      // Painel de Telemetria e Savestate
      if (ImGui::CollapsingHeader("Telemetria e Savestate",
                                  ImGuiTreeNodeFlags_DefaultOpen)) {
        uintptr_t vehAddr = Player::GetVehicleAddress();
        if (vehAddr != 0) {
          ImGui::Text("Veículo: Ancorado em 0x%llX",
                      static_cast<unsigned long long>(vehAddr));
          CarState state{};
          if (Player::CaptureState(state)) {
            float speedMs =
                std::sqrt(state.linearVelocity.x * state.linearVelocity.x +
                          state.linearVelocity.y * state.linearVelocity.y +
                          state.linearVelocity.z * state.linearVelocity.z);
            float speedKmh = speedMs * 3.6f;
            ImGui::Text("Velocidade: %.1f km/h", speedKmh);
            ImGui::Text("Posição: (X: %.2f, Y: %.2f, Z: %.2f)",
                        state.position.x, state.position.y, state.position.z);
          } else {
            ImGui::TextDisabled("Telemetria indisponível");
          }
        } else {
          ImGui::TextDisabled("Aguardando spawn do carro na pista...");
        }

        if (ImGui::Button("Re-escanear Veículo")) {
#if defined(_WIN32)
          Player::ResolveVehicleAddress(
              reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr)));
#else
          Player::ResolveVehicleAddress(0x140000000);
#endif
        }

        ImGui::Spacing();
        if (ImGui::Button("Salvar Checkpoint (F5)")) {
          SavestateManager::OnKeyAction(0x74, true); // VK_F5
        }
        ImGui::SameLine();
        if (ImGui::Button("Restaurar Checkpoint (F6)")) {
          SavestateManager::OnKeyAction(0x75, true); // VK_F6
        }

        if (SavestateManager::HasSavedState()) {
          ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f),
                             "Checkpoint salvo disponível.");
        } else {
          ImGui::TextDisabled("Nenhum checkpoint salvo.");
        }
      }
    }
    ImGui::End();
  }
}

void OverlayManager::Render(IDXGISwapChain *pSwapChain) {
  if (!g_initialized) {
    return;
  }

#if defined(_WIN32) || defined(DR2HOOK_USE_BACKENDS)
  if (pSwapChain != nullptr) {
    if (g_renderTargetView == nullptr && g_device != nullptr) {
      ID3D11Texture2D *pBackBuffer = nullptr;
      HRESULT hr =
          pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                reinterpret_cast<void **>(&pBackBuffer));
      if (SUCCEEDED(hr) && pBackBuffer != nullptr) {
        g_device->CreateRenderTargetView(pBackBuffer, nullptr,
                                         &g_renderTargetView);
        pBackBuffer->Release();
      }
    }

    if (g_renderTargetView != nullptr && g_deviceContext != nullptr) {
      g_deviceContext->OMSetRenderTargets(1, &g_renderTargetView, nullptr);
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
  }
#endif

  ImGuiIO &io = ImGui::GetIO();
  if (io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f) {
    io.DisplaySize = ImVec2(1920.0f, 1080.0f);
  }
  if (!io.Fonts->IsBuilt()) {
    unsigned char *pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
  }

  ImGui::NewFrame();
  RenderUI();
  ImGui::Render();

#if defined(_WIN32) || defined(DR2HOOK_USE_BACKENDS)
  if (pSwapChain != nullptr && g_renderTargetView != nullptr) {
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
  }
#endif
}

} // namespace dr2hook
