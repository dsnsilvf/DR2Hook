// Grade espacial das instâncias de uma rota: células de 64 m no plano xz. O corte por distância e o
// picking só percorrem as células perto da câmera, em vez das 323 mil instâncias de uma pista grande.
// Só dados (dr2core): nada de GL.
#pragma once

#include "core/dr2i.hpp"

#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace dr2 {

class InstanceGrid {
public:
    static constexpr float kCell = 64.0f;

    // Indexa todas as instâncias. `in_grid[t] == 0` deixa os tipos t numa lista à parte (always): é o
    // que não é cortado por distância (terreno distante). Tipos além do tamanho de `in_grid` entram.
    void build(const Instances& inst, const std::vector<std::uint8_t>& in_grid);

    // A instância mudou de lugar (ou nasceu)? Muda de célula se for o caso; true se mudou.
    bool relocate(const Instances& inst, std::uint32_t i);
    // relocate em todas (depois de desfazer, refazer, abrir um edits.json): ~1 ms para 300 mil.
    std::size_t refresh(const Instances& inst);

    // Instâncias fora da grade (sempre desenhadas).
    const std::vector<std::uint32_t>& always() const { return always_; }

    // Chama fn(i) para cada instância das células que tocam o quadrado de lado 2·radius em volta de
    // (x, z). Devolve quantas células visitou. Não garante que a instância está dentro do círculo.
    template <class F>
    std::size_t query(float x, float z, float radius, F&& fn) const {
        const long x0 = cell_of(x - radius), x1 = cell_of(x + radius), z0 = cell_of(z - radius), z1 = cell_of(z + radius);
        const unsigned long long span = static_cast<unsigned long long>(x1 - x0 + 1) * static_cast<unsigned long long>(z1 - z0 + 1);
        std::size_t visited = 0;
        if (span > cells_.size()) {
            // raio enorme: mais barato olhar as células que existem
            for (const auto& [key, list] : cells_) {
                const long cx = static_cast<long>(static_cast<std::int32_t>(key >> 32)), cz = static_cast<long>(static_cast<std::int32_t>(key & 0xffffffffu));
                if (cx < x0 || cx > x1 || cz < z0 || cz > z1) continue;
                ++visited;
                for (std::uint32_t i : list) fn(i);
            }
            return visited;
        }
        for (long cz = z0; cz <= z1; ++cz)
            for (long cx = x0; cx <= x1; ++cx) {
                auto it = cells_.find(key_of(cx, cz));
                if (it == cells_.end()) continue;
                ++visited;
                for (std::uint32_t i : it->second) fn(i);
            }
        return visited;
    }

    std::size_t cells() const { return cells_.size(); }
    std::size_t size() const { return count_; }

    // Célula de uma coordenada (para os testes).
    static long cell_of(float v) { return std::isfinite(v) ? static_cast<long>(std::floor(v / kCell)) : 0; }

private:
    static std::uint64_t key_of(long cx, long cz) {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(static_cast<std::int32_t>(cx))) << 32) |
               static_cast<std::uint32_t>(static_cast<std::int32_t>(cz));
    }
    void insert(std::uint32_t i, std::uint64_t key);
    void remove(std::uint32_t i);

    static constexpr std::uint64_t kNone = ~0ull;  // fora da grade (lista always)
    std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> cells_;
    std::vector<std::uint64_t> cell_;   // célula de cada instância
    std::vector<std::uint32_t> slot_;   // posição dela dentro da lista da célula
    std::vector<std::uint8_t> gridded_; // 0 = lista always
    std::vector<std::uint32_t> always_;
    std::size_t count_ = 0;
};

}  // namespace dr2
