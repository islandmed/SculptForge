// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Ink.h"

#include "core/Micro.h"
#include "core/ThreadPool.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <atomic>
#include <mutex>
#include <unordered_map>

namespace sf {

float inkProfile(InkProfile p, float s) {
    if (s >= 1.0f) return 0.0f;
    s = std::max(s, 0.0f);
    switch (p) {
    case InkProfile::Round: return std::sqrt(1.0f - s * s);
    case InkProfile::Smooth: { const float t = 1.0f - s; return t * t * (3.0f - 2.0f * t); }
    case InkProfile::Chisel: return 1.0f - s;
    case InkProfile::Flat: return s < 0.8f ? 1.0f : (1.0f - s) / 0.2f;
    }
    return 0.0f;
}

namespace {

// The contribution and whether x is inside the footprint (Max and Min only act inside it).
bool contribution(const InkStroke& s, const glm::vec3& x, float& c) {
    bool inside = false;
    c = 0.0f;
    const auto seg = [&](const InkPoint& a, const InkPoint& b) {
        const glm::vec3 ba = b.p - a.p;
        const float l2 = glm::dot(ba, ba);
        const float t = l2 > 1e-24f ? std::clamp(glm::dot(x - a.p, ba) / l2, 0.0f, 1.0f) : 0.0f;
        const float w = a.width + (b.width - a.width) * t;
        if (w <= 0.0f) return;
        const float dist = glm::length(x - (a.p + ba * t));
        if (dist >= w) return;
        const float h = a.height + (b.height - a.height) * t;
        const float v = h * inkProfile(s.profile, dist / w);
        if (!inside || std::abs(v) > std::abs(c)) c = v; // a stroke = its strongest segment there
        inside = true;
    };
    if (s.path.size() == 1) seg(s.path[0], s.path[0]);
    for (size_t k = 1; k < s.path.size(); ++k) seg(s.path[k - 1], s.path[k]);
    return inside;
}

float apply(InkOp op, float h, float c, float opacity) {
    switch (op) {
    case InkOp::Add: return h + opacity * c;
    case InkOp::Max: return std::max(h, c);
    case InkOp::Min: return std::min(h, -c);
    }
    return h;
}

} // namespace

float inkContribution(const InkStroke& s, const glm::vec3& x) {
    float c;
    contribution(s, x, c);
    return c;
}

AABB inkStrokeBox(const InkStroke& s) {
    AABB b;
    float w = 0.0f;
    for (const InkPoint& p : s.path) b.expand(p.p), w = std::max(w, p.width);
    b.min -= glm::vec3(w), b.max += glm::vec3(w);
    return b;
}

void inkStrokeBounds(const InkStroke& s, float& lo, float& hi) {
    lo = 0.0f, hi = 0.0f; // every profile is in [0, 1]
    for (const InkPoint& p : s.path) lo = std::min(lo, p.height), hi = std::max(hi, p.height);
}

// --- InkLayer ---

struct InkLayer::Impl {
    struct Seg {
        InkPoint a, b;
        AABB box; // the capsule's
    };
    struct Entry {
        InkStroke s;
        uint64_t order = 0;
        AABB box;
        float lo = 0.0f, hi = 0.0f;
        std::vector<Seg> segs;
    };
    struct Run {
        uint32_t begin = 0, end = 0; // into ids
        InkOp op = InkOp::Add;
    };
    struct Tile {
        std::vector<uint32_t> ids; // by order
        std::vector<Run> runs;
        std::vector<std::vector<float>> ck; // per run: the relief after it (before run 0: zero); the last = the result
        uint32_t count = 0;                 // samples
        AABB box;
        float lo = 0.0f, hi = 0.0f;
        const std::vector<float>& result() const { return ck.back(); }
    };
    // Per thread: the tile's sample positions (recomputed: cheaper than keeping 12 bytes a point) and the segments
    // of the stroke at hand that reach the tile.
    struct Scratch {
        std::vector<glm::vec3> pos;
        std::vector<const Seg*> segs;
        std::vector<float> zero;
        uint32_t tile = ~0u;
        uint64_t layer = 0; // whose tile `pos` holds
    };

    InkSurface& surf;
    std::unordered_map<uint32_t, Entry> strokes;
    std::vector<std::unique_ptr<Tile>> tiles;
    std::vector<uint64_t> gens;
    uint32_t nextId = 1;
    uint64_t nextOrder = 1;
    std::mutex statsLock;

