// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/Mesh.h"

#include <cstdint>

namespace sf {

class CommandRegistry;

// FNV-1a of the vertex count and triangle indices: changes whenever topology or vertex order does.
uint64_t topologyHash(const Mesh& mesh);
// mesh_read, mesh_set_positions (core/LiveApi.cpp).
void registerLiveApiCommands(CommandRegistry& registry);

} // namespace sf
