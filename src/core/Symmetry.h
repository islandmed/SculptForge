// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Stroke symmetry: X / Y / Z mirrors, radial copies around an axis, about the world origin or a local pivot.
// Sculptor::copies (core/Sculpt.h) turns the settings into the list of copies every brush applies.

#include <glm/glm.hpp>

#include <cstddef>
#include <span>
#include <vector>

namespace sf {

struct Symmetry {
    bool x = true, y = false, z = false;
    // Radial symmetry (#63): while `radialEnabled`, `radial` copies of every stroke turned evenly around
    // `radialAxis` (0 X, 1 Y, 2 Z); 2 to kMaxRadial (1 is none). With mirror axes on too, each mirror image gets
    // its copies (mirror + radial = 2N copies with one axis). The count stays while radial is toggled off.
    bool radialEnabled = false;
    int radial = 8;
    int radialAxis = 1;
    // Local symmetry: mirror planes and the radial axis go through `pivot` (the app sets it to the active Part's
    // centre) instead of the world origin.
    bool local = false;
    glm::vec3 pivot{0.0f};
    static constexpr int kMaxRadial = 64;
    bool radialOn() const { return radialEnabled && radial > 1; }
    // Only the X / Y / Z mirrors through the origin (what Sculptor::mirrors can express).
    bool mirrorOnly() const { return !radialOn() && (!local || pivot == glm::vec3(0.0f)); }
};

// One copy of a symmetric stroke: a rotation (and reflection) `m` about `pivot`. Points move as
// pivot + m (p - pivot), directions as m d; the first copy Sculptor::copies makes is the identity.
struct SymCopy {
    glm::mat3 m{1.0f};
    glm::vec3 pivot{0.0f};
    glm::vec3 point(const glm::vec3& p) const { return pivot + m * (p - pivot); }
    glm::vec3 dir(const glm::vec3& d) const { return m * d; }
    glm::vec3 inversePoint(const glm::vec3& p) const { return pivot + glm::transpose(m) * (p - pivot); }
    glm::vec3 inverseDir(const glm::vec3& d) const { return glm::transpose(m) * d; }
    // -1 when the copy is mirrored an odd number of times (det m < 0): stamps flip, pseudovectors flip once more.
    float handedness() const { return glm::determinant(m) < 0.0f ? -1.0f : 1.0f; }
    bool identity() const { return m == glm::mat3(1.0f); }
};

// Every copy of a stroke the symmetry makes: each mirror image (in Sculptor::mirrors' order) times each radial
// turn, duplicates dropped (X and Z mirrors with an even radial count repeat a half turn), the identity first.
// Defined in Sculpt.cpp.
std::vector<SymCopy> symmetryCopies(const Symmetry& sym);
// For each copy, the index of its inverse in the same list (a mirror is its own; a turn's is the opposite turn).
std::vector<size_t> symmetryInverses(std::span<const SymCopy> copies);

} // namespace sf
