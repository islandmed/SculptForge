// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// QRemesh Guides: drawing, storing and scripting the curves QRemesh follows (core/QRemeshGuides.h). The field
// side is in core/QRemesh.cpp.
#include "core/QRemeshGuides.h"

#include "core/Commands.h"
#include "core/PathStroke.h"
#include "core/SurfaceQuery.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace sf {

namespace {

using glm::vec3;

float polylineLength(const std::vector<vec3>& p, bool closed) {
    float len = 0.0f;
    for (size_t i = 1; i < p.size(); ++i) len += glm::length(p[i] - p[i - 1]);
    if (closed && p.size() > 2) len += glm::length(p.front() - p.back());
    return len;
}

// Points every `step` along the polyline (the last one at its end, or before the start of a closed one).
std::vector<vec3> resample(const std::vector<vec3>& p, bool closed, float step) {
    std::vector<vec3> line = p;
    if (closed) line.push_back(p.front());
    std::vector<vec3> out;
    if (line.empty()) return out;
    out.push_back(line.front());
    float carry = 0.0f; // distance walked since the last point placed
    for (size_t i = 1; i < line.size(); ++i) {
        const vec3 a = line[i - 1], b = line[i];
        const float len = glm::length(b - a);
        if (len <= 0.0f) continue;
        float at = step - carry;
        while (at <= len) {
            out.push_back(a + (b - a) * (at / len));
            at += step;
        }
        carry = len - (at - step);
    }
    if (closed) {
        if (out.size() > 1 && glm::length(out.back() - out.front()) < 0.5f * step) out.pop_back();
    } else if (glm::length(out.back() - line.back()) > 0.25f * step) {
        out.push_back(line.back());
    }
    return out;
}

// The closest point of the mesh within `reach` of p (p itself when there is none).
vec3 snapToSurface(const Mesh& mesh, const BVH& bvh, const vec3& p, float reach, std::vector<uint32_t>& scratch) {
    bvh.queryTriangles(mesh, p, reach, scratch);
    float best = std::numeric_limits<float>::max();
    vec3 at = p;
    for (uint32_t t : scratch) {
        const glm::uvec3 tri = mesh.triangles[t];
        glm::vec3 bary;
        const vec3 q = closestPointOnTriangle(p, mesh.positions[tri.x], mesh.positions[tri.y], mesh.positions[tri.z], bary);
        const float d = glm::dot(q - p, q - p);
        if (d < best) best = d, at = q;
    }
    return at;
}

// Mean distance from the points of `a` to the polyline `b`.
float meanDistance(const std::vector<vec3>& a, const std::vector<vec3>& b, bool closedB) {
    if (a.empty() || b.size() < 2) return std::numeric_limits<float>::max();
    double sum = 0.0;
    const size_t segs = closedB ? b.size() : b.size() - 1;
    for (const vec3& p : a) {
        float best = std::numeric_limits<float>::max();
        for (size_t s = 0; s < segs; ++s) {
            const vec3 u = b[s], v = b[(s + 1) % b.size()], d = v - u;
            const float l2 = glm::dot(d, d);
            const float t = l2 > 0.0f ? std::clamp(glm::dot(p - u, d) / l2, 0.0f, 1.0f) : 0.0f;
            best = std::min(best, glm::length(u + d * t - p));
        }
        sum += best;
    }
    return float(sum / double(a.size()));
}

} // namespace

