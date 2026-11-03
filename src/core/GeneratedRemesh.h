// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Makes a generated (image or text to 3D) model sculptable. AI meshes are often non-manifold, holey and uneven,
// so the default runs a voxel pass first as cleanup, then Quad Remesh on the watertight result, then divides it
// (Catmull-Clark, the levels kept) and projects the top level onto a full-resolution voxel surface so the detail
// survives. Quad Remesh's time grows with its input, so the cleanup pass it reads is coarser (0.7x, at most 200).
// Voxel: the voxel pass alone (triangles). Off: the raw mesh.
#pragma once

#include "core/Mesh.h"

#include <functional>

#include <string>
#include <string_view>

namespace sf {

enum class GeneratedRemesh { Quad, Voxel, Off };

// "quad" (also "true"), "voxel", "false" / "off" / "none". False for anything else.
bool generatedRemeshFromName(std::string_view name, GeneratedRemesh& mode);
const char* generatedRemeshName(GeneratedRemesh mode);

// Quad Remesh's target quads for a Resolution (the voxel resolution, 64-512): about res^2 / 2, so the default 256
// gives 32768 quads (131072 after one Divide).
int generatedQuadTarget(int resolution);

struct GeneratedRemeshStats {
    size_t rawTriangles = 0;
    size_t quads = 0;     // Quad: faces at the top level
    size_t triangles = 0; // the result's triangles
    int levels = 0;       // Quad: subdivision levels above the base cage
    double seconds = 0;
};

// The voxel resolution of the cleanup pass Quad Remesh reads.
int generatedCleanupResolution(int resolution);

// `levels` (Quad only, 0-2): Divide steps after Quad Remesh. `progress` (0..1; false cancels) follows Quad Remesh.
// Runs on any thread. False with `error` on failure or cancel.
bool remeshGenerated(const Mesh& raw, GeneratedRemesh mode, int resolution, int levels, Mesh& out, std::string& error,
                     GeneratedRemeshStats* stats = nullptr, const std::function<bool(float)>& progress = {});

// "remeshed to 131072 quads" / "remeshed to 180000 triangles" / "" (Off), for the status line.
std::string generatedRemeshNote(const GeneratedRemeshStats& stats, GeneratedRemesh mode);

} // namespace sf
