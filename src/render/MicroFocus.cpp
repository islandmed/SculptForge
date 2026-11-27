// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/MicroFocus.h"

#include "core/FocusDetail.h"
#include "core/ThreadPool.h"
#include "core/TierConfig.h"
#include "render/MicroRenderer.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstring>

namespace sf {

namespace {
// Adds the time since the last lap to one part of MicroFocus's split.
struct Lap {
    double* ms;
    std::chrono::steady_clock::time_point t = std::chrono::steady_clock::now();
    void operator()(int part) {
        const auto now = std::chrono::steady_clock::now();
        ms[part] += std::chrono::duration<double, std::milli>(now - t).count();
        t = now;
    }
};
} // namespace

namespace {

constexpr GLenum kUnitPages = 0x84CD, kUnitPool = 0x84CE, kUnitPoolShade = 0x84CF; // GL_TEXTURE13-15
constexpr uint32_t kChunkTexels = 1u << 26; // (MicroRenderer's: texture buffer limits)
constexpr uint32_t kShaded = 0x80000000u;

} // namespace

MicroFocus::~MicroFocus() { release(); }

void MicroFocus::release() {
    pageStore_.release();
    for (GLuint* b : {&poolDisp_, &poolShade_})
        if (*b) gl::DeleteBuffers(1, b), *b = 0;
    for (GLuint* t : {&poolDispTex_, &poolShadeTex_})
        if (*t) gl::DeleteTextures(1, t), *t = 0;
    capacity_ = 0;
    bytes_ = 0;
    page_.clear();
    owner_.clear();
    free_.clear();
    lastUse_.clear();
    pendingShade_.clear();
    pageCount_ = shadedPages_ = 0;
}

void MicroFocus::setup(const Mesh& mesh, uint32_t slots) {
    release();
    const MicroMesh& mm = *mesh.micro;
    n_ = mm.n;
    samples_ = mm.samples;
    coarseN_ = 1u << FocusDetail::kCoarseLevel;
    coarseSamples_ = (coarseN_ + 1) * (coarseN_ + 2) / 2;
    shift_ = uint32_t(mm.level - FocusDetail::kCoarseLevel);
    record_ = coarseSamples_ + 3 * (n_ - 1);
    slots_ = slots;
    page_.assign(slots, 0);
    pageTableDirty_ = true;
    drawn_.clear();
}

// The coarse level's grids (and mask) as subsamples of the full ones, for `tris` (empty: all of them).
void MicroFocus::buildCoarse(const Mesh& mesh, const MicroMesh& mm, const std::vector<uint32_t>& tris) {
    const uint32_t nt = mm.baseTriangles, CS = coarseSamples_, cn = coarseN_, s = 1u << shift_;
    const bool all = tris.empty() || coarse_.baseTriangles != nt || coarse_.edgeNeighbor.size() != mm.edgeNeighbor.size();
    if (all) {
        coarse_ = MicroMesh();
        coarse_.level = FocusDetail::kCoarseLevel;
        coarse_.n = cn;
        coarse_.samples = CS;
        coarse_.baseTriangles = nt;
        coarse_.baseVertices = uint32_t(mesh.vertexCount());
        coarse_.edgeNeighbor = mm.edgeNeighbor;
        coarse_.disp.assign(size_t(nt) * CS, 0);
    }
    coarse_.step = mm.step;
    coarse_.bulgeMax = mm.bulgeMax;
    coarse_.baseEdge = mm.baseEdge;
    if (mm.hasMask() != coarse_.hasMask()) {
        if (mm.hasMask()) coarse_.mask.assign(size_t(nt) * CS, 0);
        else coarse_.mask.clear();
    }
    auto one = [&](uint32_t t) {
        const int16_t* g = mm.disp.data() + size_t(t) * samples_;
        int16_t* c = coarse_.disp.data() + size_t(t) * CS;
        const uint8_t* mk = mm.hasMask() ? mm.mask.data() + size_t(t) * samples_ : nullptr;
        uint8_t* cm = mk ? coarse_.mask.data() + size_t(t) * CS : nullptr;
        for (uint32_t J = 0; J <= cn; ++J)
            for (uint32_t I = 0; I + J <= cn; ++I) {
                const uint32_t k = MicroMesh::index(I * s, J * s, n_), q = MicroMesh::index(I, J, cn);
                c[q] = g[k];
                if (mk) cm[q] = mk[k];
            }
    };
    if (all) poolFor(nt, [&](size_t b, size_t e) { for (size_t t = b; t < e; ++t) one(uint32_t(t)); }, 1024);
    else for (uint32_t t : tris) one(t);
}

// Records of `slots` (sorted, unique) or of all of them into MicroRenderer's chunks.
void MicroFocus::writeRecords(const MicroMesh& mm, MicroRenderer& r, std::vector<uint32_t>& slots, bool all) {
    const uint32_t R = record_;
    const std::vector<uint32_t>& order = r.order_;
    auto fill = [&](uint32_t slot, int16_t* out) { focusFillRecord(mm.disp.data() + size_t(order[slot]) * samples_, n_, coarseN_, out); };
    if (all) {
        for (MicroRenderer::Chunk& c : r.chunks_) {
            gl::BindBuffer(GL_TEXTURE_BUFFER, c.disp);
            auto* dst = static_cast<int16_t*>(gl::MapBufferRange(GL_TEXTURE_BUFFER, 0, GLsizeiptr(size_t(c.slots) * R * 2),
                                                                  GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT));
            if (!dst) continue;
            poolFor(c.slots, [&](size_t b, size_t e) {
                for (size_t k = b; k < e; ++k) fill(c.firstSlot + uint32_t(k), dst + k * R);
            }, 1024);
            gl::UnmapBuffer(GL_TEXTURE_BUFFER);
        }
        gl::BindBuffer(GL_TEXTURE_BUFFER, 0);
        return;
    }
    if (slots.empty()) return;
    staging_.resize(slots.size() * R);
    poolFor(slots.size(), [&](size_t b, size_t e) {
        for (size_t k = b; k < e; ++k) fill(slots[k], staging_.data() + k * R);
    }, 256);
    r.upload_.stage(staging_.data(), staging_.size() * 2); // (render/BufferUpload.h: copies on the GPU, not BufferSubData)
    for (size_t i = 0; i < slots.size();) {
        const uint32_t chunk = slots[i] / r.chunkSlots_;
        size_t j = i + 1;
        while (j < slots.size() && slots[j] == slots[j - 1] + 1 && slots[j] / r.chunkSlots_ == chunk) ++j;
        const MicroRenderer::Chunk& c = r.chunks_[chunk];
        r.upload_.copy(c.disp, i * R * 2, size_t(slots[i] - c.firstSlot) * R * 2, (j - i) * R * 2);
        i = j;
    }
    BufferUpload::done();
}

// Coarse shading of `tris` and the tiles around them (all of it when `all`), shaded on the coarse level and sent.
void MicroFocus::shadeCoarse(const Mesh& mesh, MicroRenderer& r, const std::vector<uint32_t>& tris, bool all) {
    const uint32_t CS = coarseSamples_;
    const std::vector<uint32_t>& slotOf = r.slotOf_;
    if (all) {
        std::vector<uint32_t*> mapped(r.chunks_.size(), nullptr);
        for (size_t i = 0; i < r.chunks_.size(); ++i) {
            gl::BindBuffer(GL_TEXTURE_BUFFER, r.chunks_[i].shade);
            mapped[i] = static_cast<uint32_t*>(gl::MapBufferRange(GL_TEXTURE_BUFFER, 0, GLsizeiptr(size_t(r.chunks_[i].slots) * CS * 4),
                                                                   GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT));
        }
        thread_local std::vector<uint32_t> sink;
        shader_.shadeLevel(mesh, coarse_, {}, true, written_, [&](uint32_t t) -> uint32_t* {
            const uint32_t s = slotOf[t], c = s / r.chunkSlots_;
            if (!mapped[c]) {
                sink.resize(CS);
                return sink.data();
            }
            return mapped[c] + size_t(s - r.chunks_[c].firstSlot) * CS;
        });
        for (size_t i = 0; i < r.chunks_.size(); ++i) {
            gl::BindBuffer(GL_TEXTURE_BUFFER, r.chunks_[i].shade);
            if (mapped[i]) gl::UnmapBuffer(GL_TEXTURE_BUFFER);
        }
        gl::BindBuffer(GL_TEXTURE_BUFFER, 0);
        return;
    }
    if (tris.empty()) return;
    // In slices, each written straight into its staging in slot order (the shader's prepare sees the tiles first) and
    // sent in runs. A whole-queue batch (Undo, Redo, Move under shadeAll) at once kept a base-sized scatter copy, the
    // batch's staging and ~1 GB of the shader's edge sums on a streamed 2.6M base (+1.9 GB over 0.13.12).
    constexpr size_t kSlice = 16384;
    std::vector<uint32_t>& index = r.stageIndex_;
    if (index.size() != r.baseTriangles_) index.assign(r.baseTriangles_, 0);
    std::vector<uint32_t> sorted;
    for (size_t at = 0; at < tris.size(); at += kSlice) {
        const std::span<const uint32_t> slice = std::span<const uint32_t>(tris).subspan(at, std::min(kSlice, tris.size() - at));
        sorted.clear();
        shader_.shadeLevel(mesh, coarse_, slice, false, written_,
                           [&](uint32_t t) -> uint32_t* { return shadeStaging_.data() + size_t(index[t]) * CS; }, true,
                           [&](std::span<const uint32_t> set) {
                               sorted.assign(set.begin(), set.end());
                               std::sort(sorted.begin(), sorted.end(), [&](uint32_t a, uint32_t b) { return slotOf[a] < slotOf[b]; });
                               for (size_t i = 0; i < sorted.size(); ++i) index[sorted[i]] = uint32_t(i);
                               shadeStaging_.resize(sorted.size() * CS);
                           });
        if (sorted.empty()) continue;
        r.upload_.stage(shadeStaging_.data(), sorted.size() * CS * 4);
        for (size_t i = 0; i < sorted.size();) {
            const uint32_t s0 = slotOf[sorted[i]], chunk = s0 / r.chunkSlots_;
            size_t j = i + 1;
            while (j < sorted.size() && slotOf[sorted[j]] == slotOf[sorted[j - 1]] + 1 && slotOf[sorted[j]] / r.chunkSlots_ == chunk) ++j;
            const MicroRenderer::Chunk& c = r.chunks_[chunk];
            r.upload_.copy(c.shade, i * CS * 4, size_t(s0 - c.firstSlot) * CS * 4, (j - i) * CS * 4);
            i = j;
        }
    }
    BufferUpload::done();
    trimScratch(kSlice);
}

// Gives back what one big batch left at its size (the next big one takes it again; slices stay small).
void MicroFocus::trimScratch(size_t slice) {
    const size_t keep = slice * 8 * std::max<size_t>(samples_, coarseSamples_) * 4;
    if (shadeStaging_.capacity() * 4 > keep) std::vector<uint32_t>().swap(shadeStaging_);
    if (staging_.capacity() * 2 > keep) std::vector<int16_t>().swap(staging_);
    shader_.trimScratch(keep);
}

bool MicroFocus::growPool(uint32_t want) {
    const size_t S = samples_;
    const uint32_t maxPages = uint32_t(std::min<size_t>(gl::bufferChunkTexels(kChunkTexels) / S, poolBudgetBytes / (S * 6)));
    if (capacity_ >= maxPages) return false;
    const uint32_t cap = std::min(maxPages, std::max({want, capacity_ * 2, 256u}));
    GLuint disp = 0, shade = 0;
    gl::GenBuffers(1, &disp);
    gl::GenBuffers(1, &shade);
    gl::BindBuffer(GL_COPY_WRITE_BUFFER, disp);
    gl::BufferData(GL_COPY_WRITE_BUFFER, GLsizeiptr(size_t(cap) * S * 2), nullptr, GL_DYNAMIC_DRAW);
    gl::BindBuffer(GL_COPY_WRITE_BUFFER, shade);
    gl::BufferData(GL_COPY_WRITE_BUFFER, GLsizeiptr(size_t(cap) * S * 4), nullptr, GL_DYNAMIC_DRAW);
    if (capacity_) { // the pages held move along
        gl::BindBuffer(GL_COPY_READ_BUFFER, poolDisp_);
        gl::BindBuffer(GL_COPY_WRITE_BUFFER, disp);
        gl::CopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, GLsizeiptr(size_t(capacity_) * S * 2));
        gl::BindBuffer(GL_COPY_READ_BUFFER, poolShade_);
        gl::BindBuffer(GL_COPY_WRITE_BUFFER, shade);
        gl::CopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, GLsizeiptr(size_t(capacity_) * S * 4));
        gl::BindBuffer(GL_COPY_READ_BUFFER, 0);
        gl::DeleteBuffers(1, &poolDisp_);
        gl::DeleteBuffers(1, &poolShade_);
        bytes_ -= size_t(capacity_) * S * 6;
    }
    gl::BindBuffer(GL_COPY_WRITE_BUFFER, 0);
    poolDisp_ = disp, poolShade_ = shade;
    if (!poolDispTex_) gl::GenTextures(1, &poolDispTex_);
    if (!poolShadeTex_) gl::GenTextures(1, &poolShadeTex_);
    gl::BindTexture(GL_TEXTURE_BUFFER, poolDispTex_);
    gl::TexBuffer(GL_TEXTURE_BUFFER, GL_R16I, poolDisp_);
    gl::BindTexture(GL_TEXTURE_BUFFER, poolShadeTex_);
    gl::TexBuffer(GL_TEXTURE_BUFFER, GL_RGBA8, poolShade_);
    gl::BindTexture(GL_TEXTURE_BUFFER, 0);
    for (uint32_t p = cap; p > capacity_; --p) free_.push_back(p - 1); // (lowest first off the back)
    owner_.resize(cap, ~0u);
    lastUse_.resize(cap, 0);
    bytes_ += size_t(cap) * S * 6;
    capacity_ = cap;
    return true;
}

