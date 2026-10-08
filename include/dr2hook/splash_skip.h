#pragma once

namespace dr2hook {

// Esconde o splash de 512x512 com o logo grande do DR2 na abertura. O jogo o
// mostra antes do core carregar, então a troca de imports fica na dxgi.dll:
// instalar no DLL_PROCESS_ATTACH. pular_splash=0 no dr2hook_intro.ini desliga.
// Não loga na instalação.
bool InstallSplashSkip();
// Depois do Logger::Init: relata o resultado.
void LogSplashSkip();

} // namespace dr2hook
