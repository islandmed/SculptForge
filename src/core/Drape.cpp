// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Drape.h"

#include "core/Commands.h"
#include "core/Document.h"

#include <algorithm>
#include <cmath>

namespace sf {

bool drapeScene(const Document& doc, const DrapeRequest& request, DrapeScene& out, std::string& error) {
    const Mesh& mesh = doc.mesh;
    if (doc.sculptor.strokeActive()) return error = "Drape: finish the stroke first", false;
    if (mesh.vertexCount() == 0 || mesh.triangleCount() == 0) return error = "Drape: the active Part is empty", false;
    if (!mesh.forgespheres.empty()) return error = "Drape: the active Part is a Forge Sphere Part (Make Mesh first)", false;
    if (mesh.micro) return error = "Drape: the active Part has a micro level (Bake Micro or Delete Micro first)", false;
    const float scale = std::clamp(request.scale, 1e-4f, 1e4f);
    const float inv = 1.0f / scale;
    out.scale = scale;
    out.fabric = fabricPreset(request.fabric);
    ClothSetup& s = out.setup;
    s = ClothSetup{};
    s.positions.resize(mesh.vertexCount());
    for (size_t v = 0; v < mesh.vertexCount(); ++v) s.positions[v] = mesh.positions[v] * inv;
    s.triangles.reserve(mesh.triangleCount());
    for (uint32_t t = 0; t < mesh.triangleCount(); ++t)
        if (!mesh.triangleHidden(t)) s.triangles.push_back(mesh.triangles[t]);
    if (s.triangles.empty()) return error = "Drape: every face of the active Part is hidden", false;
    // Pins: the mask, and the vertices only hidden faces touch (they stay with the hidden part).
    const bool masked = request.useMask && mesh.hasMask();
    if (masked || mesh.hasHidden()) {
        s.pin.assign(mesh.vertexCount(), 0.0f);
        for (uint32_t v = 0; v < mesh.vertexCount(); ++v) {
            if (masked) s.pin[v] = std::clamp(mesh.maskAt(v), 0.0f, 1.0f);
            if (mesh.hasHidden() && mesh.vertexHidden(v)) s.pin[v] = 1.0f;
        }
    }
    out.colliders = ClothColliders{};
    if (request.collide) {
        std::vector<glm::vec3> scaled;
        // The other Parts in the active Part's space (a moved linked copy has its own transform).
        const glm::mat4 toActive = glm::inverse(doc.parts[size_t(doc.activePart)].transform);
        for (int i = 0; i < doc.partCount(); ++i) {
            const Part& part = doc.parts[size_t(i)];
            if (i == doc.activePart || !part.visible) continue;
            const Mesh& other = part.data->mesh;
            if (other.micro || other.triangleCount() == 0) continue; // (a micro level collides at its base: later)
            const glm::mat4 m = toActive * part.transform;
            scaled.resize(other.vertexCount());
            for (size_t v = 0; v < other.vertexCount(); ++v) scaled[v] = glm::vec3(m * glm::vec4(other.positions[v], 1.0f)) * inv;
            if (!other.hasHidden()) {
                out.colliders.add(scaled, other.triangles);
            } else {
                std::vector<glm::uvec3> shown;
                for (uint32_t t = 0; t < other.triangleCount(); ++t)
                    if (!other.triangleHidden(t)) shown.push_back(other.triangles[t]);
                out.colliders.add(scaled, shown);
            }
        }
    }
    out.colliders.build();
    return true;
}

size_t applyDrape(Document& doc, const std::vector<glm::vec3>& metres, float scale, std::string& error) {
    Mesh& mesh = doc.mesh;
    if (metres.size() != mesh.vertexCount()) return error = "Drape: the Part changed while it simulated", 0;
    std::vector<uint32_t> verts;
    std::vector<glm::vec3> d;
    for (uint32_t v = 0; v < mesh.vertexCount(); ++v) {
        const glm::vec3 dv = metres[v] * scale - mesh.positions[v];
        if (dv != glm::vec3(0.0f) && std::isfinite(dv.x + dv.y + dv.z)) {
            verts.push_back(v);
            d.push_back(dv);
        }
    }
    if (verts.empty()) return error = "Drape: nothing moved", 0;
    doc.syncBvh();
    if (!doc.sculptor.beginStroke(mesh, "Drape", doc.activeLayer)) return error = "Drape: the active layer's weight is 0", 0;
    doc.sculptor.moveVertices(mesh, doc.bvh, verts, d);
    UndoRecord r = doc.sculptor.endStroke();
    if (!r.empty()) doc.undo.push(std::move(r));
    return verts.size();
}

void registerDrapeCommands(CommandRegistry& reg) {
    using T = ParamSpec::Type;
    reg.add({"cloth_drape",
             "Drape the active Part as cloth: it falls under gravity onto the other visible Parts and settles (the "
             "mask pins it). One undo step.",
             {{"fabric", T::String, "silk, cotton (default), denim or leather.", false},
              {"scale", T::Number, "World units per metre of the sculpt (default 1: modelled at real size).", false},
              {"frames", T::Number, "Most simulation frames (1/60 s each; default 600).", false},
              {"masked", T::Bool, "Masked vertices are pins (default true).", false},
              {"collide", T::Bool, "Collide with the other visible Parts (default true).", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 DrapeRequest q;
                 const std::string fabric = argString(a, "fabric", "cotton");
                 if (!fabricFromName(fabric, q.fabric)) return {false, "cloth_drape: no fabric called " + fabric};
                 q.scale = float(argNumber(a, "scale", 1.0));
                 q.maxFrames = std::clamp(int(argNumber(a, "frames", 600.0)), 1, 20000);
                 q.useMask = argBool(a, "masked", true);
                 q.collide = argBool(a, "collide", true);
                 DrapeScene scene;
                 std::string err;
                 if (!drapeScene(doc, q, scene, err)) return {false, err};
                 DrapeOptions o;
                 o.maxFrames = q.maxFrames;
                 const DrapeResult r = drapeToRest(scene.setup, scene.colliders, scene.fabric, o);
                 const size_t moved = applyDrape(doc, r.positions, scene.scale, err);
                 if (moved == 0) return {false, err};
                 char msg[160];
                 std::snprintf(msg, sizeof msg, "Drape (%s): %s after %d frames, %.1f s, %zu vertices moved", fabricLabel(q.fabric),
                               r.rested ? "at rest" : "stopped", r.frames, r.milliseconds / 1000.0, moved);
                 return {true, msg};
             }});
}

} // namespace sf
