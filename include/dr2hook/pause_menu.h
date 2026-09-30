#pragma once

#include "common.h"

namespace dr2hook {

// Reaproveita o item oculto reset_view do menu de pausa como "DR2 ModLoader".
// Exige MinHook já inicializado. Não faz nada se o executável não bater.
bool InstallPauseMenuHooks();
bool ConsumePauseMenuActivation();

} // namespace dr2hook

DR2HOOK_API int Dr2Host_ConsumePauseMenuRequest();
