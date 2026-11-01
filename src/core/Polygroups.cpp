// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Polygroups.h"

#include "core/Commands.h"
#include "core/Masking.h"
#include "core/ScreenSelect.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>

namespace sf {

namespace {

constexpr uint32_t kUnassigned = ~0u;

glm::vec3 faceNormal(const Mesh& m, uint32_t t) {
    const glm::uvec3& tri = m.triangles[t];
    const glm::vec3 n = glm::cross(m.positions[tri.y] - m.positions[tri.x], m.positions[tri.z] - m.positions[tri.x]);
    const float len = glm::length(n);
    return len > 0.0f ? n / len : glm::vec3(0.0f);
}

// Calls fn(u) for every triangle sharing an edge with `t` (needs topology).
template <class Fn>
void forEachEdgeNeighbor(const Mesh& m, uint32_t t, Fn fn) {
    const glm::uvec3& tri = m.triangles[t];
    for (int k = 0; k < 3; ++k) {
        const uint32_t a = tri[k], b = tri[(k + 1) % 3];
        for (uint32_t u : m.vertexTriangles(a)) {
            if (u == t) continue;
            const glm::uvec3& o = m.triangles[u];
            if (o.x == b || o.y == b || o.z == b) fn(u);
        }
    }
}

// First id for regrouping the visible triangles: above every id a hidden triangle keeps.
uint32_t firstIdForVisible(const Mesh& m) {
    uint32_t id = 0;
    for (uint32_t t = 0; t < m.triangleCount(); ++t)
        if (m.triangleHidden(t)) id = std::max(id, m.polygroupAt(t) + 1);
    return id;
}

std::vector<uint32_t> currentGroups(const Mesh& m) {
    return m.hasPolygroups() ? m.polygroups : std::vector<uint32_t>(m.triangleCount(), 0u);
}

std::vector<uint8_t> currentHidden(const Mesh& m) {
    return m.hasHidden() ? m.hidden : std::vector<uint8_t>(m.triangleCount(), uint8_t(0));
}

// Flood-fills visible triangles into regions; `accept(seed, candidate)` decides
// whether a neighbour joins the seed's region.
template <class Accept>
std::vector<uint32_t> floodGroups(const Mesh& m, Accept accept) {
    std::vector<uint32_t> groups = currentGroups(m);
    std::vector<uint32_t> region(m.triangleCount(), kUnassigned);
    uint32_t next = firstIdForVisible(m);
    std::vector<uint32_t> stack;
    for (uint32_t seed = 0; seed < m.triangleCount(); ++seed) {
        if (region[seed] != kUnassigned || m.triangleHidden(seed)) continue;
        const uint32_t id = next++;
        region[seed] = id;
        stack.assign(1, seed);
        while (!stack.empty()) {
            const uint32_t t = stack.back();
            stack.pop_back();
            forEachEdgeNeighbor(m, t, [&](uint32_t u) {
                if (region[u] != kUnassigned || m.triangleHidden(u) || !accept(seed, t, u)) return;
                region[u] = id;
                stack.push_back(u);
            });
        }
    }
    for (uint32_t t = 0; t < m.triangleCount(); ++t)
        if (region[t] != kUnassigned) groups[t] = region[t];
    return groups;
}

// Hidden flags that would leave nothing visible are refused (empty result).
std::vector<uint8_t> unlessAllHidden(std::vector<uint8_t> hidden) {
    if (!hidden.empty() && std::all_of(hidden.begin(), hidden.end(), [](uint8_t h) { return h != 0; })) return {};
    return hidden;
}

// Writes proposed per-triangle values, recording only the triangles that
// change. Frees the attribute again when it is back to its default (all 0).
// Returns the number of changed triangles (0 = nothing pushed).
size_t commitHidden(Document& doc, const std::vector<uint8_t>& proposed, const std::string& label) {
    Mesh& m = doc.mesh;
    UndoRecord r;
    r.label = label;
    for (uint32_t t = 0; t < m.triangleCount(); ++t) {
        const uint8_t old = uint8_t(m.triangleHidden(t));
        if (old != (proposed[t] ? 1 : 0)) r.hidden.add(t, old);
    }
    if (r.hidden.empty()) return 0;
    m.ensureHidden();
    for (uint32_t t : r.hidden.indices) m.hidden[t] = uint8_t(proposed[t] ? 1 : 0);
    if (std::all_of(m.hidden.begin(), m.hidden.end(), [](uint8_t h) { return h == 0; })) m.clearHidden();
    m.facesDirty = true;
    const size_t changed = r.hidden.indices.size();
    doc.undo.push(std::move(r));
    doc.modified = true;
    return changed;
}

size_t commitPolygroups(Document& doc, const std::vector<uint32_t>& proposed, const std::string& label) {
    Mesh& m = doc.mesh;
    UndoRecord r;
    r.label = label;
    for (uint32_t t = 0; t < m.triangleCount(); ++t) {
        const uint32_t old = m.polygroupAt(t);
        if (old != proposed[t]) r.polygroups.add(t, old);
    }
    if (r.polygroups.empty()) return 0;
    m.ensurePolygroups();
    for (uint32_t t : r.polygroups.indices) m.polygroups[t] = proposed[t];
    if (std::all_of(m.polygroups.begin(), m.polygroups.end(), [](uint32_t g) { return g == 0; })) m.clearPolygroups();
    m.facesDirty = true;
    const size_t changed = r.polygroups.indices.size();
    doc.undo.push(std::move(r));
    doc.modified = true;
    return changed;
}

std::string visibleSummary(const Mesh& m) {
    size_t visible = 0;
    for (uint32_t t = 0; t < m.triangleCount(); ++t) visible += m.triangleHidden(t) ? 0 : 1;
    return std::to_string(visible) + " of " + std::to_string(m.triangleCount()) + " triangles visible";
}

std::string groupSummary(const Mesh& m) {
    return std::to_string(visiblePolygroupCount(m)) + " visible face groups";
}

bool groupExists(const Mesh& m, uint32_t group) {
    for (uint32_t t = 0; t < m.triangleCount(); ++t)
        if (m.polygroupAt(t) == group) return true;
    return false;
}

// Hides or keeps the visible triangles by whether their centre projects inside a screen shape.
template <class Inside>
std::vector<uint8_t> hiddenByShape(const Mesh& mesh, const glm::mat4& viewProj, Inside inside, bool hideInside,
                                   const std::vector<SymCopy>& mirrors) {
    std::vector<uint8_t> hidden = currentHidden(mesh);
    for (uint32_t t = 0; t < mesh.triangleCount(); ++t) {
        if (hidden[t]) continue;
        const glm::uvec3& tri = mesh.triangles[t];
        const glm::vec3 c = (mesh.positions[tri.x] + mesh.positions[tri.y] + mesh.positions[tri.z]) / 3.0f;
        glm::vec2 ndc;
        bool in = projectToNdc(viewProj, c, ndc) && inside(ndc);
        for (size_t k = 0; k < mirrors.size() && !in; ++k) // or its mirror image, with symmetry (#51)
            in = !mirrors[k].identity() && projectToNdc(viewProj, mirrors[k].point(c), ndc) && inside(ndc);
        if (in == hideInside) hidden[t] = 1;
    }
    return unlessAllHidden(std::move(hidden));
}

} // namespace

uint32_t nextPolygroupId(const Mesh& mesh) {
    if (mesh.triangleCount() == 0) return 0;
    uint32_t id = 1; // everything is at least in the default group 0
    for (uint32_t g : mesh.polygroups) id = std::max(id, g + 1);
    return id;
}

size_t visiblePolygroupCount(const Mesh& mesh) {
    std::set<uint32_t> ids;
    for (uint32_t t = 0; t < mesh.triangleCount(); ++t)
        if (!mesh.triangleHidden(t)) ids.insert(mesh.polygroupAt(t));
    return ids.size();
}

std::vector<uint32_t> groupsByNormals(const Mesh& mesh, float maxAngleDegrees) {
    std::vector<glm::vec3> normals(mesh.triangleCount());
    for (uint32_t t = 0; t < mesh.triangleCount(); ++t) normals[t] = faceNormal(mesh, t);
    const float minDot = std::cos(glm::radians(std::clamp(maxAngleDegrees, 0.0f, 180.0f))) - 1e-5f;
    return floodGroups(mesh, [&](uint32_t seed, uint32_t, uint32_t u) {
        // Degenerate slivers have no normal; let them join their neighbours.
        if (normals[u] == glm::vec3(0.0f) || normals[seed] == glm::vec3(0.0f)) return true;
        return glm::dot(normals[seed], normals[u]) >= minDot;
    });
}

std::vector<uint32_t> groupsByConnectivity(const Mesh& mesh) {
    return floodGroups(mesh, [](uint32_t, uint32_t, uint32_t) { return true; });
}

std::vector<uint32_t> groupsByUvs(const Mesh& mesh) {
    if (!mesh.hasUvs()) return {};
    // Neighbours join when the corners at both shared vertices have the same UVs (no seam between them).
    return floodGroups(mesh, [&](uint32_t, uint32_t t, uint32_t u) {
        const glm::uvec3& a = mesh.triangles[t];
        const glm::uvec3& b = mesh.triangles[u];
        int shared = 0;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) {
                if (a[i] != b[j]) continue;
                if (glm::any(glm::greaterThan(glm::abs(mesh.uvs[3 * size_t(t) + size_t(i)] - mesh.uvs[3 * size_t(u) + size_t(j)]),
                                              glm::vec2(1e-6f))))
                    return false;
                ++shared;
            }
        return shared >= 2;
    });
}

