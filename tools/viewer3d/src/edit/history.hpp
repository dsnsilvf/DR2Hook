// Histórico de edições de uma rota, porte de tvSnap/tvCommit/tvHistGo (web/js/trackview.js).
// Só dados (dr2core): nada de GL.
#pragma once

#include "core/dr2i.hpp"

#include <array>
#include <string>
#include <vector>

namespace dr2::edit {

struct Snap {
    std::uint32_t index = 0;               // posição no array de instâncias da rota
    std::array<float, kInstFloats> m{};
    std::uint8_t hidden = 0;
};

std::vector<Snap> snapshot(const Instances& inst, const std::vector<std::uint32_t>& indices);
void apply_snap(Instances& inst, const std::vector<Snap>& snap);

class History {
public:
    static constexpr std::size_t kMax = 300;  // passos por rota, como TV_HIST_MAX

    struct Entry {
        std::string label;
        std::vector<Snap> before, after;
    };

    // Empilha se algo mudou (descarta o futuro); devolve false se `before` == `after`.
    bool commit(std::string label, std::vector<Snap> before, std::vector<Snap> after);
    bool undo(Instances& inst);
    bool redo(Instances& inst);

    std::size_t size() const { return entries_.size(); }
    std::size_t pos() const { return pos_; }
    const std::vector<Entry>& entries() const { return entries_; }

private:
    std::vector<Entry> entries_;
    std::size_t pos_ = 0;
};

// Gira as três linhas de `base` em torno de Y por `th` radianos e grava em `m` (a posição não muda),
// como tvSpin: x' = x·cos + z·sin, z' = −x·sin + z·cos.
void spin(float* m, const float* base, float th);

// A instância difere do arquivo (alguma das 12 floats) ou foi apagada?
bool changed(const Instances& inst, std::size_t i);

}  // namespace dr2::edit
