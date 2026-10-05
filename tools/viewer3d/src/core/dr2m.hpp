// DR2M: malhas exportadas por tools/uiview/mesh.py (pack_geom). Especificação em
// docs/plans/viewer3d/formatos.md#dr2m--malhas.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dr2 {

struct Mesh {
    std::string name;
    std::string material;
    std::uint32_t verts = 0;
    std::uint32_t indices = 0;
    std::vector<float> pos;          // 3 por vértice
    std::vector<float> uv;           // 2 por vértice
    std::vector<std::uint8_t> col;   // 4 por vértice (RGBA) ou vazio
    std::vector<std::uint32_t> idx;  // sempre 32 bits depois da leitura
    bool wide = false;               // o arquivo tinha índices de 32 bits
};

// Lê o arquivo inteiro. Lança std::runtime_error se a magia não bate, se algo passa do fim
// ou se um índice aponta para além dos vértices (com o nome da malha).
std::vector<Mesh> read_dr2m(const std::vector<std::uint8_t>& data);

struct MeshTotals {
    std::size_t meshes = 0, verts = 0, tris = 0;
};
MeshTotals totals(const std::vector<Mesh>& meshes);

}  // namespace dr2
