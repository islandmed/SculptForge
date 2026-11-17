// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/MicroHistory.h"

#include "core/BVH.h"
#include "core/BrushAlpha.h"
#include "core/Micro.h"
#include "core/Sculpt.h"

#include <algorithm>
#include <cstring>
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

uint64_t mix(uint64_t h, uint64_t v) {
    h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    return h * 0xff51afd7ed558ccdull;
}

uint64_t hashBytes(uint64_t h, const void* data, size_t n) {
    const unsigned char* p = static_cast<const unsigned char*>(data);
    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        uint64_t v;
        std::memcpy(&v, p + i, 8);
        h = mix(h, v);
    }
    uint64_t tail = 0;
    std::memcpy(&tail, p + i, n - i);
    return mix(h, tail ^ (uint64_t(n) << 56));
}

// Sorts a record's tiles ascending with their hashes alongside.
void sortTiles(MicroRecord& r) {
    const size_t n = r.tiles.size();
    if (r.pre.size() != n || r.post.size() != n) {
        r.pre.clear(), r.post.clear();
        std::sort(r.tiles.begin(), r.tiles.end());
        return;
    }
    std::vector<size_t> order(n);
    for (size_t i = 0; i < n; ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return r.tiles[a] < r.tiles[b]; });
    std::vector<uint32_t> t(n);
    std::vector<uint64_t> a(n), b(n);
    for (size_t i = 0; i < n; ++i) t[i] = r.tiles[order[i]], a[i] = r.pre[order[i]], b[i] = r.post[order[i]];
    r.tiles.swap(t), r.pre.swap(a), r.post.swap(b);
}

// Where tile `t` is in a record's (ascending) tiles, or -1.
long tileIndex(const MicroRecord& r, uint32_t t) {
    const auto it = std::lower_bound(r.tiles.begin(), r.tiles.end(), t);
    return it != r.tiles.end() && *it == t ? long(it - r.tiles.begin()) : -1;
}

} // namespace

uint64_t microTileHash(const Mesh& m, uint32_t tri, const int16_t* disp, const uint8_t* mask) {
    const MicroMesh& mm = *m.micro;
    const size_t S = mm.tileSamples(tri);
    uint64_t h = hashBytes(0x5f0c1d2e3a4b6879ull, disp, S * sizeof(int16_t));
    if (mask) h = hashBytes(h, mask, S);
    else { // (no mask = zeros, the same as a mask added later that leaves this tile free)
        static const std::vector<uint8_t> zeros(size_t(MicroMesh::index(0, 1u << MicroMesh::kMaxLevel, 1u << MicroMesh::kMaxLevel)) + 1, 0);
        h = hashBytes(h, zeros.data(), S);
    }
    const glm::uvec3 tr = m.triangles[tri];
    for (int k = 0; k < 3; ++k) {
        h = hashBytes(h, &m.positions[tr[k]], sizeof(glm::vec3));
        if (m.normals.size() == m.positions.size()) h = hashBytes(h, &m.normals[tr[k]], sizeof(glm::vec3));
    }
    return mix(h, m.triangleHidden(tri) ? 1u : 0u);
}

bool MicroRecord::applied() const { return !undo || undo->applied; }

MicroHistory::MicroHistory() = default;
MicroHistory::~MicroHistory() = default;

bool MicroHistory::replayable(BrushType type) { return type != BrushType::Move && microBrushSupport(type) > 0; }

