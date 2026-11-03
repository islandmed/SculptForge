// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Masking.h"

#include "core/Commands.h"
#include "core/ScreenSelect.h"
#include "core/Sculpt.h"
#include "core/ThreadPool.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace sf {

namespace {

// Applies `next` (old mask -> new value of vertex v) to every visible vertex,
// double-buffered so each pass reads the previous one.
template <class F>
bool maskPass(Mesh& mesh, F next) {
    const uint32_t n = static_cast<uint32_t>(mesh.vertexCount());
    std::vector<float>& mask = mesh.ensureMask();
    std::vector<float> out(mask);
    const bool anyHidden = mesh.hasHidden();
    bool changed = false;
    for (uint32_t v = 0; v < n; ++v) {
        if (anyHidden && mesh.vertexHidden(v)) continue;
        const float m = std::clamp(next(mask, v), 0.0f, 1.0f);
        if (m != mask[v]) {
            out[v] = m;
            changed = true;
        }
    }
    if (changed) {
        mask.swap(out);
        mesh.markAllDirty();
    }
    return changed;
}

float neighbourAverage(const Mesh& mesh, const std::vector<float>& mask, uint32_t v) {
    auto ring = mesh.neighbors(v);
    if (ring.empty()) return mask[v];
    float sum = 0.0f;
    for (uint32_t u : ring) sum += mask[u];
    return sum / static_cast<float>(ring.size());
}

template <class F>
bool repeatPass(Mesh& mesh, int iterations, F next) {
    bool changed = false;
    for (int i = 0; i < std::max(1, iterations); ++i) {
        if (!maskPass(mesh, next)) break;
        changed = true;
    }
    return changed;
}


float smoothstep01(float e0, float e1, float x) {
    float t = std::clamp((x - e0) / std::max(e1 - e0, 1e-6f), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

bool symmetryArg(const Args& a, const Mesh& mesh, Symmetry& sym, std::string& error) {
    sym = Symmetry{false, false, false};
    for (char c : argString(a, "symmetry")) {
        const char l = char(std::tolower(static_cast<unsigned char>(c)));
        if (l == 'x') sym.x = true;
        else if (l == 'y') sym.y = true;
        else if (l == 'z') sym.z = true;
        else {
            error = "symmetry takes the axes x, y and z";
            return false;
        }
    }
    const double radial = argNumber(a, "radial", 1.0);
    if (!(radial >= 1.0 && radial <= double(Symmetry::kMaxRadial)) || radial != std::floor(radial)) {
        error = "radial takes a whole number of copies, 1 (off) to " + std::to_string(Symmetry::kMaxRadial);
        return false;
    }
    sym.radial = int(radial);
    sym.radialEnabled = sym.radial > 1;
    std::string axis = argString(a, "radial_axis");
    if (axis.empty()) axis = "y";
    for (char& c : axis) c = char(std::tolower(static_cast<unsigned char>(c)));
    if (axis != "x" && axis != "y" && axis != "z") {
        error = "radial_axis takes x, y or z";
        return false;
    }
    sym.radialAxis = axis[0] - 'x';
    sym.local = argBool(a, "local", false);
    if (sym.local) sym.pivot = symmetryPivot(mesh);
    return true;
}

bool symmetryCopiesArg(const Args& a, const Mesh& mesh, std::vector<SymCopy>& copies, std::string& error) {
    Symmetry sym;
    if (!symmetryArg(a, mesh, sym, error)) return false;
    copies = Sculptor::copies(sym);
    return true;
}

std::vector<ParamSpec> radialSymmetryParams() {
    return {{"radial", ParamSpec::Type::Number,
             "Radial symmetry: this many copies turned evenly around radial_axis (2-64; default 1, off). With "
             "mirror axes too, each mirror image gets its copies.",
             false},
            {"radial_axis", ParamSpec::Type::String, "The axis radial copies turn around: x, y or z (default y).", false},
            {"local", ParamSpec::Type::Bool,
             "Local symmetry: mirror planes and the radial axis go through the Part's centre instead of the world "
             "origin (default false).",
             false}};
}

bool maskClear(Mesh& mesh) {
    if (!mesh.hasMask()) return false;
    const bool changed = std::any_of(mesh.mask.begin(), mesh.mask.end(), [](float m) { return m != 0.0f; });
    mesh.clearMask();
    mesh.markAllDirty();
    return changed;
}

bool maskAll(Mesh& mesh) {
    return maskPass(mesh, [](const std::vector<float>&, uint32_t) { return 1.0f; });
}

bool maskInvert(Mesh& mesh) {
    return maskPass(mesh, [](const std::vector<float>& m, uint32_t v) { return 1.0f - m[v]; });
}

bool maskBlur(Mesh& mesh, int iterations) {
    if (!mesh.hasMask()) return false;
    return repeatPass(mesh, iterations, [&](const std::vector<float>& m, uint32_t v) {
        return 0.5f * m[v] + 0.5f * neighbourAverage(mesh, m, v);
    });
}

bool maskSharpen(Mesh& mesh, int iterations) {
    if (!mesh.hasMask()) return false;
    return repeatPass(mesh, iterations, [&](const std::vector<float>& m, uint32_t v) {
        // Unsharp step (away from the neighbour average) plus a mild contrast boost around 0.5.
        float s = m[v] + (m[v] - neighbourAverage(mesh, m, v));
        return (s - 0.5f) * 1.25f + 0.5f;
    });
}

bool maskGrow(Mesh& mesh, int iterations) {
    if (!mesh.hasMask()) return false;
    return repeatPass(mesh, iterations, [&](const std::vector<float>& m, uint32_t v) {
        float r = m[v];
        for (uint32_t u : mesh.neighbors(v)) r = std::max(r, m[u]);
        return r;
    });
}

bool maskShrink(Mesh& mesh, int iterations) {
    if (!mesh.hasMask()) return false;
    return repeatPass(mesh, iterations, [&](const std::vector<float>& m, uint32_t v) {
        float r = m[v];
        for (uint32_t u : mesh.neighbors(v)) r = std::min(r, m[u]);
        return r;
    });
}

float vertexConcavity(const Mesh& mesh, uint32_t v) {
    auto ring = mesh.neighbors(v);
    if (ring.empty()) return 0.0f;
    const glm::vec3& p = mesh.positions[v];
    glm::vec3 avg(0.0f);
    float edge = 0.0f;
    for (uint32_t u : ring) {
        avg += mesh.positions[u];
        edge += glm::distance(p, mesh.positions[u]);
    }
    avg /= static_cast<float>(ring.size());
    edge /= static_cast<float>(ring.size());
    if (edge <= 1e-12f) return 0.0f;
    return glm::dot(mesh.normals[v], avg - p) / edge;
}

bool maskByCavity(Mesh& mesh, float strength, float threshold) {
    const uint32_t n = static_cast<uint32_t>(mesh.vertexCount());
    if (n == 0) return false;
    strength = std::clamp(strength, 0.0f, 1.0f);
    // One smoothing pass over the raw concavity keeps single noisy vertices out.
    std::vector<float> raw(n), cav(n);
    for (uint32_t v = 0; v < n; ++v) raw[v] = vertexConcavity(mesh, v);
    for (uint32_t v = 0; v < n; ++v) cav[v] = 0.5f * raw[v] + 0.5f * neighbourAverage(mesh, raw, v);
    constexpr float kRamp = 0.15f; // concavity above the threshold at which the mask is full
    return maskPass(mesh, [&](const std::vector<float>&, uint32_t v) {
        return strength * smoothstep01(threshold, threshold + kRamp, cav[v]);
    });
}

bool maskByPolygroup(Mesh& mesh, uint32_t group, bool add) {
    const uint32_t n = static_cast<uint32_t>(mesh.vertexCount());
    if (n == 0) return false;
    std::vector<uint8_t> inGroup(n, 0);
    bool found = false;
    for (uint32_t t = 0; t < mesh.triangleCount(); ++t) {
        if (mesh.polygroupAt(t) != group || mesh.triangleHidden(t)) continue;
        const glm::uvec3& tri = mesh.triangles[t];
        inGroup[tri.x] = inGroup[tri.y] = inGroup[tri.z] = 1;
        found = true;
    }
    if (!found && add) return false;
    return maskPass(mesh, [&](const std::vector<float>& m, uint32_t v) {
        return inGroup[v] ? 1.0f : add ? m[v] : 0.0f;
    });
}

std::vector<uint8_t> borderVertices(const Mesh& mesh, bool polygroups) {
    const uint32_t n = static_cast<uint32_t>(mesh.vertexCount());
    std::vector<uint8_t> border(n, 0);
    poolFor(n, [&](size_t b, size_t e) {
        std::vector<uint32_t> corners;
        for (size_t i = b; i < e; ++i) {
            const uint32_t v = uint32_t(i);
            corners.clear();
            bool mixed = false, seenGroup = false;
            uint32_t group = 0;
            for (uint32_t t : mesh.vertexTriangles(v)) {
                const glm::uvec3& tri = mesh.triangles[t];
                for (int k = 0; k < 3; ++k)
                    if (tri[k] != v) corners.push_back(tri[k]);
                if (!polygroups || mesh.triangleHidden(t)) continue;
                const uint32_t g = mesh.polygroupAt(t);
                if (seenGroup && g != group) mixed = true;
                group = g;
                seenGroup = true;
            }
            if (corners.empty()) continue; // a loose vertex is no border
            // Each edge v-u of a closed surface belongs to two of v's triangles, so u is listed twice; once is open.
            std::sort(corners.begin(), corners.end());
            bool open = false;
            for (size_t c = 0; c < corners.size() && !open;) {
                size_t d = c + 1;
                while (d < corners.size() && corners[d] == corners[c]) ++d;
                open = d - c == 1;
                c = d;
            }
            border[v] = (open || mixed) ? 1 : 0;
        }
    });
    return border;
}

bool maskBorder(Mesh& mesh, int rings, bool polygroups, bool soft, float strength, bool add, size_t* found) {
    const uint32_t n = static_cast<uint32_t>(mesh.vertexCount());
    if (found) *found = 0;
    if (n == 0) return false;
    rings = std::clamp(rings, 1, 20);
    strength = std::clamp(strength, 0.0f, 1.0f);
    // Ring index of each vertex from the border (0 = on it), out to rings - 1; -1 beyond.
    const std::vector<uint8_t> border = borderVertices(mesh, polygroups);
    std::vector<int8_t> ring(n, -1);
    std::vector<uint32_t> frontier, next;
    for (uint32_t v = 0; v < n; ++v)
        if (border[v]) {
            ring[v] = 0;
            frontier.push_back(v);
        }
    if (found) *found = frontier.size();
    if (frontier.empty()) return false;
    for (int r = 1; r < rings && !frontier.empty(); ++r) {
        next.clear();
        for (uint32_t v : frontier)
            for (uint32_t u : mesh.neighbors(v))
                if (ring[u] < 0) {
                    ring[u] = int8_t(r);
                    next.push_back(u);
                }
        frontier.swap(next);
    }
    return maskPass(mesh, [&](const std::vector<float>& m, uint32_t v) {
        float value = 0.0f;
        if (ring[v] >= 0) value = strength * (soft ? 1.0f - float(ring[v]) / float(rings) : 1.0f);
        return add ? std::max(m[v], value) : value;
    });
}

namespace {

// Vertex v, or one of its mirror images, projects inside the shape.
bool inShape(const Mesh& mesh, const glm::mat4& viewProj, const ScreenPolygon& shape,
             const std::vector<SymCopy>& mirrors, uint32_t v, const glm::mat4* invViewProj = nullptr) {
    // With `invViewProj` (Front faces only), the point's normal must face the viewer: against the view ray
    // through it, which works for perspective and orthographic views alike.
    auto facing = [&](const glm::vec3& n, glm::vec2 ndc) {
        if (!invViewProj || v >= mesh.normals.size()) return true;
        const glm::vec4 a = *invViewProj * glm::vec4(ndc, -1.0f, 1.0f), b = *invViewProj * glm::vec4(ndc, 1.0f, 1.0f);
        return glm::dot(n, glm::vec3(b) / b.w - glm::vec3(a) / a.w) < 0.0f;
    };
    glm::vec2 ndc;
    const glm::vec3 n = v < mesh.normals.size() ? mesh.normals[v] : glm::vec3(0.0f);
    if (projectToNdc(viewProj, mesh.positions[v], ndc) && shape.contains(ndc) && facing(n, ndc)) return true;
    for (const SymCopy& m : mirrors)
        if (!m.identity() && projectToNdc(viewProj, m.point(mesh.positions[v]), ndc) && shape.contains(ndc) &&
            facing(m.dir(n), ndc))
            return true;
    return false;
}

} // namespace

bool maskInShape(Mesh& mesh, const glm::mat4& viewProj, const ScreenPolygon& shape, float strength, bool unmask,
                 const std::vector<SymCopy>& mirrors, bool frontFacesOnly) {
    if (mesh.vertexCount() == 0 || !shape.valid()) return false;
    const float s = std::clamp(strength, 0.0f, 1.0f);
    const glm::mat4 inv = glm::inverse(viewProj);
    return maskPass(mesh, [&](const std::vector<float>& m, uint32_t v) {
        if (!inShape(mesh, viewProj, shape, mirrors, v, frontFacesOnly ? &inv : nullptr)) return m[v];
        return unmask ? m[v] * (1.0f - s) : m[v] + (1.0f - m[v]) * s;
    });
}

bool shapeCoversMesh(const Mesh& mesh, const glm::mat4& viewProj, const ScreenPolygon& shape,
                     const std::vector<SymCopy>& mirrors) {
    if (!shape.valid()) return false;
    const bool anyHidden = mesh.hasHidden();
    for (uint32_t v = 0; v < uint32_t(mesh.vertexCount()); ++v)
        if (!(anyHidden && mesh.vertexHidden(v)) && inShape(mesh, viewProj, shape, mirrors, v)) return true;
    return false;
}

void Sculptor::dabMask(Mesh& mesh, const BVH& bvh, const DabParams& p, Symmetry sym) {
    if (!active_) return;
    const float s = std::clamp(p.strength, 0.0f, 1.0f) * std::clamp(p.pressure, 0.0f, 1.0f);
    if (s <= 0.0f) return;
    if (microReady(mesh)) { // the micro level's own mask, per grid point (core/MicroSculpt.cpp)
        const double k0 = micro_.findMs + micro_.kernelMs + micro_.stitchMs;
        for (const SymCopy& mirror : copies(sym))
            micro_.dab(mesh, bvh, BrushType::Standard, mirror.point(p.center), mirror.dir(p.viewDir), p.radius, 1.0f, p.invert,
                       p.focal, mirror.dir(p.alphaUp), true, s);
        timings.kernelMs += micro_.findMs + micro_.kernelMs + micro_.stitchMs - k0;
        ++timings.dabs;
        return;
    }
    auto clock = std::chrono::steady_clock::now();
    std::vector<float>& mask = mesh.ensureMask();
    for (const SymCopy& mirror : copies(sym)) {
        const glm::vec3 center = mirror.point(p.center), viewDir = mirror.dir(p.viewDir);
        brushRegion(mesh, bvh, center, viewDir, p.radius, p.connectedOnly, p.viewFalloff, verts_, connected_);
        // New values in parallel; recording and writing them in order.
        weights_.resize(verts_.size());
        poolFor(verts_.size(), [&](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) {
                const uint32_t v = verts_[i];
                weights_[i] = -1.0f; // unchanged
                if (p.frontFacesOnly && glm::dot(mesh.normals[v], viewDir) > 0.0f) continue;
                const float a = s * brushFalloff(brushDistance(mesh.positions[v], center, viewDir, p.viewFalloff), p.radius, p.focal);
                if (a <= 0.0f) continue;
                const float m = p.invert ? mask[v] * (1.0f - a) : mask[v] + (1.0f - mask[v]) * a;
                if (m != mask[v]) weights_[i] = m;
            }
        });
        for (size_t i = 0; i < verts_.size(); ++i) {
            if (weights_[i] < 0.0f) continue;
            const uint32_t v = verts_[i];
            recordMask(mesh, v);
            mask[v] = weights_[i];
            mesh.markDirty(v);
        }
        timings.vertices += verts_.size();
    }
    timings.kernelMs += lapMs(clock);
    ++timings.dabs;
}

void registerMaskingCommands(CommandRegistry& registry) {
    using T = ParamSpec::Type;
    // Wraps a mask operation as an undoable command.
    auto add = [&registry](std::string name, std::string label, std::string description, std::vector<ParamSpec> params,
                           std::function<bool(Mesh&, const Args&)> op) {
        registry.add({std::move(name), std::move(description), std::move(params),
                      [label, op](Document& doc, const Args& a) -> CommandResult {
                          if (doc.mesh.vertexCount() == 0) return {false, "The mesh is empty"};
                          UndoRecord record = UndoRecord::captureMask(doc.mesh, label);
                          if (!op(doc.mesh, a)) return {true, label + ": mask unchanged"};
                          doc.undo.push(std::move(record));
                          doc.modified = true;
                          return {true, label};
                      }});
    };
    const ParamSpec iterations{"iterations", T::Number, "Number of passes (1-100). Default 1.", false};
    auto iters = [](const Args& a) { return std::clamp(int(argNumber(a, "iterations", 1)), 1, 100); };

    add("mask_clear", "Clear Mask", "Remove the mask so every vertex can be sculpted again.", {},
        [](Mesh& m, const Args&) { return maskClear(m); });
    add("mask_all", "Mask All", "Mask every visible vertex (protects the whole mesh from brushes).", {},
        [](Mesh& m, const Args&) { return maskAll(m); });
    add("mask_invert", "Invert Mask", "Invert the mask of visible vertices: masked becomes free and vice versa.", {},
        [](Mesh& m, const Args&) { return maskInvert(m); });
    add("mask_blur", "Blur Mask", "Soften the mask edges by averaging with neighbouring vertices.", {iterations},
        [iters](Mesh& m, const Args& a) { return maskBlur(m, iters(a)); });
    add("mask_sharpen", "Sharpen Mask", "Tighten soft mask edges, pushing values towards fully masked or free.",
        {iterations}, [iters](Mesh& m, const Args& a) { return maskSharpen(m, iters(a)); });
    add("mask_grow", "Grow Mask", "Expand the masked region by one edge ring per iteration.", {iterations},
        [iters](Mesh& m, const Args& a) { return maskGrow(m, iters(a)); });
    add("mask_shrink", "Shrink Mask", "Contract the masked region by one edge ring per iteration.", {iterations},
        [iters](Mesh& m, const Args& a) { return maskShrink(m, iters(a)); });
    add("mask_by_cavity", "Mask by Cavity",
        "Mask concave areas (creases, folds, pores), replacing the current mask - e.g. to protect detail while smoothing.",
        {{"strength", T::Number, "Mask value in the deepest cavities (0-1). Default 1.", false},
         {"threshold", T::Number,
          "Concavity above which vertices get masked (-1 to 1; 0 = anything concave, higher = only deep creases). "
          "Default 0.02.",
          false}},
        [](Mesh& m, const Args& a) {
            return maskByCavity(m, float(argNumber(a, "strength", 1.0)),
                                std::clamp(float(argNumber(a, "threshold", 0.02)), -1.0f, 1.0f));
        });
    add("mask_by_polygroup", "Mask by Face Group",
        "Mask the vertices of one face group (meshes without face groups are all group 0).",
        {{"group", T::Number, "Face Group id.", true},
         {"add", T::Bool, "Add to the current mask instead of replacing it. Default false.", false}},
        [](Mesh& m, const Args& a) {
            return maskByPolygroup(m, uint32_t(std::max(0.0, argNumber(a, "group", 0))), argBool(a, "add", false));
        });

    registry.add(
        {"mask_border",
         "Mask from the mesh border (Blender: Mask from mesh boundary): mask the vertices on open edges - and, with "
         "face groups, where face groups meet - plus edge rings inside them, so sculpting keeps the border in place. "
         "Replaces the mask unless add is set. A closed mesh has no open edges and masks nothing.",
         {{"rings", T::Number, "Width of the mask in edge rings from the border (1-20; 1 = the border vertices only). "
                               "Default 1.",
           false},
          {"polygroups", T::Bool, "Also mask face group borders (ZBrush: Mask by polygroup border). Default false.",
           false},
          {"soft", T::Bool, "Fade the mask from the border towards the innermost ring. Default false.", false},
          {"strength", T::Number, "Mask value on the border (0-1). Default 1.", false},
          {"add", T::Bool, "Add to the current mask instead of replacing it. Default false.", false}},
         [](Document& doc, const Args& a) -> CommandResult {
             if (doc.mesh.vertexCount() == 0) return {false, "The mesh is empty"};
             const std::string label = "Mask Border";
             const bool polygroups = argBool(a, "polygroups", false);
             UndoRecord record = UndoRecord::captureMask(doc.mesh, label);
             size_t found = 0;
             const bool changed =
                 maskBorder(doc.mesh, int(argNumber(a, "rings", 1)), polygroups, argBool(a, "soft", false),
                            float(argNumber(a, "strength", 1.0)), argBool(a, "add", false), &found);
             if (found == 0)
                 return {true, polygroups ? "No open edges or face group borders: the mesh is closed and one face group"
                                          : "No open edges: the mesh is closed"};
             if (!changed) return {true, label + ": mask unchanged"};
             doc.undo.push(std::move(record));
             doc.modified = true;
             return {true, label + ": " + std::to_string(found) + " border vertices"};
         }});

    // MaskRect / MaskLasso. Bad arguments are errors, unlike the "mask unchanged" of a selection that misses.
    const ParamSpec unmask{"unmask", T::Bool, "Unmask the inside instead (ZBrush: Alt held). Default false.", false};
    const ParamSpec strength{"strength", T::Number, "How much to mask or unmask (0-1). Default 1.", false};
    const ParamSpec viewProj{"view_proj", T::String, "Optional 16 numbers: column-major view-projection matrix.",
                             false};
    const ParamSpec symmetry{"symmetry", T::String,
                             "Mirror axes, e.g. \"x\" or \"xz\": the mirror image of the shape is selected too, as the "
                             "brushes mirror (default none).",
                             false};
    const ParamSpec clearOutside{"clear_outside", T::Bool,
                                 "When the shape covers none of the model, clear the mask instead (ZBrush: Ctrl+drag on "
                                 "the background). Default false.",
                                 false};
    const ParamSpec frontFaces{"front_faces_only", T::Bool,
                               "Only vertices facing the viewer: the far side of the model, and folds turned away, "
                               "are left as they are (#91.3). Default false.",
                               false};
    auto selection = [&registry](std::string name, std::string description, std::vector<ParamSpec> params,
                                 std::function<bool(const Args&, std::vector<glm::vec2>&, std::string&)> shape) {
        registry.add(
            {std::move(name), std::move(description), std::move(params),
             [shape](Document& doc, const Args& a) -> CommandResult {
                 if (doc.mesh.vertexCount() == 0) return {false, "The mesh is empty"};
                 glm::mat4 vp;
                 std::vector<glm::vec2> points;
                 std::string error;
                 if (!viewProjArg(doc.mesh, a, vp, error) || !shape(a, points, error)) return {false, error};
                 std::vector<SymCopy> mirrors;
                 if (!symmetryCopiesArg(a, doc.mesh, mirrors, error)) return {false, error};
                 const ScreenPolygon polygon(points);
                 const bool unmask = argBool(a, "unmask", false);
                 if (argBool(a, "clear_outside", false) && !shapeCoversMesh(doc.mesh, vp, polygon, mirrors)) {
                     UndoRecord record = UndoRecord::captureMask(doc.mesh, "Clear Mask");
                     if (!maskClear(doc.mesh)) return {true, "Clear Mask: mask unchanged"};
                     doc.undo.push(std::move(record));
                     doc.modified = true;
                     return {true, "Clear Mask"};
                 }
                 const std::string label = unmask ? "Unmask" : "Mask";
                 UndoRecord record = UndoRecord::captureMask(doc.mesh, label);
                 if (!maskInShape(doc.mesh, vp, polygon, float(argNumber(a, "strength", 1.0)), unmask, mirrors,
                                  argBool(a, "front_faces_only", false)))
                     return {true, label + ": mask unchanged"};
                 doc.undo.push(std::move(record));
                 doc.modified = true;
                 return {true, label};
             }});
    };
    selection(
        "mask_rect",
        "Rectangle mask selection (ZBrush MaskRect, Ctrl+drag): mask the visible vertices inside a screen rectangle, "
        "or unmask them. Coordinates are normalised (-1..1, y up) in the given view; without a view the rectangle "
        "spans the mesh seen from the front (+Z).",
        {{"x0", T::Number, "Rectangle left (-1..1). Default -1.", false},
         {"y0", T::Number, "Rectangle bottom (-1..1). Default -1.", false},
         {"x1", T::Number, "Rectangle right (-1..1). Default 1.", false},
         {"y1", T::Number, "Rectangle top (-1..1). Default 1.", false},
         unmask,
         strength,
         viewProj,
         symmetry,
         clearOutside,
         frontFaces},
        [](const Args& a, std::vector<glm::vec2>& points, std::string&) {
            const float x0 = float(argNumber(a, "x0", -1)), y0 = float(argNumber(a, "y0", -1));
            const float x1 = float(argNumber(a, "x1", 1)), y1 = float(argNumber(a, "y1", 1));
            points = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
            return true;
        });
    selection(
        "mask_lasso",
        "Lasso mask selection (ZBrush MaskLasso, Ctrl+drag): mask the visible vertices inside a screen polygon, or "
        "unmask them. Points are normalised (-1..1, y up) in the given view; without a view they span the mesh "
        "seen from the front (+Z).",
        {{"points", T::String, "The lasso: at least 3 points as \"x0 y0 x1 y1 ...\" (-1..1, y up).", true},
         unmask,
         strength,
         viewProj,
         symmetry,
         clearOutside,
         frontFaces},
        lassoArg);
}

} // namespace sf
