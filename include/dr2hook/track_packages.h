#pragma once

#include <string>
#include <utility>
#include <vector>

namespace dr2hook {

// Pistas de pacotes `<jogo>/ctracks/*.dr2track` (src/trackpkg). No boot, uma thread monta
// `ctracks/_cache/` (catálogo, bundles enxertados, arquivos dos pacotes); o hook da resolução de
// caminhos do VFS (0x14080e450) monta essa pasta em /data na frente dos pacotes do jogo no primeiro
// pedido do boot_data.xml, e manda à pista hospedeira os arquivos que a pista do pacote não traz.
// O pacote da location (`<loc>__<pista>.nefs`) vira o da hospedeira (0x1405df3c0 e 0x1405dcea0).

// DLL_PROCESS_ATTACH: só instala os hooks (não loga, não lê arquivo).
bool InstallTrackPackages();
// Depois do Logger::Init: começa a montagem do cache numa thread e relata a instalação.
void StartTrackPackages();
// Textos lng_dr2hook_* das pistas (espera a montagem terminar).
const std::vector<std::pair<std::string, std::string>> &TrackPackageTexts();
// Alias do pacote da location (track_alias do dr2hook_loadprobe.ini): a pista `track` usa o
// .nefs de `host`.
void AddTrackAlias(const std::string &track, const std::string &host);

} // namespace dr2hook
