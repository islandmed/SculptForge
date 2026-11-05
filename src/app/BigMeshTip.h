// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The one big-mesh tip (PanelSupport.cpp draws it, PanelProject.cpp decides): the first time the scene holds more
// than about 20 million triangles (base meshes plus micro levels), a quiet note along the bottom of the view says
// dense meshes got faster and names the settings that matter for them. Shown once per install, never again after
// (project.json "big_mesh_tip_done", written the moment it shows).
#pragma once

#include <cstddef>
#include <cstdlib>

namespace sf {

inline constexpr size_t kBigMeshTipTriangles = 20'000'000;

// The threshold: kBigMeshTipTriangles, or SF_BIG_MESH_TIP_TRIANGLES (UI tests use a small sphere).
inline size_t bigMeshTipTriangles() {
    const char* env = std::getenv("SF_BIG_MESH_TIP_TRIANGLES");
    if (!env || !*env) return kBigMeshTipTriangles;
    const long long v = std::atoll(env);
    return v > 0 ? size_t(v) : kBigMeshTipTriangles;
}

// Whether the tip shows now: a scene over the threshold, never shown before, and nothing else on screen or in hand
// (a stroke, the welcome, What's New, a membership card, the quick tour, a benchmark): it waits for those.
inline bool bigMeshTipDue(size_t sceneTriangles, size_t threshold, bool alreadyShown, bool busy) {
    return !alreadyShown && !busy && threshold > 0 && sceneTriangles >= threshold;
}

} // namespace sf
