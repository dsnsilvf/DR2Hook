#include "dr2hook/ui/overlay.h"
#include "dr2hook/logger.h"
#include "dr2hook/player.h"
#include "dr2hook/safety.h"
#include "dr2hook/savestate.h"
#include "dr2hook/script/lua_engine.h"
#include "dr2hook/script/mod_manager.h"

#include "lua.h"
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
    ImGui::SetNextWindowSize(ImVec2(660, 540), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("DR2Hook Overlay - DiRT Rally 2.0", &g_showMenu)) {
      // Cabeçalho superior: Identidade e Controle de Fair Play
      ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "DR2Hook v%s",
                         DR2HOOK_VERSION);
      ImGui::SameLine();
      ImGui::TextDisabled("| Telemetria, Savestate & Gerenciador de Mods");

      ImGui::TextColored(
          ImVec4(0.2f, 1.0f, 0.4f, 1.0f),
          "● Modo Estritamente Offline (Racenet Bloqueada - Fair Play Garantido)");
      ImGui::SameLine();
      if (ImGui::SmallButton("Re-escanear Carro")) {
#if defined(_WIN32)
        Player::ResolveVehicleAddress(
            reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr)));
#else
        Player::ResolveVehicleAddress(0x140000000);
#endif
      }

      ImGui::Separator();

      // Navegação por Abas
      if (ImGui::BeginTabBar("DR2HookTabBar", ImGuiTabBarFlags_None)) {

        // ===================================================================
        // ABA 1: DIAGNÓSTICO
        // ===================================================================
        if (ImGui::BeginTabItem("Diagnóstico")) {
          VehicleTelemetryInfo vInfo;
          Player::GetVehicleTelemetry(vInfo);

          TrackTelemetryInfo tInfo;
          Player::GetTrackTelemetry(tInfo);

          CarState state{};
          bool hasState = Player::CaptureState(state);

          // Card 1: Carro
          if (ImGui::CollapsingHeader("Carro",
                                      ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Modelo: ");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 1.0f, 1.0f), "%s",
                               vInfo.model.c_str());

            ImGui::SameLine(360.0f);
            ImGui::Text("Classe: ");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.3f, 1.0f), "%s",
                               vInfo.category.c_str());

            ImGui::Text("Estado: ");
            ImGui::SameLine();
            if (vInfo.isAnchored) {
              ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.3f, 1.0f), "%s",
                                 vInfo.state.c_str());
            } else {
              ImGui::TextColored(ImVec4(0.8f, 0.4f, 0.2f, 1.0f), "%s",
                                 vInfo.state.c_str());
            }

            ImGui::SameLine(360.0f);
            uintptr_t vehAddr = Player::GetVehicleAddress();
            if (vehAddr != 0) {
              ImGui::TextDisabled("Rig: 0x%llX",
                                  static_cast<unsigned long long>(vehAddr));
            } else {
              ImGui::TextDisabled("Rig: Desconectado");
            }

            ImGui::Spacing();

            // Velocidade em grande destaque visual
            ImGui::Text("Velocidade:");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.95f, 0.1f, 1.0f),
                               "[ %.1f km/h ]", vInfo.speedKmh);
            ImGui::SameLine();
            ImGui::TextDisabled("(%.1f mph)", vInfo.speedMph);

            ImGui::SameLine(360.0f);
            ImGui::Text("Aceleração:");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "%.2f G",
                               vInfo.accelerationG);

            // Marcha e RPM
            ImGui::Text("Marcha: ");
            ImGui::SameLine();
            if (vInfo.gear > 0) {
              ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.5f, 1.0f), "%d / %d",
                                 vInfo.gear, vInfo.forwardGears);
            } else {
              ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "N");
            }

            ImGui::SameLine(200.0f);
            ImGui::Text("RPM: ");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%.0f RPM",
                               vInfo.rpm);
            ImGui::SameLine();
            ImGui::TextDisabled("(Lenta: %.0f | Corte: %.0f)", vInfo.idleRpm,
                                vInfo.maxPowerRpm);

            // Barra gráfica de RPM do motor
            float rpmRatio = 0.0f;
            if (vInfo.maxPowerRpm > 0.0f) {
              rpmRatio = std::clamp(vInfo.rpm / (vInfo.maxPowerRpm * 1.25f),
                                    0.0f, 1.0f);
            }
            char rpmBuf[32];
            std::snprintf(rpmBuf, sizeof(rpmBuf), "%.0f RPM", vInfo.rpm);

            if (rpmRatio > 0.85f) {
              ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                                    ImVec4(0.95f, 0.2f, 0.2f, 0.9f));
            } else if (rpmRatio > 0.70f) {
              ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                                    ImVec4(1.0f, 0.75f, 0.1f, 0.9f));
            } else {
              ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                                    ImVec4(0.2f, 0.85f, 0.4f, 0.9f));
            }
            ImGui::ProgressBar(rpmRatio, ImVec2(-1.0f, 15.0f), rpmBuf);
            ImGui::PopStyleColor();
          }

          ImGui::Spacing();

          // Card 2: Track / Sessão
          if (ImGui::CollapsingHeader("Track / Sessão",
                                      ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Pista: ");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 1.0f, 1.0f), "%s",
                               tInfo.trackName.c_str());

            ImGui::Text("Localização: ");
            ImGui::SameLine();
            ImGui::TextUnformatted(tInfo.location.c_str());

            ImGui::Text("Superfície: ");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.85f, 0.75f, 0.55f, 1.0f), "%s",
                               tInfo.surface.c_str());

            ImGui::SameLine(360.0f);
            ImGui::Text("Condições: ");
            ImGui::SameLine();
            ImGui::TextUnformatted(tInfo.conditions.c_str());

            ImGui::Text("Modo da Sessão: ");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s",
                               tInfo.sessionState.c_str());
          }

          ImGui::Spacing();

          // Card 3: Telemetria Detalhada
          if (ImGui::CollapsingHeader("Telemetria",
                                      ImGuiTreeNodeFlags_DefaultOpen)) {
            if (hasState) {
              // Posição mundial
              ImGui::Text("Posição Mundial (X, Y, Z):");
              ImGui::SameLine();
              ImGui::TextColored(ImVec4(0.3f, 0.9f, 1.0f, 1.0f),
                                 "X: %.2f  |  Y: %.2f  |  Z: %.2f",
                                 state.position.x, state.position.y,
                                 state.position.z);

              // Orientação: Cálculo de Euler (Pitch, Roll, Yaw) a partir do Quaternion
              float qNormSq = state.quaternion.x * state.quaternion.x +
                              state.quaternion.y * state.quaternion.y +
                              state.quaternion.z * state.quaternion.z +
                              state.quaternion.w * state.quaternion.w;
              float roll = 0.0f, pitch = 0.0f, yaw = 0.0f;
              if (qNormSq > 0.001f) {
                float sinr_cosp = 2.0f * (state.quaternion.w * state.quaternion.x +
                                          state.quaternion.y * state.quaternion.z);
                float cosr_cosp = 1.0f - 2.0f * (state.quaternion.x * state.quaternion.x +
                                                state.quaternion.y * state.quaternion.y);
                roll = std::atan2(sinr_cosp, cosr_cosp) * (180.0f / 3.14159265f);

                float sinp = 2.0f * (state.quaternion.w * state.quaternion.y -
                                     state.quaternion.z * state.quaternion.x);
                if (std::abs(sinp) >= 1.0f) {
                  pitch = std::copysign(90.0f, sinp);
                } else {
                  pitch = std::asin(sinp) * (180.0f / 3.14159265f);
                }

                float siny_cosp = 2.0f * (state.quaternion.w * state.quaternion.z +
                                          state.quaternion.x * state.quaternion.y);
                float cosy_cosp = 1.0f - 2.0f * (state.quaternion.y * state.quaternion.y +
                                                state.quaternion.z * state.quaternion.z);
                yaw = std::atan2(siny_cosp, cosy_cosp) * (180.0f / 3.14159265f);
              }

              ImGui::Text("Orientação (Euler):");
              ImGui::SameLine();
              ImGui::TextColored(ImVec4(0.9f, 0.7f, 1.0f, 1.0f),
                                 "Pitch: %+.1f°  |  Roll: %+.1f°  |  Yaw: %+.1f°",
                                 pitch, roll, yaw);

              ImGui::Text("Quaternion:");
              ImGui::SameLine();
              ImGui::TextDisabled("(x: %.3f, y: %.3f, z: %.3f, w: %.3f)",
                                  state.quaternion.x, state.quaternion.y,
                                  state.quaternion.z, state.quaternion.w);

              // Vetor Linear e Angular
              ImGui::Text("Velocidade Linear (m/s):");
              ImGui::SameLine();
              ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.6f, 1.0f),
                                 "Vx: %+.2f  |  Vy: %+.2f  |  Vz: %+.2f",
                                 state.linearVelocity.x, state.linearVelocity.y,
                                 state.linearVelocity.z);

              ImGui::Text("Velocidade Angular (rad/s):");
              ImGui::SameLine();
              ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f),
                                 "Wx: %+.3f  |  Wy: %+.3f  |  Wz: %+.3f",
                                 state.angularVelocity.x,
                                 state.angularVelocity.y,
                                 state.angularVelocity.z);

              // Tabela das 4 Rodas e Suspensão
              ImGui::Spacing();
              ImGui::Text("Suspensão & Contato das Rodas:");
              if (ImGui::BeginTable("WheelsTelemetryTable", 4,
                                    ImGuiTableFlags_Borders |
                                        ImGuiTableFlags_RowBg |
                                        ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Diant. Esq. (FL)");
                ImGui::TableSetupColumn("Diant. Dir. (FR)");
                ImGui::TableSetupColumn("Tras. Esq. (RL)");
                ImGui::TableSetupColumn("Tras. Dir. (RR)");
                ImGui::TableHeadersRow();

                // Linha de compressão
                ImGui::TableNextRow();
                for (int i = 0; i < 4; ++i) {
                  ImGui::TableSetColumnIndex(i);
                  float compPercent =
                      state.wheels[i].suspensionCompression * 100.0f;
                  ImGui::Text("Comp: %.1f%%", compPercent);
                }

                // Linha de contato com o solo
                ImGui::TableNextRow();
                for (int i = 0; i < 4; ++i) {
                  ImGui::TableSetColumnIndex(i);
                  if (state.wheels[i].inContact) {
                    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f),
                                       "● No Solo");
                  } else {
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                                       "○ No Ar");
                  }
                }

                ImGui::EndTable();
              }
            } else {
              ImGui::TextDisabled("Aguardando spawn do carro para leitura de "
                                  "telemetria em tempo real...");
            }
          }

          ImGui::EndTabItem();
        }

        // ===================================================================
        // ABA 2: MODS
        // ===================================================================
        if (ImGui::BeginTabItem("Mods")) {
          ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f),
                             "Gerenciador de Mods & Extensões Lua");
          ImGui::SameLine();
          if (ImGui::Button("Recarregar Scripts (Hot-Reload)")) {
            ModManager::ReloadMods();
          }

          ImGui::Separator();

          const auto &mods = ModManager::GetLoadedMods();
          static int selectedModIndex = 0;

          if (mods.empty()) {
            ImGui::TextDisabled(
                "Nenhum mod carregado no diretório 'mods/'.");
          } else {
            if (ImGui::BeginTable("ModsManagerTable", 5,
                                  ImGuiTableFlags_Borders |
                                      ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_SizingStretchProp)) {
              ImGui::TableSetupColumn("Ativo",
                                      ImGuiTableColumnFlags_WidthFixed, 45.0f);
              ImGui::TableSetupColumn("Nome",
                                      ImGuiTableColumnFlags_WidthStretch);
              ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed,
                                      120.0f);
              ImGui::TableSetupColumn("Versão",
                                      ImGuiTableColumnFlags_WidthFixed, 65.0f);
              ImGui::TableSetupColumn("Autor",
                                      ImGuiTableColumnFlags_WidthFixed, 110.0f);
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
                bool isSelected = (selectedModIndex == static_cast<int>(i));
                if (ImGui::Selectable(mod.name.c_str(), isSelected,
                                      ImGuiSelectableFlags_SpanAllColumns)) {
                  selectedModIndex = static_cast<int>(i);
                }

                ImGui::TableSetColumnIndex(2);
                ImGui::TextDisabled("%s", mod.id.c_str());

                ImGui::TableSetColumnIndex(3);
                ImGui::TextDisabled("v%s", mod.version.c_str());

                ImGui::TableSetColumnIndex(4);
                ImGui::TextDisabled("%s", mod.author.c_str());
              }
              ImGui::EndTable();
            }

            // Card de detalhes do mod selecionado
            if (selectedModIndex >= 0 &&
                selectedModIndex < static_cast<int>(mods.size())) {
              const auto &selMod = mods[selectedModIndex];
              ImGui::Spacing();
              if (ImGui::CollapsingHeader("Detalhes do Mod",
                                          ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "%s",
                                   selMod.name.c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("(%s) v%s por %s", selMod.id.c_str(),
                                    selMod.version.c_str(),
                                    selMod.author.c_str());

                ImGui::TextWrapped("%s", selMod.description.c_str());
                ImGui::Spacing();
                ImGui::Text("Script Principal: %s",
                            selMod.mainScriptPath.c_str());
                ImGui::Text("Diretório: %s", selMod.directoryPath.c_str());

                ImGui::Text("Callbacks Registrados:");
                ImGui::BulletText("onInit: %s",
                                  (selMod.refOnInit != LUA_NOREF) ? "Sim" : "Não");
                ImGui::BulletText("onTick: %s",
                                  (selMod.refOnTick != LUA_NOREF) ? "Sim" : "Não");
                ImGui::BulletText("onKeyDown: %s",
                                  (selMod.refOnKeyDown != LUA_NOREF) ? "Sim"
                                                                     : "Não");
                ImGui::BulletText("onStageStart: %s",
                                  (selMod.refOnStageStart != LUA_NOREF)
                                      ? "Sim"
                                      : "Não");
                ImGui::BulletText("onRenderUI: %s",
                                  (selMod.refOnRenderUI != LUA_NOREF) ? "Sim"
                                                                      : "Não");
              }
            }
          }

          // Estatísticas do Motor Lua
          ImGui::Spacing();
          if (ImGui::CollapsingHeader("Estatísticas do Motor Lua",
                                      ImGuiTreeNodeFlags_DefaultOpen)) {
            lua_State *L = LuaEngine::GetState();
            int memKb = (L != nullptr) ? lua_gc(L, LUA_GCCOUNT, 0) : 0;
            ImGui::Text("Memória Lua Alocada: %d KB", memKb);
            ImGui::Text("Total de Mods Carregados: %zu", mods.size());
          }

          ImGui::EndTabItem();
        }

        // ===================================================================
        // ABAS DINÂMICAS: CADA MOD ATIVO GANHA SUA PRÓPRIA ABA
        // ===================================================================
        const auto &activeMods = ModManager::GetLoadedMods();
        for (size_t i = 0; i < activeMods.size(); ++i) {
          const auto &mod = activeMods[i];
          if (!mod.enabled) {
            continue;
          }

          if (ImGui::BeginTabItem(mod.name.c_str())) {
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "%s",
                               mod.name.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("v%s | %s", mod.version.c_str(),
                                mod.author.c_str());
            ImGui::Separator();

            // Disparar hook Lua onRenderUI caso implementado pelo mod
            if (mod.refOnRenderUI != LUA_NOREF) {
              ModManager::DispatchRenderUI(const_cast<ModInstance &>(mod));
            }

            // Painel nativo especializado para o mod de Treino (Practice Mode)
            if (mod.id == "dr2.practice_mode" || mod.id == "practice_mode" ||
                mod.name == "Practice Mode") {
              ImGui::TextColored(
                  ImVec4(1.0f, 0.9f, 0.2f, 1.0f),
                  "Painel de Treinamento de Curvas & Setores");
              ImGui::TextWrapped(
                  "Este mod permite gravar checkpoints instantâneos para treinar "
                  "curvas difíceis, pontos de frenagem e saltos repetidamente.");

              ImGui::Spacing();
              if (ImGui::CollapsingHeader("Atalhos do Treino",
                                          ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::BulletText(
                    "F5: Gravar Checkpoint instantâneo (Posição e Velocidade).");
                ImGui::BulletText(
                    "F6: Restaurar Checkpoint no modo ativo (Normal ou Momentum).");
                ImGui::BulletText(
                    "F7: Restaurar Checkpoint diretamente COM MOMENTUM integral.");
              }

              ImGui::Spacing();
              if (ImGui::CollapsingHeader("Configuração de Restauração",
                                          ImGuiTreeNodeFlags_DefaultOpen)) {
                int pmMode =
                    static_cast<int>(SavestateManager::GetRestoreMode());
                ImGui::Text("Comportamento Padrão ao Restaurar:");
                if (ImGui::RadioButton("Normal (Teleportar Parado)##dyn",
                                       pmMode == 0)) {
                  SavestateManager::SetRestoreMode(RestoreMode::Normal);
                }
                ImGui::SameLine();
                if (ImGui::RadioButton(
                        "Com Momentum (Continuar em Velocidade)##dyn",
                        pmMode == 1)) {
                  SavestateManager::SetRestoreMode(RestoreMode::WithMomentum);
                }

                ImGui::Spacing();
                if (ImGui::Button("Gravar Checkpoint Agora (F5)",
                                 ImVec2(210, 32))) {
                  SavestateManager::OnKeyAction(0x74, true);
                }
                ImGui::SameLine();
                if (ImGui::Button("Restaurar Normal (F6)",
                                 ImVec2(170, 32))) {
                  SavestateManager::RestoreCheckpoint(RestoreMode::Normal);
                }
                ImGui::SameLine();
                if (ImGui::Button("Restaurar com Momentum (F7)",
                                 ImVec2(210, 32))) {
                  SavestateManager::RestoreCheckpoint(
                      RestoreMode::WithMomentum);
                }
              }

              ImGui::Spacing();
              if (ImGui::CollapsingHeader("Status do Treino em Tempo Real",
                                          ImGuiTreeNodeFlags_DefaultOpen)) {
                if (SavestateManager::HasSavedState()) {
                  const auto &saved = SavestateManager::GetSavedState();
                  float sSpd =
                      std::sqrt(saved.linearVelocity.x *
                                    saved.linearVelocity.x +
                                saved.linearVelocity.y *
                                    saved.linearVelocity.y +
                                saved.linearVelocity.z *
                                    saved.linearVelocity.z) *
                      3.6f;
                  ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f),
                                     "● Checkpoint Gravado e Pronto");
                  ImGui::Text("Posição: (X: %.2f, Y: %.2f, Z: %.2f)",
                              saved.position.x, saved.position.y,
                              saved.position.z);
                  ImGui::Text("Velocidade Gravada: %.1f km/h", sSpd);
                } else {
                  ImGui::TextDisabled("Nenhum checkpoint salvo. Pressione F5 "
                                      "para salvar o primeiro ponto.");
                }
              }
            } else if (mod.refOnRenderUI == LUA_NOREF) {
              // Informações gerais para outros mods que não possuem UI personalizada
              ImGui::Spacing();
              ImGui::Text("Descrição: %s", mod.description.c_str());
              ImGui::Text("Diretório: %s", mod.directoryPath.c_str());
              ImGui::Text("Arquivo: %s", mod.mainScriptPath.c_str());
            }

            ImGui::EndTabItem();
          }
        }

        ImGui::EndTabBar();
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