std::vector<uint32_t> groupsFromMask(const Mesh& mesh, size_t* changed) {
    std::vector<uint32_t> groups = currentGroups(mesh);
    const uint32_t id = nextPolygroupId(mesh);
    size_t n = 0;
    if (mesh.hasMask()) {
        for (uint32_t t = 0; t < mesh.triangleCount(); ++t) {
            if (mesh.triangleHidden(t)) continue;
            const glm::uvec3& tri = mesh.triangles[t];
            if (mesh.maskAt(tri.x) + mesh.maskAt(tri.y) + mesh.maskAt(tri.z) >= 1.5f) {
                groups[t] = id;
                ++n;
            }
        }
    }
    if (changed) *changed = n;
    return groups;
}

std::vector<uint32_t> groupsMergeVisible(const Mesh& mesh) {
    std::vector<uint32_t> groups = currentGroups(mesh);
    uint32_t id = kUnassigned;
    for (uint32_t t = 0; t < mesh.triangleCount(); ++t)
        if (!mesh.triangleHidden(t)) id = std::min(id, groups[t]);
    if (id == kUnassigned) return groups;
    for (uint32_t t = 0; t < mesh.triangleCount(); ++t)
        if (!mesh.triangleHidden(t)) groups[t] = id;
    return groups;
}

