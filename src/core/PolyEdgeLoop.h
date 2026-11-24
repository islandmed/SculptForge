// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Polygon mode's Edge loop op (core/PolyOps.h): new loops of edges across the ring of quads through each selected
// edge (core/EdgeLoopInsert.h does the cutting). One selected edge, or a ring selected with Ring, gives one ring; edges
// on several rings cut every one of them. Parameters: count (loops per ring, evenly spaced) and position (0..1 across
// the quads, 0.5 = centred). The new loops are selected afterwards.
#pragma once

#include "core/PolyOps.h"

#include <string>

namespace sf {

bool polyEdgeLoop(const PolyOpInput& in, PolyOpOutput& out, std::string& error);
void registerPolyEdgeLoopOp(PolyOpRegistry& reg);

} // namespace sf
