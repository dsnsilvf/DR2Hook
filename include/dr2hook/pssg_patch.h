#pragma once

namespace dr2hook {

// Troca, durante a leitura, o tipo de nós de texto de cenas PSSG da UI
// (docs/reverse_engineering/ui_tabs.md, "Texto rico"). Instalar no
// DLL_PROCESS_ATTACH: as cenas do frontend são lidas no boot. Não loga.
bool InstallPssgPatch();
// Depois do Logger::Init: relata instalação e trocas feitas.
void LogPssgPatchStatus();

} // namespace dr2hook
