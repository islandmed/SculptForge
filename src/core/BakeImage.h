// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// High-precision image files for baked maps (core/Bake.h): 16-bit greyscale or RGB PNG and 32-bit float
// OpenEXR (uncompressed scanlines, one Y channel or R/G/B), plus readers for both so tests and tools can
// check what was written. Pixel rows are top-down, as in the files.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sf {

// `channels` is 1 (grey) or 3 (RGB); `pixels` holds width * height * channels values.
bool savePng16(const std::string& path, int width, int height, int channels, const std::vector<uint16_t>& pixels,
               std::string& error);
// The same PNG in memory; `channels` 1-4 (grey, grey + alpha, RGB, RGBA).
bool encodePng16(int width, int height, int channels, const std::vector<uint16_t>& pixels, std::vector<uint8_t>& out,
                 std::string& error);
// Decodes PNG bytes at 16 bits per channel (8-bit files are scaled up), as `wantChannels` (0 = as stored).
bool decodePng16(const uint8_t* bytes, size_t size, int& width, int& height, int& channels, std::vector<uint16_t>& pixels,
                 std::string& error, int wantChannels = 0);
// Reads any PNG as 16 bits per channel (8-bit files are scaled up); `channels` is what the file has.
bool loadPng16(const std::string& path, int& width, int& height, int& channels, std::vector<uint16_t>& pixels,
               std::string& error);

bool saveExr(const std::string& path, int width, int height, int channels, const std::vector<float>& pixels,
             std::string& error);
// Reads uncompressed float or half EXR scanline files (what saveExr writes, and other tools' "no compression").
bool loadExr(const std::string& path, int& width, int& height, int& channels, std::vector<float>& pixels,
             std::string& error);

} // namespace sf
