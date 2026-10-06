#include "core/grid.hpp"

namespace dr2 {

void InstanceGrid::insert(std::uint32_t i, std::uint64_t key) {
    auto& list = cells_[key];
    cell_[i] = key;
    slot_[i] = static_cast<std::uint32_t>(list.size());
    list.push_back(i);
}

void InstanceGrid::remove(std::uint32_t i) {
    auto it = cells_.find(cell_[i]);
    if (it == cells_.end()) return;
    auto& list = it->second;
    const std::uint32_t at = slot_[i];
    const std::uint32_t last = list.back();
    list[at] = last;
    slot_[last] = at;
    list.pop_back();
    if (list.empty()) cells_.erase(it);
}

void InstanceGrid::build(const Instances& inst, const std::vector<std::uint8_t>& in_grid) {
    cells_.clear();
    always_.clear();
    cell_.assign(inst.n, kNone);
    slot_.assign(inst.n, 0);
    gridded_.assign(inst.n, 1);
    count_ = inst.n;
    for (std::uint32_t i = 0; i < inst.n; ++i) {
        const std::uint16_t t = inst.type[i];
        if (t < in_grid.size() && !in_grid[t]) {
            gridded_[i] = 0;
            always_.push_back(i);
            continue;
        }
        const float* m = inst.matrix(i);
        insert(i, key_of(cell_of(m[9]), cell_of(m[11])));
    }
}

bool InstanceGrid::relocate(const Instances& inst, std::uint32_t i) {
    if (i >= count_ || !gridded_[i]) return false;
    const float* m = inst.matrix(i);
    const std::uint64_t key = key_of(cell_of(m[9]), cell_of(m[11]));
    if (key == cell_[i]) return false;
    remove(i);
    insert(i, key);
    return true;
}

std::size_t InstanceGrid::refresh(const Instances& inst) {
    if (inst.n != count_) return 0;  // cópias novas: quem chama refaz com build
    std::size_t moved = 0;
    for (std::uint32_t i = 0; i < inst.n; ++i) moved += relocate(inst, i);
    return moved;
}

}  // namespace dr2
