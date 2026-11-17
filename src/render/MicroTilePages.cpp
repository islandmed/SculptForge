// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/MicroTilePages.h"

#include "core/MicroTiles.h"
#include "core/ThreadPool.h"
#include "core/TierConfig.h"
#include "render/MicroRenderer.h"

#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace sf {

namespace {
constexpr GLenum kUnitTilePages = 0x84CD, kUnitTilePool = 0x84CE, kUnitTileSlope = 0x84CF; // GL_TEXTURE13-15 (the plain program's; FOCUS uses them for its own)
constexpr uint32_t kChunkTexels = 1u << 26;                       // (MicroRenderer's: texture buffer limits)
} // namespace

MicroTilePages::~MicroTilePages() { release(); }

void MicroTilePages::release() {
    for (GLuint* b : {&pool_, &slope_, &table_})
        if (*b) gl::DeleteBuffers(1, b), *b = 0;
    for (GLuint* t : {&poolTex_, &slopeTex_, &tableTex_})
        if (*t) gl::DeleteTextures(1, t), *t = 0;
    capacity_ = 0;
    page_.clear(), owner_.clear(), lastUse_.clear(), free_.clear(), drawn_.clear();
    pageCount_ = starved_ = bytes_ = 0;
    tableLo_ = ~0u, tableHi_ = 0;
    level_ = -1;
}

void MicroTilePages::setup(uint32_t slots, int pageLevel) {
    release();
    level_ = pageLevel;
    m_ = 1u << pageLevel;
    samples_ = (m_ + 1) * (m_ + 2) / 2;
    slots_ = slots;
    page_.assign(slots, 0);
    if (const char* e = std::getenv("SF_MICRO_TILE_PAGES"); e && *e) budgetPages = uint32_t(std::max(1L, std::atol(e)));
    gl::GenBuffers(1, &table_);
    gl::GenTextures(1, &tableTex_);
    gl::BindBuffer(GL_TEXTURE_BUFFER, table_);
    gl::BufferData(GL_TEXTURE_BUFFER, GLsizeiptr(size_t(slots) * 4), page_.data(), GL_DYNAMIC_DRAW);
    gl::BindTexture(GL_TEXTURE_BUFFER, tableTex_);
    gl::TexBuffer(GL_TEXTURE_BUFFER, GL_R32UI, table_);
    gl::BindBuffer(GL_TEXTURE_BUFFER, 0);
    gl::BindTexture(GL_TEXTURE_BUFFER, 0);
    bytes_ = size_t(slots) * 4;
}

bool MicroTilePages::growPool(uint32_t want) {
    const size_t S = samples_;
    const uint32_t maxPages = uint32_t(std::min<size_t>(gl::bufferChunkTexels(kChunkTexels) / S, budgetPages));
    if (capacity_ >= maxPages) return false;
    const uint32_t cap = std::min(maxPages, std::max({want, capacity_ * 2, 64u}));
    // The deltas (R16I, 2 bytes a point) and the slopes (RG16F, 4): the pages held move along.
    auto grow = [&](GLuint& buffer, GLuint& tex, GLenum format, size_t bytesPerPoint) {
        GLuint b = 0;
        gl::GenBuffers(1, &b);
        gl::BindBuffer(GL_COPY_WRITE_BUFFER, b);
        gl::BufferData(GL_COPY_WRITE_BUFFER, GLsizeiptr(size_t(cap) * S * bytesPerPoint), nullptr, GL_DYNAMIC_DRAW);
        if (capacity_) {
            gl::BindBuffer(GL_COPY_READ_BUFFER, buffer);
            gl::CopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, GLsizeiptr(size_t(capacity_) * S * bytesPerPoint));
            gl::BindBuffer(GL_COPY_READ_BUFFER, 0);
            gl::DeleteBuffers(1, &buffer);
        }
        gl::BindBuffer(GL_COPY_WRITE_BUFFER, 0);
        buffer = b;
        if (!tex) gl::GenTextures(1, &tex);
        gl::BindTexture(GL_TEXTURE_BUFFER, tex);
        gl::TexBuffer(GL_TEXTURE_BUFFER, format, buffer);
        gl::BindTexture(GL_TEXTURE_BUFFER, 0);
    };
    grow(pool_, poolTex_, GL_R16I, 2);
    grow(slope_, slopeTex_, GL_RG16F, 4);
    bytes_ -= size_t(capacity_) * pageBytes();
    for (uint32_t p = cap; p > capacity_; --p) free_.push_back(p - 1); // (lowest first off the back)
    owner_.resize(cap, ~0u);
    lastUse_.resize(cap, 0);
    bytes_ += size_t(cap) * pageBytes();
    capacity_ = cap;
    return true;
}

