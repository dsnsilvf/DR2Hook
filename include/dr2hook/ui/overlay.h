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
  // Tela preta por cima do jogo (inicialização rápida do editor): o overlay e
  // os avisos continuam por cima dela.
  static void SetLoadCover(bool on);
  static bool IsLoadCoverOn();
  // O que vai atrás do terminal da tela preta: a cena que o jogo desenhou
  // neste quadro (ou nullptr) e uma linha de estado da GPU. exposure > 0 =
  // cena HDR (luz linear), desenhada com exposição e curva de tonemap.
  // Foto da pista (PPM binário) atrás do terminal; tem prioridade sobre a
  // cena do jogo. Vazio = sem foto.
  static void SetLoadCoverImage(std::string path);
  static void SetLoadCoverScene(ID3D11ShaderResourceView *scene, float aspect,
                                std::string gpuStatus, float exposure = 0.0f);
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
