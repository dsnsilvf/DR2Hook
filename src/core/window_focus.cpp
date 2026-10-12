#include "dr2hook/window_focus.h"

#include "dr2hook/logger.h"

#include <atomic>
#include <fstream>
#include <string>

namespace dr2hook {
namespace {

std::atomic<bool> g_keep{false};

const char *Name(UINT msg) {
  switch (msg) {
  case WM_ACTIVATE: return "WM_ACTIVATE";
  case WM_ACTIVATEAPP: return "WM_ACTIVATEAPP";
  case WM_SETFOCUS: return "WM_SETFOCUS";
  case WM_KILLFOCUS: return "WM_KILLFOCUS";
  default: return nullptr;
  }
}

// A janela do jogo está sendo desativada (perdendo o foco).
bool Losing(UINT msg, WPARAM wParam) {
  return (msg == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE) ||
         (msg == WM_ACTIVATEAPP && wParam == FALSE) || msg == WM_KILLFOCUS;
}

} // namespace

void WindowFocus::Install() {
  std::ifstream in("dr2hook_live.ini");
  std::string line;
  bool keep = false;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.rfind("keep_running_unfocused=", 0) == 0) keep = line.substr(23) != "0";
  }
  g_keep.store(keep, std::memory_order_relaxed);
  if (keep) Logger::Info("WindowFocus: o jogo segue rodando sem o foco (dr2hook_live.ini).");
}

bool WindowFocus::KeepRunning() { return g_keep.load(std::memory_order_relaxed); }

void WindowFocus::SetKeepRunning(bool on) {
  g_keep.store(on, std::memory_order_relaxed);
  Logger::Info(on ? "WindowFocus: segue sem o foco." : "WindowFocus: pausa normal ao perder o foco.");
}

int WindowFocus::OnWndProc(HWND, UINT msg, WPARAM wParam, LPARAM) {
  const char *name = Name(msg);
  if (name == nullptr) return 0;
  const bool swallow = Losing(msg, wParam) && g_keep.load(std::memory_order_relaxed);
  Logger::Info(std::string("WindowFocus: ") + name + " w=" + std::to_string(static_cast<unsigned long long>(wParam)) +
               (swallow ? " (engolida)" : ""));
  return swallow ? 1 : 0;
}

} // namespace dr2hook
