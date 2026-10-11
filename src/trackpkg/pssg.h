#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace trackpkg {

// Arquivo PSSG (big-endian; port do essencial de tools/egodata/pssg.py): esquema (tipos de nó e
// atributos por id), árvore de nós e uma cauda opcional. Sem mudanças, Serialize() devolve os
// mesmos bytes lidos.
struct PssgNode {
  uint32_t type = 0;
  std::vector<std::pair<uint32_t, std::vector<uint8_t>>> attrs; // (id do atributo, valor cru)
  std::vector<std::unique_ptr<PssgNode>> children;
  std::vector<uint8_t> payload; // só quando não tem filhos
};

class Pssg {
public:
  bool Parse(const std::vector<uint8_t> &data, std::string *error);
  std::vector<uint8_t> Serialize() const;

  PssgNode *Root() const { return root_.get(); }
  std::string TypeName(const PssgNode &n) const;
  // Valor de um atributo de texto (u32 tamanho + bytes); vazio se falta ou não é texto.
  std::string Str(const PssgNode &n, const std::string &attr) const;

  // Enxerta `frag` neste arquivo. Cada nó do fragmento com identidade (tipo + id/nickname, ou
  // LIBRARY + type) que já existe entre os filhos do nó correspondente aqui é só um caminho: o
  // enxerto desce nele. O que não existe é acrescentado como filho novo (cópia inteira). Antes,
  // `subst` troca trechos nos valores de texto do fragmento (ex. "$LOC" -> "dr2hook_loc47").
  // Atributos e tipos são casados pelo nome nos dois esquemas. Devolve quantos nós entraram.
  int Graft(const Pssg &frag, const std::map<std::string, std::string> &subst, std::string *error);

  // Primeiro nó com o atributo id igual (em profundidade); `parent` recebe o pai.
  PssgNode *FindById(const std::string &id, PssgNode **parent = nullptr) const;
  static std::unique_ptr<PssgNode> Clone(const PssgNode &n);
  // Troca (ou cria) um atributo de texto; falso se o tipo do nó não tem esse atributo.
  bool SetStr(PssgNode &n, const std::string &attr, const std::string &value) const;

private:
  struct Type {
    std::string name;
    std::map<uint32_t, std::string> attrs;
  };
  std::unique_ptr<PssgNode> ParseNode(const std::vector<uint8_t> &d, size_t off, size_t end, int depth);
  void Write(const PssgNode &n, std::vector<uint8_t> &out) const;
  std::string Identity(const PssgNode &n) const;
  std::unique_ptr<PssgNode> Import(const Pssg &frag, const PssgNode &n, const std::map<std::string, std::string> &subst,
                                   std::string *error) const;
  uint32_t TypeId(const std::string &name) const;
  uint32_t AttrId(uint32_t type, const std::string &name) const;

  uint32_t attrCount_ = 0, typeCount_ = 0;
  std::vector<uint8_t> schemaRaw_, tail_;
  std::map<uint32_t, Type> types_;
  std::map<uint32_t, std::string> attrNames_;
  std::unique_ptr<PssgNode> root_;
};

} // namespace trackpkg