    const uint64_t serial;

    static uint64_t newSerial() {
        static std::atomic<uint64_t> next{1};
        return next++;
    }
    explicit Impl(InkSurface& s) : surf(s), tiles(s.tileCount()), gens(s.tileCount(), 0), serial(newSerial()) {}

    static Scratch& scratch() {
        thread_local Scratch s;
        return s;
    }
    static void prepare(Entry& e) {
        e.segs.clear();
        const auto add = [&](const InkPoint& a, const InkPoint& b) {
            Seg g{a, b, {}};
            const float w = std::max(a.width, b.width);
            g.box.expand(a.p), g.box.expand(b.p);
            g.box.min -= glm::vec3(w), g.box.max += glm::vec3(w);
            e.segs.push_back(g);
        };
        if (e.s.path.size() == 1) add(e.s.path[0], e.s.path[0]);
        for (size_t k = 1; k < e.s.path.size(); ++k) add(e.s.path[k - 1], e.s.path[k]);
        e.box = inkStrokeBox(e.s);
        inkStrokeBounds(e.s, e.lo, e.hi);
    }
    static bool overlaps(const AABB& a, const AABB& b) {
        return a.min.x <= b.max.x && b.min.x <= a.max.x && a.min.y <= b.max.y && b.min.y <= a.max.y && a.min.z <= b.max.z &&
               b.min.z <= a.max.z;
    }
    static bool inBox(const AABB& b, const glm::vec3& p) {
        return p.x >= b.min.x && p.x <= b.max.x && p.y >= b.min.y && p.y <= b.max.y && p.z >= b.min.z && p.z <= b.max.z;
    }
    // The contribution of the gathered segments at x (a stroke = its strongest segment there).
    static bool contributionOf(const std::vector<const Seg*>& segs, InkProfile profile, const glm::vec3& x, float& c) {
        bool inside = false;
        c = 0.0f;
        for (const Seg* g : segs) {
            if (!inBox(g->box, x)) continue;
            const glm::vec3 ba = g->b.p - g->a.p;
            const float l2 = glm::dot(ba, ba);
            const float t = l2 > 1e-24f ? std::clamp(glm::dot(x - g->a.p, ba) / l2, 0.0f, 1.0f) : 0.0f;
            const float w = g->a.width + (g->b.width - g->a.width) * t;
            if (w <= 0.0f) continue;
            const glm::vec3 o = x - (g->a.p + ba * t);
            const float d2 = glm::dot(o, o);
            if (d2 >= w * w) continue;
            const float v = (g->a.height + (g->b.height - g->a.height) * t) * inkProfile(profile, std::sqrt(d2) / w);
            if (!inside || std::abs(v) > std::abs(c)) c = v;
            inside = true;
        }
        return inside;
    }

    const Entry& entry(uint32_t id) const { return strokes.at(id); }

