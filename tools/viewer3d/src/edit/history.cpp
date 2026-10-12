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
    entries_.push_back({std::move(label), std::move(before), std::move(after), nullptr, nullptr});
    if (entries_.size() > kMax) {
        entries_.erase(entries_.begin());
        ++dropped_;
    }
    pos_ = entries_.size();
    return true;
}

bool History::commit_replay(std::string label, Replay before, Replay after) {
    if (replay_json(before) == replay_json(after)) return false;
    entries_.resize(pos_);
    Entry e;
    e.label = std::move(label);
    e.replay_before = std::make_shared<const Replay>(std::move(before));
    e.replay_after = std::make_shared<const Replay>(std::move(after));
    entries_.push_back(std::move(e));
    if (entries_.size() > kMax) {
        entries_.erase(entries_.begin());
        ++dropped_;
    }
    pos_ = entries_.size();
    return true;
}

bool History::undo(Instances& inst, Replay* replay) {
    if (pos_ == 0) return false;
    const Entry& e = entries_[--pos_];
    apply_snap(inst, e.before);
    if (replay && e.replay_before) *replay = *e.replay_before;
    return true;
}

bool History::redo(Instances& inst, Replay* replay) {
    if (pos_ >= entries_.size()) return false;
    const Entry& e = entries_[pos_++];
    apply_snap(inst, e.after);
    if (replay && e.replay_after) *replay = *e.replay_after;
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
    const float off[3] = {2.0f, 0.0f, 0.0f};
    return duplicate(inst, hist, std::vector<std::uint32_t>{src}, off).front();
}

std::vector<std::uint32_t> duplicate(Instances& inst, History& hist, const std::vector<std::uint32_t>& src, const float* offset,
                                     const char* label) {
    std::vector<std::uint32_t> made;
    for (std::uint32_t s : src) {
        const std::uint32_t n = inst.n;
        inst.type.push_back(inst.type[s]);
        inst.idnum.push_back(inst.idnum[s] >= kAdded ? inst.idnum[s] : kAdded + inst.idnum[s]);
        const float* m = inst.matrix(s);
        std::vector<float> copy(m, m + kInstFloats);
        for (int a = 0; a < 3; ++a) copy[9 + a] += offset[a];
        inst.m.insert(inst.m.end(), copy.begin(), copy.end());
        inst.m0.insert(inst.m0.end(), copy.begin(), copy.end());
        inst.hidden.push_back(1);
        if (!inst.flags.empty()) inst.flags.resize(n + 1, 0);
        inst.n = n + 1;
        made.push_back(n);
    }
    auto before = snapshot(inst, made);
    for (std::uint32_t i : made) inst.hidden[i] = 0;
    hist.commit(label, std::move(before), snapshot(inst, made));
    return made;
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
    snap_to_file(inst, i);
    hist.commit("Girar", std::move(before), snapshot(inst, {i}));
}

void snap_to_file(Instances& inst, std::size_t i, float eps) {
    float* a = inst.matrix(i);
    const float* b = &inst.m0[i * kInstFloats];
    for (std::size_t k = 0; k < kInstFloats; ++k)
        if (std::fabs(a[k] - b[k]) > eps) return;
    std::memcpy(a, b, sizeof(float) * kInstFloats);
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
