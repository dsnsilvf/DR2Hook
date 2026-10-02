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

  // 2. Main In-Game Menu (DR2Hook)
  if (g_showMenu) {
    ImGui::SetNextWindowSize(ImVec2(660, 540), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("DR2Hook v0.1.0", &g_showMenu)) {
      // Tab Navigation
      if (ImGui::BeginTabBar("DR2TabBar", ImGuiTabBarFlags_None)) {

        // ===================================================================
        // TAB 1: DIAGNOSTICS
        // ===================================================================
        if (ImGui::BeginTabItem("Diagnostics")) {
          VehicleTelemetryInfo vInfo;
          Player::GetVehicleTelemetry(vInfo);

          TrackTelemetryInfo tInfo;
          Player::GetTrackTelemetry(tInfo);

          CarState state{};
          bool hasState = Player::CaptureState(state);

          // Card 1: Vehicle
          if (ImGui::CollapsingHeader("Vehicle",
                                      ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Model: ");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 1.0f, 1.0f), "%s",
                               vInfo.model.c_str());

            ImGui::SameLine(360.0f);
            ImGui::Text("Class: ");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.3f, 1.0f), "%s",
                               vInfo.category.c_str());

            ImGui::Text("Status: ");
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
              ImGui::TextDisabled("Rig: Disconnected");
            }

            ImGui::Spacing();

            // Speed in prominent display
            ImGui::Text("Speed:");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.95f, 0.1f, 1.0f),
                               "[ %.1f km/h ]", vInfo.speedKmh);
            ImGui::SameLine();
            ImGui::TextDisabled("(%.1f mph)", vInfo.speedMph);

            ImGui::SameLine(360.0f);
            ImGui::Text("Acceleration:");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "%.2f G",
                               vInfo.accelerationG);

            // Gear & RPM
            ImGui::Text("Gear: ");
            ImGui::SameLine();
            if (vInfo.gear > 0) {
              ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.5f, 1.0f), "%d / %d",
                                 vInfo.gear, vInfo.forwardGears);
            } else if (vInfo.gear == 0) {
              ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "N");
            } else if (vInfo.gear == -1) {
              ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.2f, 1.0f), "R");
            } else {
              ImGui::TextDisabled("--");
            }

            ImGui::SameLine(200.0f);
            ImGui::Text("RPM: ");
            ImGui::SameLine();
            if (vInfo.rpm > 0.0f) {
              ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%.0f RPM",
                                 vInfo.rpm);
            } else {
              ImGui::TextDisabled("N/A");
            }
            ImGui::SameLine();
            float redline = vInfo.redlineRpm > 0.0f ? vInfo.redlineRpm
                                                     : vInfo.maxPowerRpm;
            ImGui::TextDisabled("(Idle: %.0f | Redline: %.0f)", vInfo.idleRpm,
                                redline);

            // Engine RPM graphical bar
            float rpmRatio = 0.0f;
            if (redline > 0.0f && vInfo.rpm > 0.0f) {
              rpmRatio = std::clamp(vInfo.rpm / (redline * 1.05f), 0.0f, 1.0f);
            }
            char rpmBuf[32];
            if (vInfo.rpm > 0.0f) {
              std::snprintf(rpmBuf, sizeof(rpmBuf), "%.0f RPM", vInfo.rpm);
            } else {
              std::snprintf(rpmBuf, sizeof(rpmBuf), "Telemetry N/A");
            }

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

          // Card 2: Track / Session
          if (ImGui::CollapsingHeader("Track / Session",
                                      ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Stage: ");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 1.0f, 1.0f), "%s",
                               tInfo.trackName.c_str());

            ImGui::Text("Location: ");
            ImGui::SameLine();
            ImGui::TextUnformatted(tInfo.location.c_str());

            ImGui::Text("Surface: ");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.85f, 0.75f, 0.55f, 1.0f), "%s",
                               tInfo.surface.c_str());

            ImGui::SameLine(360.0f);
            ImGui::Text("Conditions: ");
            ImGui::SameLine();
            ImGui::TextUnformatted(tInfo.conditions.c_str());

            ImGui::Text("Session Mode: ");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s",
                               tInfo.sessionState.c_str());
          }

          ImGui::Spacing();

          // Card 3: Telemetry
          if (ImGui::CollapsingHeader("Telemetry",
                                      ImGuiTreeNodeFlags_DefaultOpen)) {
            if (hasState) {
              // World position
              ImGui::Text("World Position (X, Y, Z):");
              ImGui::SameLine();
              ImGui::TextColored(ImVec4(0.3f, 0.9f, 1.0f, 1.0f),
                                 "X: %.2f  |  Y: %.2f  |  Z: %.2f",
                                 state.position.x, state.position.y,
                                 state.position.z);

              // Orientation Euler (Pitch, Roll, Yaw)
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

              // Linear and Angular Velocity
              ImGui::Text("Linear Velocity (m/s):");
              ImGui::SameLine();
              ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.6f, 1.0f),
                                 "Vx: %+.2f  |  Vy: %+.2f  |  Vz: %+.2f",
                                 state.linearVelocity.x, state.linearVelocity.y,
                                 state.linearVelocity.z);

              ImGui::Text("Angular Velocity (rad/s):");
              ImGui::SameLine();
              ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f),
                                 "Wx: %+.3f  |  Wy: %+.3f  |  Wz: %+.3f",
                                 state.angularVelocity.x,
                                 state.angularVelocity.y,
                                 state.angularVelocity.z);

              // 4 Wheels & Suspension Table
              ImGui::Spacing();
              ImGui::Text("Suspension & Wheel Contact:");
              if (ImGui::BeginTable("WheelsTelemetryTable", 4,
                                    ImGuiTableFlags_Borders |
                                        ImGuiTableFlags_RowBg |
                                        ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Front Left (FL)");
                ImGui::TableSetupColumn("Front Right (FR)");
                ImGui::TableSetupColumn("Rear Left (RL)");
                ImGui::TableSetupColumn("Rear Right (RR)");
                ImGui::TableHeadersRow();

                // Compression row
                ImGui::TableNextRow();
                for (int i = 0; i < 4; ++i) {
                  ImGui::TableSetColumnIndex(i);
                  float compPercent =
                      state.wheels[i].suspensionCompression * 100.0f;
                  ImGui::Text("Comp: %.1f%%", compPercent);
                }

                // Ground contact row
                ImGui::TableNextRow();
                for (int i = 0; i < 4; ++i) {
                  ImGui::TableSetColumnIndex(i);
                  if (state.wheels[i].inContact) {
                    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f),
                                       "● Grounded");
                  } else {
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                                       "○ Airborne");
                  }
                }

                ImGui::EndTable();
              }
            } else {
              ImGui::TextDisabled("Waiting for vehicle to spawn for "
                                  "real-time telemetry...");
            }
          }

          ImGui::EndTabItem();
        }

        // ===================================================================
        // TAB 2: MODS
        // ===================================================================
        if (ImGui::BeginTabItem("Mods")) {
          ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f),
                             "Lua Mod Manager & Extensions");
          ImGui::SameLine();
          if (ImGui::Button("Reload Scripts (Hot-Reload)")) {
            ModManager::ReloadMods();
          }
          ImGui::SameLine();
          if (ImGui::Button("Reload Native Core (F8)")) {
#if defined(_WIN32)
            HMODULE host = GetModuleHandleA("dxgi.dll");
            auto requestReload =
                host == nullptr
                    ? nullptr
                    : reinterpret_cast<void (*)()>(
                          GetProcAddress(host, "Dr2Host_RequestReload"));
            if (requestReload != nullptr) {
              requestReload();
            }
#endif
          }
          if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Reloads dr2hook_core.dll from disk without closing the game. "
                "The in-memory checkpoint is cleared.");
          }

          ImGui::Separator();

          const auto &mods = ModManager::GetLoadedMods();
          static int selectedModIndex = 0;

          if (mods.empty()) {
            ImGui::TextDisabled(
                "No mods loaded in 'mods/' directory.");
          } else {
            if (ImGui::BeginTable("ModsManagerTable", 5,
                                  ImGuiTableFlags_Borders |
                                      ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_SizingStretchProp)) {
              ImGui::TableSetupColumn("Active",
                                      ImGuiTableColumnFlags_WidthFixed, 55.0f);
              ImGui::TableSetupColumn("Name",
                                      ImGuiTableColumnFlags_WidthStretch);
              ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed,
                                      130.0f);
              ImGui::TableSetupColumn("Version",
                                      ImGuiTableColumnFlags_WidthFixed, 65.0f);
              ImGui::TableSetupColumn("Author",
                                      ImGuiTableColumnFlags_WidthFixed, 130.0f);
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

            // Selected mod details
            if (selectedModIndex >= 0 &&
                selectedModIndex < static_cast<int>(mods.size())) {
              const auto &selMod = mods[selectedModIndex];
              ImGui::Spacing();
              if (ImGui::CollapsingHeader("Mod Details",
                                          ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "%s",
                                   selMod.name.c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("(%s) v%s by %s", selMod.id.c_str(),
                                    selMod.version.c_str(),
                                    selMod.author.c_str());

                ImGui::TextWrapped("%s", selMod.description.c_str());
                ImGui::Spacing();
                ImGui::Text("Main Script: %s",
                            selMod.mainScriptPath.c_str());
                ImGui::Text("Directory: %s", selMod.directoryPath.c_str());

                ImGui::Text("Registered Callbacks:");
                ImGui::BulletText("onInit: %s",
                                  (selMod.refOnInit != LUA_NOREF) ? "Yes" : "No");
                ImGui::BulletText("onTick: %s",
                                  (selMod.refOnTick != LUA_NOREF) ? "Yes" : "No");
                ImGui::BulletText("onKeyDown: %s",
                                  (selMod.refOnKeyDown != LUA_NOREF) ? "Yes"
                                                                     : "No");
                ImGui::BulletText("onStageStart: %s",
                                  (selMod.refOnStageStart != LUA_NOREF)
                                      ? "Yes"
                                      : "No");
                ImGui::BulletText("onRenderUI: %s",
                                  (selMod.refOnRenderUI != LUA_NOREF) ? "Yes"
                                                                      : "No");
              }
            }
          }

          // Lua Engine Stats
          ImGui::Spacing();
          if (ImGui::CollapsingHeader("Lua Engine Statistics",
                                      ImGuiTreeNodeFlags_DefaultOpen)) {
            lua_State *L = LuaEngine::GetState();
            int memKb = (L != nullptr) ? lua_gc(L, LUA_GCCOUNT, 0) : 0;
            ImGui::Text("Allocated Lua Memory: %d KB", memKb);
            ImGui::Text("Total Loaded Mods: %zu", mods.size());
          }

          ImGui::EndTabItem();
        }

        // ===================================================================
        // DYNAMIC TABS: EACH ACTIVE MOD RECEIVES ITS OWN DEDICATED TAB
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

            // Trigger Lua onRenderUI hook if implemented
            if (mod.refOnRenderUI != LUA_NOREF) {
              ModManager::DispatchRenderUI(const_cast<ModInstance &>(mod));
            }

            // Specialized native panel for Practice Mode
            if (mod.id == "dr2.practice_mode" || mod.id == "practice_mode" ||
                mod.name == "Practice Mode") {
              ImGui::TextColored(
                  ImVec4(1.0f, 0.9f, 0.2f, 1.0f),
                  "Corner & Sector Practice Panel");
              ImGui::TextWrapped(
                  "Save and restore instant checkpoints in real time to practice "
                  "difficult corners, braking zones, and jumps repeatedly.");

              ImGui::Spacing();
              if (ImGui::CollapsingHeader("Practice Shortcuts",
                                          ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::BulletText(
                    "F5: Save instant checkpoint (Position and Velocity).");
                ImGui::BulletText(
                    "F6: Restore checkpoint in active mode (Normal or Momentum).");
                ImGui::BulletText(
                    "F7: Restore checkpoint directly WITH FULL MOMENTUM.");
              }

              ImGui::Spacing();
              if (ImGui::CollapsingHeader("Restore Configuration",
                                          ImGuiTreeNodeFlags_DefaultOpen)) {
                int pmMode =
                    static_cast<int>(SavestateManager::GetRestoreMode());
                ImGui::Text("Default Restore Behavior:");
                if (ImGui::RadioButton("Normal (Stationary Teleport)##dyn",
                                       pmMode == 0)) {
                  SavestateManager::SetRestoreMode(RestoreMode::Normal);
                }
                ImGui::SameLine();
                if (ImGui::RadioButton(
                        "With Momentum (Preserve Velocity)##dyn",
                        pmMode == 1)) {
                  SavestateManager::SetRestoreMode(RestoreMode::WithMomentum);
                }

                ImGui::Spacing();
                if (ImGui::Button("Save Checkpoint (F5)",
                                 ImVec2(210, 32))) {
                  SavestateManager::OnKeyAction(0x74, true);
                }
                ImGui::SameLine();
                if (ImGui::Button("Restore Normal (F6)",
                                 ImVec2(170, 32))) {
                  SavestateManager::RestoreCheckpoint(RestoreMode::Normal);
                }
                ImGui::SameLine();
                if (ImGui::Button("Restore with Momentum (F7)",
                                 ImVec2(210, 32))) {
                  SavestateManager::RestoreCheckpoint(
                      RestoreMode::WithMomentum);
                }
              }

              ImGui::Spacing();
              if (ImGui::CollapsingHeader("Real-Time Practice Status",
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
                                     "● Checkpoint Saved & Ready");
                  ImGui::Text("Position: (X: %.2f, Y: %.2f, Z: %.2f)",
                              saved.position.x, saved.position.y,
                              saved.position.z);
                  ImGui::Text("Saved Speed: %.1f km/h", sSpd);
                } else {
                  ImGui::TextDisabled("No checkpoint saved yet. Press F5 "
                                      "to save your first point.");
                }
              }
            } else if (mod.refOnRenderUI == LUA_NOREF) {
              // General information for mods without custom UI
              ImGui::Spacing();
              ImGui::Text("Description: %s", mod.description.c_str());
              ImGui::Text("Directory: %s", mod.directoryPath.c_str());
              ImGui::Text("File: %s", mod.mainScriptPath.c_str());
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