void MicroFocus::freePage(uint32_t slot) {
    const uint32_t e = page_[slot];
    if (!e) return;
    const uint32_t p = (e & ~kShaded) - 1;
    owner_[p] = ~0u;
    free_.push_back(p);
    if (e & kShaded) --shadedPages_;
    page_[slot] = 0;
    --pageCount_;
    pageTableDirty_ = true;
}

// The full grids of `slots` (with pages) to their pages.
void MicroFocus::sendPages(const MicroMesh& mm, MicroRenderer& r, const std::vector<uint32_t>& slots) {
    if (slots.empty()) return;
    const size_t S = samples_;
    std::vector<std::pair<uint32_t, uint32_t>> list; // (page, slot) in page order: runs of consecutive pages
    list.reserve(slots.size());
    for (uint32_t s : slots)
        if (page_[s]) list.push_back({(page_[s] & ~kShaded) - 1, s});
    std::sort(list.begin(), list.end());
    // In slices: a widen re-sends every resident page (resendHeights), which staged the whole pool at once.
    constexpr size_t kSlice = 16384;
    for (size_t at = 0; at < list.size(); at += kSlice) {
        const size_t count = std::min(kSlice, list.size() - at);
        staging_.resize(count * S);
        poolFor(count, [&](size_t b, size_t e) {
            for (size_t k = b; k < e; ++k)
                std::memcpy(staging_.data() + k * S, mm.disp.data() + size_t(r.order_[list[at + k].second]) * S, S * 2);
        }, 64);
        r.upload_.stage(staging_.data(), count * S * 2);
        for (size_t i = 0; i < count;) {
            size_t j = i + 1;
            while (j < count && list[at + j].first == list[at + j - 1].first + 1) ++j;
            r.upload_.copy(poolDisp_, i * S * 2, size_t(list[at + i].first) * S * 2, (j - i) * S * 2);
            i = j;
        }
    }
    BufferUpload::done();
    trimScratch(kSlice);
}

