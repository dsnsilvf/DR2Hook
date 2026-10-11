#include "trackpkg/pssg.h"

#include <cstring>

namespace trackpkg {
namespace {

uint32_t Be32(const std::vector<uint8_t> &d, size_t at) {
  return static_cast<uint32_t>(d[at]) << 24 | static_cast<uint32_t>(d[at + 1]) << 16 |
         static_cast<uint32_t>(d[at + 2]) << 8 | d[at + 3];
}

void PutBe32(std::vector<uint8_t> &out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v >> 24));
  out.push_back(static_cast<uint8_t>(v >> 16));
  out.push_back(static_cast<uint8_t>(v >> 8));
  out.push_back(static_cast<uint8_t>(v));
}

size_t NodeSize(const PssgNode &n) {
  size_t s = 12;
  for (const auto &a : n.attrs) s += 8 + a.second.size();
  if (n.children.empty()) return s + n.payload.size();
  for (const auto &c : n.children) s += NodeSize(*c);
  return s;
}

bool IsText(const std::vector<uint8_t> &v) {
  if (v.size() < 5) return false;
  const uint32_t n = Be32(v, 0);
  return n == v.size() - 4;
}

std::string TextOf(const std::vector<uint8_t> &v) {
  return IsText(v) ? std::string(v.begin() + 4, v.end()) : std::string();
}

std::vector<uint8_t> MakeText(const std::string &s) {
  std::vector<uint8_t> out;
  PutBe32(out, static_cast<uint32_t>(s.size()));
  out.insert(out.end(), s.begin(), s.end());
  return out;
}

} // namespace

bool Pssg::Parse(const std::vector<uint8_t> &d, std::string *error) {
  if (d.size() < 16 || std::memcmp(d.data(), "PSSG", 4) != 0) {
    *error = "PSSG sem a magia";
    return false;
  }
  attrCount_ = Be32(d, 8);
  typeCount_ = Be32(d, 12);
  size_t p = 16;
  types_.clear();
  attrNames_.clear();
  for (uint32_t i = 0; i < typeCount_; ++i) {
    if (p + 8 > d.size()) break;
    const uint32_t id = Be32(d, p), len = Be32(d, p + 4);
    p += 8;
    if (p + len + 4 > d.size()) {
      *error = "esquema PSSG cortado";
      return false;
    }
    Type t;
    t.name.assign(reinterpret_cast<const char *>(d.data() + p), len);
    p += len;
    const uint32_t na = Be32(d, p);
    p += 4;
    for (uint32_t k = 0; k < na; ++k) {
      if (p + 8 > d.size()) {
        *error = "esquema PSSG cortado";
        return false;
      }
      const uint32_t aid = Be32(d, p), alen = Be32(d, p + 4);
      p += 8;
      if (p + alen > d.size()) {
        *error = "esquema PSSG cortado";
        return false;
      }
      std::string name(reinterpret_cast<const char *>(d.data() + p), alen);
      p += alen;
      t.attrs[aid] = name;
      attrNames_[aid] = name;
    }
    types_[id] = std::move(t);
  }
  schemaRaw_.assign(d.begin() + 16, d.begin() + p);
  root_ = p < d.size() ? ParseNode(d, p, d.size(), 0) : nullptr;
  if (root_ == nullptr) {
    *error = "PSSG sem árvore de nós";
    return false;
  }
  const size_t rootEnd = p + 8 + Be32(d, p + 4);
  tail_.assign(d.begin() + std::min(rootEnd, d.size()), d.end());
  return true;
}

