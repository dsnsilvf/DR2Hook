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

std::uint32_t duplicate(Instances& inst, History& hist, std::uint32_t src) {
    const std::uint32_t n = inst.n;
    inst.type.push_back(inst.type[src]);
    inst.idnum.push_back(inst.idnum[src] >= kAdded ? inst.idnum[src] : kAdded + inst.idnum[src]);
    const float* m = inst.matrix(src);
    std::vector<float> copy(m, m + kInstFloats);
    copy[9] += 2.0f;
    inst.m.insert(inst.m.end(), copy.begin(), copy.end());
    inst.m0.insert(inst.m0.end(), copy.begin(), copy.end());
    inst.hidden.push_back(1);
    inst.n = n + 1;
    auto before = snapshot(inst, {n});
    inst.hidden[n] = 0;
    hist.commit("Duplicar", std::move(before), snapshot(inst, {n}));
    return n;
}

void restore(Instances& inst, History& hist, std::uint32_t i) {
    auto before = snapshot(inst, {i});
    std::memcpy(inst.matrix(i), &inst.m0[i * kInstFloats], sizeof(float) * kInstFloats);
    inst.hidden[i] = 0;
    hist.commit("Restaurar", std::move(before), snapshot(inst, {i}));
}

void turn(Instances& inst, History& hist, std::uint32_t i, float deg) {
    auto before = snapshot(inst, {i});
    float base[kInstFloats];
    std::memcpy(base, inst.matrix(i), sizeof base);
    spin(inst.matrix(i), base, deg * 3.14159265358979f / 180.0f);
    hist.commit("Girar", std::move(before), snapshot(inst, {i}));
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
