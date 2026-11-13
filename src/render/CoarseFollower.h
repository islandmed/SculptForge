// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// While a stroke edits a Part drawn on its coarse levels, the cells its vertices are in follow it (so the coarse underlay of the
// edited clusters stays on the surface they edit). The follower does that on a thread of its own, from the packed vertices
// each stroke batch uploads, never from the live mesh (the stroke's thread writes that): it keeps the vertices' positions as the
// GPU has them, works out the changed cells' means and their parents', packs them, and hands the result over for the GL thread
// to copy and send. Nothing is added to the stroke's thread; the GL thread copies and sends what it is given.
#pragma once

#include "render/Clusters.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace sf {

class CoarseFollower {
public:
    struct Batch {
        struct Range {
            uint32_t begin, end; // GPU slots
            size_t offset;       // into data
        };
        std::vector<Range> ranges;
        std::vector<PackedVertex> data;
    };
    struct Result {
        struct Run {
            uint32_t begin, end; // cells
            size_t offset;       // into packed
        };
        std::vector<uint32_t> cells;       // ascending (level 0 first, then up)
        std::vector<glm::vec3> positions;  // of cells
        std::vector<Run> runs;             // empty: send the whole array
        std::vector<PackedVertex> packed;
        void clear() { cells.clear(), positions.clear(), runs.clear(), packed.clear(); }
    };

    CoarseFollower();
    ~CoarseFollower();
    CoarseFollower(const CoarseFollower&) = delete;
    CoarseFollower& operator=(const CoarseFollower&) = delete;

    // Main thread, the mesh not changing: start from the mesh as it is (the levels' cells are as `levels` has them).
    void init(const CoarseLevels& levels, const Mesh& mesh, const UploadOrder& order, const PositionGrid& grid);
    bool ready() const { return ready_; }
    void reset(); // waits for the thread to be idle, drops the state
    // A stroke batch's packed vertices (swapped in: `batch` comes back as an empty spare).
    void push(Batch& batch);
    // Main thread: the latest result, if there is one (swapped into `out`).
    bool take(Result& out);
    // Returns when the thread has worked out what was pushed, or a result waits (the thread goes on once it is taken):
    // `do { drain(); if (take(r)) use(r); } while (!idle());` has everything.
    void drain();
    bool idle();

private:
    void loop();
    void process(std::vector<Batch>& batches);

    const CoarseLevels* levels_ = nullptr;
    const UploadOrder* order_ = nullptr;
    PositionGrid grid_;
    std::vector<uint64_t> mirror_;      // per mesh vertex: the position as last uploaded (pos[0] | pos[1] << 32)
    std::vector<glm::vec3> cellPos_;    // per cell, as the worker last worked it out
    std::vector<uint8_t> flag_;         // per level-0 cell (worker)
    bool ready_ = false;                // (main thread's view)

    std::mutex mutex_;
    std::condition_variable wake_, idle_;
    std::deque<Batch> queue_;
    std::vector<Batch> spare_;
    Result out_;
    bool hasOut_ = false, busy_ = false, quit_ = false;
    std::thread thread_;
};

} // namespace sf
