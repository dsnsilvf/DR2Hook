#include "trackpkg/package.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <stdexcept>

#include "miniz.h"
#include "trackpkg/fsutil.h"
#include "trackpkg/json.h"

namespace trackpkg {

struct Package::Zip {
  mz_zip_archive a{};
  bool open = false;
};

bool ValidName(const std::string &s) {
  if (s.empty() || s.size() > 48) return false;
  for (char c : s)
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
  return true;
}

bool AllowedDataPath(const std::string &path, const std::string &trackId) {
  for (const char *dir : {"frontend/streamed_textures/loading/", "frontend/streamed_textures/splines/"}) {
    const std::string prefix = std::string(dir) + trackId + "_";
    if (path.compare(0, prefix.size(), prefix) == 0 && path.find('/', prefix.size()) == std::string::npos) return true;
  }
  return false;
}

std::string GraftTarget(const std::string &path) {
  for (const char *t : {"frontend/bundles/b_locations_nonpersistent_base.pssg",
                        "frontend/bundles/b_locations_persistent_base.pssg", "frontend/databases/loadingScreen.pssg"}) {
    std::string lower = t;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    if (lower == path) return t;
  }
  return {};
}

bool ParseManifest(const std::string &text, Manifest *out, std::string *error) {
  try {
    const json::Value v = json::parse(text);
    auto str = [&](const json::Value &o, const char *key, bool required) -> std::string {
      const json::Value *f = o.find(key);
      if (f == nullptr || f->is_null()) {
        if (required) throw std::runtime_error(std::string("falta \"") + key + "\"");
        return {};
      }
      return f->as_string();
    };
    Manifest m;
    m.format = static_cast<int>(v.number_or("format", 0));
    if (m.format != 1) throw std::runtime_error("format " + std::to_string(m.format) + " desconhecido (este carregador lê o 1)");
    m.id = str(v, "id", true);
    if (!ValidName(m.id)) throw std::runtime_error("id \"" + m.id + "\" inválido (só a-z, 0-9 e _)");
    m.name = str(v, "name", true);
    m.country = str(v, "country", false);
    m.description = str(v, "description", false);
    m.author = str(v, "author", false);
    m.version = str(v, "version", false);
    const json::Value &host = v["host"];
    m.hostLocation = str(host, "location", true);
    m.hostTrack = str(host, "track", true);
    if (!ValidName(m.hostLocation) || !ValidName(m.hostTrack)) throw std::runtime_error("host inválido");
    if (m.hostTrack == m.id) throw std::runtime_error("id igual ao da hospedeira");
    for (const json::Value &r : v["routes"].as_array()) {
      RouteInfo ri;
      ri.id = str(r, "id", true);
      ri.hostRoute = str(r, "host_route", false);
      if (ri.hostRoute.empty()) ri.hostRoute = ri.id;
      ri.name = str(r, "name", false);
      if (ri.name.empty()) ri.name = ri.id;
      ri.lengthM = r.number_or("length_m", 0);
      if (!ValidName(ri.id) || !ValidName(ri.hostRoute)) throw std::runtime_error("rota \"" + ri.id + "\" inválida");
      for (const RouteInfo &o : m.routes)
        if (o.id == ri.id) throw std::runtime_error("rota \"" + ri.id + "\" repetida");
      m.routes.push_back(std::move(ri));
    }
    if (m.routes.empty()) throw std::runtime_error("nenhuma rota");
    if (const json::Value *map = v.find("map"); map != nullptr && !map->is_null()) {
      const double lat = map->number_or("lat", 1000), lon = map->number_or("lon", 1000);
      if (lat < -90 || lat > 90 || lon < -180 || lon > 180) throw std::runtime_error("map: lat/lon fora da faixa");
      m.hasMap = true;
      m.mapLat = lat;
      m.mapLon = lon;
      const double region = map->number_or("region", -1);
      if (region >= 0) m.mapRegion = static_cast<int>(region);
    }
    *out = std::move(m);
    return true;
  } catch (const std::exception &e) {
    *error = std::string("manifest.json: ") + e.what();
    return false;
  }
}

Package::Package() : zip_(std::make_unique<Zip>()) {}

Package::~Package() {
  if (zip_->open) mz_zip_reader_end(&zip_->a);
  if (file_ != nullptr) std::fclose(file_);
}

bool Package::Open(const std::string &path, std::string *error) {
  file_ = OpenU8(path, "rb");
  if (file_ == nullptr) {
    *error = "não abriu";
    return false;
  }
  if (!mz_zip_reader_init_cfile(&zip_->a, file_, 0, 0)) {
    *error = std::string("zip inválido (") + mz_zip_get_error_string(mz_zip_get_last_error(&zip_->a)) + ")";
    return false;
  }
  zip_->open = true;
  const mz_uint n = mz_zip_reader_get_num_files(&zip_->a);
  for (mz_uint i = 0; i < n; ++i) {
    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(&zip_->a, i, &st) || st.m_is_directory) continue;
    std::string name = st.m_filename;
    std::replace(name.begin(), name.end(), '\\', '/');
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
    while (!name.empty() && name[0] == '/') name.erase(0, 1);
    if (name.find("..") != std::string::npos || name.find(':') != std::string::npos) {
      *error = "caminho inválido no zip: " + name;
      return false;
    }
    entries_.push_back(name);
    indices_.push_back(i);
    crcs_.push_back(st.m_crc32);
    sizes_.push_back(st.m_uncomp_size);
  }
  std::vector<uint8_t> mf;
  if (!Read("manifest.json", &mf, error)) return false;
  return ParseManifest(std::string(mf.begin(), mf.end()), &manifest_, error);
}

bool Package::Read(const std::string &entry, std::vector<uint8_t> *out, std::string *error) {
  for (size_t k = 0; k < entries_.size(); ++k) {
    if (entries_[k] != entry) continue;
    size_t size = 0;
    void *p = mz_zip_reader_extract_to_heap(&zip_->a, indices_[k], &size, 0);
    if (p == nullptr) {
      *error = entry + ": " + mz_zip_get_error_string(mz_zip_get_last_error(&zip_->a));
      return false;
    }
    out->assign(static_cast<uint8_t *>(p), static_cast<uint8_t *>(p) + size);
    mz_free(p);
    return true;
  }
  *error = "falta " + entry;
  return false;
}

bool Package::Stat(const std::string &entry, uint32_t *crc, uint64_t *size) const {
  for (size_t k = 0; k < entries_.size(); ++k) {
    if (entries_[k] != entry) continue;
    *crc = crcs_[k];
    *size = sizes_[k];
    return true;
  }
  return false;
}

} // namespace trackpkg
