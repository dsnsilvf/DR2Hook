#include "core/dr2m.hpp"

#include "core/bytes.hpp"

#include <stdexcept>

namespace dr2 {

std::vector<Mesh> read_dr2m(const std::vector<std::uint8_t>& data) {
    ByteReader r(data.data(), data.size(), "DR2M");
    r.need(8);
    if (r.get_string(4) != "DR2M") throw std::runtime_error("DR2M: magia errada");
    const std::uint32_t count = r.get<std::uint32_t>();
    // Cada malha ocupa pelo menos 16 bytes de cabeçalho: um count absurdo falha antes de alocar.
    if (count > r.left() / 16) throw std::runtime_error("DR2M: número de malhas maior que o arquivo");

    std::vector<Mesh> out(count);
    for (Mesh& m : out) {
        const std::uint16_t name_len = r.get<std::uint16_t>();
        const std::uint16_t mat_len = r.get<std::uint16_t>();
        m.verts = r.get<std::uint32_t>();
        m.indices = r.get<std::uint32_t>();
        const std::uint32_t flags = r.get<std::uint32_t>();
        m.name = r.get_string(name_len);
        m.material = r.get_string(mat_len);
        r.align4();
        r.get_array(m.pos, static_cast<std::size_t>(m.verts) * 3);
        r.get_array(m.uv, static_cast<std::size_t>(m.verts) * 2);
        if (flags & 2u) r.get_array(m.col, static_cast<std::size_t>(m.verts) * 4);
        m.wide = (flags & 1u) != 0;
        if (m.wide) {
            r.get_array(m.idx, m.indices);
        } else {
            std::vector<std::uint16_t> narrow;
            r.get_array(narrow, m.indices);
            m.idx.assign(narrow.begin(), narrow.end());
        }
        r.align4();
        for (std::uint32_t i : m.idx) {
            if (i >= m.verts) {
                throw std::runtime_error("DR2M: malha \"" + m.name + "\" tem índice " + std::to_string(i) + " com " +
                                         std::to_string(m.verts) + " vértices");
            }
        }
    }
    return out;
}

MeshTotals totals(const std::vector<Mesh>& meshes) {
    MeshTotals t;
    t.meshes = meshes.size();
    for (const Mesh& m : meshes) {
        t.verts += m.verts;
        t.tris += m.indices / 3;
    }
    return t;
}

}  // namespace dr2
