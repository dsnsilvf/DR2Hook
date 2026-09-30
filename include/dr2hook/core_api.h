#pragma once

#include "common.h"

#include <dxgi.h>

namespace dr2hook {

inline constexpr unsigned int kCoreAbiVersion = 1;

// ABI estável entre a dxgi.dll residente e a dr2hook_core.dll recarregável.
// Ponteiros crus só: cada DLL tem a própria libstdc++.
struct Dr2CoreApi {
  unsigned int abiVersion;
  int (*Initialize)(int truncateLog);
  void (*Shutdown)();
  void (*OnFrame)(IDXGISwapChain *swapChain, HWND hwnd, double deltaTime);
  int (*OnWndProc)(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};

} // namespace dr2hook
