// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// What QRemesh's two methods share (core/QRemesh.cpp runs the common steps: symmetry, guides, pieces, attributes;
// each method remeshes one piece). Not a public header.
//
//  - Legacy (core/QRemesh.cpp): the Instant Meshes style position field, extracted at twice the spacing and
//    split into quads.
//  - QRemesh 2 (core/QRemesh2*.cpp): a feature-preserving proxy surface, a curvature-aligned cross field, an
//    integer-grid map (seamless parametrization with singularities on lattice points) and extraction of its
//    lattice at the target resolution. See QRemesh2.cpp for the outline.
#pragma once

#include "core/QRemesh.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace sf::qr {

// A piece of the sculpt being remeshed (triangles), in the working frame (with symmetry: the half on the + side of
// the plane, which sits at 0).
struct WorkMesh {
    std::vector<glm::vec3> P;
    std::vector<glm::uvec3> T;
    std::vector<uint32_t> group;  // per triangle
    std::vector<glm::vec3> color; // per vertex (only with usePolypaint)
    std::vector<uint8_t> cutEdge; // filled later: per boundary segment, 1 = made by the symmetry cut
};

// A remeshed piece: quads (counter-clockwise) and, for pieces kept as they were, triangles.
struct QuadMesh {
    std::vector<glm::vec3> P;
    std::vector<uint8_t> feature; // per vertex: lies on a hard edge / border line
    std::vector<glm::uvec4> quads;
    std::vector<glm::uvec3> tris;
};

// QRemesh 2 on one piece.
struct FieldPieceResult {
    QuadMesh mesh;
    std::vector<uint8_t> seam;   // per vertex: on the symmetry plane (welded to its mirror image)
    int attempts = 0;            // parametrizations solved (the count is corrected once when far off)
    size_t singularities = 0;    // cross-field singularities (the poles the layout needs)
    size_t proxyTriangles = 0;
    size_t flippedTriangles = 0; // proxy triangles the map turned over (should be ~0)
    std::string why;             // why the piece fell back to the legacy method
};

// False with `cancelled` set when the progress callback asked to stop, false with `why` when the method could not
// build a good result (the caller then uses the legacy method on that piece).
bool remeshPieceV2(const WorkMesh& piece, int axis, double wantQuads, const QRemeshOptions& opt, float diag,
                   const std::vector<QRemeshGuide>& guides, const std::function<bool(float)>& report,
                   FieldPieceResult& out, bool& cancelled);

} // namespace sf::qr