void MicroTilePages::markTable(uint32_t slot) { tableLo_ = std::min(tableLo_, slot), tableHi_ = std::max(tableHi_, slot + 1); }

void MicroTilePages::freePage(uint32_t slot) {
    const uint32_t e = page_[slot];
    if (!e) return;
    owner_[e - 1] = ~0u;
    free_.push_back(e - 1);
    page_[slot] = 0;
    --pageCount_;
    markTable(slot);
}

void MicroTilePages::sync(const Mesh& mesh, MicroRenderer& r, const std::vector<uint32_t>& dirty, bool all) {
    const auto started = std::chrono::steady_clock::now();
    lastFilled = 0;
    starved_ = 0;
    wantSync_ = capped_ = false;
    ++frame_;
    const MicroTiles* tiles = mesh.micro ? mesh.micro->tiles.get() : nullptr;
    tiled_ = tiles && tiles->alive() > 0;
    std::vector<uint32_t> fill; // slots whose page to (re)fill
    if (!tiles || tiles->alive() == 0) {
        if (pageCount_)
            for (uint32_t s = 0; s < slots_; ++s) freePage(s);
    } else {
        // 1. Out of date: refill, or give the page back when the triangle has no tiles left.
        auto check = [&](uint32_t s) {
            if (!page_[s]) return;
            if (tiles->hasTiles(r.order_[s])) fill.push_back(s);
            else freePage(s);
        };
        if (all) {
            if (pageCount_)
                for (uint32_t s = 0; s < slots_; ++s) check(s);
        } else {
            for (uint32_t t : dirty) check(r.slotOf_[t]);
        }
        // 2. Pages for the tiled slots the last draw drew, finest first, within the budget and the frame's upload.
        std::vector<std::pair<int, uint32_t>> want; // (-level, slot)
        const uint32_t shift = r.clusterShift_, size = 1u << shift;
        for (uint32_t c = 0; c < uint32_t(drawn_.size()); ++c) {
            const int l = drawn_[c];
            if (l < 0) continue;
            const uint32_t s0 = c << shift, s1 = std::min(slots_, s0 + size);
            for (uint32_t s = s0; s < s1; ++s) {
                if (page_[s]) lastUse_[page_[s] - 1] = frame_;
                else if (tiles->hasTiles(r.order_[s])) want.push_back({-l, s});
            }
        }
        if (!want.empty()) {
            std::sort(want.begin(), want.end());
            const size_t perFrame = std::max<size_t>(64, size_t(currentTierConfig().uploadBudgetMBPerFrame << 20) / pageBytes());
            if (want.size() > perFrame) starved_ += want.size() - perFrame, want.resize(perFrame), capped_ = true;
            std::vector<uint32_t> victims; // pages not drawn this frame, least recently drawn last
            bool listed = false;
            for (size_t k = 0; k < want.size(); ++k) {
                const uint32_t s = want[k].second;
                if (free_.empty() && !growPool(capacity_ + uint32_t(want.size() - k))) {
                    if (!listed) {
                        for (uint32_t p = 0; p < capacity_; ++p)
                            if (owner_[p] != ~0u && lastUse_[p] != frame_) victims.push_back(p);
                        std::sort(victims.begin(), victims.end(), [&](uint32_t a, uint32_t b) { return lastUse_[a] > lastUse_[b]; });
                        listed = true;
                    }
                    if (victims.empty()) { // (the pool is full of pages drawn this frame: the rest wait)
                        starved_ += want.size() - k;
                        break;
                    }
                    freePage(owner_[victims.back()]);
                    victims.pop_back();
                }
                const uint32_t p = free_.back();
                free_.pop_back();
                owner_[p] = s;
                lastUse_[p] = frame_;
                page_[s] = p + 1;
                ++pageCount_;
                markTable(s);
                fill.push_back(s);
            }
        }
    }
    // 3. Fill (4+ threads, parked tiles unpacked once per page) with the slopes, and send, in runs of consecutive pages.
    fill.erase(std::remove_if(fill.begin(), fill.end(), [&](uint32_t s) { return !page_[s]; }), fill.end()); // (refills evicted since)
    if (!fill.empty()) {
        std::sort(fill.begin(), fill.end(), [&](uint32_t a, uint32_t b) { return page_[a] < page_[b]; });
        fill.erase(std::unique(fill.begin(), fill.end()), fill.end());
        const size_t S = samples_;
        staging_.resize(fill.size() * S);
        slopeStaging_.resize(fill.size() * S * 2);
        poolFor(fill.size(), [&](size_t b, size_t e) {
            thread_local std::vector<float> slopes;
            slopes.resize(S * 2);
            for (size_t k = b; k < e; ++k) {
                const uint32_t tri = r.order_[fill[k]];
                int16_t* values = staging_.data() + k * S;
                tiles->pageValues(tri, m_, values);
                microTileSlopes(mesh, tri, m_, values, slopes.data());
                for (size_t q = 0; q < S * 2; ++q) slopeStaging_[k * S * 2 + q] = glm::packHalf1x16(slopes[q]);
            }
        }, 4);
        auto send = [&](GLuint buffer, const void* data, size_t bytesPerPoint) {
            gl::BindBuffer(GL_TEXTURE_BUFFER, buffer);
            for (size_t i = 0; i < fill.size();) {
                size_t j = i + 1;
                while (j < fill.size() && page_[fill[j]] == page_[fill[j - 1]] + 1) ++j;
                gl::BufferSubData(GL_TEXTURE_BUFFER, GLintptr(size_t(page_[fill[i]] - 1) * S * bytesPerPoint), GLsizeiptr((j - i) * S * bytesPerPoint),
                                  static_cast<const uint8_t*>(data) + i * S * bytesPerPoint);
                i = j;
            }
            gl::BindBuffer(GL_TEXTURE_BUFFER, 0);
        };
        send(pool_, staging_.data(), 2);
        send(slope_, slopeStaging_.data(), 4);
        lastFilled = fill.size();
    }
    if (tableLo_ < tableHi_) {
        gl::BindBuffer(GL_TEXTURE_BUFFER, table_);
        gl::BufferSubData(GL_TEXTURE_BUFFER, GLintptr(size_t(tableLo_) * 4), GLsizeiptr(size_t(tableHi_ - tableLo_) * 4), page_.data() + tableLo_);
        gl::BindBuffer(GL_TEXTURE_BUFFER, 0);
        tableLo_ = ~0u, tableHi_ = 0;
    }
    lastFillMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    if (static const bool dbg = std::getenv("SF_MICRO_TILES_DEBUG") != nullptr; dbg && (lastFilled || starved_))
        std::fprintf(stderr, "tile pages: level %d, %zu alive (%zu KB GPU), filled %zu in %.2f ms, starved %zu\n", level_, pageCount_,
                     bytes_ >> 10, lastFilled, lastFillMs, starved_);
}