std::vector<uint8_t> hiddenHideGroup(const Mesh& mesh, uint32_t group) {
    std::vector<uint8_t> hidden = currentHidden(mesh);
    for (uint32_t t = 0; t < mesh.triangleCount(); ++t)
        if (mesh.polygroupAt(t) == group) hidden[t] = 1;
    return unlessAllHidden(std::move(hidden));
}

std::vector<uint8_t> hiddenIsolateGroup(const Mesh& mesh, uint32_t group) {
    std::vector<uint8_t> hidden(mesh.triangleCount());
    for (uint32_t t = 0; t < mesh.triangleCount(); ++t) hidden[t] = uint8_t(mesh.polygroupAt(t) == group ? 0 : 1);
    return unlessAllHidden(std::move(hidden));
}

std::vector<uint8_t> hiddenInverted(const Mesh& mesh) {
    std::vector<uint8_t> hidden(mesh.triangleCount());
    for (uint32_t t = 0; t < mesh.triangleCount(); ++t) hidden[t] = uint8_t(mesh.triangleHidden(t) ? 0 : 1);
    return unlessAllHidden(std::move(hidden));
}

std::vector<uint8_t> hiddenByRect(const Mesh& mesh, const glm::mat4& viewProj, glm::vec2 rectMin, glm::vec2 rectMax,
                                  bool hideInside, const std::vector<SymCopy>& mirrors) {
    const glm::vec2 lo = glm::min(rectMin, rectMax), hi = glm::max(rectMin, rectMax);
    return hiddenByShape(
        mesh, viewProj, [&](glm::vec2 p) { return p.x >= lo.x && p.x <= hi.x && p.y >= lo.y && p.y <= hi.y; },
        hideInside, mirrors);
}

std::vector<uint8_t> hiddenByLasso(const Mesh& mesh, const glm::mat4& viewProj, const std::vector<glm::vec2>& lasso,
                                   bool hideInside, const std::vector<SymCopy>& mirrors) {
    const ScreenPolygon polygon(lasso);
    return hiddenByShape(mesh, viewProj, [&](glm::vec2 p) { return polygon.contains(p); }, hideInside, mirrors);
}