// Full shading for the pages waiting for it, up to `budgetPoints` grid points.
void MicroFocus::shadePages(const Mesh& mesh, MicroRenderer& r, size_t budgetPoints) {
    if (pendingShade_.empty()) return;
    const size_t S = samples_;
    const size_t limit = budgetPoints ? std::max<size_t>(1, budgetPoints / S) : pendingShade_.size();
    std::vector<uint32_t> seeds; // base triangles
    // From the slot after the last one shaded, round again (sorted by slot): a stroke that dirties more pages every frame than
    // the budget takes would shade the lowest slots only, and the side of the mesh behind them only on release (#184).
    std::rotate(pendingShade_.begin(), std::lower_bound(pendingShade_.begin(), pendingShade_.end(), shadeCursor_), pendingShade_.end());
    size_t used = 0;
    for (; used < pendingShade_.size() && seeds.size() < limit; ++used) {
        const uint32_t s = pendingShade_[used];
        if (page_[s]) seeds.push_back(r.order_[s]);
    }
    if (used) shadeCursor_ = pendingShade_[used - 1] + 1;
    pendingShade_.erase(pendingShade_.begin(), pendingShade_.begin() + std::ptrdiff_t(used));
    if (seeds.empty()) return;
    std::sort(seeds.begin(), seeds.end());
    seeds.erase(std::unique(seeds.begin(), seeds.end()), seeds.end());
    const auto started = std::chrono::steady_clock::now();
    std::vector<uint32_t>& index = r.stageIndex_;
    if (index.size() != r.baseTriangles_) index.assign(r.baseTriangles_, 0);
    for (size_t i = 0; i < seeds.size(); ++i) index[seeds[i]] = uint32_t(i);
    shadeStaging_.resize(seeds.size() * S);
    // (each seed alone and exact: its neighbours' edge sums are read, not written)
    r.shader_.shade(mesh, seeds, false, written_, [&](uint32_t t) -> uint32_t* { return shadeStaging_.data() + size_t(index[t]) * S; },
                    {}, false, nullptr);
    r.upload_.stage(shadeStaging_.data(), seeds.size() * S * 4);
    for (size_t i = 0; i < seeds.size(); ++i) {
        const uint32_t s = r.slotOf_[seeds[i]];
        uint32_t& e = page_[s];
        if (!e) continue;
        r.upload_.copy(poolShade_, i * S * 4, size_t((e & ~kShaded) - 1) * S * 4, S * 4);
        if (!(e & kShaded)) e |= kShaded, ++shadedPages_, pageTableDirty_ = true;
    }
    BufferUpload::done();
    const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - started).count() / double(seeds.size() * S);
    shadeNsPerPoint_ = shadeNsPerPoint_ > 0.0 ? shadeNsPerPoint_ * 0.7 + ns * 0.3 : ns;
}

