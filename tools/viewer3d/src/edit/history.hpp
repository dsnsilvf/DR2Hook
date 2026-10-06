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

// Se a matriz ficou a menos de `eps` da do arquivo em todas as 12 floats (girar 360° em passos deixa
// ~1e-7 de erro), volta exatamente à do arquivo, para não virar uma edição que não muda nada.
void snap_to_file(Instances& inst, std::size_t i, float eps = 1e-5f);

// A instância difere do arquivo (alguma das 12 floats) ou foi apagada?
bool changed(const Instances& inst, std::size_t i);

// Copia a instância `src` para o fim, 2 m adiante em x, como tvDuplicateSel: a cópia nasce oculta e o
// passo de histórico a revela (desfazer a esconde de novo). Devolve o índice da cópia.
std::uint32_t duplicate(Instances& inst, History& hist, std::uint32_t src);

// Volta a instância à matriz do arquivo e a mostra (tvRestoreSel), com passo de histórico.
void restore(Instances& inst, History& hist, std::uint32_t i);

// Gira em torno de Y por `deg` graus (tvTurnSel), com passo de histórico.
void turn(Instances& inst, History& hist, std::uint32_t i, float deg);

}  // namespace dr2::edit
