#include "trackpkg/ctpk.h"

#include <algorithm>
#include <cstring>

namespace trackpkg {
namespace {

bool Varint(const std::vector<uint8_t> &b, size_t &p, uint64_t *v) {
  *v = 0;
  for (int shift = 0; shift < 64; shift += 7) {
    if (p >= b.size()) return false;
    const uint8_t c = b[p++];
    *v |= static_cast<uint64_t>(c & 0x7f) << shift;
    if (c < 0x80) return true;
  }
  return false;
}

void PutVarint(std::vector<uint8_t> &out, uint64_t v) {
  do {
    uint8_t b = v & 0x7f;
    v >>= 7;
    out.push_back(v ? (b | 0x80) : b);
  } while (v);
}

uint32_t Rd32(const std::vector<uint8_t> &b, size_t at) {
  uint32_t v;
  std::memcpy(&v, b.data() + at, 4);
  return v;
}

void Wr32(std::vector<uint8_t> &b, uint32_t v) {
  uint8_t t[4];
  std::memcpy(t, &v, 4);
  b.insert(b.end(), t, t + 4);
}

} // namespace

uint32_t Djb2(const std::string &s) {
  uint32_t h = 5381;
  for (unsigned char c : s) h = h * 33 + c;
  return h;
}

bool DecodeFields(const std::vector<uint8_t> &msg, Fields *out) {
  out->clear();
  size_t p = 0;
  while (p < msg.size()) {
    uint64_t key;
    if (!Varint(msg, p, &key)) return false;
    Field f;
    f.number = static_cast<uint32_t>(key >> 3);
    f.wire = static_cast<uint32_t>(key & 7);
    switch (f.wire) {
    case 0:
      if (!Varint(msg, p, &f.value)) return false;
      break;
    case 1:
      if (p + 8 > msg.size()) return false;
      std::memcpy(&f.value, msg.data() + p, 8);
      p += 8;
      break;
    case 2: {
      uint64_t n;
      if (!Varint(msg, p, &n) || p + n > msg.size()) return false;
      f.bytes.assign(msg.begin() + p, msg.begin() + p + n);
      p += n;
      break;
    }
    case 5: {
      if (p + 4 > msg.size()) return false;
      uint32_t v;
      std::memcpy(&v, msg.data() + p, 4);
      f.value = v;
      p += 4;
      break;
    }
    default:
      return false;
    }
    out->push_back(std::move(f));
  }
  return true;
}

std::vector<uint8_t> EncodeFields(const Fields &fields) {
  std::vector<uint8_t> out;
  for (const Field &f : fields) {
    PutVarint(out, static_cast<uint64_t>(f.number) << 3 | f.wire);
    if (f.wire == 0) {
      PutVarint(out, f.value);
    } else if (f.wire == 1) {
      uint8_t t[8];
      std::memcpy(t, &f.value, 8);
      out.insert(out.end(), t, t + 8);
    } else if (f.wire == 2) {
      PutVarint(out, f.bytes.size());
      out.insert(out.end(), f.bytes.begin(), f.bytes.end());
    } else {
      Wr32(out, static_cast<uint32_t>(f.value));
    }
  }
  return out;
}

const Field *FindField(const Fields &f, uint32_t number) {
  for (const Field &x : f)
    if (x.number == number) return &x;
  return nullptr;
}

bool SetField(Fields &f, uint32_t number, uint64_t value) {
  bool any = false;
  for (Field &x : f)
    if (x.number == number && x.wire != 2) {
      x.value = value;
      any = true;
    }
  return any;
}

bool Ctpk::Parse(const std::vector<uint8_t> &d, std::string *error) {
  auto fail = [&](const char *why) {
    *error = std::string("catálogo: ") + why;
    return false;
  };
  if (d.size() < 0x1c || std::memcmp(d.data(), "CTPK", 4) != 0) return fail("sem a magia CTPK");
  version_ = Rd32(d, 4);
  checksum_ = Rd32(d, 8);
  const uint32_t strOff = Rd32(d, 12), strEnd = Rd32(d, 16);
  if (strOff + 4 > d.size() || strEnd + 4 > d.size()) return fail("cabeçalho fora do arquivo");
  const uint32_t n = Rd32(d, strOff);
  size_t p = strOff + 4;
  strings_.clear();
  stringIndex_.clear();
  for (uint32_t i = 0; i < n; ++i) {
    if (p + 4 > d.size()) return fail("strings cortadas");
    const uint32_t len = Rd32(d, p);
    if (p + 4 + len > d.size()) return fail("strings cortadas");
    strings_.emplace_back(reinterpret_cast<const char *>(d.data() + p + 4), len);
    stringIndex_.emplace(Djb2(strings_.back()), strings_.size() - 1);
    p += 4 + len;
  }
  if (p != strEnd) return fail("fim das strings não bate");
  const uint32_t nt = Rd32(d, p);
  if (p + 4 + 8ull * nt > d.size()) return fail("diretório cortado");
  tables_.clear();
  for (uint32_t i = 0; i < nt; ++i) {
    Table t;
    t.hash = Rd32(d, p + 4 + 8 * i);
    const uint32_t off = Rd32(d, p + 8 + 8 * i);
    if (off == 0xffffffffu) {
      t.empty = true;
      tables_.push_back(std::move(t));
      continue;
    }
    if (off + 4 > d.size()) return fail("tabela fora do arquivo");
    const uint32_t nr = Rd32(d, off);
    size_t q = off + 4;
    for (uint32_t r = 0; r < nr; ++r) {
      if (q + 8 > d.size()) return fail("linhas cortadas");
      Row row;
      row.id = Rd32(d, q);
      const uint32_t len = Rd32(d, q + 4);
      if (q + 8 + len > d.size()) return fail("linhas cortadas");
      row.raw.assign(d.begin() + q + 8, d.begin() + q + 8 + len);
      t.rows.push_back(std::move(row));
      q += 8 + len;
    }
    tables_.push_back(std::move(t));
  }
  return true;
}

std::vector<uint8_t> Ctpk::Build() const {
  std::vector<uint8_t> body;
  Wr32(body, static_cast<uint32_t>(strings_.size()));
  for (const auto &s : strings_) {
    Wr32(body, static_cast<uint32_t>(s.size()));
    body.insert(body.end(), s.begin(), s.end());
  }
  const uint32_t strEnd = static_cast<uint32_t>(0x18 + body.size());
  const uint32_t dirEnd = static_cast<uint32_t>(strEnd + 4 + 8 * tables_.size());
  std::vector<uint8_t> dir, rows;
  Wr32(dir, static_cast<uint32_t>(tables_.size()));
  for (const Table &t : tables_) {
    Wr32(dir, t.hash);
    if (t.empty && t.rows.empty()) {
      Wr32(dir, 0xffffffffu);
      continue;
    }
    Wr32(dir, static_cast<uint32_t>(dirEnd + rows.size()));
    Wr32(rows, static_cast<uint32_t>(t.rows.size()));
    for (const Row &r : t.rows) {
      Wr32(rows, r.id);
      Wr32(rows, static_cast<uint32_t>(r.raw.size()));
      rows.insert(rows.end(), r.raw.begin(), r.raw.end());
    }
  }
  std::vector<uint8_t> out = {'C', 'T', 'P', 'K'};
  Wr32(out, version_);
  Wr32(out, checksum_);
  Wr32(out, 0x18);
  Wr32(out, strEnd);
  Wr32(out, 0);
  out.insert(out.end(), body.begin(), body.end());
  out.insert(out.end(), dir.begin(), dir.end());
  out.insert(out.end(), rows.begin(), rows.end());
  return out;
}

Table *Ctpk::Find(uint32_t hash) {
  for (Table &t : tables_)
    if (t.hash == hash) return &t;
  return nullptr;
}

uint32_t Ctpk::AddString(const std::string &s) {
  const uint32_t h = Djb2(s);
  if (!stringIndex_.count(h)) {
    strings_.push_back(s);
    stringIndex_.emplace(h, strings_.size() - 1);
  }
  return h;
}

bool Ctpk::HasString(uint32_t hash) const { return stringIndex_.count(hash) != 0; }

std::string Ctpk::StringOf(uint32_t hash) const {
  const auto it = stringIndex_.find(hash);
  return it == stringIndex_.end() ? std::string() : strings_[it->second];
}

void Ctpk::PutRow(uint32_t table, Row row) {
  Table *t = Find(table);
  if (t == nullptr) return;
  t->empty = false;
  auto it = std::lower_bound(t->rows.begin(), t->rows.end(), row.id, [](const Row &r, uint32_t id) { return r.id < id; });
  if (it != t->rows.end() && it->id == row.id) *it = std::move(row);
  else t->rows.insert(it, std::move(row));
}

uint32_t Ctpk::MaxId(uint32_t table) {
  Table *t = Find(table);
  uint32_t m = 0;
  if (t != nullptr)
    for (const Row &r : t->rows) m = std::max(m, r.id);
  return m;
}

} // namespace trackpkg
