#pragma once

#include "dr2hook/common.h"
#include <d3d11.h>
#include <dxgi.h>
#include <string>
#include <vector>

namespace dr2hook {

enum class ToastType { Info, Warning, Error };

struct ToastNotification {
  using ToastType = dr2hook::ToastType;

  std::string message;
  float duration = 3.0f;
  float elapsed = 0.0f;
  dr2hook::ToastType type = dr2hook::ToastType::Info;
};

class OverlayManager {
public:
  static bool Initialize(HWND hWnd, ID3D11Device *pDevice,
                         ID3D11DeviceContext *pContext);
  static void Shutdown();
  static bool IsInitialized();
  static void Render(IDXGISwapChain *pSwapChain);
  static void ToggleMenu();
  static bool IsMenuVisible();
  static void SetMenuVisible(bool visible);
  static void AddNotification(std::string message, float duration = 3.0f,
                              ToastType type = ToastType::Info);
  static bool HandleWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

  static void UpdateNotifications(float deltaTime);
  static size_t GetNotificationCount();
  static std::vector<ToastNotification> GetActiveNotifications();
  static void RenderUI();
};

} // namespace dr2hook

using dr2hook::OverlayManager;
using dr2hook::ToastNotification;
using dr2hook::ToastType;
