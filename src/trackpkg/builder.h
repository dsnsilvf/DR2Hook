#pragma once

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace trackpkg {

// Monta, a partir dos pacotes `<jogo>/ctracks/*.dr2track`, a pasta `<jogo>/ctracks/_cache/` que a
// dxgi monta em /data na frente dos pacotes do jogo:
// - catalogues/base.ctpk: o catálogo do jogo com as linhas de cada pista (location, país, pista,
//   rotas e superfícies clonadas da hospedeira);
// - os arquivos do jogo enxertados (graft/ dos pacotes, cards de reserva e o selo do DR2Hook);
// - os arquivos track/ e data/ dos pacotes.
// Os ids do catálogo ficam em `ctracks/registry.ini` e nunca mudam para uma pista. Uma assinatura
// (versão do carregador, exe, pacotes e registro) evita refazer o cache a cada boot.

// Andamento para a tela de boot (textos em inglês): etapa atual com a fração da barra (0..1; negativa
// mantém), etapa concluída e pacote ou pista recusados.
enum class BuildEvent { Step, Done, Problem };

struct BuildOptions {
  std::string gameDir; // UTF-8; pasta do dirtrally2.exe
  std::function<void(const std::string &)> info, warn;
  std::function<void(BuildEvent, const std::string &, double)> progress;
};

struct TrackRuntime {
  std::string id;           // nome interno da pista (pasta e linha do catálogo)
  std::string hostLocation; // pasta em tracks/locations
  std::string hostTrack;    // pista da hospedeira (pacote .nefs e arquivos que faltam)
  uint32_t location = 0;    // id da location no catálogo; o card usa dr2hook_loc<id>
  std::set<std::string> files; // caminhos (minúsculas) dentro de track/ que o pacote traz
};

struct BuildResult {
  bool cacheReady = false; // a pasta do cache está completa e pode ser montada
  bool regenerated = false;
  std::string cacheDir;    // UTF-8
  std::vector<TrackRuntime> tracks;
  std::vector<std::pair<std::string, std::string>> texts; // chave lng_* -> texto
};

BuildResult BuildTrackPackages(const BuildOptions &opt);

// Nome da location que o card usa (s_<nome>), para as locations criadas por pacotes.
std::string CardLocationName(uint32_t locationId);
// Marcador do mapa-múndi (map_marker_<nome> no persistentDB) que a location usa: um dos quatro sem
// location no jogo, movido para o lat/lon do pacote. Com mais de quatro pistas, elas dividem marcadores.
std::string MapMarkerName(uint32_t locationId);

} // namespace trackpkg
