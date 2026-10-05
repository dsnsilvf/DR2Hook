// DR2I: instâncias de uma rota, exportadas por tools/uiview/track/export.py (pack_instances).
// Especificação em docs/plans/viewer3d/formatos.md#dr2i--instâncias-de-uma-rota.
#pragma once

#include <cstdint>
#include <vector>

namespace dr2 {

// Matriz de uma instância: linhas 0..2 do bloco 3×3 (com escala) e a posição, vetor-linha:
// mundo = p.x * linha0 + p.y * linha1 + p.z * linha2 + posição.
constexpr std::size_t kInstFloats = 12;

struct Instances {
    std::uint32_t n = 0;
    std::vector<std::uint16_t> type;   // índice em type_order
    std::vector<std::uint32_t> idnum;  // registro no arquivo de origem (o "index" do edits.json)
    std::vector<float> m;              // 12 por instância, editável
    std::vector<float> m0;             // cópia do arquivo, para saber o que mudou
    std::vector<std::uint8_t> hidden;  // apagada na sessão

    const float* matrix(std::size_t i) const { return &m[i * kInstFloats]; }
    float* matrix(std::size_t i) { return &m[i * kInstFloats]; }
};

// Lança std::runtime_error se a magia não bate, se o tamanho não é exatamente
// 8 + pad4(2n) + 4n + 48n, ou se algum tipo é >= `type_count`.
Instances read_dr2i(const std::vector<std::uint8_t>& data, std::size_t type_count);

}  // namespace dr2
