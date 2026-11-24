// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Polygon mode's operations registry and the two built-in ops; see core/PolyOps.h.
#include "core/PolyOps.h"

#include "core/Document.h"
#include "core/FaceGroups.h"
#include "core/Mesh.h"
#include "core/PolyExtrude.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace sf {

namespace {

using T = ParamSpec::Type;

std::string withCommas(size_t n) {
    std::string digits = std::to_string(n);
    for (int i = int(digits.size()) - 3; i > 0; i -= 3) digits.insert(size_t(i), 1, ',');
    return digits;
}

const char* elementNoun(PolyElement e, bool plural) {
    switch (e) {
    case PolyElement::Points: return plural ? "points" : "point";
    case PolyElement::Edges: return plural ? "edges" : "edge";
    case PolyElement::Faces: return plural ? "faces" : "face";
    }
    return plural ? "faces" : "face";
}

// --- Delete: the selected faces go (hidden flags, groups and the rest are compacted with the triangles). ---
bool polyDeleteFaces(const PolyOpInput& in, PolyOpOutput& out, std::string& error) {
    const Mesh& m = in.mesh;
    if (in.triangles.empty()) {
        error = "Delete needs faces: select the faces to delete";
        return false;
    }
    std::vector<uint8_t> keep(m.triangleCount(), 1);
    size_t polygons = 0;
    for (uint32_t t : in.triangles) {
        if (t >= keep.size()) continue;
        keep[t] = 0;
        polygons += edgePolygon(m, t) == t;
    }
    if (std::find(keep.begin(), keep.end(), uint8_t(1)) == keep.end()) {
        error = "That would delete every face: delete the Part instead (Tool > Parts)";
        return false;
    }
    out.mesh = extractTriangles(m, keep);
    out.element = PolyElement::Faces;
    out.message = "Deleted " + withCommas(polygons) + (polygons == 1 ? " face" : " faces");
    return true;
}

// --- Flip: the selected faces turn inside out (their winding reverses; a quad's halves together). ---
bool polyFlipFaces(const PolyOpInput& in, PolyOpOutput& out, std::string& error) {
    const Mesh& m = in.mesh;
    if (in.triangles.empty()) {
        error = "Flip needs faces: select the faces to turn inside out";
        return false;
    }
    out.mesh = m;
    size_t polygons = 0;
    for (uint32_t t : in.triangles) {
        if (t >= out.mesh.triangles.size()) continue;
        glm::uvec3& tri = out.mesh.triangles[t];
        std::swap(tri.y, tri.z);
        // Corner attributes follow their corners (y <-> z).
        if (out.mesh.hasUvs()) std::swap(out.mesh.uvs[t * 3 + 1], out.mesh.uvs[t * 3 + 2]);
        // Per-corner edge flags name the edge from a corner to the next: after the swap the edges are x->z, z->y, y->x,
        // so flag k (edge k -> k+1) moves to the corner the same edge now starts from.
        auto rotateFlags = [&](auto& flags) {
            auto a = flags[t * 3], b = flags[t * 3 + 1], c = flags[t * 3 + 2];
            flags[t * 3] = c;     // x->z was z->x
            flags[t * 3 + 1] = b; // z->y was y->z
            flags[t * 3 + 2] = a; // y->x was x->y
        };
        if (out.mesh.hasUvFlags()) rotateFlags(out.mesh.uvFlags);
        if (out.mesh.hasCreases()) rotateFlags(out.mesh.creases);
        polygons += edgePolygon(m, t) == t;
    }
    out.element = PolyElement::Faces;
    out.triangles = in.triangles;
    out.message = "Flipped " + withCommas(polygons) + (polygons == 1 ? " face" : " faces");
    return true;
}

bool sameTopology(const Mesh& a, const Mesh& b) {
    return a.vertexCount() == b.vertexCount() && a.triangles == b.triangles && a.quadPartners == b.quadPartners &&
           a.facegroups == b.facegroups && a.hidden == b.hidden && a.uvs == b.uvs && a.uvFlags == b.uvFlags &&
           a.creases == b.creases && a.mask == b.mask && a.colors == b.colors;
}

// Lands `out` on the active Part as one undo step and selects its result.
void land(Document& doc, const PolyOp& op, PolyOpOutput& out) {
    Mesh& m = doc.mesh;
    Mesh& after = out.mesh;
    if (sameTopology(m, after)) {
        // Only positions moved: a sparse step, normals of the moved vertices, the tree refitted.
        UndoRecord r;
        r.label = op.label;
        std::vector<uint32_t> moved;
        for (uint32_t v = 0; v < m.positions.size(); ++v) {
            if (m.positions[v] == after.positions[v]) continue;
            r.positions.add(v, m.positions[v]);
            m.positions[v] = after.positions[v];
            m.markDirty(v);
            moved.push_back(v);
        }
        if (moved.empty()) return;
        m.updateNormals(moved);
        doc.syncBvh();
        if (moved.size() * 4 > m.vertexCount()) doc.bvh.refitAll(m);
        else doc.bvh.refit(m, moved);
        doc.undo.push(std::move(r));
        doc.modified = true;
    } else {
        if (!after.texture && m.texture) { // the op left the painted texture alone: it rides on the UVs it kept
            after.texture = m.texture;
            after.paintToTexture = m.paintToTexture;
        }
        after.sanitizeAttributes();
        after.rebuildTopology();
        after.recomputeAllNormals();
        doc.replaceMeshUndoable(std::move(after), op.label);
    }
    // The result selected, in the new mesh's numbering.
    PolySelection& sel = doc.polySelection;
    const EdgeTopology& topo = sel.sync(doc.mesh);
    const bool any = !out.triangles.empty() || !out.edges.empty() || !out.vertices.empty();
    if (!any) {
        if (!sel.synced(doc.mesh)) sel.clear();
        return;
    }
    sel.setElement(doc.mesh, out.element);
    polySelectAll(sel, doc.mesh, false);
    sel.setElement(doc.mesh, out.element);
    for (uint32_t t : out.triangles)
        if (t < doc.mesh.triangleCount()) sel.setFace(doc.mesh, t, true);
    for (uint32_t v : out.vertices)
        if (v < doc.mesh.vertexCount()) sel.setPoint(v, true);
    for (const glm::uvec2& ab : out.edges) {
        if (ab.x >= doc.mesh.vertexCount() || ab.y >= doc.mesh.vertexCount()) continue;
        for (uint32_t i = topo.vertexOffsets[ab.x]; i < topo.vertexOffsets[ab.x + 1]; ++i) {
            const uint32_t e = topo.vertexEdges[i];
            const glm::uvec2 ends = topo.ends[e];
            if (ends.x == ab.y || ends.y == ab.y) sel.setEdge(e, true);
        }
    }
    // Faces named by one half, edges and points as given: the element's own flags are what was set; the others
    // follow when the user switches (setElement converts).
}

} // namespace

