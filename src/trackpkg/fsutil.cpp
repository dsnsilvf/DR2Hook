#include "trackpkg/fsutil.h"

#include <system_error>

namespace trackpkg {

std::filesystem::path U8Path(const std::string &utf8) {
  return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

std::string PathU8(const std::filesystem::path &p) {
  const std::u8string s = p.u8string();
  return std::string(s.begin(), s.end());
}

FILE *OpenU8(const std::string &utf8, const char *mode) {
#if defined(_WIN32)
  const std::wstring wmode(mode, mode + std::char_traits<char>::length(mode));
  return _wfopen(U8Path(utf8).c_str(), wmode.c_str());
#else
  return std::fopen(utf8.c_str(), mode);
#endif
}

bool ReadAll(const std::string &utf8, std::vector<uint8_t> *out) {
  FILE *f = OpenU8(utf8, "rb");
  if (f == nullptr) return false;
  out->clear();
  uint8_t buf[1 << 16];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out->insert(out->end(), buf, buf + n);
  const bool ok = !std::ferror(f);
  std::fclose(f);
  return ok;
}

bool WriteAll(const std::string &utf8, const std::vector<uint8_t> &data) {
  std::error_code ec;
  const auto path = U8Path(utf8);
  std::filesystem::create_directories(path.parent_path(), ec);
  const std::string tmp = utf8 + ".tmp";
  FILE *f = OpenU8(tmp, "wb");
  if (f == nullptr) return false;
  const bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
  if (std::fclose(f) != 0 || !ok) {
    std::filesystem::remove(U8Path(tmp), ec);
    return false;
  }
  std::filesystem::rename(U8Path(tmp), path, ec);
  return !ec;
}

} // namespace trackpkg
