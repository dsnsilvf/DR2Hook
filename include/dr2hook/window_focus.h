#pragma once

#include "dr2hook/common.h"

namespace dr2hook {

// Jogo sem pausa ao perder o foco (opção "Não pausar ao perder o foco" do Testar no jogo).
// O ring_deploy grava `keep_running_unfocused=1` no dr2hook_live.ini; com ela, o WndProc
// do jogo não recebe a desativação (WM_ACTIVATE inativo, WM_ACTIVATEAPP falso e
// WM_KILLFOCUS) e o jogo segue como se ainda tivesse o foco. Sem a opção, só registra
// no log as mudanças de foco. Comando remoto: `focus [on|off]`.
class WindowFocus {
public:
  static void Install();
  // 1 quando a mensagem foi consumida e não deve seguir para o jogo.
  static int OnWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
  static bool KeepRunning();
  static void SetKeepRunning(bool on);
};

} // namespace dr2hook