    // The scratch holds tile ti's positions afterwards.
    const std::vector<glm::vec3>& positions(uint32_t ti) {
        Scratch& s = scratch();
        if (s.tile != ti || s.layer != serial) surf.samples(ti, s.pos), s.tile = ti, s.layer = serial;
        return s.pos;
    }
    // Gathers e's segments reaching tile t; false when none does.
    bool gather(const Tile& t, const Entry& e) {
        Scratch& s = scratch();
        s.segs.clear();
        if (!overlaps(t.box, e.box)) return false;
        for (const Seg& g : e.segs)
            if (overlaps(t.box, g.box)) s.segs.push_back(&g);
        return !s.segs.empty();
    }
    void partition(Tile& t) {
        t.runs.clear();
        for (uint32_t k = 0; k < t.ids.size(); ++k) {
            const InkOp op = entry(t.ids[k]).s.op;
            if (t.runs.empty() || t.runs.back().op != op) t.runs.push_back({k, k + 1, op});
            else t.runs.back().end = k + 1;
        }
        t.ck.resize(std::max<size_t>(t.runs.size(), 1));
        for (auto& c : t.ck) c.resize(t.count);
    }
    const std::vector<float>& start(Tile& t, size_t r) {
        if (r > 0) return t.ck[r - 1];
        Scratch& s = scratch();
        s.zero.assign(t.count, 0.0f);
        return s.zero;
    }
    void foldRun(uint32_t ti, Tile& t, size_t r, InkStats& st) {
        const Run& run = t.runs[r];
        const std::vector<glm::vec3>& pos = positions(ti);
        std::vector<float>& out = t.ck[r];
        out = start(t, r);
        for (uint32_t k = run.begin; k < run.end; ++k) {
            const Entry& e = entry(t.ids[k]);
            if (!gather(t, e)) continue;
            const auto& segs = scratch().segs;
            for (uint32_t i = 0; i < t.count; ++i) {
                float c;
                if (inBox(e.box, pos[i]) && contributionOf(segs, e.s.profile, pos[i], c)) out[i] = apply(run.op, out[i], c, e.s.opacity);
            }
            st.evaluated += t.count;
        }
    }
    void foldFrom(uint32_t ti, Tile& t, size_t r, InkStats& st) {
        for (size_t k = r; k < t.runs.size(); ++k) foldRun(ti, t, k, st);
    }
    void addContribution(uint32_t ti, const Tile& t, const Entry& e, float sign, std::vector<float>& out, InkStats& st) {
        if (!gather(t, e)) return;
        const std::vector<glm::vec3>& pos = positions(ti);
        const auto& segs = scratch().segs;
        for (uint32_t i = 0; i < t.count; ++i) {
            float c;
            if (inBox(e.box, pos[i]) && contributionOf(segs, e.s.profile, pos[i], c)) out[i] += sign * e.s.opacity * c;
        }
        st.evaluated += t.count;
    }
    void composeBounds(Tile& t) {
        float lo = 0.0f, hi = 0.0f;
        for (uint32_t id : t.ids) {
            const Entry& e = entry(id);
            switch (e.s.op) {
            case InkOp::Add: lo += e.s.opacity * e.lo, hi += e.s.opacity * e.hi; break;
            case InkOp::Max: hi = std::max(hi, e.hi); break; // (outside its footprint a point keeps its value)
            case InkOp::Min: lo = std::min(lo, -e.hi); break;
            }
        }
        t.lo = lo, t.hi = hi;
    }
    Tile& tileFor(uint32_t ti) {
        auto& t = tiles[ti];
        if (!t) {
            t = std::make_unique<Tile>();
            const std::vector<glm::vec3>& pos = positions(ti);
            t->count = uint32_t(pos.size());
            for (const glm::vec3& p : pos) t->box.expand(p);
            t->ck.assign(1, std::vector<float>(t->count, 0.0f));
        }
        return *t;
    }
    void rebuildTile(uint32_t ti, Tile& t, InkStats& st) {
        partition(t);
        if (t.runs.empty()) std::fill(t.ck[0].begin(), t.ck[0].end(), 0.0f);
        foldFrom(ti, t, 0, st);
    }
    void finish(uint32_t ti, InkStats& st) {
        auto& t = tiles[ti];
        composeBounds(*t);
        surf.write(ti, t->result());
        ++gens[ti];
        ++st.tiles, st.samples += t->count;
        if (t->ids.empty()) t.reset();
    }
    size_t runOf(const Tile& t, uint32_t k) const {
        for (size_t r = 0; r < t.runs.size(); ++r)
            if (k >= t.runs[r].begin && k < t.runs[r].end) return r;
        return t.runs.size();
    }
    uint32_t position(const Tile& t, uint64_t order) const {
        return uint32_t(std::lower_bound(t.ids.begin(), t.ids.end(), order,
                                         [&](uint32_t id, uint64_t o) { return entry(id).order < o; }) -
                        t.ids.begin());
    }

