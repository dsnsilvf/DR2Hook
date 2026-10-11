#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace trackpkg {

// Leitor dos pacotes NeFS v2 do jogo (port de tools/egodata/nefs.py, só leitura).
// `game.nefs` traz o cabeçalho no início; os `game*.dat` não têm cabeçalho: ele fica embutido no
// `dirtrally2.exe`. Cada bloco de 64 KiB é deflate cru, ou AES-256-ECB (chave em hex no cabeçalho)
// e depois deflate.
class NefsArchive {
public:
  // Falso e `error` preenchido se não deu para abrir.
  bool Open(const std::string &nefsPath, std::string *error);
  // Pacote sem cabeçalho: procura no exe o cabeçalho embutido que corresponde ao tamanho do .dat.
  bool OpenHeadless(const std::string &dataPath, const std::vector<uint8_t> &exe, std::string *error);

  bool Has(const std::string &path) const { return files_.count(path) != 0; }
  // Conteúdo do arquivo (caminho virtual sem barra inicial, ex. "catalogues/base.ctpk").
  bool Read(const std::string &path, std::vector<uint8_t> *out, std::string *error) const;

private:
  struct File {
    uint64_t offset = 0;
    uint32_t size = 0;
    uint32_t firstBlock = 0; // índice na tabela de fins de bloco
  };
  bool Parse(std::vector<uint8_t> header, std::string *error);
  uint64_t DataEnd() const;

  std::string dataPath_;
  std::vector<uint8_t> header_;
  uint8_t key_[32] = {};
  uint32_t blockEnds_ = 0, blockEndsEnd_ = 0; // trecho do cabeçalho com os fins de bloco
  std::unordered_map<std::string, File> files_;
};

} // namespace trackpkg
