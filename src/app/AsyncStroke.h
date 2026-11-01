// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The solver thread of a sculpt stroke (App::StrokeSolver, see App.h "Strokes on heavy meshes").
#pragma once

#include "app/App.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace sf {

struct App::StrokeSolver {
    explicit StrokeSolver(App& app);
    ~StrokeSolver();
    StrokeSolver(const StrokeSolver&) = delete;
    StrokeSolver& operator=(const StrokeSolver&) = delete;

    // Main thread: hands over `batch` (and view); the thread places the dabs, then packs the upload.
    void start();
    bool busy() const { return busy_.load(std::memory_order_acquire); }
    void wait(); // until the batch in flight is done

    App& app;
    // While busy, owned by the thread (the main thread only reads batch, to draw the preview).
    std::vector<PathDab> batch;
    DabView view;
    std::vector<glm::vec2> probe; // --measure-stroke: where the batch's dabs went
    double dabSeconds = 0.0;      // the batch's mean cost per dab
    double prepSeconds = 0.0;     // what packing its upload cost
    uint32_t uploadId = 0;        // the active SubTool's renderer slot
    bool prepare = true;          // pack the upload on the thread
    bool finalize = false;        // before the batch: Sculptor::flushDraft (a draft stroke's end)
    Renderer::StrokeUpload upload;
    bool uploadReady = false; // packed (else the main thread's syncScene sends the change)
    bool done = false;        // a finished batch waits for its commit

private:
    void loop();
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable wake_, idle_;
    bool pending_ = false, quit_ = false;
    std::atomic<bool> busy_{false};
};

} // namespace sf
