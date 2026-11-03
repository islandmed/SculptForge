// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Starter meshes: base meshes to start a sculpt from (Library > Tool > Starter meshes, File > New from starter
// mesh, the welcome's "Start from a head"). Each is a small all-quad base cage in a project file, compressed and
// embedded in the program; opening one adds its subdivision levels (Divide), so the files stay small and the
// artist can step down to the cage. Centimetres, Y up, looking down +Z, standing on the ground.
//
// Sources: made by tools/starter/build_starter_meshes.py, from scratch or cut from the MakeHuman base mesh (CC0);
// docs/starter-meshes.md has the provenance.
#pragma once

#include "core/Commands.h"
#include "core/Document.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sf {

struct StarterMesh {
    const char* id;          // "human-head" (new_starter_mesh name=, the unpacked file's name)
    const char* name;        // "Human Head"
    const char* description; // one line for tooltips
    int levels;              // Divide this many times on open
    bool symmetric;          // mirror-symmetric in X
    bool closed;             // no open edges (the cut pieces have one or more open borders)
    const unsigned char* data;
    size_t size; // compressed bytes
};

const std::vector<StarterMesh>& starterMeshes();
// By id or name, case-insensitive ("head" finds Human Head too); null if none.
const StarterMesh* findStarterMesh(std::string_view idOrName);
// The project file's bytes (zlib-decompressed). False with `error` if the embedded data is damaged.
bool unpackStarterMesh(const StarterMesh& mesh, std::vector<uint8_t>& project, std::string& error);
// Writes every starter mesh's project to `dir`/<id>.sfproj (creating `dir`; files already identical are left
// alone, so their thumbnails stay cached). False with `error` on the first failure.
bool writeStarterMeshes(const std::string& dir, std::string& error);
// The starter mesh `path` is a copy of (a file writeStarterMeshes wrote to `dir`), or null.
const StarterMesh* starterMeshForPath(const std::string& dir, const std::string& path);
// Replaces the scene with the starter mesh, divided `levels` times (-1 = its own count); every Part keeps its
// name, doc.path stays empty (Save asks where). On failure `doc` is untouched.
bool loadStarterMesh(const StarterMesh& mesh, Document& doc, int levels, std::string& error);

// new_starter_mesh (called by the CommandRegistry constructor).
void registerStarterMeshCommands(CommandRegistry& registry);

} // namespace sf