void MicroFocus::syncAll(Mesh& mesh, MicroRenderer& r) {
    const MicroMesh& mm = *mesh.micro;
    Lap lap{splitMs_};
    for (uint32_t s = 0; s < slots_; ++s) freePage(s);
    pendingShade_.clear();
    pendingCoarse_.clear(); // (shadeCoarse(all) below shades every tile)
    coarseHead_ = 0;
    sweepNext_ = sweepEnd_ = 0;
    coarseQueued_.assign(r.baseTriangles_, 0);
    buildCoarse(mesh, mm, {});
    std::vector<uint32_t> none;
    writeRecords(mm, r, none, true);
    shadeCoarse(mesh, r, none, true);
    pageStore_.create(TexelStore::Format::R32UI, slots_, page_.data(), r.wide(), GL_DYNAMIC_DRAW);
    bytes_ += size_t(slots_) * 4;
    pageTableDirty_ = false;
    lap(kSyncAll);
}

void MicroFocus::resendHeights(const Mesh& mesh, MicroRenderer& r) {
    const MicroMesh& mm = *mesh.micro;
    Lap lap{splitMs_};
    buildCoarse(mesh, mm, {});
    std::vector<uint32_t> held;
    writeRecords(mm, r, held, true);
    for (uint32_t s = 0; s < slots_; ++s)
        if (page_[s]) held.push_back(s);
    sendPages(mm, r, held);
    pendingShade_.insert(pendingShade_.end(), held.begin(), held.end());
    sweepNext_ = 0, sweepEnd_ = r.baseTriangles_;
    lap(kSyncAll);
}

