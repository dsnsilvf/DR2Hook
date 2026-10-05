#include "core/io.hpp"

#include <fstream>
#include <stdexcept>

namespace dr2 {

std::vector<std::uint8_t> read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("não abriu " + path);
    const std::streamsize size = in.tellg();
    if (size < 0) throw std::runtime_error("não leu o tamanho de " + path);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    in.seekg(0);
    if (size > 0 && !in.read(reinterpret_cast<char*>(data.data()), size)) throw std::runtime_error("falha ao ler " + path);
    return data;
}

std::string join_path(const std::string& dir, const std::string& name) {
    if (dir.empty()) return name;
    if (dir.back() == '/' || dir.back() == '\\') return dir + name;
    return dir + "/" + name;
}

}  // namespace dr2