bool polygroupIsolated(const Mesh& mesh, uint32_t group) {
    bool any = false;
    for (uint32_t t = 0; t < mesh.triangleCount(); ++t) {
        const bool in = mesh.polygroupAt(t) == group;
        if (in == mesh.triangleHidden(t)) return false;
        any = any || in;
    }
    return any;
}

Mesh deleteHiddenTriangles(const Mesh& mesh) {
    if (!mesh.hasHidden()) return mesh;
    std::vector<uint8_t> keep(mesh.triangleCount());
    for (uint32_t t = 0; t < keep.size(); ++t) keep[t] = !mesh.triangleHidden(t);
    return extractTriangles(mesh, keep);
}

Mesh extractTriangles(const Mesh& mesh, const std::vector<uint8_t>& keep) {
    const size_t nv = mesh.vertexCount(), nt = mesh.triangleCount();
    std::vector<uint32_t> remap(nv, kUnassigned);
    std::vector<uint32_t> kept;
    Mesh out;
    bool anyHidden = false;
    std::vector<uint32_t> newTriangle(mesh.hasQuads() ? nt : 0, Mesh::kNoQuad);
    for (uint32_t t = 0; t < nt; ++t) {
        if (t >= keep.size() || !keep[t]) continue;
        if (!newTriangle.empty()) newTriangle[t] = uint32_t(out.triangles.size());
        glm::uvec3 tri;
        for (int k = 0; k < 3; ++k) {
            uint32_t& r = remap[mesh.triangles[t][k]];
            if (r == kUnassigned) {
                r = static_cast<uint32_t>(kept.size());
                kept.push_back(mesh.triangles[t][k]);
            }
            tri[k] = r;
        }
        out.triangles.push_back(tri);
        out.hidden.push_back(mesh.triangleHidden(t) ? 1 : 0);
        anyHidden |= mesh.triangleHidden(t);
        if (mesh.hasPolygroups()) out.polygroups.push_back(mesh.polygroups[t]);
        if (mesh.hasUvs()) out.uvs.insert(out.uvs.end(), mesh.uvs.begin() + std::ptrdiff_t(t) * 3,
                                          mesh.uvs.begin() + std::ptrdiff_t(t) * 3 + 3);
    }
    out.positions.reserve(kept.size());
    for (uint32_t v : kept) out.positions.push_back(mesh.positions[v]);
    if (mesh.hasUvs()) { // the painted texture still fits the kept UVs (shared until one side paints)
        out.texture = mesh.texture;
        out.paintToTexture = mesh.paintToTexture;
    }
    if (mesh.hasMask())
        for (uint32_t v : kept) out.mask.push_back(mesh.mask[v]);
    if (mesh.hasColors())
        for (uint32_t v : kept) out.colors.push_back(mesh.colors[v]);
    if (!mesh.layers.empty()) {
        for (uint32_t v : kept) out.base.push_back(mesh.base[v]);
        for (const auto& l : mesh.layers) {
            SculptLayer nl{l.name, {}, l.weight};
            nl.delta.reserve(kept.size());
            for (uint32_t v : kept) nl.delta.push_back(l.delta[v]);
            out.layers.push_back(std::move(nl));
        }
    }
    copyPaintLayers(mesh, out, kept);
    if (!anyHidden) out.clearHidden();
    remapQuads(mesh, newTriangle, out); // a quad loses its pairing when only one half is kept
    out.rebuildTopology();
    out.recomputeAllNormals();
    return out;
}