    // Stroke `id` (its entry holding the new state; `old` the previous one, or null for a new stroke) in tile ti.
    void update(uint32_t ti, uint32_t id, const Entry* old, bool inNew, InkStats& st) {
        Tile& t = tileFor(ti);
        const Entry& now = entry(id);
        const uint32_t k = position(t, now.order);
        const bool has = k < t.ids.size() && t.ids[k] == id;
        if (has && inNew && old && old->s.op == now.s.op) { // changed in place
            const size_t r = runOf(t, k);
            if (now.s.op == InkOp::Add) {
                addContribution(ti, t, *old, -1.0f, t.ck[r], st);
                addContribution(ti, t, now, 1.0f, t.ck[r], st);
                foldFrom(ti, t, r + 1, st);
            } else {
                foldFrom(ti, t, r, st);
            }
        } else if (!has && inNew) { // reaches this tile now
            t.ids.insert(t.ids.begin() + k, id);
            size_t r = t.runs.size(); // joins the run it lands in or touches when the operator matches
            for (size_t q = 0; q < t.runs.size() && r == t.runs.size(); ++q)
                if (t.runs[q].op == now.s.op && t.runs[q].begin <= k && k <= t.runs[q].end) r = q;
            if (r == t.runs.size()) {
                rebuildTile(ti, t, st);
            } else {
                t.runs[r].end++;
                for (size_t q = r + 1; q < t.runs.size(); ++q) t.runs[q].begin++, t.runs[q].end++;
                if (now.s.op == InkOp::Add) addContribution(ti, t, now, 1.0f, t.ck[r], st), foldFrom(ti, t, r + 1, st);
                else foldFrom(ti, t, r, st);
            }
        } else if (has && !inNew) { // left this tile
            const size_t r = runOf(t, k);
            t.ids.erase(t.ids.begin() + k);
            if (t.runs[r].end - t.runs[r].begin == 1 || !old || old->s.op != now.s.op) {
                rebuildTile(ti, t, st);
            } else {
                t.runs[r].end--;
                for (size_t q = r + 1; q < t.runs.size(); ++q) t.runs[q].begin--, t.runs[q].end--;
                if (old->s.op == InkOp::Add) addContribution(ti, t, *old, -1.0f, t.ck[r], st), foldFrom(ti, t, r + 1, st);
                else foldFrom(ti, t, r, st);
            }
        } else if (has) { // operator changed
            rebuildTile(ti, t, st);
        } else {
            if (t.ids.empty()) tiles[ti].reset();
            return;
        }
        finish(ti, st);
    }

