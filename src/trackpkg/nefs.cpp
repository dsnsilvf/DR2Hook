#include "trackpkg/nefs.h"

#include "trackpkg/aes.h"
#include "trackpkg/fsutil.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>

#include "miniz.h"

namespace trackpkg {
namespace {

constexpr uint32_t kBlock = 0x10000;
constexpr uint32_t kVersion2 = 0x20000;

uint32_t U32(const std::vector<uint8_t> &b, size_t at) {
  uint32_t v;
  std::memcpy(&v, b.data() + at, 4);
  return v;
}

uint64_t U64(const std::vector<uint8_t> &b, size_t at) {
  uint64_t v;
  std::memcpy(&v, b.data() + at, 8);
  return v;
}

bool HexKey(const uint8_t *hex, uint8_t out[32]) {
  auto nib = [](uint8_t c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  for (int i = 0; i < 32; ++i) {
    const int hi = nib(hex[i * 2]), lo = nib(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = static_cast<uint8_t>(hi << 4 | lo);
  }
  return true;
}

int64_t FileSize(const std::string &path) {
  std::error_code ec;
  const auto n = std::filesystem::file_size(U8Path(path), ec);
  return ec ? -1 : static_cast<int64_t>(n);
}

bool SeekRead(FILE *f, uint64_t at, void *dst, size_t n) {
#if defined(_WIN32)
  if (_fseeki64(f, static_cast<int64_t>(at), SEEK_SET) != 0) return false;
#else
  if (fseeko(f, static_cast<off_t>(at), SEEK_SET) != 0) return false;
#endif
  return std::fread(dst, 1, n, f) == n;
}

// Deflate cru com saída exata; um bloco cifrado pode passar por deflate válido por acaso.
bool Inflate(const uint8_t *src, size_t n, uint8_t *dst, size_t expected) {
  const size_t got = tinfl_decompress_mem_to_mem(dst, expected, src, n, 0);
  return got == expected;
}

} // namespace

bool NefsArchive::Open(const std::string &nefsPath, std::string *error) {
  FILE *f = OpenU8(nefsPath, "rb");
  if (f == nullptr) {
    *error = "não abriu " + nefsPath;
    return false;
  }
  std::vector<uint8_t> intro(0x6c);
  bool ok = std::fread(intro.data(), 1, intro.size(), f) == intro.size() && std::memcmp(intro.data(), "NeFS", 4) == 0;
  std::vector<uint8_t> header;
  if (ok) {
    const uint32_t size = U32(intro, 0x64);
    ok = size >= 0x100 && size <= 64u * 1024 * 1024;
    if (ok) {
      header.resize(size);
      ok = SeekRead(f, 0, header.data(), size);
    }
  }
  std::fclose(f);
  if (!ok) {
    *error = "cabeçalho NeFS inválido em " + nefsPath;
    return false;
  }
  dataPath_ = nefsPath;
  return Parse(std::move(header), error);
}

bool NefsArchive::OpenHeadless(const std::string &dataPath, const std::vector<uint8_t> &exe, std::string *error) {
  const int64_t dataSize = FileSize(dataPath);
  if (dataSize < 0) {
    *error = "não abriu " + dataPath;
    return false;
  }
  dataPath_ = dataPath;
  std::unique_ptr<NefsArchive> best;
  uint64_t bestEnd = 0;
  for (size_t pos = 0; pos + 0x100 <= exe.size(); ++pos) {
    if (exe[pos] != 'N' || std::memcmp(exe.data() + pos, "NeFS", 4) != 0) continue;
    const uint32_t size = U32(exe, pos + 0x64), version = U32(exe, pos + 0x68);
    uint8_t key[32];
    if (version != kVersion2 || size < 0x100 || size > exe.size() - pos || !HexKey(exe.data() + pos + 0x24, key))
      continue;
    auto a = std::make_unique<NefsArchive>();
    a->dataPath_ = dataPath;
    std::string err;
    if (!a->Parse(std::vector<uint8_t>(exe.begin() + pos, exe.begin() + pos + size), &err)) continue;
    const uint64_t end = a->DataEnd();
    if (end <= static_cast<uint64_t>(dataSize) && (best == nullptr || end > bestEnd)) {
      bestEnd = end;
      best = std::move(a);
    }
  }
  if (best == nullptr || static_cast<uint64_t>(dataSize) - bestEnd > kBlock) {
    *error = "nenhum cabeçalho embutido no exe corresponde a " + dataPath;
    return false;
  }
  *this = std::move(*best);
  return true;
}

bool NefsArchive::Parse(std::vector<uint8_t> header, std::string *error) {
  header_ = std::move(header);
  const auto &h = header_;
  if (h.size() < 0xa4 || std::memcmp(h.data(), "NeFS", 4) != 0 || !HexKey(h.data() + 0x24, key_)) {
    *error = "cabeçalho NeFS sem magia ou chave";
    return false;
  }
  const uint32_t p1 = U32(h, 0x84), p2 = U32(h, 0x8c), p3 = U32(h, 0x94), p4 = U32(h, 0x98), p5 = U32(h, 0x9c);
  if (!(p1 <= p2 && p2 <= p3 && p3 <= p4 && p4 <= p5 && p5 <= h.size())) {
    *error = "partes do cabeçalho NeFS fora de ordem";
    return false;
  }
  blockEnds_ = p4;
  blockEndsEnd_ = p5;
  std::unordered_map<uint32_t, std::pair<uint64_t, uint32_t>> items; // id -> (offset, primeiro bloco)
  for (uint32_t at = p1; at + 20 <= p2; at += 20) items[U32(h, at + 16)] = {U64(h, at), U32(h, at + 12)};
  struct Dir {
    uint32_t parent, firstChild, name, size;
  };
  std::unordered_map<uint32_t, Dir> dirs;
  for (uint32_t at = p2; at + 20 <= p3; at += 20)
    dirs[U32(h, at + 16)] = {U32(h, at), U32(h, at + 4), U32(h, at + 8), U32(h, at + 12)};
  auto name = [&](uint32_t off) -> std::string {
    if (p3 + off >= p4) return {};
    const char *s = reinterpret_cast<const char *>(h.data() + p3 + off);
    return std::string(s, strnlen(s, p4 - p3 - off));
  };
  files_.clear();
  for (const auto &[id, d] : dirs) {
    if (d.firstChild != id) continue; // diretório
    const auto item = items.find(id);
    if (item == items.end()) continue; // listado mas sem dados neste pacote
    std::string path = name(d.name);
    uint32_t cur = d.parent, prev = id;
    for (int depth = 0; depth < 64 && cur != prev; ++depth) {
      const auto p = dirs.find(cur);
      if (p == dirs.end()) break;
      path = name(p->second.name) + "/" + path;
      prev = cur;
      cur = p->second.parent;
    }
    files_[path] = {item->second.first, d.size, item->second.second};
  }
  return true;
}

uint64_t NefsArchive::DataEnd() const {
  uint64_t end = 0;
  for (const auto &[path, f] : files_) {
    if (f.size == 0) continue;
    const uint32_t count = (f.size + kBlock - 1) / kBlock;
    const size_t at = blockEnds_ + (static_cast<size_t>(f.firstBlock) + count - 1) * 4;
    if (at + 4 > blockEndsEnd_) continue;
    end = std::max<uint64_t>(end, f.offset + U32(header_, at));
  }
  return end;
}

bool NefsArchive::Read(const std::string &path, std::vector<uint8_t> *out, std::string *error) const {
  const auto it = files_.find(path);
  if (it == files_.end()) {
    *error = path + " não está no pacote";
    return false;
  }
  const File &f = it->second;
  const uint32_t count = (f.size + kBlock - 1) / kBlock;
  if (blockEnds_ + (static_cast<size_t>(f.firstBlock) + count) * 4 > blockEndsEnd_) {
    *error = path + ": tabela de blocos curta";
    return false;
  }
  FILE *fh = OpenU8(dataPath_, "rb");
  if (fh == nullptr) {
    *error = "não abriu " + dataPath_;
    return false;
  }
  const Aes256 aes(key_);
  out->assign(f.size, 0);
  std::vector<uint8_t> block;
  uint32_t prev = 0;
  bool ok = true;
  for (uint32_t i = 0; i < count && ok; ++i) {
    const uint32_t end = U32(header_, blockEnds_ + (static_cast<size_t>(f.firstBlock) + i) * 4);
    const uint32_t expected = std::min<uint32_t>(kBlock, f.size - i * kBlock);
    if (end < prev) {
      ok = false;
      break;
    }
    block.resize(end - prev);
    ok = SeekRead(fh, f.offset + prev, block.data(), block.size());
    uint8_t *dst = out->data() + static_cast<size_t>(i) * kBlock;
    if (ok && !Inflate(block.data(), block.size(), dst, expected)) {
      if (block.size() % 16 == 0) aes.DecryptEcb(block.data(), block.size());
      if (!Inflate(block.data(), block.size(), dst, expected)) {
        // bloco guardado como está (ou só cifrado)
        if (block.size() < expected) ok = false;
        else std::memcpy(dst, block.data(), expected);
      }
    }
    prev = end;
  }
  std::fclose(fh);
  if (!ok) *error = path + ": bloco ilegível";
  return ok;
}

} // namespace trackpkg