std::unique_ptr<PssgNode> Pssg::ParseNode(const std::vector<uint8_t> &d, size_t off, size_t maxEnd, int depth) {
  if (off + 12 > maxEnd || depth > 256) return nullptr;
  auto n = std::make_unique<PssgNode>();
  n->type = Be32(d, off);
  const uint32_t size = Be32(d, off + 4), attrLen = Be32(d, off + 8);
  size_t p = off + 12;
  const size_t attrEnd = std::min(maxEnd, p + attrLen);
  while (p + 8 <= attrEnd) {
    const uint32_t aid = Be32(d, p), alen = Be32(d, p + 4);
    const size_t vend = std::min(attrEnd, p + 8 + alen);
    n->attrs.emplace_back(aid, std::vector<uint8_t>(d.begin() + p + 8, d.begin() + vend));
    p += 8 + alen;
  }
  const size_t start = attrEnd, end = std::min(maxEnd, off + 8 + size);
  if (end <= start) return n;
  // Filhos só se o payload se decompõe exatamente em nós de tipos conhecidos (mesma regra do pssg.py).
  if (end - start >= 12) {
    size_t q = start;
    bool valid = true;
    while (q < end) {
      if (q + 12 > end) {
        valid = false;
        break;
      }
      const uint32_t cid = Be32(d, q), csz = Be32(d, q + 4), cattr = Be32(d, q + 8);
      if (!types_.count(cid) || cattr > csz || q + 8 + csz > end) {
        valid = false;
        break;
      }
      q += 8 + csz;
    }
    if (valid && q == end) {
      for (q = start; q < end; q += 8 + Be32(d, q + 4)) {
        auto c = ParseNode(d, q, end, depth + 1);
        if (c) n->children.push_back(std::move(c));
      }
      return n;
    }
  }
  n->payload.assign(d.begin() + start, d.begin() + end);
  return n;
}

void Pssg::Write(const PssgNode &n, std::vector<uint8_t> &out) const {
  size_t attrLen = 0;
  for (const auto &a : n.attrs) attrLen += 8 + a.second.size();
  PutBe32(out, n.type);
  PutBe32(out, static_cast<uint32_t>(NodeSize(n) - 8));
  PutBe32(out, static_cast<uint32_t>(attrLen));
  for (const auto &a : n.attrs) {
    PutBe32(out, a.first);
    PutBe32(out, static_cast<uint32_t>(a.second.size()));
    out.insert(out.end(), a.second.begin(), a.second.end());
  }
  if (n.children.empty()) out.insert(out.end(), n.payload.begin(), n.payload.end());
  for (const auto &c : n.children) Write(*c, out);
}

std::vector<uint8_t> Pssg::Serialize() const {
  std::vector<uint8_t> out = {'P', 'S', 'S', 'G', 0, 0, 0, 0};
  PutBe32(out, attrCount_);
  PutBe32(out, typeCount_);
  out.insert(out.end(), schemaRaw_.begin(), schemaRaw_.end());
  if (root_) Write(*root_, out);
  out.insert(out.end(), tail_.begin(), tail_.end());
  const uint32_t size = static_cast<uint32_t>(out.size() - 8);
  out[4] = static_cast<uint8_t>(size >> 24);
  out[5] = static_cast<uint8_t>(size >> 16);
  out[6] = static_cast<uint8_t>(size >> 8);
  out[7] = static_cast<uint8_t>(size);
  return out;
}

std::string Pssg::TypeName(const PssgNode &n) const {
  const auto it = types_.find(n.type);
  return it == types_.end() ? std::string() : it->second.name;
}

std::string Pssg::Str(const PssgNode &n, const std::string &attr) const {
  for (const auto &a : n.attrs) {
    const auto it = attrNames_.find(a.first);
    if (it != attrNames_.end() && it->second == attr) return TextOf(a.second);
  }
  return {};
}

std::string Pssg::Identity(const PssgNode &n) const {
  const std::string type = TypeName(n);
  const std::string id = Str(n, "id"), nick = Str(n, "nickname");
  if (!id.empty() || !nick.empty()) return type + "|" + id + "|" + nick;
  if (type == "LIBRARY") return type + "||" + Str(n, "type");
  if (type == "PSSGDATABASE") return type;
  return {};
}

uint32_t Pssg::TypeId(const std::string &name) const {
  for (const auto &[id, t] : types_)
    if (t.name == name) return id;
  return 0;
}

uint32_t Pssg::AttrId(uint32_t type, const std::string &name) const {
  // Primeiro os atributos do tipo; um nó também pode usar atributos declarados em outro tipo.
  const auto t = types_.find(type);
  if (t != types_.end())
    for (const auto &[id, n] : t->second.attrs)
      if (n == name) return id;
  for (const auto &[id, n] : attrNames_)
    if (n == name) return id;
  return 0;
}

