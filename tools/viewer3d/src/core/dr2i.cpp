#include "core/dr2i.hpp"

#include "core/bytes.hpp"

#include <stdexcept>
#include <string>

namespace dr2 {

Instances read_dr2i(const std::vector<std::uint8_t>& data, std::size_t type_count) {
    ByteReader r(data.data(), data.size(), "DR2I");
    r.need(8);
    if (r.get_string(4) != "DR2I") throw std::runtime_error("DR2I: magia errada");
    Instances out;
    out.n = r.get<std::uint32_t>();
    const std::size_t n = out.n;
    if (n > data.size() / 54) throw std::runtime_error("DR2I: n maior que o arquivo");  // 2 + 4 + 48 bytes por instância
    const std::size_t want = 8 + ((2 * n + 3) & ~std::size_t{3}) + 4 * n + 48 * n;
    if (data.size() != want) {
        throw std::runtime_error("DR2I: tamanho " + std::to_string(data.size()) + " em vez de " + std::to_string(want) + " para " +
                                 std::to_string(n) + " instâncias");
    }
    r.get_array(out.type, n);
    r.align4();
    r.get_array(out.idnum, n);
    r.get_array(out.m, n * kInstFloats);
    for (std::size_t i = 0; i < n; ++i) {
        if (out.type[i] >= type_count) {
            throw std::runtime_error("DR2I: instância " + std::to_string(i) + " tem tipo " + std::to_string(out.type[i]) + " com " +
                                     std::to_string(type_count) + " tipos");
        }
    }
    out.m0 = out.m;
    out.hidden.assign(n, 0);
    return out;
}

}  // namespace dr2
