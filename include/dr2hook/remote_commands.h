#pragma once

#include <windows.h>

namespace dr2hook {

// Canal de comandos por arquivo, para controlar o jogo sem ninguem no teclado
// (docs/guides/remote_commands.md). A cada ~150 ms o core procura `dr2hook_cmd.txt` na
// pasta do jogo, apaga o arquivo, executa uma linha por vez e grava o resultado
// em `dr2hook_cmd.out`. Roda na thread de render.
class RemoteCommands {
public:
  static void Poll(HWND hwnd);
};

} // namespace dr2hook