std::unique_ptr<PssgNode> Pssg::Import(const Pssg &frag, const PssgNode &n, const std::map<std::string, std::string> &subst,
                                       std::string *error) const {
  auto out = std::make_unique<PssgNode>();
  const std::string tname = frag.TypeName(n);
  out->type = TypeId(tname);
  if (out->type == 0) {
    *error = "tipo de nó " + tname + " não existe no arquivo do jogo";
    return nullptr;
  }
  const auto &fragAttrs = frag.attrNames_;
  for (const auto &[aid, value] : n.attrs) {
    const auto name = fragAttrs.find(aid);
    // Mesmo id quando o arquivo dá a ele o mesmo nome (esquemas iguais); senão, pelo nome.
    const auto same = name == fragAttrs.end() ? attrNames_.end() : attrNames_.find(aid);
    const uint32_t id = name == fragAttrs.end()                                  ? 0
                        : same != attrNames_.end() && same->second == name->second ? aid
                                                                                   : AttrId(out->type, name->second);
    if (id == 0) {
      *error = "atributo " + (name == fragAttrs.end() ? std::to_string(aid) : name->second) + " de " + tname +
               " não existe no arquivo do jogo";
      return nullptr;
    }
    std::vector<uint8_t> v = value;
    if (IsText(v) && !subst.empty()) {
      std::string s = TextOf(v);
      for (const auto &[from, to] : subst)
        for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size()))
          s.replace(at, from.size(), to);
      v = MakeText(s);
    }
    out->attrs.emplace_back(id, std::move(v));
  }
  out->payload = n.payload;
  for (const auto &c : n.children) {
    auto ic = Import(frag, *c, subst, error);
    if (!ic) return nullptr;
    out->children.push_back(std::move(ic));
  }
  return out;
}

int Pssg::Graft(const Pssg &frag, const std::map<std::string, std::string> &subst, std::string *error) {
  if (!root_ || !frag.root_) {
    *error = "enxerto sem árvore";
    return -1;
  }
  auto imported = Import(frag, *frag.root_, subst, error);
  if (!imported) return -1;
  if (Identity(*imported) != Identity(*root_)) {
    *error = "raiz do enxerto não é a do arquivo (" + TypeName(*imported) + ")";
    return -1;
  }
  int added = 0;
  // Desce pelos nós que já existem; o resto entra como filho novo.
  auto merge = [&](auto &self, PssgNode &dst, PssgNode &src) -> void {
    for (auto &c : src.children) {
      const std::string key = Identity(*c);
      PssgNode *match = nullptr;
      if (!key.empty())
        for (auto &d : dst.children)
          if (Identity(*d) == key) {
            match = d.get();
            break;
          }
      if (match != nullptr && !c->children.empty()) {
        self(self, *match, *c);
      } else if (match == nullptr) {
        dst.children.push_back(std::move(c));
        ++added;
      }
    }
  };
  merge(merge, *root_, *imported);
  return added;
}

PssgNode *Pssg::FindById(const std::string &id, PssgNode **parent) const {
  auto walk = [&](auto &self, PssgNode &n) -> PssgNode * {
    for (auto &c : n.children) {
      if (Str(*c, "id") == id) {
        if (parent != nullptr) *parent = &n;
        return c.get();
      }
      if (PssgNode *r = self(self, *c)) return r;
    }
    return nullptr;
  };
  return root_ ? walk(walk, *root_) : nullptr;
}

std::unique_ptr<PssgNode> Pssg::Clone(const PssgNode &n) {
  auto out = std::make_unique<PssgNode>();
  out->type = n.type;
  out->attrs = n.attrs;
  out->payload = n.payload;
  for (const auto &c : n.children) out->children.push_back(Clone(*c));
  return out;
}

bool Pssg::SetStr(PssgNode &n, const std::string &attr, const std::string &value) const {
  for (auto &a : n.attrs) {
    const auto it = attrNames_.find(a.first);
    if (it != attrNames_.end() && it->second == attr) {
      a.second = MakeText(value);
      return true;
    }
  }
  const uint32_t id = AttrId(n.type, attr);
  if (id == 0) return false;
  n.attrs.emplace_back(id, MakeText(value));
  return true;
}

} // namespace trackpkg
