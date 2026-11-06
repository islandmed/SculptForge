// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// PLY: the de-facto exchange format for 3D Gaussian splats (INRIA layout used
// by Polycam, Luma, Postshot, Scaniverse, nerfstudio, SuperSplat...) and a
// common format for scans. ASCII and binary (either endianness) are read.
#pragma once

#include "core/Mesh.h"
#include "core/Splats.h"

#include <string>

namespace sf {

enum class PlyContent { Mesh, Splats };

// Reads a mesh (if the file has faces; vertex colours kept), Gaussian splats (if it has splat
// properties), or turns a plain point cloud into small round splats so it can
// be converted to a mesh. `content` says which output was filled.
bool loadPly(const std::string& path, Mesh& mesh, SplatCloud& splats, PlyContent& content, std::string& error);

// Writes splats in the standard binary 3DGS layout (log scales, logit
// opacity). The cloud's transform is baked in.
bool saveSplatPly(const std::string& path, const SplatCloud& splats, std::string& error);

// The compact ".splat" format used by many web viewers (antimatter15/splat):
// 32 bytes per splat - position, linear scale, RGBA8 colour/opacity and an
// 8-bit quaternion. Lossy (colour and rotation are quantised, no SH).
bool loadDotSplat(const std::string& path, SplatCloud& splats, std::string& error);
bool saveDotSplat(const std::string& path, const SplatCloud& splats, std::string& error);

// Writes a mesh (binary little-endian: positions, normals and, when the mesh
// has vertex paint, uchar red/green/blue in sRGB; quads where Mesh::quadPartners
// keeps them, else triangles) that loadPly reads back. `scale`: file units per SculptForge unit, as saveObj.
bool savePlyMesh(const std::string& path, const Mesh& mesh, std::string& error, double scale = 1.0);

} // namespace sf
