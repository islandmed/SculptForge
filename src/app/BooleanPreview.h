// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The view's Boolean preview: its state (App::boolPreview_, app/BooleanPreview.cpp).
#pragma once

#include "core/Mesh.h"

#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace sf {

struct BooleanPreviewResult {
    std::shared_ptr<Mesh> mesh; // null: the run failed (`error`)
    std::string error;
    bool voxel = false;
    double ms = 0.0;
    uint64_t signature = 0;
    std::vector<uint32_t> ids; // the Parts it stands for
};

struct BooleanPreviewState {
    static constexpr uint32_t kKey = 0xFFFFFF00u; // its renderer slot (Part keys count up from 1)
    std::future<BooleanPreviewResult> job;
    uint64_t started = 0; // signature of the inputs of the run under way (or last started)
    uint64_t edits = 0;   // bumps every frame an operand's mesh changes (a stroke, a gizmo drag)
    BooleanPreviewResult shown; // the last finished run
    bool raw = false;     // draw the Parts as they are this frame (an edit under way, and runs are slow)
    double lastMs = 0.0;
    ~BooleanPreviewState() {
        if (job.valid()) job.wait();
    }
};

} // namespace sf
