#pragma once

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace trackpkg {

// Pacote de pista `.dr2track` (zip). Conteúdo:
// - `manifest.json`: id, nomes, país, hospedeira e rotas (ParseManifest);
// - `track/<rel>`: arquivos da pista, servidos em /data/tracks/locations/<host.location>/<id>/<rel>.
//   O que falta sai da pista hospedeira (o hook do VFS troca o caminho);
// - `data/<caminho>`: arquivos soltos em /data/<caminho>, só com os prefixos de AllowedDataPath;
// - `graft/<caminho>.pssg`: fragmento enxertado no arquivo do jogo /data/<caminho> (GraftTarget),
//   com `$LOC` e `$TRACK` trocados pelo nome da location (dr2hook_loc<id>) e pelo id da pista;
// - `preview/card.png` e `source/`: para o editor, o carregador ignora.
struct RouteInfo {
  std::string id;        // pasta da rota (ex. route_0)
  std::string hostRoute; // rota da hospedeira de onde vêm a linha do catálogo e os arquivos que faltam
  std::string name;      // nome exibido
  double lengthM = 0;    // extensão (campo 15 do catálogo); 0 mantém o da hospedeira
};

struct Manifest {
  int format = 0;
  std::string id;          // nome interno da pista, [a-z0-9_]
  std::string name;        // nome exibido
  std::string country;     // país exibido
  std::string description;
  std::string author, version;
  std::string hostLocation; // pasta em tracks/locations (ex. portugal)
  std::string hostTrack;    // pista da hospedeira (ex. montalegre_rallycross)
  std::vector<RouteInfo> routes;
  // Lugar no mundo (graus): posiciona o alfinete do mapa-múndi da tela da location (o nó
  // map_marker_<location> do persistentDB.pssg) e vai, arredondado, para os campos 15 e 16 da linha
  // LOCATION. Sem "map" no manifesto fica o da hospedeira. mapRegion (campo 17, ainda sem nome) só
  // muda se vier.
  bool hasMap = false;
  double mapLat = 0, mapLon = 0;
  int mapRegion = -1;
};

bool ParseManifest(const std::string &text, Manifest *out, std::string *error);
bool ValidName(const std::string &s);
bool AllowedDataPath(const std::string &path, const std::string &trackId);
// Caminho do jogo (com as maiúsculas do pacote original) do alvo de um enxerto; vazio se não é permitido.
std::string GraftTarget(const std::string &lowerPath);

class Package {
public:
  Package();
  ~Package();
  Package(const Package &) = delete;
  Package &operator=(const Package &) = delete;

  bool Open(const std::string &path, std::string *error);
  const Manifest &manifest() const { return manifest_; }
  // Arquivos do zip (sem pastas), com o caminho em minúsculas e barras normais.
  const std::vector<std::string> &Entries() const { return entries_; }
  bool Read(const std::string &entry, std::vector<uint8_t> *out, std::string *error);

private:
  struct Zip;
  std::unique_ptr<Zip> zip_;
  FILE *file_ = nullptr;
  Manifest manifest_;
  std::vector<std::string> entries_;
  std::vector<uint32_t> indices_;
};

} // namespace trackpkg
