// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The `transform` command (core/MeshTransform.h): what a Transform gizmo drag does, for scripts, the console and AI.
#include "core/Commands.h"
#include "core/Document.h"
#include "core/MeshTransform.h"
#include "core/PathStroke.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace sf {

namespace {

using T = ParamSpec::Type;

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

// A vector argument written "x,y,z" (or one number for all three when `uniform`).
bool vecArg(const Args& a, const std::string& key, glm::vec3& out, bool uniform, std::string& error) {
    auto it = a.find(key);
    if (it == a.end()) return true;
    if (auto* d = std::get_if<double>(&it->second)) {
        if (!uniform) {
            error = "Bad " + key + " (expected x,y,z)";
            return false;
        }
        out = glm::vec3(float(*d));
        return true;
    }
    const std::string s = argString(a, key);
    if (parseVec3(s, out)) return true;
    if (uniform) {
        char* end = nullptr;
        const float v = std::strtof(s.c_str(), &end);
        if (end != s.c_str() && *end == '\0' && std::isfinite(v)) {
            out = glm::vec3(v);
            return true;
        }
    }
    error = "Bad " + key + " '" + s + "' (expected x,y,z" + (uniform ? " or one number)" : ")");
    return false;
}

CommandResult runTransform(Document& doc, const Args& a) {
    std::string err;
    glm::vec3 translate(0.0f), rotate(0.0f), scale(1.0f), orient(0.0f);
    if (!vecArg(a, "translate", translate, false, err) || !vecArg(a, "rotate", rotate, false, err) ||
        !vecArg(a, "scale", scale, true, err) || !vecArg(a, "orient", orient, false, err))
        return {false, err};
    if (scale.x <= 0.0f || scale.y <= 0.0f || scale.z <= 0.0f)
        return {false, "Scale must be above 0 (mirroring would turn the surface inside out)"};
    const bool useMask = argBool(a, "masked", true);
    const float falloff = std::clamp(float(argNumber(a, "falloff", 0)), 0.0f, 1.0f);
    const bool all = argBool(a, "all", false);
    const uint32_t folder = argBool(a, "folder", false) ? doc.activePartInfo().folder : 0; // #96
    Symmetry sym{false, false, false};
    for (char c : lower(argString(a, "symmetry"))) {
        if (c == 'x') sym.x = true;
        else if (c == 'y') sym.y = true;
        else if (c == 'z') sym.z = true;
        else if (c != ',' && c != ' ') return {false, "Bad symmetry (expected axes such as x or xz)"};
    }

    std::vector<int> targets;
    for (int i = 0; i < doc.partCount(); ++i) {
        const Part& s = doc.parts[size_t(i)];
        if (!(i == doc.activePart || (s.visible && (all || (folder && s.folder == folder))))) continue;
        // Linked copies (#94) share one mesh: a masked edit of it happens once.
        const bool again = std::any_of(targets.begin(), targets.end(),
                                       [&](int k) { return doc.parts[size_t(k)].data == s.data; });
        if (again && !partMovesWhole(doc, i, useMask)) continue;
        targets.push_back(i);
    }

    MeshTransform t;
    t.translate = translate;
    if (lower(argString(a, "orient", "world")) != "world") t.frame = glm::mat3_cast(eulerRotation(orient));
    t.rotate = eulerRotation(rotate, t.frame);
    t.scale = scale;
    const std::string pivot = lower(argString(a, "pivot", "center"));
    if (pivot == "center" || pivot == "centre") {
        AABB box;
        for (int i : targets) {
            const Mesh& m = doc.partMesh(i);
            if (m.vertexCount() == 0) continue;
            const AABB b = transformBounds(m, transformWeights(m, useMask, falloff));
            const glm::mat4& place = doc.parts[size_t(i)].transform; // (where a linked copy is)
            for (int c = 0; c < 8 && b.valid(); ++c)
                box.expand(glm::vec3(place * glm::vec4(c & 1 ? b.max.x : b.min.x, c & 2 ? b.max.y : b.min.y,
                                                       c & 4 ? b.max.z : b.min.z, 1.0f)));
        }
        t.pivot = box.valid() ? box.center() : glm::vec3(0.0f);
    } else if (pivot == "origin") {
        t.pivot = glm::vec3(0.0f);
    } else if (!parseVec3(pivot, t.pivot)) {
        return {false, "Bad pivot '" + pivot + "' (expected center, origin or x,y,z)"};
    }
    if (t.identity()) return {true, "Nothing to do: the transform changes nothing"};
    if (TransformSession::layerWeightZero(doc.mesh, doc.activeLayer))
        return {false, "Active layer weight is 0 - raise it to transform into the layer"};

    size_t moved = 0;
    for (int i : targets) moved += transformPart(doc, i, t, useMask, falloff, sym, "Transform");
    if (moved == 0)
        return {false, useMask && doc.mesh.hasMask() ? "Nothing moved: everything is masked (masked=0 ignores the mask)"
                                                    : "Nothing moved: the Part is empty or hidden"};
    // "Moved 2,562 vertices": the verb says what changed (the pivot is in the command's own arguments).
    const bool moves = t.translate != glm::vec3(0.0f), turns = t.rotate != glm::quat(1, 0, 0, 0),
               scales = t.scale != glm::vec3(1.0f);
    const char* verb = int(moves) + int(turns) + int(scales) > 1 ? "Transformed" : moves ? "Moved" : turns ? "Turned" : "Scaled";
    std::string msg = std::string(verb) + " " + vertexCountText(moved);
    if (targets.size() > 1) msg += " in " + std::to_string(targets.size()) + " Parts";
    return {true, msg};
}

} // namespace

void registerTransformCommands(CommandRegistry& reg) {
    reg.add({"transform",
             "Move, rotate and scale the active Part, or only its unmasked part (the Gizmo as a command): "
             "p' = pivot + translate + R * S * (p - pivot). Masked vertices stay, partly masked ones move partly; "
             "falloff eases it in from the mask's edge (a soft joint for posing). With a sculpt layer active the "
             "move goes into that layer (as a stroke does). One undo step per Part.",
             {{"translate", T::String, "Movement x,y,z in world units. Default 0,0,0.", false},
              {"rotate", T::String, "Rotation in degrees about the X, then Y, then Z axis (of orient). Default 0,0,0.",
               false},
              {"scale", T::String, "Scale factor: one number, or x,y,z along the axes of orient. Default 1.", false},
              {"pivot", T::String,
               "Where to rotate and scale about: center (default: centre of what moves), origin, or x,y,z.", false},
              {"orient", T::String, "Axes for rotate and scale: world (default), or x,y,z Euler degrees.", false},
              {"masked", T::Bool, "Leave masked vertices where they are (default true; false moves everything).",
               false},
              {"falloff", T::Number,
               "Soft joint: ease the movement in over this distance from the masked part, as a fraction of the "
               "Part's size (0-1, e.g. 0.1; default 0 = the mask as it is).",
               false},
              {"symmetry", T::String,
               "Mirror a masked transform across these planes through the origin, e.g. x: the unmasked part on the "
               "other side of the plane takes the mirrored transform (both arms pose together).",
               false},
              {"all", T::Bool, "Transform every visible Part. Default false.", false},
              {"folder", T::Bool, "Transform the visible Parts in the active Part's folder together. Default false.",
               false}},
             runTransform});
}

} // namespace sf