void MicroHistory::run(Mesh& m, const BVH& bvh, MicroRecord& rec) {
    MicroStroke st;
    st.adaptive = rec.ink.adaptive;
    st.hashTiles = true;
    st.begin(m);
    const BrushAlpha* alpha = rec.ink.alpha.get();
    const bool patterned = rec.ink.pattern != 0 && rec.ink.patternAmount > 0.0f;
    for (const MicroDab& d : rec.dabs) {
        switch (d.op) {
        case MicroDab::Op::Dab: {
            std::function<float(const glm::vec3&)> texture;
            if (patterned) texture = microPatternTexture(rec.ink.pattern, rec.ink.patternScale, rec.ink.patternAmount, d.radius, d.copy);
            st.dab(m, bvh, d.type, d.center, d.viewDir, d.radius, d.strength, d.invert, d.focal, d.alphaUp, rec.ink.maskBrush,
                   d.maskStrength, alpha, texture, d.strokeDir, d.pxPerUnit); // (the same tile depth cap as when placed)
            break;
        }
        case MicroDab::Op::GrabBegin: st.beginGrab(m, bvh, d.center, d.radius, d.focal, d.copy.m); break;
        case MicroDab::Op::Grab: st.grab(m, d.delta); break;
        case MicroDab::Op::Restore: st.restore(m); break;
        }
    }
    std::unique_ptr<MicroUndo> fresh = st.end(m);
    // Into the same step (the app's undo step for the stroke holds it): it then undoes the stroke as it is now.
    if (rec.undo) {
        if (fresh) *rec.undo = std::move(*fresh);
        else {
            const float step = rec.undo->step;
            *rec.undo = MicroUndo{}; // (changes nothing now: an empty step)
            rec.undo->step = step;
            rec.undo->samples = m.micro ? m.micro->samples : 0;
        }
    } else if (fresh) {
        rec.undo = std::shared_ptr<MicroUndo>(std::move(fresh));
    }
    rec.tiles = rec.undo ? rec.undo->tris : std::vector<uint32_t>{};
    rec.pre = std::move(st.tilePre), rec.post = std::move(st.tilePost);
    sortTiles(rec);
}

uint32_t MicroHistory::add(Mesh& m, const BVH& bvh, std::vector<MicroDab> dabs, MicroInk ink) {
    MicroRecord rec;
    rec.id = next_++;
    rec.ink = std::move(ink);
    rec.dabs = std::move(dabs);
    run(m, bvh, rec);
    recs_.push_back(std::move(rec));
    landed(recs_.size() - 1);
    return recs_.back().id;
}

uint32_t MicroHistory::record(MicroInk ink, std::vector<MicroDab> dabs, std::shared_ptr<MicroUndo> undo, std::vector<uint64_t> pre,
                              std::vector<uint64_t> post) {
    sync();
    MicroRecord rec;
    rec.id = next_++;
    rec.ink = std::move(ink);
    rec.dabs = std::move(dabs);
    rec.tiles = undo ? undo->tris : std::vector<uint32_t>{};
    rec.pre = std::move(pre), rec.post = std::move(post);
    sortTiles(rec);
    rec.undo = std::move(undo);
    rec.shared = true;
    recs_.push_back(std::move(rec));
    landed(recs_.size() - 1);
    return recs_.back().id;
}

void MicroHistory::sync() {
    bool baked = false;
    for (size_t i = recs_.size(); i-- > 0;) {
        MicroRecord& r = recs_[i];
        if (!r.shared || !r.undo || r.undo.use_count() > 1) continue;
        if (r.undo->applied) bake(i), baked = true; // (the byte budget dropped its step: it stays in the mesh)
        else recs_.erase(recs_.begin() + long(i)); // (a redo branch that is gone)
    }
    if (baked) release();
}

void MicroHistory::landed(size_t k) {
    const std::vector<uint32_t>& top = recs_[k].tiles;
    bool baked = false;
    for (size_t i = 0; i < k; ++i) {
        MicroRecord& r = recs_[i];
        if (r.baked || !r.applied() || !meets(r.tiles, top)) continue;
        if (++r.over >= uint32_t(std::max(bakeDepth, 1))) bake(i), baked = true;
    }
    if (baked) release();
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
}

void MicroHistory::release() {
    // Baked strokes keep only their tiles (later strokes still meet them); the app's undo steps keep the grids.
    std::erase_if(recs_, [](const MicroRecord& r) { return r.baked && !r.applied(); });
    for (MicroRecord& r : recs_)
        if (r.baked && (r.undo || !r.dabs.empty()))
            r.undo.reset(), r.dabs.clear(), r.dabs.shrink_to_fit(), r.pre.clear(), r.pre.shrink_to_fit(), r.post.clear(),
                r.post.shrink_to_fit();
}

size_t MicroHistory::bakeMeeting(const std::vector<uint32_t>& tiles) {
    sync();
    if (tiles.empty()) return bakeAll();
    const size_t before = editable();
    for (size_t i = recs_.size(); i-- > 0;)
        if (!recs_[i].baked && recs_[i].applied() && meets(recs_[i].tiles, tiles)) bake(i);
    const size_t baked = before - editable();
    if (baked) release();
    return baked;
}

