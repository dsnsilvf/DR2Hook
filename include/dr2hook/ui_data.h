#pragma once

#include "dr2hook/common.h"

namespace dr2hook {

// Instalado no DLL_PROCESS_ATTACH: o jogo lê os documentos de boot antes de a
// thread de init terminar. Não usa o log; os registros ficam guardados.
// Altera states.bin, flow.bin e screens.bin para abrir a tela dr2modloader,
// a menos que exista dr2hook_ui_patch.disabled ao lado da dxgi.dll.
bool InstallUiDataHook(HMODULE hostModule);

// Grava no log os documentos vistos até agora e passa a gravar os próximos.
void StartUiDataLog();

} // namespace dr2hook
