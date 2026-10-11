#pragma once

namespace dr2hook {

// Splash de 512x512 com o logo grande do DR2 na abertura. O jogo o mostra antes
// do core carregar, então a troca de imports fica na dxgi.dll: instalar no
// DLL_PROCESS_ATTACH, depois do DetectEditorLaunch. Sempre segura o splash até a
// tela de boot do DR2Hook fechar; só o esconde numa abertura pelo editor ou com
// pular_splash=1 no dr2hook_intro.ini. Não loga na instalação.
bool InstallSplashSkip();
// Depois do Logger::Init: relata o resultado.
void LogSplashSkip();

} // namespace dr2hook