size_t MicroHistory::bakeAll() {
    const size_t baked = editable();
    for (MicroRecord& r : recs_) r.baked = true;
    release();
    return baked;
}

size_t MicroHistory::indexOf(uint32_t id) const {
    for (size_t i = 0; i < recs_.size(); ++i)
        if (recs_[i].id == id) return i;
    return recs_.size();
}

std::vector<uint32_t> MicroHistory::reach(const Mesh& m, const BVH& bvh, const std::vector<MicroDab>& dabs, const MicroInk& ink) const {
    std::vector<uint32_t> region, one;
    for (const MicroDab& d : dabs) {
        if (d.op != MicroDab::Op::Dab && d.op != MicroDab::Op::GrabBegin) continue;
        one.clear();
        // (as MicroStroke::dab finds its triangles)
        const bool useAlpha = d.op == MicroDab::Op::Dab && ink.alpha && !ink.alpha->empty() && !ink.maskBrush;
        const float r = d.radius * (useAlpha ? kAlphaReach : d.type == BrushType::ClayBlock && !ink.maskBrush ? 1.3f : 1.0f);
        microQueryTriangles(m, bvh, d.center, r, one);
        std::sort(one.begin(), one.end());
        merge(region, one);
    }
    return region;
}

bool MicroHistory::consistent(const Mesh& m, size_t k, std::vector<uint32_t> region) const {
    // The chain unwind() will take, without touching the mesh.
    std::vector<size_t> chain;
    for (size_t j = k; j < recs_.size(); ++j) {
        const MicroRecord& r = recs_[j];
        if (!r.applied() || r.tiles.empty()) continue;
        if (j == k || meets(r.tiles, region)) chain.push_back(j), merge(region, r.tiles);
    }
    for (size_t j : chain)
        if (recs_[j].baked || recs_[j].pre.size() != recs_[j].tiles.size()) return false;
    const MicroMesh& mm = *m.micro;
    for (uint32_t t : region) {
        bool any = false;
        uint64_t expect = 0;
        for (size_t j : chain) {
            const long i = tileIndex(recs_[j], t);
            if (i < 0) continue;
            if (any && recs_[j].pre[size_t(i)] != expect) return false; // (written between two strokes)
            expect = recs_[j].post[size_t(i)];
            any = true;
        }
        if (any && microTileHash(m, t, mm.tileDisp(t), mm.tileMask(t)) != expect)
            return false; // (written since the last stroke there)
    }
    return true;
}

std::vector<size_t> MicroHistory::unwind(Mesh& m, size_t k, std::vector<uint32_t>& region) {
    merge(region, recs_[k].tiles);
    std::vector<size_t> chain{k};
    for (size_t j = k + 1; j < recs_.size(); ++j)
        if (recs_[j].applied() && meets(recs_[j].tiles, region)) chain.push_back(j), merge(region, recs_[j].tiles);
    for (size_t q = chain.size(); q-- > 0;)
        if (recs_[chain[q]].undo && recs_[chain[q]].undo->applied) recs_[chain[q]].undo->swapWith(m);
    return chain;
}

bool MicroHistory::edit(Mesh& m, const BVH& bvh, uint32_t id, std::vector<MicroDab>& dabs, MicroReplayStats* stats, std::string* why) {
    sync();
    const size_t k = indexOf(id);
    if (k == recs_.size() || recs_[k].baked || !recs_[k].applied() || !m.micro) {
        if (why) *why = k == recs_.size() ? "no such stroke" : "the stroke is baked";
        return false;
    }
    // where the new dabs can reach joins the region (later strokes there must run again on top of them)
    std::vector<uint32_t> region = reach(m, bvh, dabs, recs_[k].ink);
    if (!consistent(m, k, region)) {
        bake(k), release();
        if (why) *why = "the surface there changed outside the stroke history: the stroke is baked";
        return false;
    }
    const float step = m.micro->step;
    const std::vector<size_t> chain = unwind(m, k, region);
    std::swap(recs_[k].dabs, dabs);
    for (size_t i : chain) run(m, bvh, recs_[i]);
    if (m.micro->step != step) bakeAll(); // (the range grew: every value was rescaled, no replay repeats that)
    if (stats) stats->replayed = chain.size(), stats->tiles = region.size();
    return true;
}

