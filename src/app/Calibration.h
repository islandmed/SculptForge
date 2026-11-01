// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The OpenGL half of hardware detection and calibration (core/HardwareProfile.h has the rest):
//  - probeGl: GL_VENDOR / GL_RENDERER / GL_VERSION, GL_MAX_TEXTURE_SIZE, timer queries, and video memory through
//    GL_NVX_gpu_memory_info, GL_ATI_meminfo, GLX_MESA_query_renderer (X11), amdgpu sysfs (Linux), DXGI (Windows)
//    or CGL (macOS), whichever answers first;
//  - Calibrator: the benchmarks in short steps (one per frame in the app, so the window stays responsive and the
//    user can skip): triangle throughput (instanced dense grids into an offscreen target, timed with GL timer
//    queries where available, else glFinish), buffer upload bandwidth (glBufferSubData), memory bandwidth and the
//    cost of a brush dab on a generated mesh. Medians of several runs; the work per run is fixed.
// Everything restores the GL state it touched, so it can run between the app's frames. No network.
#pragma once

#include "core/HardwareProfile.h"

#include <string>

namespace sf {

// Fills the GPU fields of `info` from the current OpenGL context (and the machine's, through probeSystem).
void probeHardware(HardwareInfo& info);

class Calibrator {
public:
    explicit Calibrator(bool quick = false) : quick_(quick) {}
    ~Calibrator();
    // Runs the next step (each well under a second); false once finished.
    bool step();
    bool done() const { return stage_ >= kStages; }
    float progress() const { return float(stage_) / float(kStages); }
    const char* stageName() const; // what the next step measures, for the overlay
    // Marks calibration skipped: the profile is mapped from the probe alone.
    void skip();
    // The result (valid once done()), mapped with `overrides`.
    // `previous`: the tier of the same GPU's last calibration (kept when the measurement is close to a boundary).
    HardwareProfile profile(const ProfileOverrides& overrides = {}, const ProfileTier* previous = nullptr) const;

private:
    static constexpr int kStages = 5; // probe, triangles, upload, memory, dab
    bool quick_ = false;
    int stage_ = 0;
    HardwareInfo hw_;
    Calibration cal_;
    double startedMs_ = 0.0;
};

// GPU time of each frame through GL timer queries (GL_TIME_ELAPSED), read a few frames later so the CPU never
// waits for the GPU. Unavailable (available() false) where the driver has no timer queries.
class GpuFrameTimer {
public:
    bool available();          // loads the entry points on first use (needs the context)
    void begin();              // before the frame's GL work
    void end(uint64_t frame);  // after it; `frame` tags the result
    bool poll(uint64_t& frame, double& ms); // an earlier frame's GPU time, if one is ready (never waits)
    void release();            // deletes the queries (needs the context)

private:
    static constexpr int kSlots = 4;
    unsigned queries_[kSlots] = {};
    uint64_t frames_[kSlots] = {};
    bool pending_[kSlots] = {};
    int next_ = 0, active_ = -1;
    bool loaded_ = false, ok_ = false;
};

// Sets the shared thread pool's size from the cached profile (call before any brush work; core/ThreadPool.h).
void applyCachedThreadCount();

// Calibrates now, blocking (about 2-3 s; quick: about 1 s). Needs a current OpenGL context.
HardwareProfile calibrateNow(bool quick, const ProfileOverrides& overrides, const ProfileTier* previous = nullptr);

// sculptforge --calibrate [--calibrate-quick] [--calibrate-no-save]: a hidden window and context, calibrate, print
// the JSON (schema sculptforge.calibration/1) to stdout, save it as the cached profile. Returns the exit code.
int runCalibrateCommandLine(bool quick, bool save);

} // namespace sf