PolyOpRegistry& PolyOpRegistry::instance() {
    static PolyOpRegistry reg = [] {
        PolyOpRegistry r;
        registerBuiltinPolyOps(r);
        return r;
    }();
    return reg;
}

void PolyOpRegistry::add(PolyOp op) {
    for (PolyOp& o : ops_)
        if (o.id == op.id) {
            o = std::move(op);
            return;
        }
    ops_.push_back(std::move(op));
}

const PolyOp* PolyOpRegistry::find(const std::string& id) const {
    for (const PolyOp& o : ops_)
        if (o.id == id) return &o;
    return nullptr;
}

void registerBuiltinPolyOps(PolyOpRegistry& reg) {
    {
        PolyOp op;
        op.id = "delete";
        op.label = "Delete";
        op.description = "Delete the selected faces. Points and edges delete the faces they fully cover.";
        op.needs = PolyElement::Faces;
        op.run = polyDeleteFaces;
        reg.add(std::move(op));
    }
    {
        PolyOp op;
        op.id = "flip";
        op.label = "Flip";
        op.description = "Turn the selected faces inside out (their normals point the other way).";
        op.needs = PolyElement::Faces;
        op.run = polyFlipFaces;
        reg.add(std::move(op));
    }
    // The modelling ops register here, one line each (core/PolyExtrude.cpp, PolyInset.cpp, PolyBevel.cpp,
    // PolyEdgeLoop.cpp, PolySpan.cpp).
    registerPolyExtrudeOp(reg);
}

Args polyOpArgs(const PolyOp& op, const Args& args) {
    Args full = args;
    for (const PolyOpParam& p : op.params) {
        auto it = full.find(p.name);
        if (it == full.end()) {
            if (p.isBool) full[p.name] = p.value != 0.0;
            else full[p.name] = p.value;
            continue;
        }
        if (!p.isBool) {
            double v = argNumber(full, p.name, p.value);
            if (!std::isfinite(v)) v = p.value;
            full[p.name] = std::clamp(v, p.min, p.max);
        } else {
            full[p.name] = argBool(full, p.name, p.value != 0.0);
        }
    }
    return full;
}

