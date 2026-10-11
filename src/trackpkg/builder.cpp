#include "trackpkg/builder.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <sstream>

#include "trackpkg/ctpk.h"
#include "trackpkg/fsutil.h"
#include "trackpkg/nefs.h"
#include "trackpkg/package.h"
#include "trackpkg/pssg.h"

namespace trackpkg {
namespace {

namespace fs = std::filesystem;

// Muda quando o que o cache contém muda (força refazer em quem atualizar a dxgi).
constexpr const char *kLoaderVersion = "trackpkg 5";

constexpr uint32_t kTrackModel = 0xB8E12AAA;
constexpr uint32_t kTrack = 0x1072479A;
// Primeiro id de location livre no catálogo do jogo (o das pistas dos pacotes).
constexpr uint32_t kFirstCardLocation = 47;
constexpr uint32_t kTrackModelSurface = 0x29199252;
constexpr uint32_t kLocation = 0x7604D91E;
constexpr uint32_t kCountry = 0xD3F53B19;
constexpr uint32_t kLengthField = 15;

const char *const kBadgeBundle = "frontend/bundles/b_persistent.pssg";
const char *const kMapDatabase = "frontend/databases/persistentDB.pssg";
const char *const kCardBundles[] = {"frontend/bundles/b_locations_nonpersistent_base.pssg",
                                    "frontend/bundles/b_locations_persistent_base.pssg"};

#include "trackpkg/badge_fragment.inc" // kBadgeFragment[], kBadgeFragmentSize

std::string TextPrefix(const std::string &id) {
  const std::string bare = id.compare(0, 8, "dr2hook_") == 0 ? id.substr(8) : id;
  return "lng_dr2hook_" + bare;
}

std::string Upper(std::string s) {
  for (auto &c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

std::string Join(const std::string &a, const std::string &b) { return a + "/" + b; }

uint64_t Varint(const Fields &f, uint32_t n) {
  const Field *x = FindField(f, n);
  return x != nullptr && x->wire != 2 ? x->value : 0;
}

// ---- registro (ctracks/registry.ini) ----

struct RegEntry {
  uint32_t location = 0, country = 0, track = 0;
  std::vector<std::pair<std::string, uint32_t>> routes;
  uint32_t Route(const std::string &id) const {
    for (const auto &r : routes)
      if (r.first == id) return r.second;
    return 0;
  }
};

struct Registry {
  std::vector<std::pair<std::string, RegEntry>> entries; // ordem do arquivo = ordem no catálogo
  bool dirty = false;

  RegEntry *Find(const std::string &id) {
    for (auto &e : entries)
      if (e.first == id) return &e.second;
    return nullptr;
  }

  void Load(const std::string &text) {
    std::istringstream in(text);
    std::string line;
    RegEntry *cur = nullptr;
    while (std::getline(in, line)) {
      while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
      if (line.empty() || line[0] == ';' || line[0] == '#') continue;
      if (line[0] == '[' && line.back() == ']') {
        const std::string id = line.substr(1, line.size() - 2);
        cur = Find(id);
        if (cur == nullptr && ValidName(id)) {
          entries.emplace_back(id, RegEntry{});
          cur = &entries.back().second;
        }
        continue;
      }
      const size_t eq = line.find('=');
      if (cur == nullptr || eq == std::string::npos) continue;
      std::string key = line.substr(0, eq), val = line.substr(eq + 1);
      while (!key.empty() && key.back() == ' ') key.pop_back();
      while (!val.empty() && val[0] == ' ') val.erase(0, 1);
      const uint32_t v = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 10));
      if (key == "location") cur->location = v;
      else if (key == "country") cur->country = v;
      else if (key == "track") cur->track = v;
      else if (key.compare(0, 6, "route.") == 0) cur->routes.emplace_back(key.substr(6), v);
    }
  }

  std::string Save() const {
    std::string out = "; ids do catálogo de cada pista de ctracks/ (gerado pela dxgi do DR2Hook).\n"
                      "; Uma pista mantém os ids para sempre; ids de pistas removidas ficam reservados.\n";
    for (const auto &[id, e] : entries) {
      out += "\n[" + id + "]\nlocation = " + std::to_string(e.location) + "\ncountry = " + std::to_string(e.country) +
             "\ntrack = " + std::to_string(e.track) + "\n";
      for (const auto &r : e.routes) out += "route." + r.first + " = " + std::to_string(r.second) + "\n";
    }
    return out;
  }

  // Maior id usado no registro para a tabela.
  uint32_t Max(uint32_t table) const {
    uint32_t m = 0;
    for (const auto &[id, e] : entries) {
      if (table == kLocation) m = std::max(m, e.location);
      else if (table == kCountry) m = std::max(m, e.country);
      else if (table == kTrack) m = std::max(m, e.track);
      else if (table == kTrackModel)
        for (const auto &r : e.routes) m = std::max(m, r.second);
    }
    return m;
  }
};

// ---- arquivos originais do jogo ----

class GameFiles {
public:
  explicit GameFiles(std::string gameDir) : dir_(std::move(gameDir)) {}

