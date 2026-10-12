// DR2I: instâncias de uma rota, exportadas por tools/uiview/track/export.py (pack_instances).
// Especificação em docs/plans/viewer3d/formatos.md#dr2i--instâncias-de-uma-rota.
#pragma once

#include <cstdint>
#include <vector>

namespace dr2 {

// Matriz de uma instância: linhas 0..2 do bloco 3×3 (com escala) e a posição, vetor-linha:
// mundo = p.x * linha0 + p.y * linha1 + p.z * linha2 + posição.
constexpr std::size_t kInstFloats = 12;

// idnum de uma cópia feita na sessão: kAdded + idnum da instância copiada (como TV_ADDED no web).
// O arquivo exportado nunca tem valores assim.
constexpr std::uint32_t kAdded = 0x80000000u;

struct Instances {
    std::uint32_t n = 0;
    std::vector<std::uint16_t> type;   // índice em type_order
    std::vector<std::uint32_t> idnum;  // registro no arquivo de origem (o "index" do edits.json)
    std::vector<float> m;              // 12 por instância, editável
    std::vector<float> m0;             // cópia do arquivo, para saber o que mudou
    std::vector<std::uint8_t> hidden;  // apagada na sessão
    // Só da vista (não vai ao histórico nem ao edits.json): kVeiled = escondida pelo olho da Cena,
    // kLocked = travada (o clique e o retângulo passam por ela). Pode ser menor que n (vale 0).
    static constexpr std::uint8_t kVeiled = 1, kLocked = 2;
    std::vector<std::uint8_t> flags;
    bool flag(std::size_t i, std::uint8_t f) const { return i < flags.size() && (flags[i] & f); }
    void set_flag(std::size_t i, std::uint8_t f, bool on) {
        if (flags.size() < n) flags.resize(n, 0);
        if (i < flags.size()) flags[i] = static_cast<std::uint8_t>(on ? flags[i] | f : flags[i] & ~f);
    }

    const float* matrix(std::size_t i) const { return &m[i * kInstFloats]; }
    float* matrix(std::size_t i) { return &m[i * kInstFloats]; }
};

// Lança std::runtime_error se a magia não bate, se o tamanho não é exatamente
// 8 + pad4(2n) + 4n + 48n, ou se algum tipo é >= `type_count`.
Instances read_dr2i(const std::vector<std::uint8_t>& data, std::size_t type_count);

}  // namespace dr2