void MicroTilePages::bind(GLuint program, const std::vector<uint8_t>& visible, const std::vector<int8_t>& levels, int maxDraw) {
    if (drawn_.size() != levels.size()) drawn_.assign(levels.size(), int8_t(-1)), wantSync_ = tiled_;
    for (size_t c = 0; c < levels.size(); ++c) {
        const int8_t l = visible[c] ? levels[c] : int8_t(-1);
        wantSync_ |= tiled_ && l >= 0 && drawn_[c] < 0; // (newly drawn: its tiled slots want pages; a level change doesn't)
        drawn_[c] = l;
    }
    if (u_.program != program) {
        u_.program = program;
        u_.level = gl::GetUniformLocation(program, "uTilePageLevel");
        u_.samples = gl::GetUniformLocation(program, "uTileSamples");
    }
    const bool on = pageCount_ > 0 && pool_ && maxDraw <= level_;
    gl::Uniform1i(u_.level, on ? level_ : -1);
    gl::Uniform1i(u_.samples, int(samples_));
    if (!on) return;
    gl::ActiveTexture(kUnitTilePages);
    gl::BindTexture(GL_TEXTURE_BUFFER, tableTex_);
    gl::ActiveTexture(kUnitTilePool);
    gl::BindTexture(GL_TEXTURE_BUFFER, poolTex_);
    gl::ActiveTexture(kUnitTileSlope);
    gl::BindTexture(GL_TEXTURE_BUFFER, slopeTex_);
}

void MicroTilePages::unbind() {
    for (GLenum unit : {kUnitTilePages, kUnitTilePool, kUnitTileSlope}) {
        gl::ActiveTexture(unit);
        gl::BindTexture(GL_TEXTURE_BUFFER, 0);
    }
}

} // namespace sf
