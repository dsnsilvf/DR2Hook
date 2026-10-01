#pragma once

namespace dr2hook {

// Linha do tempo de carregamento para pesquisa: loga aberturas de arquivos de
// pista/localidade, volume lido do disco por segundo e frames travados.
// Instalar depois de InitializeHooks (usa o tick do Present).
bool InstallLoadTrace();
void UninstallLoadTrace();

} // namespace dr2hook
