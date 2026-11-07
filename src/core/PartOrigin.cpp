// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/PartOrigin.h"

#include "core/Commands.h"
#include "core/PathStroke.h" // parseVec3
#include "core/Symmetry.h"

#include <glm/geometric.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

namespace sf {

glm::vec3 boundsCentre(const Mesh& mesh) {
    if (mesh.vertexCount() == 0) return glm::vec3(0.0f);
    glm::vec3 lo = mesh.positions[0], hi = lo;
    for (const glm::vec3& p : mesh.positions) {
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
    }
    return (lo + hi) * 0.5f;
}

glm::vec3 centreOfMass(const Mesh& mesh) {
    // Tetrahedra from a point near the mesh to each triangle (the divergence theorem): their signed volumes sum to
    // the enclosed volume and their centroids, weighted by it, to the volume centroid. Relative to the bounds
    // centre, in doubles, so a model far from 0,0,0 keeps its precision.
    const glm::dvec3 c0(boundsCentre(mesh));
    double volume = 0.0, area = 0.0;
    glm::dvec3 vsum(0.0), asum(0.0);
    for (const glm::uvec3& t : mesh.triangles) {
        const glm::dvec3 a = glm::dvec3(mesh.positions[t.x]) - c0, b = glm::dvec3(mesh.positions[t.y]) - c0,
                         c = glm::dvec3(mesh.positions[t.z]) - c0;
        const double v = glm::dot(a, glm::cross(b, c)) / 6.0;
        volume += v;
        vsum += v * (a + b + c) * 0.25;
        const double ar = 0.5 * glm::length(glm::cross(b - a, c - a));
        area += ar;
        asum += ar * (a + b + c) / 3.0;
    }
    if (area <= 0.0) return glm::vec3(c0);
    // A volume small against the surface (a plane, an open shell, a mesh whose sides cancel) has no useful
    // centroid: use the surface's. The scale: area^1.5 is the volume of a cube with that much surface, roughly.
    if (std::abs(volume) > 1e-4 * std::pow(area, 1.5)) return glm::vec3(c0 + vsum / volume);
    return glm::vec3(c0 + asum / area);
}

glm::vec3 partPivot(const Document& doc, int index) {
    if (index < 0 || index >= doc.partCount()) return glm::vec3(0.0f);
    const Part& p = doc.parts[size_t(index)];
    return glm::vec3(p.transform * glm::vec4(p.hasOrigin ? p.origin : boundsCentre(doc.partMesh(index)), 1.0f));
}

glm::vec3 partOriginInScene(const Part& p) { return glm::vec3(p.transform * glm::vec4(p.origin, 1.0f)); }

namespace {

// An origin change of Part `id` for its undo step: holds the values before.
std::shared_ptr<PartStep> originStep(const Part& p) {
    auto step = std::make_shared<PartStep>();
    step->id = p.id;
    step->originStep = true;
    step->hasOrigin = p.hasOrigin;
    step->origin = p.origin;
    return step;
}


int targetIndex(const Document& doc, const Args& a) {
    if (a.contains("name")) return doc.findPart(argString(a, "name"));
    if (a.contains("index")) {
        const int i = int(argNumber(a, "index", -1));
        return i >= 0 && i < doc.partCount() ? i : -1;
    }
    return doc.activePart;
}

} // namespace

void carryPartOrigin(Document& doc, int index, const MeshTransform& t, UndoStack& undo) {
    if (index < 0 || index >= doc.partCount()) return;
    Part& p = doc.parts[size_t(index)];
    UndoRecord* r = undo.lastUndo();
    if (!p.hasOrigin || !r || r->part) return;
    r->part = originStep(p);
    p.origin = t.apply(p.origin);
}

void registerPartOriginCommands(CommandRegistry& reg) {
    using T = ParamSpec::Type;
    const ParamSpec indexParam{"index", T::Number, "Part index (0 = top of the list). Default: the active one.", false};
    const ParamSpec nameParam{"name", T::String, "Part name (instead of index).", false};

    reg.add({"part_origin",
             "Set a Part's origin, the point the gizmo turns and scales it about and part_to_world puts at the world "
             "centre (one undo step). It moves with the Part.",
             {{"set", T::String, "bounds (its bounds centre), mass (its centre of mass), world (0,0,0), point (at point=) "
                                 "or none (the gizmo uses the bounds centre again).", true},
              {"point", T::String, "x,y,z for set=point (e.g. the gizmo's pivot).", false}, indexParam, nameParam},
             [](Document& doc, const Args& a) -> CommandResult {
                 const int i = targetIndex(doc, a);
                 if (i < 0) return {false, "No such Part"};
                 const std::string set = argString(a, "set");
                 bool has = true;
                 glm::vec3 at(0.0f);
                 if (set == "bounds") at = boundsCentre(doc.partMesh(i));
                 else if (set == "mass") at = centreOfMass(doc.partMesh(i));
                 else if (set == "world") at = glm::vec3(0.0f);
                 else if (set == "point") {
                     if (!parseVec3(argString(a, "point"), at)) return {false, "Give point=x,y,z"};
                 } else if (set == "none") has = false;
                 else return {false, "Unknown set='" + set + "' (bounds, mass, world, point, none)"};
                 Part& p = doc.parts[size_t(i)];
                 const bool inScene = set == "world" || set == "point"; // (bounds and mass are in the mesh's space)
                 if (inScene && p.moved()) at = glm::vec3(glm::inverse(p.transform) * glm::vec4(at, 1.0f));
                 if (p.hasOrigin == has && (!has || p.origin == at))
                     return {true, p.name + (has ? "'s origin is already there" : " has no origin")};
                 UndoRecord r;
                 r.label = has ? "Set Origin" : "Clear Origin";
                 r.part = originStep(p);
                 p.hasOrigin = has;
                 p.origin = has ? at : glm::vec3(0.0f);
                 doc.partUndo(i).push(std::move(r));
                 doc.modified = true;
                 if (!has) return {true, p.name + ": no origin (the gizmo uses the bounds centre)"};
                 const glm::vec3 shown = partOriginInScene(p);
                 char text[96];
                 std::snprintf(text, sizeof text, "%.4g, %.4g, %.4g", shown.x, shown.y, shown.z);
                 return {true, p.name + " origin: " + text};
             }});

    reg.add({"part_to_world",
             "Move a Part so its origin (its bounds centre without one) is at the world centre, or only onto a symmetry "
             "plane (axis=x: centred across X, e.g. before Mirror And Weld). All of it moves, hidden parts and every "
             "sculpt layer too; the mask doesn't matter. One undo step.",
             {{"axis", T::String, "all (default), or x, y or z: only that coordinate goes to 0.", false}, indexParam, nameParam},
             [](Document& doc, const Args& a) -> CommandResult {
                 const int i = targetIndex(doc, a);
                 if (i < 0) return {false, "No such Part"};
                 const std::string axis = argString(a, "axis", "all");
                 glm::vec3 keep(0.0f); // 1 for the coordinates that stay
                 if (axis == "x") keep = {0, 1, 1};
                 else if (axis == "y") keep = {1, 0, 1};
                 else if (axis == "z") keep = {1, 1, 0};
                 else if (axis != "all") return {false, "Unknown axis='" + axis + "' (all, x, y, z)"};
                 const glm::vec3 centre = partPivot(doc, i);
                 MeshTransform t;
                 t.pivot = centre;
                 t.translate = centre * keep - centre;
                 const std::string name = doc.parts[size_t(i)].name;
                 const AABB box = doc.partMesh(i).bounds();
                 const float eps = 1e-6f * std::max(1.0f, glm::length(box.max - box.min));
                 if (glm::length(t.translate) <= eps) return {true, name + " is already there"};
                 const std::string label = axis == "all" ? "Move to World Centre" : "Move to Symmetry Plane";
                 char text[96];
                 std::snprintf(text, sizeof text, " by %.4g, %.4g, %.4g", t.translate.x, t.translate.y, t.translate.z);
                 if (partMovesWhole(doc, i, false)) { // a linked copy (#94): its transform moves (its origin with it)
                     doc.setPartTransform(i, t.matrix() * doc.parts[size_t(i)].transform, label);
                     return {true, "Moved " + name + text};
                 }
                 Mesh& mesh = doc.partMesh(i); // (not moved: its space is the scene's)
                 BVH& bvh = doc.partBvh(i);
                 TransformSession s; // every vertex, base and layers alike (not transformWeights: hidden ones move too)
                 if (!s.begin(mesh, {}, Symmetry{false, false, false}, t.pivot, -1)) return {false, name + " has no points"};
                 s.apply(mesh, t);
                 UndoRecord r = s.finish(mesh, bvh, label);
                 UndoStack& undo = doc.partUndo(i);
                 undo.push(std::move(r));
                 carryPartOrigin(doc, i, t, undo);
                 doc.modified = true;
                 return {true, "Moved " + name + text};
             }});
}

} // namespace sf
