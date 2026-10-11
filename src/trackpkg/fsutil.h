#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace trackpkg {

// Caminhos em UTF-8 em toda a biblioteca; no Windows viram wchar_t só na hora de abrir.
std::filesystem::path U8Path(const std::string &utf8);
std::string PathU8(const std::filesystem::path &p);
FILE *OpenU8(const std::string &utf8, const char *mode);
bool ReadAll(const std::string &utf8, std::vector<uint8_t> *out);
// Grava em <arquivo>.tmp e troca, criando as pastas.
bool WriteAll(const std::string &utf8, const std::vector<uint8_t> &data);

} // namespace trackpkg
