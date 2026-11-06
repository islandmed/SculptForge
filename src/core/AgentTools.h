// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Higher-level commands for AI agents (and scripts), built on the plain ones so every step lands in the undo
// history as it would from the UI:
//   blockout        a whole Forge Sphere armature in one call ("x,y,z,r,parent | ..."), skinned into a sculptable Part
//   measure         a Part's size, centre and proportions, or two Parts compared, or the distance between two points
//   scene_overview  the Parts with their triangles, levels, sizes and visibility, the active brush, the scene's size
#pragma once

namespace sf {

class CommandRegistry;

void registerAgentTools(CommandRegistry& registry);

} // namespace sf
