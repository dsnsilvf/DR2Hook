#include "edit/history.hpp"

#include <cmath>
#include <cstring>

namespace dr2::edit {

std::vector<Snap> snapshot(const Instances& inst, const std::vector<std::uint32_t>& indices) {
    std::vector<Snap> out;
    out.reserve(indices.size());
    for (std::uint32_t i : indices) {
        Snap s;
        s.index = i;
        std::memcpy(s.m.data(), inst.matrix(i), sizeof(float) * kInstFloats);
        s.hidden = inst.hidden[i];
        out.push_back(s);
    }
    return out;
}

void apply_snap(Instances& inst, const std::vector<Snap>& snap) {
    for (const Snap& s : snap) {
        std::memcpy(inst.matrix(s.index), s.m.data(), sizeof(float) * kInstFloats);
        inst.hidden[s.index] = s.hidden;
    }
}

namespace {

bool same(const std::vector<Snap>& a, const std::vector<Snap>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t k = 0; k < a.size(); ++k)
        if (a[k].index != b[k].index || a[k].m != b[k].m || a[k].hidden != b[k].hidden) return false;
    return true;
}

}  // namespace

bool History::commit(std::string label, std::vector<Snap> before, std::vector<Snap> after) {
    if (same(before, after)) return false;
    entries_.resize(pos_);
    entries_.push_back({std::move(label), std::move(before), std::move(after)});
    if (entries_.size() > kMax) entries_.erase(entries_.begin());
    pos_ = entries_.size();
    return true;
}

bool History::undo(Instances& inst) {
    if (pos_ == 0) return false;
    apply_snap(inst, entries_[--pos_].before);
    return true;
}

bool History::redo(Instances& inst) {
    if (pos_ >= entries_.size()) return false;
    apply_snap(inst, entries_[pos_++].after);
    return true;
}

void spin(float* m, const float* base, float th) {
    const float c = std::cos(th), s = std::sin(th);
    for (int r = 0; r < 3; ++r) {
        const float x = base[r * 3], y = base[r * 3 + 1], z = base[r * 3 + 2];
        m[r * 3] = x * c + z * s;
        m[r * 3 + 1] = y;
        m[r * 3 + 2] = -x * s + z * c;
    }
    for (int k = 9; k < 12; ++k) m[k] = base[k];
}

bool changed(const Instances& inst, std::size_t i) {
    if (inst.hidden[i]) return true;
    const float* a = inst.matrix(i);
    const float* b = &inst.m0[i * kInstFloats];
    for (std::size_t k = 0; k < kInstFloats; ++k)
        if (a[k] != b[k]) return true;
    return false;
}

}  // namespace dr2::edit
