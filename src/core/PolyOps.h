// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Polygon mode's operations (Tool > Polygon): Extrude, Inset, Bevel, Edge loop, Span, Delete... Each is a PURE
// function of the mesh and the selection (core/PolyMode.h): it gets the mesh as it is, the selected vertices, edges
// and triangles and its parameters, and returns the mesh after and what should be selected then. The registry here
// does everything else, the same way for every op: refuses what Polygon mode refuses (polyModeRefusal), checks the
// selection the op needs, runs it, lands the result on the Part as ONE undo step (sparse when only positions moved,
// a whole-mesh step when the topology changed), rebuilds normals and the BVH, re-syncs the selection and selects
// the op's result. The `poly_op` command (console, scripts, the AI link) and the Tool > Polygon panel both go
// through runPolyOp; the panel lists the ops of the registry with a slider per parameter.
//
// Adding an op (one .cpp under src/core, one unit test under tests/):
//
//   // core/PolyInset.h: the pure function, testable on its own
//   bool polyInset(const PolyOpInput& in, PolyOpOutput& out, std::string& error);
//   // core/PolyInset.cpp, at the end
//   void registerPolyInsetOp(PolyOpRegistry& reg) {
//       PolyOp op;
//       op.id = "inset";                    // poly_op op=inset thickness=0.1
//       op.label = "Inset";                 // the panel's button (tr() translates it)
//       op.description = "...";             // the Ctrl-hover help and the command's doc
//       op.needs = PolyElement::Faces;      // Faces: the selected triangles (a quad's two halves), Edges, Points, or
//                                           // PolyOpRegistry::kAny when the op reads whichever element is picked
//       op.params = {{"thickness", "Thickness", 0.0, 1.0, 0.1, "Of the faces' size..."}};
//       op.run = polyInset;
//       reg.add(std::move(op));
//   }
//   // and one line in registerBuiltinPolyOps() (core/PolyOps.cpp), so the op exists in the app, the CLI and the tests.
//
// Rules for the op function: never touch `in.mesh` (it is the live Part); `out.mesh` is the whole mesh after
// (start from a copy: `out.mesh = in.mesh`), with every per-vertex / per-triangle / per-corner attribute the input had
// resized to fit (mask, colours, face groups, hidden flags, UVs, creases, quadPartners; Mesh::sanitizeAttributes drops
// any that doesn't fit, which loses data), its triangles valid, and no topology built (the registry rebuilds it).
// New faces join the face group of the face they came from unless the op says otherwise. The result selection
// (out.triangles / out.edges / out.vertices with out.element) is in the OUTPUT mesh's numbering. An op that only moves
// vertices keeps the triangle list identical (same count, same order) and the registry records a sparse undo step.
// Return false with `error` in the user's words when the op can't run ("Inset needs a closed ring of faces").
#pragma once

#include "core/Commands.h"
#include "core/PolyMode.h"

#include <functional>
#include <string>
#include <vector>

namespace sf {

struct PolyOpParam {
    std::string name;  // the argument's name (poly_op op=<id> <name>=<value>)
    std::string label; // shown on the panel's slider (tr() translates it)
    double min = 0.0, max = 1.0;
    double value = 0.0; // the default
    std::string help;   // the Ctrl-hover help of the slider
    bool isBool = false; // a checkbox instead of a slider (min / max ignored)
};

struct PolyOpInput {
    const Mesh& mesh;
    const EdgeTopology& topo;
    PolyElement element;                   // what the user had picked
    const std::vector<uint32_t>& vertices; // PolySelection::vertices (sorted)
    const std::vector<uint32_t>& edges;    // PolySelection::edgeList (sorted; edge ids of `topo`)
    const std::vector<uint32_t>& triangles; // PolySelection::triangles (sorted; both halves of a quad)
    // The same selection in the other two shapes ops like to take: one flag per triangle of `mesh`, and the edges as
    // vertex pairs (x < y, the order of `edges`).
    const std::vector<uint8_t>& triangleFlags;
    const std::vector<glm::uvec2>& edgePairs;
    const Args& args;                       // the op's parameters, defaults filled in (polyOpArgs)
};

struct PolyOpOutput {
    Mesh mesh;                              // the whole mesh after the op (no topology built)
    PolyElement element = PolyElement::Faces; // what to select afterwards...
    std::vector<uint32_t> triangles;        // ... Faces: these triangles (either half of a quad is enough)
    std::vector<glm::uvec2> edges;          // ... Edges: the edges between these vertex pairs
    std::vector<uint32_t> vertices;         // ... Points: these vertices
    std::string message;                    // optional, for the status line ("Extruded 24 faces")
};

using PolyOpFn = std::function<bool(const PolyOpInput& in, PolyOpOutput& out, std::string& error)>;

struct PolyOp {
    std::string id;
    std::string label;
    std::string description;
    std::string credit;              // who asked for it ("@handle (#225)"), shown nowhere, kept for the notes
    PolyElement needs = PolyElement::Faces; // the selection kind the op wants (converted from the picked kind)
    bool anyElement = false;         // true: the op takes whichever kind is picked (needs is then only the default)
    bool preview = false;            // cheap enough to run live while a slider moves (previewPolyOp)
    std::vector<PolyOpParam> params;
    PolyOpFn run;
};

class PolyOpRegistry {
public:
    static PolyOpRegistry& instance(); // the app's, with the built-in ops (registerBuiltinPolyOps)
    void add(PolyOp op);               // replaces an op with the same id
    const PolyOp* find(const std::string& id) const;
    const std::vector<PolyOp>& all() const { return ops_; }

private:
    std::vector<PolyOp> ops_;
};

// The built-in ops (Delete...) and every op file's register function: one line per op.
void registerBuiltinPolyOps(PolyOpRegistry& reg);

// `args` with the op's defaults filled in for the parameters it doesn't name (and clamped to their ranges).
Args polyOpArgs(const PolyOp& op, const Args& args);

// What the op would get for the active Part now (false with `error` when Polygon mode refuses the Part or the
// selection isn't what the op needs). `vertices` / `edges` / `triangles` back the input's spans.
struct PolyOpSelection {
    std::vector<uint32_t> vertices, edges, triangles;
    std::vector<uint8_t> triangleFlags;
    std::vector<glm::uvec2> edgePairs;
};
bool polyOpSelection(Document& doc, const PolyOp& op, PolyOpSelection& out, std::string& error);

// Runs op `id` on the active Part's selection as one undo step; the result (or why not) in the user's words.
CommandResult runPolyOp(Document& doc, const std::string& id, const Args& args);
// The same without touching the Part: the mesh and selection the op would leave (for a live preview).
bool previewPolyOp(Document& doc, const std::string& id, const Args& args, PolyOpOutput& out, std::string& error);

// poly_op (core/PolyOps.cpp).
void registerPolyOpCommands(CommandRegistry& registry);

} // namespace sf