int addQRemeshGuide(std::vector<QRemeshGuide>& guides, const Mesh& mesh, const BVH& bvh, const std::vector<vec3>& path,
                    const Symmetry& symmetry, float step) {
    if (path.size() < 2 || mesh.triangleCount() == 0) return 0;
    const float diag = glm::length(mesh.bounds().extent());
    if (!(diag > 0.0f)) return 0;
    if (step <= 0.0f) step = 0.005f * diag;
    const float length = polylineLength(path, false);
    if (length < 4.0f * step) return 0;
    // Ends close together (within a tenth of the curve's length and a few steps): a loop.
    const bool closed = length > 12.0f * step && glm::length(path.back() - path.front()) < std::max(0.1f * length, 3.0f * step);
    QRemeshGuide g;
    g.closed = closed;
    std::vector<uint32_t> scratch;
    // Two passes of resample + snap: the chord between hits cuts under a curved surface.
    g.points = path;
    for (int pass = 0; pass < 2; ++pass) {
        g.points = resample(g.points, closed && pass > 0, step);
        for (vec3& p : g.points) p = snapToSurface(mesh, bvh, p, 4.0f * step, scratch);
    }
    if (g.points.size() < 3) return 0;
    uint32_t stroke = 1;
    for (const QRemeshGuide& o : guides) stroke = std::max(stroke, o.stroke + 1);
    g.stroke = stroke;

    std::vector<QRemeshGuide> added{g};
    for (const vec3& m : Sculptor::mirrors(symmetry)) {
        if (m == vec3(1.0f)) continue;
        QRemeshGuide copy = g;
        for (vec3& p : copy.points) p = snapToSurface(mesh, bvh, p * m, 4.0f * step, scratch);
        // A curve that is its own mirror image (a loop round the mouth, across the middle) is kept once.
        bool same = false;
        for (const QRemeshGuide& a : added) same |= meanDistance(copy.points, a.points, a.closed) < 2.0f * step;
        if (!same) added.push_back(std::move(copy));
    }
    guides.insert(guides.end(), added.begin(), added.end());
    return int(added.size());
}

bool deleteLastQRemeshGuide(std::vector<QRemeshGuide>& guides) {
    if (guides.empty()) return false;
    const uint32_t last = guides.back().stroke;
    guides.erase(std::remove_if(guides.begin(), guides.end(), [&](const QRemeshGuide& g) { return g.stroke == last; }),
                 guides.end());
    return true;
}

float qremeshGuideLength(const std::vector<QRemeshGuide>& guides) {
    float len = 0.0f;
    for (const QRemeshGuide& g : guides) len += polylineLength(g.points, g.closed);
    return len;
}

Json qremeshGuidesToJson(const std::vector<QRemeshGuide>& guides) {
    Json out = Json::array();
    for (const QRemeshGuide& g : guides) {
        Json j = Json::object();
        j.set("closed", g.closed);
        j.set("stroke", double(g.stroke));
        Json pts = Json::array();
        for (const vec3& p : g.points)
            for (int k = 0; k < 3; ++k) pts.push(double(p[k]));
        j.set("points", std::move(pts));
        out.push(std::move(j));
    }
    return out;
}

std::vector<QRemeshGuide> qremeshGuidesFromJson(const Json& json) {
    std::vector<QRemeshGuide> out;
    if (!json.isArray()) return out;
    for (const Json& j : json.items()) {
        const Json& pts = j["points"];
        if (!pts.isArray() || pts.size() < 6 || pts.size() % 3 != 0) continue;
        QRemeshGuide g;
        g.closed = j["closed"].asBool(false);
        g.stroke = uint32_t(std::max(0.0, j["stroke"].asNumber(double(out.size() + 1))));
        bool ok = true;
        for (size_t i = 0; i < pts.size() && ok; i += 3) {
            const vec3 p(float(pts[i].asNumber(NAN)), float(pts[i + 1].asNumber(NAN)), float(pts[i + 2].asNumber(NAN)));
            ok = std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
            g.points.push_back(p);
        }
        if (ok) out.push_back(std::move(g));
    }
    return out;
}

