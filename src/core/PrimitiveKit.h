// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Parametric primitives (Tool > Parts > Append): a Part made from a PrimitiveSpec keeps its settings (segments,
// rings, caps, radius, height...) editable in the Parts panel for as long as its mesh is still exactly the
// generated one. Any sculpt, remesh, face-group or Polygon-mode edit ends that silently (the Part is then an
// ordinary mesh); undoing the edit brings the settings back, because freshness is judged on the mesh, not on the
// history (primitiveFresh). A whole-Part move with the Gizmo keeps them: the move goes into `placement`.
//
// The meshes are the blockout shapes polygon modelling starts from: quads (Mesh::quadPartners, pairs sharing a
// face group) everywhere but in the fan of a cap or a pole, closed and facing outwards, one face group per part
// (bottom cap, side, top cap), built around the origin with their axis along +Y, then placed.
#pragma once

#include "core/Json.h"
#include "core/Mesh.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace sf {

struct Document;
struct PartData;
class CommandRegistry;

struct PrimitiveSpec {
    // sphere (an icosphere, as File > New Sphere), icosphere, quadsphere, cube, plane, cylinder, cone, torus,
    // capsule, pipe (a hollow cylinder with a wall).
    std::string type = "cylinder";
    float size = 1.0f;     // the box it fits, scene units (part_append sizes it from the scene)
    int segments = 24;     // around the axis; cube / plane / quadsphere: cells per side; sphere / icosphere: subdivisions
    int rings = 1;         // along the side (cylinder, cone, capsule, pipe); torus: around the tube
    int caps = 1;          // cylinder, cone, pipe ends: 0 open, 1 a fan, k > 1 k-1 rings of quads then a fan
    bool quadCaps = false; // caps as an all-quad grid (no pole; segments rounded to a multiple of 4)
    float radius = 0.6f;   // as a fraction of size / 2
    float radius2 = 0.0f;  // cone: the top radius (0 = a point); torus: the tube radius; pipe: the inner radius
    float height = 1.0f;   // as a fraction of size (cylinder, cone, capsule, pipe)
    int axis = 1;          // the shape's axis: 0 X, 1 Y (up, the default), 2 Z (a wheel, an eye)
    glm::mat4 placement{1.0f}; // where the Part sits: applied to the generated positions

    bool operator==(const PrimitiveSpec& o) const;
    bool operator!=(const PrimitiveSpec& o) const { return !(*this == o); }
};

// The primitive types part_append knows, in menu order.
const std::vector<std::string>& primitiveTypes();
bool isPrimitiveType(const std::string& type);
// The settings Append gives `type`: the dense meshes Append has always made (Classic), or the low-poly cage for
// Polygon mode (the Low-poly switch in the Append menu).
PrimitiveSpec primitiveDefaults(const std::string& type, bool lowPoly);
// Brings every setting into its range (and the dependent ones: a cone's top below its base, a pipe's wall > 0).
void clampPrimitiveSpec(PrimitiveSpec& spec);
// The mesh of `spec`: topology and normals built, placement applied.
Mesh makePrimitive(const PrimitiveSpec& spec);

// Which settings `type` uses, and how the panel names them.
struct PrimitiveControls {
    bool segments = true, rings = false, caps = false, radius = false, radius2 = false, height = false, axis = false;
    const char* segmentsLabel = "Segments %d"; // "Cells %d" for cube / plane / quadsphere, "Subdivisions %d" for spheres
    const char* radius2Label = "Top radius %.2f"; // "Tube radius %.2f" (torus), "Inner radius %.2f" (pipe)
};
PrimitiveControls primitiveControls(const std::string& type);
const char* primitiveLabel(const std::string& type); // "Cylinder", "Quad Sphere", ...

Json primitiveSpecJson(const PrimitiveSpec& spec);
bool primitiveSpecFromJson(const Json& j, PrimitiveSpec& out); // false when it is not a spec

// A Part's parametric state (PartData::primitive): the specs it was generated with, newest last. Several are kept
// because undo and redo walk back through earlier settings: primitiveFresh looks for the one the mesh matches.
struct PrimitiveState {
    std::vector<PrimitiveSpec> history;
    uint64_t checkedRevision = ~uint64_t(0); // the undo revision the cached answer is for
    bool fresh = false;
    bool any() const { return !history.empty(); }
    const PrimitiveSpec& spec() const { return history.back(); }
    PrimitiveSpec& spec() { return history.back(); }
    void clear() { history.clear(); checkedRevision = ~uint64_t(0); fresh = false; }
};
// Records `spec` as the newest settings of a Part just generated from it (fresh at `revision`).
void setPrimitiveSpec(PartData& data, PrimitiveSpec spec, uint64_t revision);
// True when Part `index`'s mesh is still exactly a mesh of its specs (within a rounding tolerance): then its
// settings show and part_primitive may regenerate it. Cheap when nothing changed since the last call.
bool primitiveFresh(const Document& doc, int index); // (the cached answer lives on the shared PartData)
// Whether `mesh` is what `spec` generates (counts, triangles, face groups, positions within a tolerance).
bool primitiveMatches(const Mesh& mesh, const PrimitiveSpec& spec);

// part_primitive: change the settings of the active Part's fresh primitive and regenerate it, one undo step.
void registerPrimitiveCommands(CommandRegistry& registry);

} // namespace sf
