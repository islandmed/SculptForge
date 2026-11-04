// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Screen-space selection shapes shared by the lasso and rectangle tools
// (MaskLasso / MaskRect and SelectRect / SelectLasso). Shapes live in
// normalised device coordinates (-1..1, y up) under a view-projection
// matrix; the commands take that matrix as 16 numbers ("view_proj") and the
// lasso as "x0 y0 x1 y1 ..." ("points").
#pragma once

#include "core/Commands.h"
#include "core/Mesh.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace sf {

// A closed polygon with a fast inside test (even-odd rule, so a lasso that
// crosses itself leaves the doubly-enclosed parts out).
// Edges are bucketed into horizontal bands, so testing a vertex only walks
// the few edges near its height even for a long lasso.
class ScreenPolygon {
public:
    explicit ScreenPolygon(std::vector<glm::vec2> points);
    // At least 3 points enclosing some area.
    bool valid() const { return valid_; }
    bool contains(glm::vec2 p) const;

private:
    std::vector<glm::vec2> points_;
    glm::vec2 lo_{0.0f}, hi_{0.0f};
    float bandHeight_ = 1.0f;
    std::vector<std::vector<uint32_t>> bands_; // edge i runs from points_[i] to points_[i + 1]
    bool valid_ = false;
};

// Projects a world point; false when it lies behind the camera.
bool projectToNdc(const glm::mat4& viewProj, const glm::vec3& p, glm::vec2& ndc);
// Front orthographic view of the mesh (+Z, like image_project): NDC -1..1
// spans its bounds. The default view of the selection commands.
glm::mat4 frontViewProj(const Mesh& mesh);
// The "view_proj" argument, or frontViewProj without one. False (with
// `error` set) when it isn't 16 numbers.
bool viewProjArg(const Mesh& mesh, const Args& args, glm::mat4& out, std::string& error);
// The "points" argument as NDC points. False (with `error` set) unless it
// holds at least 3 x y pairs.
bool lassoArg(const Args& args, std::vector<glm::vec2>& out, std::string& error);

} // namespace sf