    void touch(uint32_t id, const Entry* old, InkStats* stats) {
        std::vector<uint32_t> before, after, all;
        if (old) surf.tilesIn(old->box, before);
        const auto it = strokes.find(id);
        if (it != strokes.end() && !it->second.s.path.empty()) surf.tilesIn(it->second.box, after);
        std::sort(before.begin(), before.end()), std::sort(after.begin(), after.end());
        std::set_union(before.begin(), before.end(), after.begin(), after.end(), std::back_inserter(all));
        // tiles are independent: one per job (each writes its own tile and its own part of the surface)
        poolFor(all.size(), [&](size_t b, size_t e) {
            InkStats local;
            for (size_t i = b; i < e; ++i) update(all[i], id, old, std::binary_search(after.begin(), after.end(), all[i]), local);
            if (stats) {
                std::lock_guard<std::mutex> lock(statsLock);
                stats->tiles += local.tiles, stats->evaluated += local.evaluated, stats->samples += local.samples;
            }
        }, 8);
    }
};

InkLayer::InkLayer(InkSurface& surface) : d_(std::make_unique<Impl>(surface)) {}
InkLayer::~InkLayer() = default;

uint32_t InkLayer::add(InkStroke s, InkStats* stats) {
    if (s.id == 0) s.id = d_->nextId++;
    else d_->nextId = std::max(d_->nextId, s.id + 1);
    Impl::Entry e;
    e.order = d_->nextOrder++;
    e.s = std::move(s);
    Impl::prepare(e);
    const uint32_t id = e.s.id;
    d_->strokes[id] = std::move(e);
    d_->touch(id, nullptr, stats);
    return id;
}

bool InkLayer::edit(const InkStroke& s, InkStats* stats) {
    const auto it = d_->strokes.find(s.id);
    if (it == d_->strokes.end()) return false;
    const Impl::Entry old = it->second;
    it->second.s = s;
    Impl::prepare(it->second);
    d_->touch(s.id, &old, stats);
    return true;
}

bool InkLayer::remove(uint32_t id, InkStats* stats) {
    const auto it = d_->strokes.find(id);
    if (it == d_->strokes.end()) return false;
    // leaves every tile it was in (an edit to an empty path), then goes
    const Impl::Entry old = it->second;
    it->second.s.path.clear();
    Impl::prepare(it->second);
    d_->touch(id, &old, stats);
    d_->strokes.erase(id);
    return true;
}

const InkStroke* InkLayer::find(uint32_t id) const {
    const auto it = d_->strokes.find(id);
    return it == d_->strokes.end() ? nullptr : &it->second.s;
}

size_t InkLayer::size() const { return d_->strokes.size(); }

void InkLayer::rebuild(InkStats* stats) {
    std::vector<std::vector<uint32_t>> lists(d_->tiles.size());
    std::vector<std::pair<uint64_t, uint32_t>> order;
    for (const auto& [id, e] : d_->strokes) order.push_back({e.order, id});
    std::sort(order.begin(), order.end());
    std::vector<uint32_t> found;
    for (const auto& [o, id] : order) {
        found.clear();
        d_->surf.tilesIn(d_->strokes[id].box, found);
        for (uint32_t ti : found) lists[ti].push_back(id);
    }
    std::vector<uint32_t> todo;
    for (uint32_t ti = 0; ti < d_->tiles.size(); ++ti)
        if (!lists[ti].empty() || d_->tiles[ti]) todo.push_back(ti);
    poolFor(todo.size(), [&](size_t b, size_t e) {
        InkStats local;
        for (size_t i = b; i < e; ++i) {
            const uint32_t ti = todo[i];
            Impl::Tile& t = d_->tileFor(ti);
            t.ids = std::move(lists[ti]);
            d_->rebuildTile(ti, t, local);
            d_->finish(ti, local);
        }
        if (stats) {
            std::lock_guard<std::mutex> lock(d_->statsLock);
            stats->tiles += local.tiles, stats->evaluated += local.evaluated, stats->samples += local.samples;
        }
    }, 8);
}

const std::vector<float>* InkLayer::relief(uint32_t tile) const {
    return tile < d_->tiles.size() && d_->tiles[tile] ? &d_->tiles[tile]->result() : nullptr;
}

void InkLayer::bounds(uint32_t tile, float& lo, float& hi) const {
    lo = hi = 0.0f;
    if (tile < d_->tiles.size() && d_->tiles[tile]) lo = d_->tiles[tile]->lo, hi = d_->tiles[tile]->hi;
}

uint64_t InkLayer::generation(uint32_t tile) const { return tile < d_->gens.size() ? d_->gens[tile] : 0; }

size_t InkLayer::bytes() const {
    size_t b = d_->gens.size() * sizeof(uint64_t) + d_->tiles.size() * sizeof(void*);
    for (const auto& t : d_->tiles) {
        if (!t) continue;
        b += sizeof(*t) + t->ids.capacity() * 4 + t->runs.capacity() * sizeof(Impl::Run);
        for (const auto& c : t->ck) b += c.capacity() * sizeof(float);
    }
    for (const auto& [id, e] : d_->strokes) b += sizeof(e) + e.s.path.capacity() * sizeof(InkPoint) + e.segs.capacity() * sizeof(Impl::Seg);
    return b;
}


// --- Micro surface ---

namespace {

class MicroInk final : public InkSurface {
public:
    explicit MicroInk(Mesh& m) : mesh_(m), mm_(*m.micro) {
        // a uniform grid over the base triangles' boxes (grown by the Phong bulge and the largest displacement)
        const size_t nt = mm_.baseTriangles;
        AABB all;
        boxes_.resize(nt);
        const float grow = mm_.bulgeMax + float(mm_.rangeMax) * mm_.step;
        for (uint32_t t = 0; t < nt; ++t) {
            AABB b;
            for (int c = 0; c < 3; ++c) b.expand(mesh_.positions[mesh_.triangles[t][c]]);
            b.min -= glm::vec3(grow), b.max += glm::vec3(grow);
            boxes_[t] = b, all.expand(b.min), all.expand(b.max);
        }
        origin_ = all.min;
        cell_ = std::max(mm_.baseEdge * 4.0f, 1e-6f);
        const glm::vec3 ext = (all.max - all.min) / cell_;
        dim_ = glm::ivec3(glm::max(glm::ceil(ext), glm::vec3(1.0f)));
        cells_.resize(size_t(dim_.x) * dim_.y * dim_.z);
        for (uint32_t t = 0; t < nt; ++t) forCells(boxes_[t], [&](size_t c) { cells_[c].push_back(t); });
        base_.resize(nt);
    }
    size_t tileCount() const override { return mm_.baseTriangles; }
    void tilesIn(const AABB& box, std::vector<uint32_t>& out) const override {
        const size_t first = out.size();
        forCells(box, [&](size_t c) {
            for (uint32_t t : cells_[c])
                if (overlaps(boxes_[t], box)) out.push_back(t);
        });
        std::sort(out.begin() + long(first), out.end());
        out.erase(std::unique(out.begin() + long(first), out.end()), out.end());
    }
    void samples(uint32_t tile, std::vector<glm::vec3>& out) const override {
        MicroTriangle mt;
        mt.set(mesh_, tile, mm_.n);
        out.resize(mm_.samples);
        glm::vec3 s, n;
        for (uint32_t j = 0; j <= mm_.n; ++j)
            for (uint32_t i = 0; i + j <= mm_.n; ++i) {
                mt.eval(i, j, s, n);
                out[MicroMesh::index(i, j, mm_.n)] = s;
            }
    }
    void write(uint32_t tile, std::span<const float> relief) override { // (tiles in parallel: each its own grid)
        int16_t* d = mm_.disp.data() + size_t(tile) * mm_.samples;
        auto& base = base_[tile];
        if (!base) base = std::make_unique<std::vector<int16_t>>(d, d + mm_.samples);
        const float inv = 1.0f / mm_.step;
        int most = 0;
        for (uint32_t k = 0; k < mm_.samples; ++k) {
            const long v = long((*base)[k]) + std::lround(relief[k] * inv);
            d[k] = int16_t(std::clamp(v, -32767L, 32767L));
            most = std::max(most, std::abs(int(d[k])));
        }
        std::lock_guard<std::mutex> lock(lock_);
        if (tile < mm_.triRange.size()) mm_.triRange[tile] = uint16_t(most), mm_.rangeMax = std::max<uint16_t>(mm_.rangeMax, uint16_t(most));
        mm_.markDirty(tile);
        ++mm_.revision;
    }
    size_t bytes() const {
        size_t b = cells_.size() * sizeof(cells_[0]) + boxes_.size() * sizeof(AABB) + base_.size() * sizeof(void*);
        for (const auto& v : base_) b += v ? v->size() * 2 : 0;
        return b;
    }

private:
    static bool overlaps(const AABB& a, const AABB& b) {
        return a.min.x <= b.max.x && b.min.x <= a.max.x && a.min.y <= b.max.y && b.min.y <= a.max.y && a.min.z <= b.max.z &&
               b.min.z <= a.max.z;
    }
    template <class F>
    void forCells(const AABB& b, F&& f) const {
        const glm::ivec3 lo = glm::clamp(glm::ivec3(glm::floor((b.min - origin_) / cell_)), glm::ivec3(0), dim_ - 1);
        const glm::ivec3 hi = glm::clamp(glm::ivec3(glm::floor((b.max - origin_) / cell_)), glm::ivec3(0), dim_ - 1);
        for (int z = lo.z; z <= hi.z; ++z)
            for (int y = lo.y; y <= hi.y; ++y)
                for (int x = lo.x; x <= hi.x; ++x) f((size_t(z) * dim_.y + y) * dim_.x + x);
    }
    Mesh& mesh_;
    MicroMesh& mm_;
    std::vector<AABB> boxes_;
    std::vector<std::vector<uint32_t>> cells_;
    glm::vec3 origin_{0.0f};
    float cell_ = 1.0f;
    glm::ivec3 dim_{1};
    std::vector<std::unique_ptr<std::vector<int16_t>>> base_; // the sculpt's values under the ink, per inked tile
    std::mutex lock_;                                          // the level's shared state (dirty bits, ranges)
};

// --- Sheet surface ---

struct SheetTiles {
    uint32_t tx = 1, ty = 1;
    explicit SheetTiles(const InkSheet& s) : tx((s.cols + s.tile - 1) / s.tile), ty((s.rows + s.tile - 1) / s.tile) {}
};

class SheetInk final : public InkSurface {
public:
    SheetInk(const InkSheet& s, std::vector<float>& heights) : s_(s), t_(s), h_(heights) { h_.assign(size_t(s.cols) * s.rows, 0.0f); }
    size_t tileCount() const override { return size_t(t_.tx) * t_.ty; }
    void tilesIn(const AABB& box, std::vector<uint32_t>& out) const override {
        const auto range = [&](float lo, float hi, uint32_t count, float o, uint32_t& a, uint32_t& b) {
            const float fa = std::floor((lo - o) / s_.spacing), fb = std::ceil((hi - o) / s_.spacing);
            if (fb < 0.0f || fa > float(count - 1)) return false;
            a = uint32_t(std::max(fa, 0.0f)), b = uint32_t(std::min(fb, float(count - 1)));
            return true;
        };
        uint32_t x0, x1, y0, y1;
        if (!range(box.min.x, box.max.x, s_.cols, s_.origin.x, x0, x1) || !range(box.min.y, box.max.y, s_.rows, s_.origin.y, y0, y1)) return;
        for (uint32_t ty = y0 / s_.tile; ty <= y1 / s_.tile; ++ty)
            for (uint32_t tx = x0 / s_.tile; tx <= x1 / s_.tile; ++tx) out.push_back(ty * t_.tx + tx);
    }
    void samples(uint32_t tile, std::vector<glm::vec3>& out) const override {
        out.clear();
        forPoints(tile, [&](uint32_t x, uint32_t y) { out.push_back(s_.origin + glm::vec3(float(x) * s_.spacing, float(y) * s_.spacing, 0.0f)); });
    }
    void write(uint32_t tile, std::span<const float> relief) override {
        size_t k = 0;
        forPoints(tile, [&](uint32_t x, uint32_t y) { h_[size_t(y) * s_.cols + x] = relief[k++]; });
    }
    template <class F>
    void forPoints(uint32_t tile, F&& f) const {
        const uint32_t tx = tile % t_.tx, ty = tile / t_.tx;
        for (uint32_t y = ty * s_.tile; y < std::min(s_.rows, (ty + 1) * s_.tile); ++y)
            for (uint32_t x = tx * s_.tile; x < std::min(s_.cols, (tx + 1) * s_.tile); ++x) f(x, y);
    }

private:
    InkSheet s_;
    SheetTiles t_;
    std::vector<float>& h_;
};

float sheetHeight(const InkSheet& s, std::span<const float> h, float x, float y) { // bilinear, in point units
    x = std::clamp(x, 0.0f, float(s.cols - 1)), y = std::clamp(y, 0.0f, float(s.rows - 1));
    const uint32_t x0 = std::min(uint32_t(x), s.cols - 2), y0 = std::min(uint32_t(y), s.rows - 2);
    const float fx = x - float(x0), fy = y - float(y0);
    const auto at = [&](uint32_t a, uint32_t b) { return h[size_t(b) * s.cols + a]; };
    return (at(x0, y0) * (1 - fx) + at(x0 + 1, y0) * fx) * (1 - fy) + (at(x0, y0 + 1) * (1 - fx) + at(x0 + 1, y0 + 1) * fx) * fy;
}

glm::vec3 sheetNormal(const InkSheet& s, std::span<const float> h, uint32_t x, uint32_t y) {
    const auto at = [&](long a, long b) {
        a = std::clamp(a, 0L, long(s.cols) - 1), b = std::clamp(b, 0L, long(s.rows) - 1);
        return h[size_t(b) * s.cols + size_t(a)];
    };
    const float dx = (at(long(x) + 1, y) - at(long(x) - 1, y)) / (2.0f * s.spacing);
    const float dy = (at(x, long(y) + 1) - at(x, long(y) - 1)) / (2.0f * s.spacing);
    return glm::normalize(glm::vec3(-dx, -dy, 1.0f));
}

} // namespace

std::unique_ptr<InkSurface> inkMicroSurface(Mesh& mesh) {
    if (!mesh.micro) return nullptr;
    return std::make_unique<MicroInk>(mesh);
}

std::unique_ptr<InkSurface> inkSheetSurface(const InkSheet& sheet, std::vector<float>& heights) {
    return std::make_unique<SheetInk>(sheet, heights);
}

Mesh inkSheetMesh(const InkSheet& s, std::span<const float> h) {
    Mesh m;
    m.positions.resize(size_t(s.cols) * s.rows);
    m.normals.resize(m.positions.size());
    for (uint32_t y = 0; y < s.rows; ++y)
        for (uint32_t x = 0; x < s.cols; ++x) {
            const size_t k = size_t(y) * s.cols + x;
            m.positions[k] = s.origin + glm::vec3(float(x) * s.spacing, float(y) * s.spacing, h[k]);
            m.normals[k] = sheetNormal(s, h, x, y);
        }
    for (uint32_t y = 0; y + 1 < s.rows; ++y)
        for (uint32_t x = 0; x + 1 < s.cols; ++x) {
            const uint32_t a = y * s.cols + x, b = a + 1, c = a + s.cols, d = c + 1;
            m.triangles.push_back({a, b, d}), m.triangles.push_back({a, d, c});
        }
    return m;
}

size_t inkRebake(const InkSheet& s, const InkLayer& layer, std::span<const float> h, Mesh& mesh, std::vector<uint64_t>& baked) {
    const SheetTiles t(s);
    const size_t n = size_t(t.tx) * t.ty;
    baked.resize(n, 0);
    size_t count = 0;
    for (uint32_t ti = 0; ti < n; ++ti) {
        const uint64_t g = layer.generation(ti);
        if (g == baked[ti]) continue;
        baked[ti] = g, ++count;
        const uint32_t tx = ti % t.tx, ty = ti / t.tx;
        // the tile's points, and one ring around them (their normals read the neighbours)
        const uint32_t x0 = tx * s.tile, y0 = ty * s.tile;
        const uint32_t x1 = std::min(s.cols, x0 + s.tile), y1 = std::min(s.rows, y0 + s.tile);
        for (uint32_t y = y0; y < y1; ++y)
            for (uint32_t x = x0; x < x1; ++x) mesh.positions[size_t(y) * s.cols + x].z = s.origin.z + h[size_t(y) * s.cols + x];
        for (uint32_t y = y0 > 0 ? y0 - 1 : 0; y < std::min(s.rows, y1 + 1); ++y)
            for (uint32_t x = x0 > 0 ? x0 - 1 : 0; x < std::min(s.cols, x1 + 1); ++x)
                mesh.normals[size_t(y) * s.cols + x] = sheetNormal(s, h, x, y);
    }
    return count;
}

bool inkSheetPick(const InkSheet& s, const InkLayer& layer, std::span<const float> h, const glm::vec3& origin,
                  const glm::vec3& dir, glm::vec3& hit, size_t* tilesTested) {
    const SheetTiles t(s);
    struct Cand { float t0, t1; uint32_t tile; };
    std::vector<Cand> cands;
    const glm::vec3 inv = 1.0f / glm::vec3(std::abs(dir.x) > 1e-20f ? dir.x : 1e-20f, std::abs(dir.y) > 1e-20f ? dir.y : 1e-20f,
                                           std::abs(dir.z) > 1e-20f ? dir.z : 1e-20f);
    for (uint32_t ti = 0; ti < t.tx * t.ty; ++ti) {
        float lo, hi;
        layer.bounds(ti, lo, hi); // closed form: no evaluation
        const uint32_t tx = ti % t.tx, ty = ti / t.tx;
        const glm::vec3 a = s.origin + glm::vec3(float(tx * s.tile) * s.spacing, float(ty * s.tile) * s.spacing, lo);
        const glm::vec3 b = s.origin + glm::vec3(float(std::min(s.cols - 1, (tx + 1) * s.tile)) * s.spacing,
                                                 float(std::min(s.rows - 1, (ty + 1) * s.tile)) * s.spacing, hi);
        const glm::vec3 t0 = (a - origin) * inv, t1 = (b - origin) * inv;
        const glm::vec3 tn = glm::min(t0, t1), tf = glm::max(t0, t1);
        const float enter = std::max({tn.x, tn.y, tn.z, 0.0f}), leave = std::min({tf.x, tf.y, tf.z});
        if (enter <= leave) cands.push_back({enter, leave, ti});
    }
    std::sort(cands.begin(), cands.end(), [](const Cand& p, const Cand& q) { return p.t0 < q.t0; });
    size_t tested = 0;
    const float step = s.spacing * 0.25f / std::max(glm::length(dir), 1e-12f);
    const auto f = [&](float tt) {
        const glm::vec3 p = origin + dir * tt;
        return p.z - (s.origin.z + sheetHeight(s, h, (p.x - s.origin.x) / s.spacing, (p.y - s.origin.y) / s.spacing));
    };
    for (const Cand& c : cands) {
        ++tested;
        float ta = c.t0, fa = f(ta);
        for (float tb = std::min(c.t0 + step, c.t1);; tb = std::min(tb + step, c.t1)) {
            const float fb = f(tb);
            if ((fa > 0.0f) != (fb > 0.0f) || fb == 0.0f) {
                for (int k = 0; k < 40; ++k) { // bisection to the crossing
                    const float tm = 0.5f * (ta + tb), fm = f(tm);
                    if ((fa > 0.0f) != (fm > 0.0f)) tb = tm;
                    else ta = tm, fa = fm;
                }
                hit = origin + dir * (0.5f * (ta + tb));
                if (tilesTested) *tilesTested = tested;
                return true;
            }
            ta = tb, fa = fb;
            if (tb >= c.t1) break;
        }
    }
    if (tilesTested) *tilesTested = tested;
    return false;
}

} // namespace sf
