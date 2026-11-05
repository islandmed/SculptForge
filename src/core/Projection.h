// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ProjectAll (Part palette): moves the active Part's vertices onto the surface of other
// Parts and takes their vertex paint, so detail survives a retopology: QRemesh a copy, divide it a few
// times, then project from the original. Each vertex shoots a ray both ways along its normal and takes the
// nearest hit within Dist (or the mean of both sides with Mean); the mask holds vertices back (partly
// masked ones move part way). Rays run on all threads against a read-only BVH (core/SurfaceQuery.h).
#pragma once

#include "core/Document.h"

#include <functional>
#include <string>
#include <vector>

namespace sf {

struct ProjectOptions {
    float distance = 0.02f; // Dist: how far to search, as a fraction of the target's bounding-box diagonal
    bool mean = false;      // Mean: with hits on both sides, settle halfway between them
    bool geometry = true;   // project the shape
    bool vertexpaint = true;  // project the vertex paint (when a source has any)
    int blur = 0;           // Blur: smoothing passes over the projected offsets
};

struct ProjectStats {
    size_t projected = 0; // vertices that found the source
    size_t missed = 0;    // nothing within Dist
    size_t masked = 0;    // fully masked or hidden: left alone
    bool colors = false;  // vertex paint was projected
    double seconds = 0.0;
};

// Fraction 0..1; return false to cancel.
using ProjectProgress = std::function<bool(float fraction)>;

// New positions (and colours, when `colors` is not null and a source has vertex paint) for every vertex of
// `target`, from the surfaces of `sources`. Vertices are moved by (1 - mask); hidden ones stay.
bool computeProjection(const Mesh& target, const std::vector<const Mesh*>& sources, const ProjectOptions& options,
                       std::vector<glm::vec3>& positions, std::vector<glm::vec3>* colors, ProjectStats& stats,
                       std::string& error, const ProjectProgress& progress = {});

// ProjectAll on the document: from every visible Part except the active one (or only Part `source`
// when >= 0) onto the active one, as one undo step. Shape offsets go into the active sculpt layer, colours
// into the active paint layer.
bool projectAll(Document& doc, const ProjectOptions& options, std::string& message, int source = -1,
                ProjectStats* stats = nullptr);

} // namespace sf