  bool Read(const std::string &path, std::vector<uint8_t> *out, std::string *error) {
    // O patch (game_1.dat) vem antes do pacote base, como o jogo resolve.
    static const char *const kArchives[] = {"game_1.dat", "game.nefs"};
    for (const char *name : kArchives) {
      NefsArchive *a = Open(name, error);
      if (a != nullptr && a->Has(path)) return a->Read(path, out, error);
    }
    *error = "o jogo não tem " + path;
    return false;
  }

private:
  NefsArchive *Open(const std::string &name, std::string *error) {
    auto it = archives_.find(name);
    if (it != archives_.end()) return it->second.get();
    auto a = std::make_unique<NefsArchive>();
    const std::string path = Join(dir_, "game/" + name);
    bool ok;
    if (name.size() > 5 && name.compare(name.size() - 5, 5, ".nefs") == 0) {
      ok = a->Open(path, error);
    } else {
      if (exe_.empty() && !ReadAll(Join(dir_, "dirtrally2.exe"), &exe_)) {
        *error = "não leu o dirtrally2.exe";
        return nullptr;
      }
      ok = a->OpenHeadless(path, exe_, error);
    }
    NefsArchive *raw = ok ? a.get() : nullptr;
    archives_[name] = ok ? std::move(a) : nullptr;
    return raw;
  }

