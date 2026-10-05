#pragma once

#include "common.h"
#include <cstdint>

namespace dr2hook {

// Reaproveita o item oculto reset_view do menu de pausa como "DR2 Hook".
// Exige MinHook já inicializado. Não faz nada se o executável não bater.
bool InstallPauseMenuHooks();
// O core abre o overlay no próximo frame.
void RequestPauseMenuActivation();
bool ConsumePauseMenuActivation();
// Mostra o item Reiniciar do menu de pausa mesmo onde o jogo o esconde.
void SetRestartVisible(bool visible);
// Reenvia o texto dos BTextStatic de um item da UI, que volta a passar pela
// busca de idioma. Só na thread da UI (a fila de comandos não tem trava).
void RefreshItemText(uintptr_t item);

} // namespace dr2hook

DR2HOOK_API int Dr2Host_ConsumePauseMenuRequest();
DR2HOOK_API int Dr2Host_SetRestartVisible(int visible);
