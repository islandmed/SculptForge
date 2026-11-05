// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Moving, rotating and scaling a Part, or only its unmasked part (Transform gizmo): the core
// of the app's Gizmo mode (W Move, E Scale, R Rotate) and of the `transform` command.
//
// A transform is a translation, a rotation and a per-axis scale about a pivot, with the rotation and scale
// expressed in the gizmo's axes (`frame`: world axes, or the surface the pivot was snapped to). Every vertex
// takes it with a weight from the mask: 1 - mask, eased in over a distance from the masked part (`falloff`) so
// a hard mask still bends softly at a joint (posing: mask the body, rotate the arm at the shoulder). A vertex with
// weight w moves by w * translate, turns by the rotation slerped to w and scales by mix(1, scale, w).
// Hidden vertices never move.
//
// With Base mesh active, sculpt layers move with the surface: the base takes the whole transform and every
// layer's offsets the linear part, so positions == base + sum(weight * offset) still holds and the layers keep
// working. With a sculpt layer active, the transform is recorded into that layer like a stroke: its offsets take
// (moved - current) / weight, the base and the other layers stay, and the layer's weight dials the move in and out.
// Topology, UVs, quads, face groups, vertex paint and textures are left as they are.
#pragma once

#include "core/BVH.h"
#include "core/Mesh.h"
#include "core/Sculpt.h"
#include "core/Undo.h"

#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace sf {

class CommandRegistry;
struct Document;

struct MeshTransform {
    glm::vec3 pivot{0.0f};
    glm::mat3 frame{1.0f};        // orthonormal gizmo axes (columns); rotate and scale act along them
    glm::vec3 translate{0.0f};    // world units
    glm::quat rotate{1, 0, 0, 0}; // world-space rotation about the pivot
    glm::vec3 scale{1.0f};        // along frame[0], frame[1], frame[2]; must be > 0

    bool identity() const;
    // The transform at weight `w` (0 = nothing, 1 = all of it) as p' = linear * p + offset.
    void at(float w, glm::mat3& linear, glm::vec3& offset) const;
    glm::vec3 apply(const glm::vec3& p) const; // full weight
};

// Rotation by Euler angles in degrees about axes[0], then axes[1], then axes[2] (the splat_rotate order).
glm::quat eulerRotation(const glm::vec3& degrees, const glm::mat3& axes = glm::mat3(1.0f));
// Euler angles in degrees (the inverse of eulerRotation with world axes), for showing a rotation as numbers.
glm::vec3 eulerDegrees(const glm::quat& q);

// Per-vertex weights of a transform: 1 - mask (when `useMask`), 0 on hidden vertices. With a `falloff` (a
// fraction of the mesh's bounding box diagonal) the weight eases in (smoothstep) over that distance along the
// surface from the masked part (mask >= 0.5), which itself stays still: a soft joint whatever the resolution.
// Empty means "every vertex, fully" (no mask, nothing hidden).
std::vector<float> transformWeights(const Mesh& mesh, bool useMask, float falloff);
// Centre of the bounds of the vertices that move (weight >= 0.5, or the heaviest ones when none reach it);
// the bounds centre of the visible mesh for empty weights. Falls back to the whole mesh's centre.
glm::vec3 transformCenter(const Mesh& mesh, const std::vector<float>& weights);
// Bounds of the same vertices.
AABB transformBounds(const Mesh& mesh, const std::vector<float>& weights);

// One interactive transform (a gizmo drag) or one command: remembers the vertices that move, then applies
// any transform to those originals (so a drag never accumulates error), and finishes with one undo step.
// Only moved vertices are touched: their normals, the GPU dirty range (Mesh::markDirty) and the BVH.
class TransformSession {
public:
    // `weights` as from transformWeights (empty = all). `mirror`: with a mask, the parts on the far side of
    // each enabled symmetry plane (through the origin, as for brushes) from the pivot take the mirrored
    // transform, so both arms pose together. Ignored for unmasked transforms and pivots on the plane.
    // `layer`: the sculpt layer the transform records into (-1 = Base mesh: move the base and every layer).
    // False (and nothing to do) when no vertex can move or `layer`'s weight is 0 (see layerWeightZero).
    bool begin(const Mesh& mesh, std::vector<float> weights, Symmetry mirror, const glm::vec3& pivot, int layer = -1);
    void apply(Mesh& mesh, const MeshTransform& t);
    void restore(Mesh& mesh); // back to the originals (the session stays open)
    // Refits the BVH and returns the undo step (empty when nothing moved). Ends the session.
    UndoRecord finish(Mesh& mesh, BVH& bvh, std::string label);
    void cancel(Mesh& mesh, BVH& bvh); // restore + end without an undo step

    bool active() const { return active_; }
    size_t movingVertices() const { return verts_.size(); }
    bool mirrored() const { return mirrors_.size() > 1; }
    // True when begin failed because the layer to record into has weight 0 (its offsets would not show).
    static bool layerWeightZero(const Mesh& mesh, int layer);

private:
    struct Level {
        glm::mat3 linear{1.0f}, normal{1.0f};
        glm::vec3 offset{0.0f};
        bool translateOnly = true;
    };
    void writeBack(Mesh& mesh);

    bool active_ = false;
    bool layered_ = false;
    int layer_ = -1;                 // the sculpt layer recorded into (-1: base and every layer move)
    float invWeight_ = 1.0f;         // 1 / that layer's weight
    std::vector<uint32_t> verts_;    // vertices with weight > 0
    std::vector<uint16_t> level_;    // their weight, quantized to kLevels
    std::vector<uint8_t> side_;      // index into mirrors_
    std::vector<uint32_t> boundary_; // vertices whose normals are recomputed (partial weight or a still neighbour)
    std::vector<glm::vec3> base_, normals_;           // originals: Mesh::layerTarget(-1) (positions when
                                                      // recording into a layer) and normals
    std::vector<std::vector<glm::vec3>> deltas_;      // originals of each sculpt layer's offsets (only layer_'s)
    std::vector<glm::vec3> mirrors_;                  // sign vectors; [0] = (1,1,1)
    std::vector<uint8_t> usedLevels_;                 // which quantized weights occur
    std::vector<Level> table_;                        // mirrors_.size() * (kLevels + 1)
    bool moved_ = false;

public:
    static constexpr int kLevels = 4096;
};

// Transforms Part `index` of `doc` (active or not) as one undoable step on that Part; on the active Part it
// records into the active sculpt layer (doc.activeLayer). Returns the number of vertices that moved.
size_t transformPart(Document& doc, int index, const MeshTransform& t, bool useMask, float falloff, Symmetry mirror,
                        const std::string& label);

// transform (translate, rotate, scale, pivot, orient, masked, falloff, symmetry, all).
void registerTransformCommands(CommandRegistry& registry);

} // namespace sf