  std::string dir_;
  std::vector<uint8_t> exe_;
  std::map<std::string, std::unique_ptr<NefsArchive>> archives_;
};

// ---- catálogo ----

const Row *FindRow(Ctpk &c, uint32_t table, uint32_t id) {
  Table *t = c.Find(table);
  if (t == nullptr) return nullptr;
  for (const Row &r : t->rows)
    if (r.id == id) return &r;
  return nullptr;
}

// Porte de register() do custom_track.py, com várias rotas na mesma pista.
bool RegisterTrack(Ctpk &c, Ctpk &original, const Manifest &m, Registry &reg, std::string *error) {
  Table *models = c.Find(kTrackModel);
  if (models == nullptr || c.Find(kTrack) == nullptr || c.Find(kLocation) == nullptr || c.Find(kCountry) == nullptr ||
      c.Find(kTrackModelSurface) == nullptr) {
    *error = "catálogo sem as tabelas de pista";
    return false;
  }
  // Rotas da hospedeira.
  const uint32_t hostH = Djb2(m.hostTrack);
  std::vector<Row> hostRoutes;
  for (const RouteInfo &ri : m.routes) {
    const uint32_t rh = Djb2(ri.hostRoute);
    const Row *found = nullptr;
    for (const Row &r : models->rows) {
      Fields f;
      if (DecodeFields(r.raw, &f) && Varint(f, 2) == hostH && Varint(f, 13) == rh) {
        found = &r;
        break;
      }
    }
    if (found == nullptr) {
      *error = "a rota " + m.hostTrack + "/" + ri.hostRoute + " não está no catálogo";
      return false;
    }
    hostRoutes.push_back(*found);
  }
  // O id não pode ser de uma pista do jogo.
  const uint32_t nameH = Djb2(m.id);
  for (const Row &r : original.Find(kTrack)->rows) {
    Fields f;
    if (DecodeFields(r.raw, &f) && Varint(f, 3) == nameH) {
      *error = "o id " + m.id + " é de uma pista do jogo";
      return false;
    }
  }

  // Ids: os do registro; os que faltam, depois do maior do jogo e do registro.
  RegEntry *e = reg.Find(m.id);
  if (e == nullptr) {
    reg.entries.emplace_back(m.id, RegEntry{});
    e = &reg.entries.back().second;
    reg.dirty = true;
  }
  auto alloc = [&](uint32_t table, uint32_t &slot) {
    if (slot != 0 && FindRow(original, table, slot) == nullptr) return;
    slot = std::max(original.MaxId(table), reg.Max(table)) + 1;
    reg.dirty = true;
  };
  alloc(kLocation, e->location);
  alloc(kCountry, e->country);
  alloc(kTrack, e->track);
  std::vector<uint32_t> routeIds;
  for (const RouteInfo &ri : m.routes) {
    uint32_t id = e->Route(ri.id);
    if (id == 0 || FindRow(original, kTrackModel, id) != nullptr) {
      id = std::max(original.MaxId(kTrackModel), reg.Max(kTrackModel)) + 1;
      bool set = false;
      for (auto &r : e->routes)
        if (r.first == ri.id) r.second = id, set = true;
      if (!set) e->routes.emplace_back(ri.id, id);
      reg.dirty = true;
    }
    routeIds.push_back(id);
  }

  Fields src0;
  DecodeFields(hostRoutes[0].raw, &src0);
  c.AddString(m.id);
  // Pista: cópia da da hospedeira com o nome trocado.
  const Row *trackRow = FindRow(c, kTrack, static_cast<uint32_t>(Varint(src0, 12)));
  const Row *locRow = FindRow(c, kLocation, static_cast<uint32_t>(Varint(src0, 5)));
  const Row *countryRow = FindRow(c, kCountry, static_cast<uint32_t>(Varint(src0, 4)));
  if (trackRow == nullptr || locRow == nullptr || countryRow == nullptr) {
    *error = "linhas da hospedeira incompletas no catálogo";
    return false;
  }
  Fields tf, lf, cf;
  DecodeFields(trackRow->raw, &tf);
  DecodeFields(locRow->raw, &lf);
  DecodeFields(countryRow->raw, &cf);
  for (Field &f : tf)
    if (f.wire == 0 && f.value == hostH) f.value = nameH;
  SetField(tf, 1, e->track);

  const std::string p = TextPrefix(m.id);
  // Location e país próprios (os nomes internos ficam os da hospedeira: as pastas vêm deles).
  SetField(lf, 1, e->location);
  for (const auto &[fn, key] : std::vector<std::pair<uint32_t, std::string>>{
           {10, p + "_location_caps"}, {11, p + "_desc"}, {13, p + "_location_full"}, {19, p + "_location"}})
    SetField(lf, fn, c.AddString(key));
  SetField(cf, 1, e->country);
  for (const auto &[fn, key] :
       std::vector<std::pair<uint32_t, std::string>>{{4, p + "_country_caps"}, {7, p + "_country"}})
    SetField(cf, fn, c.AddString(key));
  c.PutRow(kTrack, Row{e->track, EncodeFields(tf)});
  c.PutRow(kCountry, Row{e->country, EncodeFields(cf)});
  SetField(lf, 3, e->country);
  if (m.hasMap) {
    SetField(lf, 15, static_cast<uint64_t>(static_cast<int64_t>(std::lround(m.mapLat))));
    SetField(lf, 16, static_cast<uint64_t>(static_cast<int64_t>(std::lround(m.mapLon))));
    if (m.mapRegion >= 0) SetField(lf, 17, static_cast<uint64_t>(m.mapRegion));
  }
  c.PutRow(kLocation, Row{e->location, EncodeFields(lf)});

  for (size_t i = 0; i < m.routes.size(); ++i) {
    const RouteInfo &ri = m.routes[i];
    Fields rf;
    DecodeFields(hostRoutes[i].raw, &rf);
    SetField(rf, 1, routeIds[i]);
    SetField(rf, 2, nameH);
    SetField(rf, 4, e->country);
    SetField(rf, 5, e->location);
    SetField(rf, 12, e->track);
    SetField(rf, 13, c.AddString(ri.id));
    SetField(rf, 17, c.AddString(p + "_" + ri.id));
    SetField(rf, 18, c.AddString(p + "_" + ri.id + "_caps"));
    if (ri.lengthM > 0) {
      const float len = static_cast<float>(ri.lengthM);
      uint32_t bits;
      std::memcpy(&bits, &len, 4);
      SetField(rf, kLengthField, bits);
    }
    SetField(rf, 74, c.AddString(m.id + "_" + ri.id));
    SetField(rf, 75, c.AddString(m.id + "_" + ri.id + "_spline"));
    c.PutRow(kTrackModel, Row{routeIds[i], EncodeFields(rf)});
  }
  // Superfícies: geradas a cada montagem, depois do maior id.
  for (size_t i = 0; i < m.routes.size(); ++i) {
    std::vector<Row> copies;
    for (const Row &r : c.Find(kTrackModelSurface)->rows) {
      Fields sf;
      if (DecodeFields(r.raw, &sf) && Varint(sf, 3) == hostRoutes[i].id) copies.push_back(r);
    }
    for (const Row &r : copies) {
      const uint32_t nid = c.MaxId(kTrackModelSurface) + 1;
      Fields sf;
      DecodeFields(r.raw, &sf);
      SetField(sf, 1, nid);
      SetField(sf, 3, routeIds[i]);
      c.PutRow(kTrackModelSurface, Row{nid, EncodeFields(sf)});
    }
  }
  return true;
}

// Nome interno da location da hospedeira (campo 2 da linha), usado nos cards de reserva.
std::string HostLocationName(Ctpk &c, const Manifest &m) {
  const uint32_t hostH = Djb2(m.hostTrack);
  for (const Row &r : c.Find(kTrackModel)->rows) {
    Fields f;
    if (!DecodeFields(r.raw, &f) || Varint(f, 2) != hostH) continue;
    const Row *loc = FindRow(c, kLocation, static_cast<uint32_t>(Varint(f, 5)));
    Fields lf;
    if (loc != nullptr && DecodeFields(loc->raw, &lf)) return c.StringOf(static_cast<uint32_t>(Varint(lf, 2)));
  }
  return {};
}

// ---- alfinete do mapa-múndi ----

// Posição do nó map_marker_* no world_map_root (translação do TRANSFORM) em função do lugar no
// mundo: reta ajustada (mínimos quadrados) aos 27 marcadores do jogo contra o lat/lon das locations
// no catálogo; erro máximo de ~0,17 em x (os do jogo foram postos à mão).
constexpr double kMapX0 = 1.96478, kMapXPerLon = 0.0129152;
constexpr double kMapY0 = -1.74648, kMapYPerLat = 0.0157565;

// Move o marcador sobrando da location (MapMarkerName) para o lat/lon do pacote ou, sem "map", para
// o lugar do marcador da hospedeira. Só troca bytes: o jogo cai ao carregar um persistentDB maior
// que o original (mesmo com a árvore válida; clones do marcador derrubaram o boot).
int MoveMapMarker(Pssg &doc, const std::string &host, uint32_t location, const Manifest &m,
                  const std::function<void(const std::string &)> &warn) {
  auto transformOf = [&](const std::string &id) -> PssgNode * {
    PssgNode *n = doc.FindById(id);
    if (n == nullptr) return nullptr;
    for (auto &c : n->children)
      if (doc.TypeName(*c) == "TRANSFORM" && c->payload.size() == 64) return c.get();
    return nullptr;
  };
  const std::string spare = MapMarkerName(location);
  PssgNode *dst = spare.empty() ? nullptr : transformOf("map_marker_" + spare);
  if (dst == nullptr) {
    if (warn) warn("ctracks: " + m.id + ": sem marcador livre no mapa-múndi; o alfinete fica no da hospedeira");
    return 0;
  }
  // Matriz 4x4 big-endian; a translação está nos floats 12 e 13.
  if (m.hasMap) {
    auto put = [&](size_t i, double v) {
      float f = static_cast<float>(v);
      uint32_t u;
      std::memcpy(&u, &f, 4);
      for (int b = 0; b < 4; ++b) dst->payload[i * 4 + b] = static_cast<uint8_t>(u >> (24 - 8 * b));
    };
    put(12, kMapX0 + kMapXPerLon * m.mapLon);
    put(13, kMapY0 + kMapYPerLat * m.mapLat);
  } else if (const PssgNode *src = transformOf("map_marker_" + host)) {
    dst->payload = src->payload;
  }
  return 1;
}

// ---- assinatura ----

std::string Stamp(const std::string &path) {
  std::error_code ec;
  const fs::path p = U8Path(path);
  const auto size = fs::file_size(p, ec);
  if (ec) return "-";
  const auto t = fs::last_write_time(p, ec).time_since_epoch().count();
  return std::to_string(size) + ":" + std::to_string(static_cast<long long>(t));
}

std::string ReadText(const std::string &path) {
  std::vector<uint8_t> d;
  return ReadAll(path, &d) ? std::string(d.begin(), d.end()) : std::string();
}

bool WriteText(const std::string &path, const std::string &text) {
  return WriteAll(path, std::vector<uint8_t>(text.begin(), text.end()));
}

} // namespace

std::string CardLocationName(uint32_t locationId) { return "dr2hook_loc" + std::to_string(locationId); }

std::string MapMarkerName(uint32_t locationId) {
  // Marcadores do world_map_root sem location no catálogo (sobras do DR1). Mesma tabela em
  // src/core/location_tabs.cpp (MapLocationName).
  static const char *const kSpare[] = {"argentina", "la_figuera", "baumholder", "argolis"};
  if (locationId < kFirstCardLocation) return {};
  return kSpare[(locationId - kFirstCardLocation) % 4];
}

BuildResult BuildTrackPackages(const BuildOptions &opt) {
  BuildResult res;
  auto info = [&](const std::string &s) {
    if (opt.info) opt.info(s);
  };
  auto warn = [&](const std::string &s) {
    if (opt.warn) opt.warn(s);
  };
  auto progress = [&](BuildEvent e, const std::string &s, double fraction = -1) {
    if (opt.progress) opt.progress(e, s, fraction);
  };
  const std::string root = Join(opt.gameDir, "ctracks");
  const std::string cache = Join(root, "_cache");
  res.cacheDir = cache;
  std::error_code ec;
  fs::create_directories(U8Path(root), ec);

  // Pacotes, em ordem de nome de arquivo.
  std::vector<std::string> files;
  for (const auto &de : fs::directory_iterator(U8Path(root), ec)) {
    if (!de.is_regular_file(ec)) continue;
    std::string name = PathU8(de.path().filename());
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    if (lower.size() > 9 && lower.compare(lower.size() - 9, 9, ".dr2track") == 0) files.push_back(name);
  }
  std::sort(files.begin(), files.end());

  std::vector<std::unique_ptr<Package>> pkgs;
  std::string sig = std::string(kLoaderVersion) + "\nexe " + Stamp(Join(opt.gameDir, "dirtrally2.exe"));
  for (size_t i = 0; i < files.size(); ++i) {
    const std::string &f = files[i];
    progress(BuildEvent::Step, "Reading track packages (" + std::to_string(i + 1) + "/" + std::to_string(files.size()) + "): " + f,
             0.1 * i / files.size());
    auto pkg = std::make_unique<Package>();
    std::string err;
    if (!pkg->Open(Join(root, f), &err)) {
      warn("ctracks: " + f + " ignorado: " + err);
      progress(BuildEvent::Problem, f + ": invalid package, skipped");
      continue;
    }
    bool dup = false;
    for (const auto &o : pkgs) dup |= o->manifest().id == pkg->manifest().id;
    if (dup) {
      warn("ctracks: " + f + " ignorado: outra pista já usa o id " + pkg->manifest().id);
      progress(BuildEvent::Problem, f + ": another package uses the same track id, skipped");
      continue;
    }
    sig += "\npkg " + f + " " + Stamp(Join(root, f));
    pkgs.push_back(std::move(pkg));
  }
  progress(BuildEvent::Done, pkgs.empty() ? std::string("No track packages in ctracks/")
                                          : std::to_string(pkgs.size()) + " track package(s) found in ctracks/",
           0.1);
  const std::string regPath = Join(root, "registry.ini");
  Registry reg;
  reg.Load(ReadText(regPath));
  auto withRegistry = [&](const std::string &text) { return sig + "\nregistry " + std::to_string(Djb2(text)); };
  const std::string sigIn = withRegistry(ReadText(regPath));

  // Cache em dia: só a tabela de execução.
  const std::string sigPath = Join(cache, "signature.txt");
  const std::string old = ReadText(sigPath);
  std::set<std::string> active;
  const bool fresh = old.compare(0, sigIn.size(), sigIn) == 0 && (old.size() == sigIn.size() || old[sigIn.size()] == '\n');
  if (fresh) {
    progress(BuildEvent::Done, "Track cache is up to date", 1);
    std::istringstream in(old.substr(sigIn.size()));
    std::string line;
    while (std::getline(in, line))
      if (line.compare(0, 7, "active ") == 0) active.insert(line.substr(7));
  } else {
    res.regenerated = true;
    info("ctracks: montando o cache (" + std::to_string(pkgs.size()) + " pacote(s))");
    progress(BuildEvent::Step, "Building the track catalogue", 0.12);
    for (const char *sub : {"catalogues", "frontend", "tracks"}) fs::remove_all(U8Path(Join(cache, sub)), ec);
    fs::remove(U8Path(sigPath), ec);

    GameFiles game(opt.gameDir);
    std::string err;
    std::vector<uint8_t> raw;
    Ctpk original, cat;
    if (!game.Read("catalogues/base.ctpk", &raw, &err) || !original.Parse(raw, &err) || !cat.Parse(raw, &err)) {
      warn("ctracks: sem o catálogo do jogo: " + err);
      return res;
    }
    std::vector<Package *> ok;
    for (const auto &pkg : pkgs) {
      if (!RegisterTrack(cat, original, pkg->manifest(), reg, &err)) {
        warn("ctracks: " + pkg->manifest().id + " ignorada: " + err);
        progress(BuildEvent::Problem, pkg->manifest().id + ": could not be added to the catalogue, skipped");
        continue;
      }
      ok.push_back(pkg.get());
      active.insert(pkg->manifest().id);
    }
    if (reg.dirty && !WriteText(regPath, reg.Save())) warn("ctracks: não gravou " + regPath);
    if (!WriteAll(Join(cache, "catalogues/base.ctpk"), cat.Build())) {
      warn("ctracks: não gravou o catálogo no cache");
      return res;
    }

    // Arquivos do jogo enxertados: os alvos dos pacotes, os bundles dos cards e o do selo.
    std::map<std::string, std::vector<std::pair<Package *, std::string>>> grafts; // alvo -> (pacote, entrada)
    for (Package *pkg : ok)
      for (const std::string &en : pkg->Entries()) {
        if (en.compare(0, 6, "graft/") != 0) continue;
        const std::string target = GraftTarget(en.substr(6));
        if (target.empty()) warn("ctracks: " + pkg->manifest().id + ": enxerto não permitido: " + en);
        else grafts[target].emplace_back(pkg, en);
      }
    std::set<std::string> targets = {kBadgeBundle, kCardBundles[0], kCardBundles[1], kMapDatabase};
    for (const auto &g : grafts) targets.insert(g.first);
    size_t done = 0;
    for (const std::string &target : targets) {
      progress(BuildEvent::Step,
               "Cards, map and loading screens (" + std::to_string(++done) + "/" + std::to_string(targets.size()) + ")",
               0.15 + 0.3 * (done - 1) / targets.size());
      Pssg doc;
      if (!game.Read(target, &raw, &err) || !doc.Parse(raw, &err)) {
        warn("ctracks: " + target + ": " + err);
        continue;
      }
      int added = 0;
      for (const auto &[pkg, entry] : grafts[target]) {
        const Manifest &m = pkg->manifest();
        std::vector<uint8_t> fragRaw;
        Pssg frag;
        const std::map<std::string, std::string> subst = {{"$LOC", CardLocationName(reg.Find(m.id)->location)},
                                                          {"$TRACK", m.id}};
        if (!pkg->Read(entry, &fragRaw, &err) || !frag.Parse(fragRaw, &err)) {
          warn("ctracks: " + m.id + ": " + entry + ": " + err);
          continue;
        }
        const int n = doc.Graft(frag, subst, &err);
        if (n < 0) warn("ctracks: " + m.id + ": " + entry + ": " + err);
        else added += n;
      }
      if (target == kBadgeBundle && kBadgeFragmentSize > 0) {
        Pssg frag;
        const int n = frag.Parse(std::vector<uint8_t>(kBadgeFragment, kBadgeFragment + kBadgeFragmentSize), &err)
                          ? doc.Graft(frag, {}, &err)
                          : -1;
        if (n < 0) warn("ctracks: selo: " + err);
        else added += n;
      }
      // Card de reserva: sem textura própria, a pista usa as da location hospedeira.
      if (target == kCardBundles[0] || target == kCardBundles[1]) {
        for (Package *pkg : ok) {
          const std::string host = HostLocationName(cat, pkg->manifest());
          const std::string loc = CardLocationName(reg.Find(pkg->manifest().id)->location);
          for (const auto &[from, to] : std::vector<std::pair<std::string, std::string>>{
                   {"s_" + host, "s_" + loc}, {"s_" + host + "_l", "s_" + loc + "_l"}, {"panel_" + host, "panel_" + loc}}) {
            PssgNode *parent = nullptr;
            const PssgNode *src = host.empty() || doc.FindById(to) != nullptr ? nullptr : doc.FindById(from, &parent);
            if (src == nullptr) continue;
            auto copy = Pssg::Clone(*src);
            doc.SetStr(*copy, "id", to);
            parent->children.push_back(std::move(copy));
            ++added;
          }
        }
      }
      if (target == kMapDatabase)
        for (Package *pkg : ok)
          added += MoveMapMarker(doc, HostLocationName(cat, pkg->manifest()), reg.Find(pkg->manifest().id)->location,
                                 pkg->manifest(), warn);
      if (!WriteAll(Join(cache, target), doc.Serialize())) {
        warn("ctracks: não gravou " + target);
        return res;
      }
      info("ctracks: " + target + " (+" + std::to_string(added) + " nós)");
    }

    progress(BuildEvent::Done, "Catalogue, cards and map updated", 0.45);

    // track/ e data/
    for (size_t i = 0; i < ok.size(); ++i) {
      Package *pkg = ok[i];
      const Manifest &m = pkg->manifest();
      const std::string step =
          "Extracting tracks (" + std::to_string(i + 1) + "/" + std::to_string(ok.size()) + "): " + m.name;
      size_t count = 0, seen = 0;
      const auto &entries = pkg->Entries();
      for (const std::string &en : entries) {
        progress(BuildEvent::Step, step, 0.45 + 0.55 * (i + double(seen++) / entries.size()) / ok.size());
        std::string dest;
        if (en.compare(0, 6, "track/") == 0) {
          dest = "tracks/locations/" + m.hostLocation + "/" + m.id + "/" + en.substr(6);
        } else if (en.compare(0, 5, "data/") == 0) {
          if (!AllowedDataPath(en.substr(5), m.id)) {
            warn("ctracks: " + m.id + ": arquivo fora dos prefixos permitidos: " + en);
            continue;
          }
          dest = en.substr(5);
        } else {
          continue;
        }
        if (!pkg->Read(en, &raw, &err) || !WriteAll(Join(cache, dest), raw)) {
          warn("ctracks: " + m.id + ": " + en + ": " + (err.empty() ? "não gravou" : err));
          return res;
        }
        ++count;
      }
      info("ctracks: " + m.id + ": " + std::to_string(count) + " arquivo(s)");
    }
    if (!ok.empty()) progress(BuildEvent::Done, "Track files extracted");
    // O registro pode ter mudado agora: a assinatura usa o que ficou gravado.
    std::string sigOut = withRegistry(ReadText(regPath));
    for (const std::string &id : active) sigOut += "\nactive " + id;
    if (!WriteText(sigPath, sigOut + "\n")) warn("ctracks: não gravou a assinatura do cache");
  }

  for (const auto &pkg : pkgs) {
    const Manifest &m = pkg->manifest();
    const RegEntry *e = reg.Find(m.id);
    if (!active.count(m.id) || e == nullptr) continue;
    TrackRuntime t;
    t.id = m.id;
    t.hostLocation = m.hostLocation;
    t.hostTrack = m.hostTrack;
    t.location = e->location;
    for (const std::string &en : pkg->Entries())
      if (en.compare(0, 6, "track/") == 0) t.files.insert(en.substr(6));
    res.tracks.push_back(std::move(t));
    const std::string p = TextPrefix(m.id);
    const std::string country = m.country.empty() ? m.hostLocation : m.country;
    res.texts.emplace_back(p, Upper(m.name));
    res.texts.emplace_back(p + "_location", m.name);
    res.texts.emplace_back(p + "_location_full", m.name + ", " + country);
    res.texts.emplace_back(p + "_country", country);
    res.texts.emplace_back(p + "_desc", m.description);
    for (const RouteInfo &ri : m.routes) res.texts.emplace_back(p + "_" + ri.id, ri.name);
  }
  res.cacheReady = true;
  info("ctracks: " + std::to_string(res.tracks.size()) + " pista(s) " + (res.regenerated ? "(cache novo)" : "(cache em dia)"));
  return res;
}

} // namespace trackpkg
