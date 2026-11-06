// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// STL (3D printing): binary and ASCII import, binary export. SculptForge sculpts
// in metres with Y up; STL files have no units, and slicers read them as
// millimetres with Z up, so both directions convert between the two.
#pragma once

#include "core/Mesh.h"

#include <cstdint>
#include <span>
#include <string>

namespace sf {

struct StlExportOptions {
    double scale = 1000.0; // file units per metre (1000: millimetres)
    float heightMm = 0.0f; // > 0: scale so the model is this tall (Y in SculptForge, Z in the file); overrides `scale`
    bool zUp = true;       // turn Y up into the Z up that slicers and printers use
};

// Files are read as millimetres with Z up (`perMetre`: file numbers per metre, 1000; 1 / 0.0254 for inches),
// except binary files written by SculptForge 0.5 and earlier (in metres, Y up), which are recognised by their
// header and kept as stored. Coincident corners are welded into a connected mesh.
bool loadStl(const std::string& path, Mesh& out, std::string& error, double perMetre = 1000.0);
bool parseStl(std::span<const uint8_t> data, Mesh& out, std::string& error, double perMetre = 1000.0);
// Writes binary STL (every triangle, including hidden ones). Paths are UTF-8.
bool saveStl(const std::string& path, const Mesh& mesh, std::string& error, const StlExportOptions& options = {});

} // namespace sf