void registerQRemeshGuideCommands(CommandRegistry& reg) {
    using T = ParamSpec::Type;
    reg.add({"qremesh_guide",
             "Quad Remesh Guides (ZBrush's ZRemesher Guides brush): draw a curve on the active Part for Quad Remesh to "
             "follow; an edge loop runs along it and the edge flow around it turns with it. A curve that ends where it "
             "started becomes a loop (around an eye or the mouth). The guides are kept with the Part and saved in "
             "projects.",
             {{"points", T::String, "The curve: \"x,y,z x,y,z ...\" (2+ points), on or near the surface.", true},
              {"view", T::String, "Project the points onto the surface along this direction \"x,y,z\" (as a stroke "
                                  "seen from the camera). Default: each point goes to the closest surface point.", false},
              {"symmetry", T::String, "Mirror the curve across these planes through the centre, e.g. \"x\" or \"xz\". "
                                      "Default: none.", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 if (doc.mesh.triangleCount() == 0) return {false, "Quad Remesh Guides: the Part is empty"};
                 std::vector<vec3> pts;
                 std::string err;
                 if (!parsePathPoints(argString(a, "points"), pts, err)) return {false, err};
                 doc.syncBvh();
                 if (a.contains("view")) {
                     vec3 view;
                     if (!parseVec3(argString(a, "view"), view) || !(glm::length(view) > 1e-6f))
                         return {false, "view must be a direction \"x,y,z\""};
                     view = glm::normalize(view);
                     const AABB box = doc.mesh.bounds();
                     const float diag = glm::length(box.extent());
                     // Through the dense samples of the path, so the curve hugs the surface between the points.
                     std::vector<vec3> hits;
                     for (const vec3& p : samplePath(pts, false, 0.005f * diag)) {
                         RayHit hit;
                         const vec3 origin = p - view * (glm::dot(p - box.center(), view) + diag);
                         if (doc.bvh.raycast(doc.mesh, origin, view, hit)) hits.push_back(hit.position);
                     }
                     pts = std::move(hits);
                 }
                 const std::string sym = argString(a, "symmetry");
                 Symmetry s{false, false, false};
                 for (char c : sym) {
                     if (c == 'x' || c == 'X') s.x = true;
                     else if (c == 'y' || c == 'Y') s.y = true;
                     else if (c == 'z' || c == 'Z') s.z = true;
                     else return {false, "symmetry takes the letters x, y and z"};
                 }
                 const int added = addQRemeshGuide(doc.qremeshGuides, doc.mesh, doc.bvh, pts, s);
                 if (added == 0) return {false, "Quad Remesh Guides: the curve is too short or misses the Part"};
                 doc.modified = true;
                 const QRemeshGuide& g = doc.qremeshGuides[doc.qremeshGuides.size() - size_t(added)];
                 return {true, std::string("Quad Remesh guide added (") + (g.closed ? "a loop" : "a curve") +
                                   (added > 1 ? ", mirrored" : "") + "); " + std::to_string(doc.qremeshGuides.size()) +
                                   " guides on the Part"};
             }});
    reg.add({"qremesh_guide_delete_last", "Quad Remesh Guides: delete the last curve drawn (with its mirror copies).", {},
             [](Document& doc, const Args&) -> CommandResult {
                 if (!deleteLastQRemeshGuide(doc.qremeshGuides)) return {false, "No Quad Remesh guides to delete"};
                 doc.modified = true;
                 return {true, std::to_string(doc.qremeshGuides.size()) + " Quad Remesh guides left"};
             }});
    reg.add({"qremesh_guides_clear", "Quad Remesh Guides: delete every guide curve of the active Part.", {},
             [](Document& doc, const Args&) -> CommandResult {
                 if (doc.qremeshGuides.empty()) return {false, "No Quad Remesh guides to delete"};
                 doc.qremeshGuides.clear();
                 doc.modified = true;
                 return {true, "Quad Remesh guides cleared"};
             }});
    reg.add({"qremesh_guides_info", "Quad Remesh Guides: how many guide curves the active Part has, and their length.", {},
             [](Document& doc, const Args&) -> CommandResult {
                 size_t loops = 0;
                 for (const QRemeshGuide& g : doc.qremeshGuides) loops += g.closed;
                 char buf[160];
                 std::snprintf(buf, sizeof buf, "%zu Quad Remesh guides (%zu loops), total length %.4g", doc.qremeshGuides.size(),
                               loops, double(qremeshGuideLength(doc.qremeshGuides)));
                 return {true, buf};
             }});
}

} // namespace sf
