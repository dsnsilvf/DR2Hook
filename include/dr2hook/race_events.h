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

// Chamado pelo AutoStage quando a tela de carregamento é preenchida (chave do catálogo sem o
// "lng_", ex.: "montalegre_rallycross_route_0"); avisa a rota uma vez por carregamento.
void NotifyRoute(const char *routeKey);

} // namespace dr2hook

DR2HOOK_API int Dr2Host_StageConsumeEvent(dr2hook::Dr2StageEvent *event);
