// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Image to 3D often turns the picture's background into a big flat plane behind or under the subject. This finds
// such planes in a generated mesh so they can be dropped before it lands in the scene: a separate piece (any
// orientation) or a flat region at one side of the bounds (also when it is joined to the subject) that is
//  - flat: thinner than 2.5% of its width,
//  - large: covering most of the model's bounds in both of its directions, and larger than the rest,
//  - behind or under: everything else stands out on one side of it, over it (not in holes of it).
// A relief or a coin on its own (thin, but the subject) is kept: the rest of it lies over the same area or in its
// holes, never standing clear of a larger plane. Nothing is removed when only the plane would remain.
#pragma once

#include "core/Mesh.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sf {

class CommandRegistry;

struct BackdropRemoval {
    int planes = 0;           // backdrops found (a ground and a back wall are two)
    size_t triangles = 0;     // triangles they had
};

// keep[t] = 0 for the triangles of every flat backdrop in `mesh`, 1 for the rest; empty when there is none.
std::vector<uint8_t> flatBackdropTriangles(const Mesh& mesh, BackdropRemoval* found = nullptr);

// Removes them (unused vertices and every attribute compacted). False, `mesh` untouched, when there is none.
bool removeFlatBackdrop(Mesh& mesh, BackdropRemoval* removed = nullptr);

// "Removed a flat background plane (N triangles)" for the status line.
std::string backdropRemovedNote(const BackdropRemoval& removed);

// remove_flat_background: the same on the active Part, one undo step.
void registerBackdropCommands(CommandRegistry& registry);

} // namespace sf