bool MicroHistory::remove(Mesh& m, const BVH& bvh, uint32_t id, MicroReplayStats* stats, std::string* why,
                          std::unique_ptr<MicroRecord>* removed, size_t* at) {
    sync();
    const size_t k = indexOf(id);
    if (k == recs_.size() || recs_[k].baked || !recs_[k].applied() || !m.micro) {
        if (why) *why = k == recs_.size() ? "no such stroke" : "the stroke is baked";
        return false;
    }
    if (!consistent(m, k, {})) {
        bake(k), release();
        if (why) *why = "the surface there changed outside the stroke history: the stroke is baked";
        return false;
    }
    std::vector<uint32_t> region;
    const std::vector<size_t> chain = unwind(m, k, region);
    for (size_t q = 1; q < chain.size(); ++q) run(m, bvh, recs_[chain[q]]);
    if (removed) *removed = std::make_unique<MicroRecord>(std::move(recs_[k]));
    if (at) *at = k;
    recs_.erase(recs_.begin() + long(k));
    if (stats) stats->replayed = chain.size() - 1, stats->tiles = region.size();
    return true;
}

bool MicroHistory::reinsert(Mesh& m, const BVH& bvh, std::unique_ptr<MicroRecord> rec, size_t at, std::string* why) {
    sync();
    if (!rec || !m.micro) return false;
    at = std::min(at, recs_.size());
    std::vector<uint32_t> region = reach(m, bvh, rec->dabs, rec->ink);
    // (its undo step is out of the mesh: unwind leaves it, the strokes above in its reach go back)
    std::vector<uint32_t> keepTiles = rec->tiles;
    rec->tiles.clear();
    recs_.insert(recs_.begin() + long(at), std::move(*rec));
    if (!consistent(m, at, region)) {
        recs_[at].tiles = std::move(keepTiles);
        recs_.erase(recs_.begin() + long(at));
        bakeMeeting(region);
        if (why) *why = "the surface there changed outside the stroke history";
        return false;
    }
    merge(region, keepTiles);
    const float step = m.micro->step;
    const std::vector<size_t> chain = unwind(m, at, region);
    for (size_t i : chain) run(m, bvh, recs_[i]);
    if (m.micro->step != step) bakeAll();
    return true;
}

const MicroRecord* MicroHistory::find(uint32_t id) const {
    const size_t k = indexOf(id);
    return k < recs_.size() ? &recs_[k] : nullptr;
}

size_t MicroHistory::editable() const {
    return size_t(std::count_if(recs_.begin(), recs_.end(), [](const MicroRecord& r) { return !r.baked && r.applied(); }));
}

size_t MicroHistory::bytes() const {
    size_t b = recs_.capacity() * sizeof(MicroRecord);
    for (const MicroRecord& r : recs_)
        b += r.dabs.capacity() * sizeof(MicroDab) + r.tiles.capacity() * 4 + (r.pre.capacity() + r.post.capacity()) * 8 +
             (r.undo && !r.shared ? r.undo->bytes() : 0);
    return b;
}

bool MicroInkStep::swapWith(Mesh& mesh, const BVH& bvh, std::string* why) {
    MicroHistory* h = microHistory(mesh, false);
    if (!h) {
        if (why) *why = "the micro level has no stroke history";
        return false;
    }
    if (op == Op::Edit) return h->edit(mesh, bvh, id, dabs, nullptr, why);
    if (out) {
        if (!h->reinsert(mesh, bvh, std::move(removed), at, why)) return false;
        out = false;
        return true;
    }
    if (!h->remove(mesh, bvh, id, nullptr, why, &removed, &at)) return false;
    out = true;
    return true;
}

size_t MicroInkStep::bytes() const {
    return sizeof(*this) + dabs.capacity() * sizeof(MicroDab) +
           (removed ? removed->dabs.capacity() * sizeof(MicroDab) + removed->tiles.capacity() * 4 : 0);
}

MicroHistory* microHistory(Mesh& mesh, bool create) {
    if (!mesh.micro) return nullptr;
    MicroMesh& mm = *mesh.micro;
    if (!mm.ink.history && create) mm.ink.history = std::make_shared<MicroHistory>();
    return mm.ink.history.get();
}

} // namespace sf
