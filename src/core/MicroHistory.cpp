// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/MicroHistory.h"

#include "core/BVH.h"
#include "core/Micro.h"
#include "core/Sculpt.h"

#include <algorithm>
#include <iterator>

namespace sf {

namespace {

bool meets(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) {
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i] == b[j]) return true;
        a[i] < b[j] ? ++i : ++j;
    }
    return false;
}

void merge(std::vector<uint32_t>& into, const std::vector<uint32_t>& add) {
    std::vector<uint32_t> out;
    out.reserve(into.size() + add.size());
    std::set_union(into.begin(), into.end(), add.begin(), add.end(), std::back_inserter(out));
    into.swap(out);
}

} // namespace

MicroHistory::MicroHistory() = default;
MicroHistory::~MicroHistory() = default;

bool MicroHistory::replayable(BrushType type) { return type != BrushType::Move && microBrushSupport(type) > 0; }

void MicroHistory::run(Mesh& m, const BVH& bvh, MicroRecord& rec) {
    MicroStroke st;
    st.begin(m);
    for (const MicroDab& d : rec.dabs)
        st.dab(m, bvh, d.type, d.center, d.viewDir, d.radius, d.strength, d.invert, d.focal, d.alphaUp, false, 0.0f, nullptr, {},
               d.strokeDir);
    rec.undo = st.end(m);
    rec.tiles = rec.undo ? rec.undo->tris : std::vector<uint32_t>{};
    std::sort(rec.tiles.begin(), rec.tiles.end());
}

uint32_t MicroHistory::add(Mesh& m, const BVH& bvh, std::vector<MicroDab> dabs) {
    MicroRecord rec;
    rec.id = next_++;
    rec.dabs = std::move(dabs);
    run(m, bvh, rec);
    recs_.push_back(std::move(rec));
    landed(recs_.size() - 1);
    return recs_.back().id;
}

uint32_t MicroHistory::record(std::vector<MicroDab> dabs, std::unique_ptr<MicroUndo> undo) {
    MicroRecord rec;
    rec.id = next_++;
    rec.dabs = std::move(dabs);
    rec.tiles = undo ? undo->tris : std::vector<uint32_t>{};
    std::sort(rec.tiles.begin(), rec.tiles.end());
    rec.undo = std::move(undo);
    recs_.push_back(std::move(rec));
    landed(recs_.size() - 1);
    return recs_.back().id;
}

void MicroHistory::landed(size_t k) {
    const std::vector<uint32_t>& top = recs_[k].tiles;
    for (size_t i = 0; i < k; ++i) {
        MicroRecord& r = recs_[i];
        if (r.baked || !meets(r.tiles, top)) continue;
        if (++r.over >= uint32_t(std::max(bakeDepth, 1))) bake(i);
    }
}

void MicroHistory::bake(size_t k) {
    // this one, and every older stroke it meets (an editable stroke must never have a baked one above it)
    std::vector<size_t> todo{k};
    while (!todo.empty()) {
        const size_t i = todo.back();
        todo.pop_back();
        MicroRecord& r = recs_[i];
        if (r.baked) continue;
        r.baked = true;
        for (size_t j = 0; j < i; ++j)
            if (!recs_[j].baked && meets(recs_[j].tiles, r.tiles)) todo.push_back(j);
    }
    for (MicroRecord& r : recs_)
        if (r.baked && (r.undo || !r.dabs.empty())) r.undo.reset(), r.dabs.clear(), r.dabs.shrink_to_fit();
}

std::vector<size_t> MicroHistory::unwind(Mesh& m, size_t k, std::vector<uint32_t>& region) {
    merge(region, recs_[k].tiles);
    std::vector<size_t> chain{k};
    for (size_t j = k + 1; j < recs_.size(); ++j)
        if (meets(recs_[j].tiles, region)) chain.push_back(j), merge(region, recs_[j].tiles);
    for (size_t q = chain.size(); q-- > 0;)
        if (recs_[chain[q]].undo) recs_[chain[q]].undo->swapWith(m);
    return chain;
}

bool MicroHistory::edit(Mesh& m, const BVH& bvh, uint32_t id, std::vector<MicroDab> dabs, MicroReplayStats* stats) {
    const auto it = std::find_if(recs_.begin(), recs_.end(), [&](const MicroRecord& r) { return r.id == id; });
    if (it == recs_.end() || it->baked) return false;
    const size_t k = size_t(it - recs_.begin());
    // where the new dabs can reach joins the region (later strokes there must run again on top of them)
    std::vector<uint32_t> region, one;
    for (const MicroDab& d : dabs) {
        one.clear();
        microQueryTriangles(m, bvh, d.center, d.radius * (d.type == BrushType::ClayBlock ? 1.3f : 1.0f), one);
        std::sort(one.begin(), one.end());
        merge(region, one);
    }
    const std::vector<size_t> chain = unwind(m, k, region);
    recs_[k].dabs = std::move(dabs);
    for (size_t i : chain) run(m, bvh, recs_[i]);
    if (stats) stats->replayed = chain.size(), stats->tiles = region.size();
    return true;
}

bool MicroHistory::remove(Mesh& m, const BVH& bvh, uint32_t id, MicroReplayStats* stats) {
    const auto it = std::find_if(recs_.begin(), recs_.end(), [&](const MicroRecord& r) { return r.id == id; });
    if (it == recs_.end() || it->baked) return false;
    const size_t k = size_t(it - recs_.begin());
    std::vector<uint32_t> region;
    const std::vector<size_t> chain = unwind(m, k, region);
    for (size_t q = 1; q < chain.size(); ++q) run(m, bvh, recs_[chain[q]]);
    recs_.erase(recs_.begin() + long(k));
    if (stats) stats->replayed = chain.size() - 1, stats->tiles = region.size();
    return true;
}

const MicroRecord* MicroHistory::find(uint32_t id) const {
    for (const MicroRecord& r : recs_)
        if (r.id == id) return &r;
    return nullptr;
}

size_t MicroHistory::editable() const {
    return size_t(std::count_if(recs_.begin(), recs_.end(), [](const MicroRecord& r) { return !r.baked; }));
}

size_t MicroHistory::bytes() const {
    size_t b = recs_.capacity() * sizeof(MicroRecord);
    for (const MicroRecord& r : recs_) b += r.dabs.capacity() * sizeof(MicroDab) + r.tiles.capacity() * 4 + (r.undo ? r.undo->bytes() : 0);
    return b;
}

} // namespace sf
