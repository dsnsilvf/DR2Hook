#pragma once

#include <string>

namespace dr2hook {

// Tela de boot do DR2Hook (src/core/boot_screen.cpp, na dxgi): uma janela própria de 800x450 com o logo,
// a etapa atual, uma barra e um pequeno feed, mostrada antes do splash do DR2. Ela acompanha a montagem do
// cache de pistas (track_packages.cpp) e a carga do core; quando os dois terminam, mostra o resumo por ~2 s
// e fecha. O hook do ShowWindow do splash (splash_skip.cpp) espera por ela, então as telas do jogo vêm
// depois, inteiras. Desligada numa abertura pelo editor ou com pular_tela_boot=1 no dr2hook_intro.ini.

enum class BootLine { Done, Problem };

// DLL_PROCESS_ATTACH: decide se a tela aparece e cria o evento de fim. Não loga.
void InstallBootScreen();
// Depois do Logger::Init: abre a janela numa thread própria.
void StartBootScreen();
// Etapa atual e fração da barra (0..1; negativa mantém).
void BootScreenStep(const std::string &text, double fraction);
// Linha do feed (etapa concluída ou problema).
void BootScreenLine(BootLine kind, const std::string &text);
// Fim da montagem das pistas (resumo de uma linha) e da carga do core.
void BootScreenTracksDone(const std::string &summary);
void BootScreenCoreDone(bool ok, const std::string &summary);
// Bloqueia até a tela fechar (ou na hora, se ela não aparece).
void WaitBootScreen();

} // namespace dr2hook
