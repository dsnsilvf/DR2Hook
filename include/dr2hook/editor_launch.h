#pragma once

namespace dr2hook {

// O editor de pistas (scripts/research/ring_deploy.py, F5) grava `dr2hook_editor_launch.txt` na pasta do
// jogo logo antes de abri-lo. A dxgi consome o arquivo no DLL_PROCESS_ATTACH (apaga; só vale com menos de
// 2 min, para uma abertura que falhou não valer na próxima) e marca DR2HOOK_EDITOR_LAUNCH=1 no ambiente do
// processo, que o core também lê. Nessa abertura o boot fica curto: sem a tela do DR2Hook, sem o splash do
// DR2 e sem o vídeo da Codemasters. Numa abertura normal tudo aparece; o dr2hook_intro.ini ainda pode
// forçar cada pulo (pular_splash, pular_logo, pular_tela_boot).

// dxgi, DLL_PROCESS_ATTACH: lê e apaga o marcador. Não loga.
bool DetectEditorLaunch();
// Qualquer módulo, a qualquer hora.
bool IsEditorLaunch();
// Valor de `chave=0|1` no dr2hook_intro.ini; sem a chave, `fallback`.
bool IntroOption(const char *key, bool fallback);

} // namespace dr2hook
