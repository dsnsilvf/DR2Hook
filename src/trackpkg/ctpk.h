#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace trackpkg {

// Catálogo do jogo (`catalogues/base.ctpk`, CTPK versão 2; port de scripts/research/ctpk.py).
// Linhas = mensagens protobuf sem descritor: aqui ficam como lista de campos genéricos. Strings
// dentro das linhas são o djb2 da string da tabela de strings.

uint32_t Djb2(const std::string &s);

struct Field {
  uint32_t number = 0;
  uint32_t wire = 0;       // 0 varint, 1 64 bits, 2 bytes, 5 32 bits
  uint64_t value = 0;      // varint, 64 ou 32 bits
  std::vector<uint8_t> bytes; // wire 2
};
using Fields = std::vector<Field>;

bool DecodeFields(const std::vector<uint8_t> &msg, Fields *out);
std::vector<uint8_t> EncodeFields(const Fields &fields);
// Primeiro campo com o número (nullptr se falta).
const Field *FindField(const Fields &f, uint32_t number);
// Troca o valor de todos os campos com o número (varint/32 bits); falso se não há nenhum.
bool SetField(Fields &f, uint32_t number, uint64_t value);

struct Row {
  uint32_t id = 0;
  std::vector<uint8_t> raw;
};

struct Table {
  uint32_t hash = 0;
  bool empty = false; // offset 0xffffffff no diretório
  std::vector<Row> rows; // ordenadas por id (a busca do jogo é binária)
};

class Ctpk {
public:
  bool Parse(const std::vector<uint8_t> &data, std::string *error);
  std::vector<uint8_t> Build() const;

  Table *Find(uint32_t hash);
  // Põe a string (se falta) e devolve o djb2 que as linhas usam.
  uint32_t AddString(const std::string &s);
  bool HasString(uint32_t hash) const;
  std::string StringOf(uint32_t hash) const; // vazio se não acha
  void PutRow(uint32_t table, Row row);
  uint32_t MaxId(uint32_t table);

private:
  uint32_t version_ = 2, checksum_ = 0;
  std::vector<std::string> strings_;
  std::map<uint32_t, size_t> stringIndex_; // djb2 -> índice
  std::vector<Table> tables_; // ordem do diretório
};

} // namespace trackpkg
