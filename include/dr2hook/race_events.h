#pragma once

#include "dr2hook/common.h"
#include "dr2hook/core_api.h"

namespace dr2hook {

// Eventos nomeados da sessao de corrida ("startlightsstart", "racestart"),
// enfileirados como Dr2StageEvent para o core.
bool InstallRaceEventsHook();
void UninstallRaceEventsHook();

// Chamado pelo LoadTrace ao abrir locations/<local>__<pista>.nefs.
void NotifyStageLoad(const char *nefsFileName);

} // namespace dr2hook

DR2HOOK_API int Dr2Host_StageConsumeEvent(dr2hook::Dr2StageEvent *event);
