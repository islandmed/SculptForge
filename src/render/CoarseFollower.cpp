// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/CoarseFollower.h"

#include "core/ThreadPool.h"

#include <algorithm>

namespace sf {

namespace {
inline glm::vec3 unpackMirror(const PositionGrid& grid, uint64_t q) {
    PackedVertex v{};
    v.pos[0] = uint32_t(q);
    v.pos[1] = uint32_t(q >> 32);
    return unpackPosition(grid, v);
}
} // namespace

CoarseFollower::CoarseFollower() = default;

CoarseFollower::~CoarseFollower() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        quit_ = true;
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void CoarseFollower::reset() {
    drain();
    ready_ = false;
    std::lock_guard<std::mutex> lock(mutex_);
    levels_ = nullptr;
    order_ = nullptr;
    hasOut_ = false;
    out_.clear();
    queue_.clear();
    std::vector<uint64_t>().swap(mirror_);
    std::vector<glm::vec3>().swap(cellPos_);
    std::vector<uint8_t>().swap(flag_);
}

void CoarseFollower::init(const CoarseLevels& L, const Mesh& mesh, const UploadOrder& order, const PositionGrid& grid) {
    reset();
    const size_t nv = mesh.vertexCount();
    if (L.levels.empty() || L.vertexCell.size() != nv || nv == 0) return;
    levels_ = &L;
    order_ = &order;
    grid_ = grid;
    mirror_.resize(nv);
    poolFor(nv, [&](size_t v0, size_t v1) {
        PackedVertex pv{};
        for (size_t v = v0; v < v1; ++v) {
            packVertex(grid_, mesh.positions[v], glm::vec3(0, 0, 1), 0.0f, pv);
            mirror_[v] = uint64_t(pv.pos[0]) | uint64_t(pv.pos[1]) << 32;
        }
    });
    cellPos_ = L.positions;
    flag_.assign(L.levels[0].keys.size(), 0);
    ready_ = true;
    if (!thread_.joinable()) thread_ = std::thread([this] { loop(); });
}

void CoarseFollower::push(Batch& batch) {
    if (!ready_) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(std::move(batch));
        batch = Batch();
        if (!spare_.empty()) batch = std::move(spare_.back()), spare_.pop_back();
    }
    wake_.notify_one();
}

bool CoarseFollower::take(Result& out) {
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock() || !hasOut_) return false;
    std::swap(out, out_);
    out_.clear();
    hasOut_ = false;
    lock.unlock();
    wake_.notify_one(); // (more may be waiting for the room)
    return true;
}

void CoarseFollower::drain() {
    std::unique_lock<std::mutex> lock(mutex_);
    // (A result nobody has taken keeps the thread from working on: the caller takes it, then it goes on. Here: until queued
    // work is done or a result waits.)
    idle_.wait(lock, [this] { return (queue_.empty() && !busy_) || hasOut_ || !thread_.joinable(); });
}

bool CoarseFollower::idle() {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.empty() && !busy_ && !hasOut_;
}

void CoarseFollower::loop() {
    for (;;) {
        std::vector<Batch> batches;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            busy_ = false;
            idle_.notify_all();
            wake_.wait(lock, [this] { return quit_ || (!queue_.empty() && !hasOut_); });
            if (quit_) return;
            while (!queue_.empty()) batches.push_back(std::move(queue_.front())), queue_.pop_front();
            busy_ = true;
        }
        process(batches);
        std::lock_guard<std::mutex> lock(mutex_);
        for (Batch& b : batches) {
            b.ranges.clear();
            b.data.clear();
            if (spare_.size() < 4) spare_.push_back(std::move(b));
        }
    }
}

void CoarseFollower::process(std::vector<Batch>& batches) {
    const CoarseLevels& L = *levels_;
    const size_t nv = mirror_.size();
    const uint32_t base0 = L.levels[0].base;
    const std::span<const uint32_t> fromGpu = order_->fromGpu();
    std::vector<uint32_t> cells;
    for (const Batch& b : batches) {
        for (const Batch::Range& range : b.ranges) {
            for (uint32_t i = range.begin; i < range.end; ++i) {
                const size_t at = range.offset + (i - range.begin);
                const uint32_t v = fromGpu[i];
                if (v >= nv || order_->toGpu(v) != i) continue; // (padding, and a vertex's later slots)
                const uint64_t q = uint64_t(b.data[at].pos[0]) | uint64_t(b.data[at].pos[1]) << 32;
                if (mirror_[v] == q) continue;
                mirror_[v] = q;
                const uint32_t k = L.vertexCell[v] - base0;
                if (!flag_[k]) flag_[k] = 1, cells.push_back(k);
            }
        }
    }
    if (cells.empty()) return;
    std::sort(cells.begin(), cells.end());
    Result r;
    r.cells.reserve(cells.size() * 2);
    // Level 0: the mean of the cell's vertices as the GPU has them.
    for (uint32_t k : cells) {
        flag_[k] = 0;
        glm::dvec3 sum(0.0);
        for (uint32_t m = L.memberFirst[k]; m < L.memberFirst[k + 1]; ++m) sum += glm::dvec3(unpackMirror(grid_, mirror_[L.members[m]]));
        const uint32_t w = L.memberFirst[k + 1] - L.memberFirst[k];
        if (w) cellPos_[base0 + k] = glm::vec3(sum / double(w));
        r.cells.push_back(base0 + k);
    }
    for (uint32_t& c : cells) c += base0;
    // Each level above, from its children (the unchanged ones as the worker last had them).
    for (size_t level = 1; level < L.levels.size() && !cells.empty(); ++level) {
        std::vector<uint32_t> up;
        up.reserve(cells.size());
        for (uint32_t k : cells)
            if (L.parent[k] != ~0u) up.push_back(L.parent[k]);
        std::sort(up.begin(), up.end());
        up.erase(std::unique(up.begin(), up.end()), up.end());
        for (uint32_t p : up) {
            glm::dvec3 sum(0.0);
            for (uint32_t j = L.childFirst[p]; j < L.childFirst[p + 1]; ++j) {
                const uint32_t ch = L.children[j];
                sum += glm::dvec3(cellPos_[ch]) * double(L.weight[ch]);
            }
            if (L.weight[p]) cellPos_[p] = glm::vec3(sum / double(L.weight[p]));
        }
        r.cells.insert(r.cells.end(), up.begin(), up.end());
        cells = std::move(up);
    }
    r.positions.resize(r.cells.size());
    for (size_t i = 0; i < r.cells.size(); ++i) r.positions[i] = cellPos_[r.cells[i]];
    // Packed as runs of the coarse vertex buffer (the cells between two close ones as they are).
    constexpr uint32_t kGap = 64;
    size_t covered = 0;
    for (uint32_t c : r.cells) {
        if (!r.runs.empty() && c < r.runs.back().end + kGap) r.runs.back().end = c + 1;
        else r.runs.push_back({c, c + 1, 0});
    }
    for (auto& run : r.runs) run.offset = covered, covered += run.end - run.begin;
    if (r.runs.size() > 1024 || covered * 2 > cellPos_.size()) r.runs.clear();
    else {
        r.packed.resize(covered);
        for (const auto& run : r.runs)
            for (uint32_t i = run.begin; i < run.end; ++i)
                packVertex(grid_, cellPos_[i], L.normals[i], 0.0f, r.packed[run.offset + (i - run.begin)]);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    out_ = std::move(r);
    hasOut_ = true;
}

} // namespace sf