void MicroFocus::sync(Mesh& mesh, MicroRenderer& r, const std::vector<uint32_t>& dirty, const std::vector<uint64_t>& moved,
                      bool stroking, bool viewMoving) {
    const MicroMesh& mm = *mesh.micro;
    const FocusDetail* fd = mm.focus.get();
    levelsChanged_ = false; // (the want list below reads what the last draw drew)
    ++frame_;
    Lap lap{splitMs_};
    // Changed heights: records, coarse grids, pages (a tile that went coarse gives its page back).
    std::vector<uint32_t> slots, resend, reshade(dirty);
    slots.reserve(dirty.size());
    for (uint32_t t : dirty) {
        const uint32_t s = r.slotOf_[t];
        slots.push_back(s);
        if (!page_[s]) continue;
        if (fd && !fd->tileLevel(t)) freePage(s);
        else resend.push_back(s), pendingShade_.push_back(s);
    }
    std::sort(slots.begin(), slots.end());
    slots.erase(std::unique(slots.begin(), slots.end()), slots.end());
    if (!dirty.empty()) {
        buildCoarse(mesh, mm, dirty);
        lap(kCoarse);
        writeRecords(mm, r, slots, false);
        lap(kRecords);
    }
    // A moved base: the shading of the tiles around it.
    for (size_t w = 0; w < moved.size(); ++w)
        for (uint64_t bits = moved[w]; bits; bits &= bits - 1) {
            const uint32_t t = uint32_t(w * 64 + size_t(std::countr_zero(bits)));
            reshade.push_back(t);
            if (page_[r.slotOf_[t]]) pendingShade_.push_back(r.slotOf_[t]);
        }
    if (!reshade.empty()) {
        std::sort(reshade.begin(), reshade.end());
        reshade.erase(std::unique(reshade.begin(), reshade.end()), reshade.end());
        if (coarseQueued_.size() != r.baseTriangles_) coarseQueued_.assign(r.baseTriangles_, 0);
        for (uint32_t t : reshade)
            if (!coarseQueued_[t]) coarseQueued_[t] = 1, pendingCoarse_.push_back(t);
    }
    if (queuedCoarse() > 0) {
        // While a stroke runs: strokeShadeMs of it, then the rest on the next frames (a tile under the brush is queued once
        // however many frames change it). While the view or the camera moves a tenth of that (an orbit after a stroke isn't slowed by the
        // catch-up), at rest the same as while stroking (a rest frame stays near 30 fps), until the queue is empty.
        const TierConfig& tc = currentTierConfig();
        const size_t take = shadeAll ? queuedCoarse() // (not the sweep: that runs only once the queue is empty)
                                     : coarseShadeBatch(queuedCoarse(), tc.strokeShadeMs * (stroking ? 1.0 : viewMoving || cameraMoving ? 0.1 : 1.0), coarseNsPerTile_);
        std::vector<uint32_t> batch(pendingCoarse_.begin() + std::ptrdiff_t(coarseHead_), pendingCoarse_.begin() + std::ptrdiff_t(coarseHead_ + take));
        for (uint32_t t : batch) coarseQueued_[t] = 0;
        coarseHead_ += take;
        if (coarseHead_ == pendingCoarse_.size()) pendingCoarse_.clear(), coarseHead_ = 0;
        else if (coarseHead_ > (size_t(1) << 16) && coarseHead_ * 2 > pendingCoarse_.size())
            pendingCoarse_.erase(pendingCoarse_.begin(), pendingCoarse_.begin() + std::ptrdiff_t(coarseHead_)), coarseHead_ = 0;
        const auto started = std::chrono::steady_clock::now();
        shadeCoarse(mesh, r, batch, false);
        const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - started).count() / double(batch.size());
        coarseNsPerTile_ = coarseNsPerTile_ > 0.0 ? coarseNsPerTile_ * 0.7 + ns * 0.3 : ns;
    } else if (sweepNext_ < sweepEnd_ && !stroking) {
        const TierConfig& tc = currentTierConfig();
        const size_t take = coarseShadeBatch(sweepEnd_ - sweepNext_, std::max(tc.strokeShadeMs, 2.0) * (viewMoving || cameraMoving ? 0.1 : 1.0), coarseNsPerTile_);
        std::vector<uint32_t> batch(take);
        for (size_t i = 0; i < take; ++i) batch[i] = sweepNext_ + uint32_t(i);
        sweepNext_ += uint32_t(take);
        if (sweepNext_ >= sweepEnd_) sweepNext_ = sweepEnd_ = 0;
        const auto started = std::chrono::steady_clock::now();
        shadeCoarse(mesh, r, batch, false);
        const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - started).count() / double(batch.size());
        coarseNsPerTile_ = coarseNsPerTile_ > 0.0 ? coarseNsPerTile_ * 0.7 + ns * 0.3 : ns;
    }
    lap(kShadeCoarse);
    // Pages for the full tiles the last draw drew finer than the coarse level, finest first.
    std::vector<std::pair<int, uint32_t>> want; // (-level, slot)
    const uint32_t shift = r.clusterShift_, size = 1u << shift;
    for (uint32_t c = 0; c < uint32_t(drawn_.size()); ++c) {
        const int l = drawn_[c];
        if (l <= FocusDetail::kCoarseLevel) continue;
        const uint32_t s0 = c << shift, s1 = std::min(slots_, s0 + size);
        for (uint32_t s = s0; s < s1; ++s) {
            if (page_[s]) {
                lastUse_[(page_[s] & ~kShaded) - 1] = frame_;
                continue;
            }
            if (fd && fd->tileLevel(r.order_[s])) want.push_back({-l, s});
        }
    }
    const TierConfig& tier = currentTierConfig();
    if (want.empty()) unpaged_ = 0;
    if (!want.empty()) {
        std::sort(want.begin(), want.end());
        // At most the frame's upload budget of heights (their shading follows within the shading budget).
        const size_t perFrame = std::max<size_t>(16, size_t(tier.uploadBudgetMBPerFrame << 20) / (size_t(samples_) * 2));
        const size_t leftover = want.size() > perFrame ? want.size() - perFrame : 0; // (for the next frames)
        if (leftover) want.resize(perFrame);
        std::vector<uint32_t> victims; // pages not drawn fine this frame, least recently first
        bool victimsListed = false;
        std::vector<uint32_t> added;
        for (const auto& [negLevel, s] : want) {
            if (free_.empty() && !growPool(capacity_ + uint32_t(want.size() - added.size()))) {
                if (!victimsListed) {
                    for (uint32_t p = 0; p < capacity_; ++p)
                        if (owner_[p] != ~0u && lastUse_[p] != frame_) victims.push_back(p);
                    std::sort(victims.begin(), victims.end(), [&](uint32_t a, uint32_t b) { return lastUse_[a] > lastUse_[b]; });
                    victimsListed = true;
                }
                if (victims.empty()) break; // (the pool is full of tiles drawn fine now: the rest stay coarse)
                freePage(owner_[victims.back()]);
                victims.pop_back();
            }
            const uint32_t p = free_.back();
            free_.pop_back();
            owner_[p] = s;
            lastUse_[p] = frame_;
            page_[s] = p + 1;
            ++pageCount_;
            pageTableDirty_ = true;
            added.push_back(s);
            pendingShade_.push_back(s);
        }
        unpaged_ = added.size() == want.size() ? leftover : 0; // (a full pool leaves the rest coarse: no frames for them)
        resend.insert(resend.end(), added.begin(), added.end());
    }
    sendPages(mm, r, resend);
    lap(kPages);
    // Full shading for the pages, within the frame's shading budget (as MicroRenderer's).
    if (!pendingShade_.empty()) {
        std::sort(pendingShade_.begin(), pendingShade_.end());
        pendingShade_.erase(std::unique(pendingShade_.begin(), pendingShade_.end()), pendingShade_.end());
        // (finest-drawn first would be better; slot order keeps runs together)
        shadePages(mesh, r, microShadeBudget(tier, stroking, viewMoving, shadeNsPerPoint_, size_t(samples_) * 3));
    }
    lap(kShadePages);
    if (pageTableDirty_) {
        if (pageStore_.wide()) pageStore_.writeWide(0, slots_, page_.data());
        else r.upload_.write(pageStore_.buffer(), 0, page_.data(), size_t(slots_) * 4);
        pageTableDirty_ = false;
    }
    lap(kPages);
}

