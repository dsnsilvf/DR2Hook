#include "dr2hook/splash_skip.h"
#include "dr2hook/logger.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace dr2hook {
namespace {

#if defined(_WIN32)

// 0x1403a5470 cria uma janela layered de 512x512 (CreateWindowExW 0x80080,
// WS_OVERLAPPEDWINDOW), dá ShowWindow(SW_SHOW) e uma thread a pinta com
// UpdateLayeredWindow a cada 20 ms (0x1403ccc2e) até a janela do jogo abrir
// (DestroyWindow em 0x1403a7b37). Os dois imports são trocados no IAT do exe:
// a janela fica escondida e sem pintura; o tempo de boot não muda.
constexpr uintptr_t kIatShowWindow = 0x109fad8;
constexpr uintptr_t kIatUpdateLayered = 0x109fae0;
using ShowWindowFn = BOOL(WINAPI *)(HWND, int);
using UpdateLayeredFn = BOOL(WINAPI *)(HWND, HDC, POINT *, SIZE *, HDC, POINT *, COLORREF,
                                       BLENDFUNCTION *, DWORD);
ShowWindowFn g_origShow = nullptr;
UpdateLayeredFn g_origUpdateLayered = nullptr;
HWND g_splash = nullptr;
bool g_logged = false;
const char *g_status = "nao instalado";

bool IsSplash(HWND hwnd) {
  if (hwnd == nullptr) {
    return false;
  }
  if (hwnd == g_splash) {
    return true;
  }
  RECT r{};
  if ((GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_LAYERED) == 0 || !GetWindowRect(hwnd, &r) ||
      r.right - r.left != 512 || r.bottom - r.top != 512) {
    return false;
  }
  g_splash = hwnd;
  return true;
}

void LogHidden(HWND hwnd, const char *where) {
  if (g_logged) {
    return;
  }
  g_logged = true;
  char msg[160];
  std::snprintf(msg, sizeof msg, "SplashSkip: splash do logo escondido no %s (janela %p).", where,
                static_cast<void *>(hwnd));
  Logger::Info(msg);
}

BOOL WINAPI DetourShowWindow(HWND hwnd, int cmd) {
  if (cmd != SW_HIDE && IsSplash(hwnd)) {
    LogHidden(hwnd, "ShowWindow");
    return FALSE; // "estava escondida", como o ShowWindow devolveria
  }
  return g_origShow(hwnd, cmd);
}

BOOL WINAPI DetourUpdateLayered(HWND hwnd, HDC dst, POINT *pos, SIZE *size, HDC src, POINT *srcPos,
                                COLORREF key, BLENDFUNCTION *blend, DWORD flags) {
  if (IsSplash(hwnd)) {
    if (IsWindowVisible(hwnd)) {
      LogHidden(hwnd, "UpdateLayeredWindow");
      g_origShow(hwnd, SW_HIDE);
    }
    return TRUE;
  }
  return g_origUpdateLayered(hwnd, dst, pos, size, src, srcPos, key, blend, flags);
}

// Troca um slot do IAT do exe se ele aponta para a função esperada.
bool SwapIat(uintptr_t rva, void *expected, void *detour) {
  auto **slot = reinterpret_cast<void **>(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) + rva);
  if (*slot != expected) {
    return false;
  }
  DWORD old = 0;
  if (!VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &old)) {
    return false;
  }
  *slot = detour;
  VirtualProtect(slot, sizeof *slot, old, &old);
  return true;
}

bool Enabled() {
  std::ifstream in("dr2hook_intro.ini");
  std::string line;
  bool on = true;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.rfind("pular_splash=", 0) == 0) {
      on = line.substr(13) != "0";
    }
  }
  return on;
}

#endif

} // namespace

bool InstallSplashSkip() {
#if defined(_WIN32)
  if (!Enabled()) {
    g_status = "desligado (pular_splash=0)";
    return false;
  }
  HMODULE user32 = GetModuleHandleW(L"user32.dll");
  auto *show = user32 ? reinterpret_cast<void *>(GetProcAddress(user32, "ShowWindow")) : nullptr;
  auto *update = user32 ? reinterpret_cast<void *>(GetProcAddress(user32, "UpdateLayeredWindow")) : nullptr;
  if (show == nullptr || update == nullptr) {
    g_status = "ShowWindow/UpdateLayeredWindow nao encontrados";
    return false;
  }
  g_origShow = reinterpret_cast<ShowWindowFn>(show);
  g_origUpdateLayered = reinterpret_cast<UpdateLayeredFn>(update);
  if (!SwapIat(kIatShowWindow, show, reinterpret_cast<void *>(&DetourShowWindow))) {
    g_status = "slot do ShowWindow no IAT nao confere";
    return false;
  }
  if (!SwapIat(kIatUpdateLayered, update, reinterpret_cast<void *>(&DetourUpdateLayered))) {
    SwapIat(kIatShowWindow, reinterpret_cast<void *>(&DetourShowWindow), show);
    g_status = "slot do UpdateLayeredWindow no IAT nao confere";
    return false;
  }
  g_status = "ativo";
  return true;
#else
  return false;
#endif
}

void LogSplashSkip() {
#if defined(_WIN32)
  Logger::Info(std::string("SplashSkip: ") + g_status + ".");
#endif
}

} // namespace dr2hook