bool polyOpSelection(Document& doc, const PolyOp& op, PolyOpSelection& out, std::string& error) {
    const Mesh& m = doc.mesh;
    error = polyModeRefusal(m);
    if (!error.empty()) return false;
    if (!m.layers.empty()) {
        error = "Polygon operations need a Part without sculpt layers: merge them first (Tool > Layers)";
        return false;
    }
    PolySelection& sel = doc.polySelection;
    sel.sync(m, doc.edgeSelection.topo);
    if (sel.empty()) {
        error = std::string(op.label) + " needs " + elementNoun(op.anyElement ? sel.element : op.needs, true) +
                ": click one, Shift+click adds, or drag a box (Tool > Polygon)";
        return false;
    }
    out.vertices = sel.vertices(m);
    out.edges = sel.edgeList(m);
    out.triangles = sel.triangles(m);
    out.triangleFlags.assign(m.triangleCount(), 0);
    for (uint32_t t : out.triangles) out.triangleFlags[t] = 1;
    out.edgePairs.clear();
    for (uint32_t e : out.edges) out.edgePairs.push_back(sel.topo->ends[e]);
    if (op.anyElement) return true;
    const bool have = op.needs == PolyElement::Faces ? !out.triangles.empty()
                      : op.needs == PolyElement::Edges ? !out.edges.empty()
                                                        : !out.vertices.empty();
    if (!have) {
        error = std::string(op.label) + " needs whole " + elementNoun(op.needs, true) + ": no " + elementNoun(op.needs, false) +
                " has all its " + elementNoun(sel.element, true) + " selected (switch to " +
                (op.needs == PolyElement::Faces ? "Faces" : op.needs == PolyElement::Edges ? "Edges" : "Points") + ")";
        return false;
    }
    return true;
}

namespace {

bool runOrPreview(Document& doc, const std::string& id, const Args& args, PolyOpOutput& out, const PolyOp*& op, std::string& error) {
    op = PolyOpRegistry::instance().find(id);
    if (!op) {
        error = "No polygon operation named \"" + id + "\" (poly_op op=list lists them)";
        return false;
    }
    PolyOpSelection s;
    if (!polyOpSelection(doc, *op, s, error)) return false;
    const Args full = polyOpArgs(*op, args);
    PolySelection& sel = doc.polySelection;
    const PolyOpInput in{doc.mesh, *sel.topo, sel.element, s.vertices, s.edges, s.triangles, s.triangleFlags, s.edgePairs, full};
    if (!op->run(in, out, error)) {
        if (error.empty()) error = op->label + " could not run on this selection";
        return false;
    }
    if (out.mesh.triangleCount() == 0 || out.mesh.vertexCount() == 0) {
        error = op->label + " would leave nothing";
        return false;
    }
    const uint32_t nv = uint32_t(out.mesh.vertexCount());
    for (const glm::uvec3& t : out.mesh.triangles)
        if (t.x >= nv || t.y >= nv || t.z >= nv) {
            error = op->label + " made an invalid mesh (a face names a missing point)";
            return false;
        }
    return true;
}

} // namespace

CommandResult runPolyOp(Document& doc, const std::string& id, const Args& args) {
    PolyOpOutput out;
    const PolyOp* op = nullptr;
    std::string error;
    if (!runOrPreview(doc, id, args, out, op, error)) return {false, error};
    const std::string message = out.message;
    land(doc, *op, out);
    return {true, (message.empty() ? op->label : message) + ": " + polySelectionText(doc.polySelection, doc.mesh)};
}

bool previewPolyOp(Document& doc, const std::string& id, const Args& args, PolyOpOutput& out, std::string& error) {
    const PolyOp* op = nullptr;
    return runOrPreview(doc, id, args, out, op, error);
}

void registerPolyOpCommands(CommandRegistry& reg) {
    reg.add({"poly_op",
             "Run a Polygon mode operation on the active Part's selection (poly_select picks it), as one undo step: "
             "op=list names the operations and their parameters; any other parameter of the op is passed as "
             "name=value (its defaults fill in the rest).",
             {{"op", T::String, "the operation (delete, flip, ...; list: name them all)", true}},
             [](Document& doc, const Args& a) -> CommandResult {
                 const std::string id = argString(a, "op");
                 if (id == "list" || id.empty()) {
                     std::string s;
                     for (const PolyOp& op : PolyOpRegistry::instance().all()) {
                         s += (s.empty() ? "" : "; ") + op.id + " (" + elementNoun(op.needs, true);
                         for (const PolyOpParam& p : op.params) {
                             char buf[64];
                             if (p.isBool) std::snprintf(buf, sizeof buf, ", %s=%s", p.name.c_str(), p.value != 0.0 ? "true" : "false");
                             else std::snprintf(buf, sizeof buf, ", %s=%g [%g..%g]", p.name.c_str(), p.value, p.min, p.max);
                             s += buf;
                         }
                         s += ")";
                     }
                     return {true, s.empty() ? "No polygon operations" : s};
                 }
                 Args rest = a;
                 rest.erase("op");
                 return runPolyOp(doc, id, rest);
             }});
}

} // namespace sf