void MicroFocus::bind(GLuint program, const std::vector<uint8_t>& visible, const std::vector<int8_t>& levels) {
    drawn_.resize(levels.size());
    for (size_t c = 0; c < levels.size(); ++c) {
        const int8_t l = visible[c] ? levels[c] : int8_t(-1);
        if (l != drawn_[c] && l > int8_t(FocusDetail::kCoarseLevel)) levelsChanged_ = true; // (pages for them: the next sync)
        drawn_[c] = l;
    }
    if (u_.program != program) {
        u_.program = program;
        auto at = [&](const char* name) { return gl::GetUniformLocation(program, name); };
        u_.cn = at("uCN");
        u_.cSamples = at("uCSamples");
        u_.record = at("uRecord");
        u_.shift = at("uCShift");
    }
    gl::Uniform1i(u_.cn, int(coarseN_));
    gl::Uniform1i(u_.cSamples, int(coarseSamples_));
    gl::Uniform1i(u_.record, int(record_));
    gl::Uniform1i(u_.shift, int(shift_));
    gl::ActiveTexture(kUnitPages);
    pageStore_.bind();
    gl::ActiveTexture(kUnitPool);
    gl::BindTexture(GL_TEXTURE_BUFFER, poolDispTex_);
    gl::ActiveTexture(kUnitPoolShade);
    gl::BindTexture(GL_TEXTURE_BUFFER, poolShadeTex_);
}

void MicroFocus::unbind() {
    for (GLenum unit : {kUnitPages, kUnitPool, kUnitPoolShade}) {
        gl::ActiveTexture(unit);
        gl::BindTexture(GL_TEXTURE_BUFFER, 0);
        gl::BindTexture(GL_TEXTURE_2D, 0);
    }
}

} // namespace sf
