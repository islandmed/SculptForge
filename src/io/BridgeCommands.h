// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Bridge commands: bridge_import, bridge_export, bridge_send,
// bridge_receive and export_stl. Registered by CommandRegistry's constructor.
#pragma once

#include "core/Commands.h"
#include "io/MeshObject.h"

#include <string>
#include <vector>

namespace sf {

void registerBridgeCommands(CommandRegistry& registry);

// Shared with the Blender bridge (io/BlenderCommands.cpp):
// The Bridge name of Part `i` (active or not): the object it came from (or Blender) or was sent as.
std::string& partBridgeName(Document& doc, int i);
// Visible Parts with triangles as objects to send: the Bridge name each came with, else its Part name.
// `indices` receives the Part of each object.
std::vector<MeshObjectRef> bridgeObjects(Document& doc, std::vector<int>& indices);
// Puts received objects into the scene: each updates the Part with its Bridge name (an undoable `undoLabel` step
// in that Part's history), the first one replaces a fresh scene (one Part that came from nowhere, renamed
// after the object when `renameFresh` or several objects arrive), the others become new Parts. The active
// Part stays selected.
void receiveObjects(Document& doc, std::vector<MeshObject>& objects, const std::string& undoLabel, bool renameFresh);

} // namespace sf
