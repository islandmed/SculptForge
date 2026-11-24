// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Scatter (Tool > Scatter): copies of a piece (another Part or a small built-in shape) laid over a surface, the whole
// Part or only its masked area, each at its own random size and turn. Keep apart relaxes the copies over the surface
// until no two overlap: each copy's bounding sphere (Round fit) or also its box (Tight fit, the piece's box turned and
// scaled as the copy is) is pushed off its neighbours, and every centre goes back onto the surface after each push.
// Copies that still have no room when the relax ends are left out, so "none overlap" always holds; the result says
// how many. Tight counts two copies apart when either their spheres or their boxes are (both hold the whole piece).
// Joined (one Part) or one Part per copy; one undo step either way.
#pragma once

#include "core/Document.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sf {

class CommandRegistry;

struct ScatterSettings {
    int count = 200;
    float scaleMin = 1.0f, scaleMax = 1.0f; // each copy's size, a random factor between these
    float turn = 180.0f;                    // random turn about the piece's up side, up to this many degrees either way
    float tilt = 0.0f;                      // random tilt of the up side off the normal, up to this many degrees
    bool align = true;                      // up side along the surface normal (else straight up, world +Y)
    bool flat = false;                      // the piece's shortest side is its up side (lie flat), else its own +Y
    float offset = 0.0f;                    // along the normal in copy heights: 0 = centred on the surface, 0.5 = resting on it
    uint32_t seed = 1;
    bool masked = false;                    // only the masked area (mask >= 0.5) of the surface
    bool keepApart = false;
    float spacing = 0.0f;                   // Keep apart: the gap between copies, in copy sizes (0 = just touching)
    enum class Fit { Round, Tight } fit = Fit::Tight;
    int iterations = 0;                     // Keep apart's relax passes at most (0 = automatic)
    // The piece's bounding sphere about its box's centre (0: the box's half diagonal). A round piece's is much smaller
    // than its box's corners: a pebble or a disc packs closer with it.
    float pieceRadius = 0.0f;
};

// One copy: where its centre is and how it is turned (the piece's own axes, already set up by flat), and its size.
struct ScatterCopy {
    glm::vec3 position{0.0f}; // the copy's centre
    glm::vec3 anchor{0.0f};   // the surface point under it
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::mat3 rotation{1.0f}; // piece space (centred, flat applied) -> scene
    float scale = 1.0f;
    uint32_t triangle = 0;
};

struct ScatterStats {
    size_t asked = 0, placed = 0, dropped = 0;
    int passes = 0;          // relax passes run
    double placeMs = 0.0, relaxMs = 0.0;
};

// Places the copies of a piece whose box is [-half, half] (in piece space: centred, its up side is +Y) over `target`
// (scene space). Empty when the surface has no (masked) area; `error` then says why.
std::vector<ScatterCopy> scatterPlace(const Mesh& target, const glm::vec3& half, const ScatterSettings& settings,
                                      ScatterStats* stats = nullptr, std::string* error = nullptr);

// True when copies a and b of a piece with box half extents `half` overlap, or are closer than the settings' spacing,
// by the fit in `settings` (what Keep apart guarantees for every pair it keeps): Round, their bounding spheres; Tight,
// their spheres and their boxes both (either one apart keeps the pieces inside them apart).
bool scatterTouching(const ScatterCopy& a, const ScatterCopy& b, const glm::vec3& half, const ScatterSettings& settings);

// The piece in piece space for `flat`: centred on its box, its up side (+Y, or its shortest side) turned to +Y.
// `axes` gets the rotation from piece space back to the Part's own space.
Mesh scatterPieceMesh(const Mesh& source, bool flat, glm::mat3* axes = nullptr);

// Builds the copies as one mesh (each its own face group) from the piece in piece space.
Mesh scatterJoin(const Mesh& piece, const std::vector<ScatterCopy>& copies);

constexpr int kScatterMaxCopies = 20000;
constexpr int kScatterMaxSeparate = 500; // one Part per copy up to this many
constexpr size_t kScatterMaxTriangles = 4000000;

// scatter
void registerScatterCommands(CommandRegistry& registry);

} // namespace sf