void registerPolygroupCommands(CommandRegistry& reg) {
    using T = ParamSpec::Type;

    // --- Polygroups ---
    reg.add(
        {"polygroup_auto_by_normals",
         "Split the visible surface into face groups wherever it turns more than an angle (each flat side of a "
         "box becomes a group), like ZBrush Auto Groups / Groups By Normals.",
         {{"angle", T::Number, "Largest normal deviation inside one group, in degrees (1-180). Default 30.", false}},
         [](Document& doc, const Args& a) -> CommandResult {
             const float angle = float(std::clamp(argNumber(a, "angle", 30.0), 1.0, 180.0));
             commitPolygroups(doc, groupsByNormals(doc.mesh, angle), "Groups by Normals");
             return {true, groupSummary(doc.mesh)};
         }});

    reg.add({"polygroup_by_connectivity",
             "Give every disconnected piece of the visible mesh its own face group (like ZBrush Auto Groups).",
             {},
             [](Document& doc, const Args&) -> CommandResult {
                 commitPolygroups(doc, groupsByConnectivity(doc.mesh), "Groups by Connectivity");
                 return {true, groupSummary(doc.mesh)};
             }});

    reg.add({"polygroup_uv",
             "Give every UV island of the visible mesh its own face group (ZBrush Polygroups > UV Groups).", {},
             [](Document& doc, const Args&) -> CommandResult {
                 if (!doc.mesh.hasUvs()) return {false, "This mesh has no UVs: unwrap it first (Tool > UV Map)"};
                 commitPolygroups(doc, groupsByUvs(doc.mesh), "Group by UVs");
                 return {true, groupSummary(doc.mesh)};
             }});

    reg.add({"polygroup_from_mask",
             "Move the masked (visible) part of the mesh into a new face group, like ZBrush Group Masked.", {},
             [](Document& doc, const Args&) -> CommandResult {
                 size_t n = 0;
                 std::vector<uint32_t> groups = groupsFromMask(doc.mesh, &n);
                 if (n == 0) return {false, "Nothing is masked"};
                 const uint32_t id = nextPolygroupId(doc.mesh);
                 commitPolygroups(doc, groups, "Group Masked");
                 return {true, "Face Group " + std::to_string(id) + " with " + std::to_string(n) + " masked triangles"};
             }});

    reg.add({"polygroup_clear", "Remove all face groups (everything returns to one group).", {},
             [](Document& doc, const Args&) -> CommandResult {
                 commitPolygroups(doc, std::vector<uint32_t>(doc.mesh.triangleCount(), 0u), "Clear Face Groups");
                 return {true, "Face Groups cleared"};
             }});

    reg.add({"polygroup_merge_visible", "Merge everything visible into a single face group (ZBrush Group Visible).", {},
             [](Document& doc, const Args&) -> CommandResult {
                 commitPolygroups(doc, groupsMergeVisible(doc.mesh), "Group Visible");
                 return {true, groupSummary(doc.mesh)};
             }});

    // --- Visibility ---
    reg.add({"hide_polygroup", "Hide one face group (ZBrush Ctrl+Shift+Alt+click).",
             {{"group", T::Number, "Face Group id.", true}},
             [](Document& doc, const Args& a) -> CommandResult {
                 const uint32_t group = uint32_t(std::max(0.0, argNumber(a, "group", 0)));
                 if (!groupExists(doc.mesh, group)) return {false, "No face group " + std::to_string(group)};
                 std::vector<uint8_t> hidden = hiddenHideGroup(doc.mesh, group);
                 if (hidden.empty()) return {false, "Hiding face group " + std::to_string(group) + " would hide everything"};
                 commitHidden(doc, hidden, "Hide Face Group");
                 return {true, visibleSummary(doc.mesh)};
             }});

    reg.add({"isolate_polygroup", "Show only one face group and hide the rest (ZBrush Ctrl+Shift+click).",
             {{"group", T::Number, "Face Group id.", true}},
             [](Document& doc, const Args& a) -> CommandResult {
                 const uint32_t group = uint32_t(std::max(0.0, argNumber(a, "group", 0)));
                 std::vector<uint8_t> hidden = hiddenIsolateGroup(doc.mesh, group);
                 if (hidden.empty()) return {false, "No face group " + std::to_string(group)};
                 commitHidden(doc, hidden, "Isolate Face Group");
                 return {true, visibleSummary(doc.mesh)};
             }});

    reg.add({"show_all", "Show every hidden part of the mesh again.", {},
             [](Document& doc, const Args&) -> CommandResult {
                 commitHidden(doc, std::vector<uint8_t>(doc.mesh.triangleCount(), uint8_t(0)), "Show All");
                 return {true, visibleSummary(doc.mesh)};
             }});

    reg.add({"invert_visibility", "Hide what is visible and show what is hidden.", {},
             [](Document& doc, const Args&) -> CommandResult {
                 std::vector<uint8_t> hidden = hiddenInverted(doc.mesh);
                 if (hidden.empty()) return {false, "Nothing is hidden"};
                 commitHidden(doc, hidden, "Invert Visibility");
                 return {true, visibleSummary(doc.mesh)};
             }});

    const ParamSpec symmetryParam{"symmetry", T::String,
                                  "Mirror axes, e.g. \"x\" or \"xz\": the mirror image of the shape counts as inside "
                                  "too, as the brushes mirror (#51; default none).",
                                  false};
    reg.add({"hide_rect",
             "Rectangle selection (ZBrush Ctrl+Shift+drag): keep the visible triangles inside a screen rectangle and "
             "hide the rest, or hide the inside instead. Coordinates are normalised (-1..1, y up) in the given view; "
             "without a view the rectangle spans the mesh seen from the front (+Z).",
             {{"x0", T::Number, "Rectangle left (-1..1). Default -1.", false},
              {"y0", T::Number, "Rectangle bottom (-1..1). Default -1.", false},
              {"x1", T::Number, "Rectangle right (-1..1). Default 1.", false},
              {"y1", T::Number, "Rectangle top (-1..1). Default 1.", false},
              {"inside", T::Bool, "Hide the inside instead of the outside (ZBrush: Alt held).", false},
              {"view_proj", T::String, "Optional 16 numbers: column-major view-projection matrix.", false},
              symmetryParam},
             [](Document& doc, const Args& a) -> CommandResult {
                 if (doc.mesh.triangleCount() == 0) return {false, "The mesh is empty"};
                 glm::mat4 vp;
                 std::string error;
                 std::vector<SymCopy> mirrors;
                 if (!viewProjArg(doc.mesh, a, vp, error) || !symmetryCopiesArg(a, doc.mesh, mirrors, error)) return {false, error};
                 const glm::vec2 lo(float(argNumber(a, "x0", -1)), float(argNumber(a, "y0", -1)));
                 const glm::vec2 hi(float(argNumber(a, "x1", 1)), float(argNumber(a, "y1", 1)));
                 const bool inside = argBool(a, "inside", false);
                 std::vector<uint8_t> hidden = hiddenByRect(doc.mesh, vp, lo, hi, inside, mirrors);
                 if (hidden.empty())
                     return {false, inside ? "That would hide everything" : "Nothing visible inside the rectangle"};
                 commitHidden(doc, hidden, inside ? "Hide Inside" : "Hide Outside");
                 return {true, visibleSummary(doc.mesh)};
             }});

    reg.add({"hide_lasso",
             "Lasso selection (ZBrush SelectLasso, Ctrl+Shift+drag): keep the visible triangles inside a screen "
             "polygon and hide the rest, or hide the inside instead. Points are normalised (-1..1, y up) in the given "
             "view; without a view they span the mesh seen from the front (+Z).",
             {{"points", T::String, "The lasso: at least 3 points as \"x0 y0 x1 y1 ...\" (-1..1, y up).", true},
              {"inside", T::Bool, "Hide the inside instead of the outside (ZBrush: Alt held).", false},
              {"view_proj", T::String, "Optional 16 numbers: column-major view-projection matrix.", false},
              symmetryParam},
             [](Document& doc, const Args& a) -> CommandResult {
                 if (doc.mesh.triangleCount() == 0) return {false, "The mesh is empty"};
                 glm::mat4 vp;
                 std::vector<glm::vec2> lasso;
                 std::string error;
                 std::vector<SymCopy> mirrors;
                 if (!viewProjArg(doc.mesh, a, vp, error) || !lassoArg(a, lasso, error) ||
                     !symmetryCopiesArg(a, doc.mesh, mirrors, error))
                     return {false, error};
                 const bool inside = argBool(a, "inside", false);
                 std::vector<uint8_t> hidden = hiddenByLasso(doc.mesh, vp, lasso, inside, mirrors);
                 if (hidden.empty())
                     return {false, inside ? "That would hide everything" : "Nothing visible inside the lasso"};
                 commitHidden(doc, hidden, inside ? "Hide Inside" : "Hide Outside");
                 return {true, visibleSummary(doc.mesh)};
             }});

    reg.add({"delete_hidden", "Permanently delete the hidden part of the mesh (undoable).", {},
             [](Document& doc, const Args&) -> CommandResult {
                 Mesh m = deleteHiddenTriangles(doc.mesh);
                 if (m.triangleCount() == 0) return {false, "Everything is hidden"};
                 const size_t removed = doc.mesh.triangleCount() - m.triangleCount();
                 if (removed == 0) return {false, "Nothing is hidden"};
                 doc.replaceMeshUndoable(std::move(m), "Delete Hidden");
                 return {true, "Deleted " + std::to_string(removed) + " triangles, " +
                                   std::to_string(doc.mesh.triangleCount()) + " left"};
             }});
}

} // namespace sf
