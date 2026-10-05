// Leitura de arquivo inteiro e caminhos, sem dependências.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dr2 {

// Lança std::runtime_error com o caminho se não conseguir ler.
std::vector<std::uint8_t> read_file(const std::string& path);

// `dir` + "/" + `name`, sem barra duplicada.
std::string join_path(const std::string& dir, const std::string& name);

}  // namespace dr2
