#pragma once

#include "common.h"
#include "core_api.h"

#include <cstddef>
#include <cstdint>

namespace dr2hook {

// Trata os eventos dr2hook_* das telas nativas, trocando o slot +0x88 da
// vtable do StateScreenFECore; cria as abas e os combos no Enter pelo slot
// +0x80; e responde às chaves lng_dr2hook_* na busca de idioma. Exige MinHook
// já inicializado. Não faz nada se o executável não bater.
bool InstallNativeScreenHook();

// Chamado pelo hook do predicado de visibilidade (pause_menu.cpp) na thread da
// UI, a cada quadro, para cada condição com a vtable de BVisibilityControlData.
// `path` é o caminho completo (ui.<tela>.<data_path>) e cabe em `available`
// bytes. Devolve verdadeiro se a condição é de uma posição das telas nativas;
// nesse caso grava `hidden` e reaplica o texto de `item` quando os rótulos
// mudaram.
bool NativeScreenVisibility(const char *path, size_t available, uintptr_t item,
                            bool &hidden);

// Chamado pelo mesmo hook, no começo de cada avaliação; no máximo uma leitura a
// cada 15 ms. Com a tela de mod aberta, lê os índices dos combos no data store
// e enfileira as mudanças para o core.
void NativeScreenTick();

} // namespace dr2hook

DR2HOOK_API int Dr2Host_ConsumeReloadModsRequest();
DR2HOOK_API void Dr2Host_NativeMenuPublish(const dr2hook::Dr2MenuMod *mods,
                                           int count);
DR2HOOK_API int Dr2Host_NativeMenuConsumeEvent(int *modIndex, int *optionIndex,
                                               int *value);
