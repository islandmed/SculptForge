// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// v0.2 cross-feature checks: masking, polygroups/visibility, the new brushes
// and alphas, polypaint, project files and GoZ working on the same mesh.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Masking.h"
#include "core/Polygroups.h"
#include "core/Polypaint.h"
#include "core/Primitives.h"
#include "core/Project.h"
#include "core/Sculpt.h"
#include "io/GozIO.h"

#include <cmath>
#include <filesystem>

using namespace sf;

namespace {

const CommandRegistry& registry() {
    static CommandRegistry r;
    return r;
}

bool run(Document& d, const std::string& name, const Args& args = {}) {
    const CommandResult r = registry().execute(d, name, args);
    if (!r.ok) std::printf("  %s: %s\n", name.c_str(), r.message.c_str());
    return r.ok;
}

// Plane (y = 0, normal +y) with the left half (x < 0) masked by a mask stroke
// the way the app does it, so every later step works on a "real" mask.
Document maskedPlane() {
    Document d = makeDoc(makePlane(48));
    DabParams p;
    p.center = glm::vec3(-0.5f, 0.0f, 0.0f);
    p.viewDir = glm::vec3(0.0f, -1.0f, 0.0f);
    p.radius = 0.2f;
    p.strength = 1.0f;
    CHECK(d.sculptor.beginStroke(d.mesh, "Mask"));
    d.sculptor.dabMask(d.mesh, d.bvh, p, Symmetry{});
    UndoRecord r = d.sculptor.endStroke();
    CHECK(!r.empty());
    d.undo.push(std::move(r));
    // Then the whole left half, as Ctrl+painting over it would.
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) d.mesh.mask[v] = d.mesh.positions[v].x < -0.02f ? 1.0f : 0.0f;
    return d;
}

DabParams dabOn(const glm::vec3& c, float radius) {
    DabParams p;
    p.center = c;
    p.viewDir = glm::vec3(0.0f, -1.0f, 0.0f);
    p.radius = radius;
    p.strength = 1.0f;
    p.strokeDelta = glm::vec3(0.02f, 0.0f, 0.02f);
    return p;
}

// Left-half vertices well inside the mask / hidden area (away from the seam).
bool leftUnchanged(const Mesh& m, const std::vector<glm::vec3>& before) {
    for (uint32_t v = 0; v < m.vertexCount(); ++v)
        if (before[v].x < -0.1f && m.positions[v] != before[v]) return false;
    return true;
}

bool rightChanged(const Mesh& m, const std::vector<glm::vec3>& before) {
    for (uint32_t v = 0; v < m.vertexCount(); ++v)
        if (before[v].x > 0.1f && glm::distance(m.positions[v], before[v]) > 1e-5f) return true;
    return false;
}

} // namespace

// Every brush (old and new) with every built-in alpha, mirrored by X symmetry
// onto the masked half, leaves the mask and the masked vertices alone.
TEST(v02_every_brush_and_alpha_respects_painted_mask) {
    for (const char* alpha : {"round", "square", "noise"}) {
        for (int i = 0; i < int(BrushType::Count); ++i) {
            const auto type = BrushType(i);
            if (type == BrushType::Move) continue; // grab, not a dab brush
            Document d = maskedPlane();
            CHECK(run(d, "brush_set_alpha", {{"name", std::string(alpha)}}));
            const auto before = d.mesh.positions;
            const auto mask = d.mesh.mask;
            DabParams p = dabOn(glm::vec3(0.5f, 0.0f, 0.0f), 0.3f);
            p.alpha = &d.brushAlpha;
            if (type == BrushType::Trim) p.invert = true; // fill: a flat plane has nothing to cut
            CHECK(d.sculptor.beginStroke(d.mesh, brushName(type)));
            for (int k = 0; k < 3; ++k) d.sculptor.dab(d.mesh, d.bvh, type, p, Symmetry{true, false, false});
            d.undo.push(d.sculptor.endStroke());
            CHECK(leftUnchanged(d.mesh, before));
            // A flat plane has nothing to smooth or flatten.
            if (type != BrushType::Smooth && type != BrushType::Flatten && type != BrushType::Morph) { // (Morph: no target)
                const bool changed = rightChanged(d.mesh, before);
                if (!changed) std::printf("  %s (%s alpha) did not sculpt\n", brushName(type), alpha);
                CHECK(changed);
            }
            CHECK(d.mesh.mask == mask);
        }
    }
}

// The mask stroke, polygroups built from it and visibility: brushes, polypaint
// and the mask commands all leave the hidden half alone.
TEST(v02_hidden_polygroup_is_untouched_by_brushes_paint_and_mask) {
    Document d = maskedPlane();
    CHECK(run(d, "polygroup_from_mask"));
    const uint32_t leftGroup = d.mesh.polygroupAt(0) != 0 ? d.mesh.polygroupAt(0) : nextPolygroupId(d.mesh) - 1;
    CHECK(run(d, "mask_clear"));
    CHECK(!d.mesh.hasMask());
    CHECK(run(d, "hide_polygroup", {{"group", double(leftGroup)}}));
    CHECK(d.mesh.hasHidden());
    size_t hiddenVerts = 0;
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) {
        if (!d.mesh.vertexHidden(v)) continue;
        ++hiddenVerts;
        CHECK(d.mesh.positions[v].x < 0.0f);
    }
    CHECK(hiddenVerts > 100);

    // New brushes mirrored onto the hidden half.
    const auto before = d.mesh.positions;
    for (BrushType type : {BrushType::DamStandard, BrushType::ClayBuildup, BrushType::Layer, BrushType::Nudge}) {
        CHECK(d.sculptor.beginStroke(d.mesh, brushName(type)));
        d.sculptor.dab(d.mesh, d.bvh, type, dabOn(glm::vec3(0.5f, 0.0f, 0.0f), 0.3f), Symmetry{true, false, false});
        d.undo.push(d.sculptor.endStroke());
    }
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v)
        if (d.mesh.vertexHidden(v)) CHECK(d.mesh.positions[v] == before[v]);
    CHECK(rightChanged(d.mesh, before));

    // Paint brush and fills.
    glm::vec3 red;
    CHECK(parseColor("red", red));
    CHECK(d.sculptor.beginStroke(d.mesh, "Paint"));
    paintDab(d.sculptor, d.mesh, d.bvh, dabOn(glm::vec3(0.5f, 0.0f, 0.0f), 0.3f), red, Symmetry{true, false, false});
    d.undo.push(d.sculptor.endStroke());
    CHECK(run(d, "paint_fill", {{"color", std::string("0,0,1")}, {"strength", 0.5}}));
    CHECK(run(d, "mask_all"));
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) {
        if (!d.mesh.vertexHidden(v)) continue;
        CHECK(d.mesh.colorAt(v) == glm::vec3(1.0f));
        CHECK(d.mesh.mask[v] == 0.0f);
    }

    // show_all brings both groups back; undoing everything restores the plane.
    CHECK(run(d, "show_all"));
    CHECK(visiblePolygroupCount(d.mesh) == 2);
    const Mesh plane = makePlane(48);
    while (d.undo.canUndo()) d.undo.undo(d.mesh, d.bvh);
    CHECK(d.mesh.positions == plane.positions);
    bool anyHidden = false;
    for (uint32_t t = 0; t < d.mesh.triangleCount(); ++t) anyHidden |= d.mesh.triangleHidden(t);
    CHECK(!anyHidden);
}

// RGB-while-sculpting: a sculpt dab and a paint dab inside one stroke undo
// together, and a DragRect-style restoreStroke reverts the paint as well.
TEST(v02_sculpt_and_paint_share_one_stroke) {
    Document d = makeDoc(makePlane(32));
    d.mesh.ensureColors();
    const auto pos = d.mesh.positions;
    const auto col = d.mesh.colors;
    glm::vec3 green;
    CHECK(parseColor("00ff00", green));
    const DabParams p = dabOn(glm::vec3(0.0f), 0.4f);
    CHECK(d.sculptor.beginStroke(d.mesh, "Standard"));
    paintDab(d.sculptor, d.mesh, d.bvh, p, green, Symmetry{});
    d.sculptor.dab(d.mesh, d.bvh, BrushType::ClayBuildup, p, Symmetry{});
    CHECK(d.mesh.colors != col && d.mesh.positions != pos);
    d.sculptor.restoreStroke(d.mesh, d.bvh);
    CHECK(d.mesh.colors == col && d.mesh.positions == pos);
    paintDab(d.sculptor, d.mesh, d.bvh, p, green, Symmetry{});
    d.sculptor.dab(d.mesh, d.bvh, BrushType::ClayBuildup, p, Symmetry{});
    d.undo.push(d.sculptor.endStroke());
    CHECK(d.dirty());
    CHECK(d.undo.undo(d.mesh, d.bvh));
    CHECK(d.mesh.colors == col && d.mesh.positions == pos);
    CHECK(!d.undo.canUndo());
}

// A project saved after masking, painting, grouping and hiding reopens with
// all of it, through the commands the CLI/Python/MCP use.
TEST(v02_project_keeps_attributes_made_by_the_features) {
    Document d = maskedPlane();
    CHECK(run(d, "polygroup_from_mask"));
    CHECK(run(d, "mask_blur", {{"iterations", 2.0}}));
    CHECK(run(d, "paint_fill", {{"color", std::string("skin")}}));
    CHECK(run(d, "paint_from_cavity"));
    CHECK(run(d, "hide_polygroup", {{"group", double(nextPolygroupId(d.mesh) - 1)}}));
    CHECK(d.mesh.hasMask() && d.mesh.hasColors() && d.mesh.hasPolygroups() && d.mesh.hasHidden());
    CHECK(d.dirty());

    const std::string path = tempPath("sf_v02_integration.sfproj");
    CHECK(run(d, "save_project", {{"path", path}}));
    CHECK(!d.dirty());

    Document back = makeDoc(makeIcosphere(1));
    CHECK(run(back, "open", {{"path", path}}));
    CHECK(back.mesh.positions == d.mesh.positions);
    CHECK(back.mesh.mask == d.mesh.mask);
    CHECK(back.mesh.colors == d.mesh.colors);
    CHECK(back.mesh.polygroups == d.mesh.polygroups);
    CHECK(back.mesh.hidden == d.mesh.hidden);
    CHECK(!back.dirty());
    // The reopened scene keeps working: commands still honour the hidden part.
    CHECK(run(back, "mask_invert"));
    CHECK(back.dirty());
    for (uint32_t v = 0; v < back.mesh.vertexCount(); ++v)
        if (back.mesh.vertexHidden(v)) CHECK(back.mesh.mask[v] == d.mesh.mask[v]);
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

// GoZ carries mask, polypaint and polygroups made by the new tools (but has
// no hidden flag), and delete_hidden keeps the attributes aligned for export.
TEST(v02_goz_keeps_mask_paint_and_groups) {
    Document d = maskedPlane();
    CHECK(run(d, "polygroup_from_mask"));
    CHECK(run(d, "paint_fill", {{"color", std::string("#336699")}, {"area", std::string("masked")}}));
    const std::string path = tempPath("sf_v02_integration.GoZ");
    CHECK(run(d, "goz_export", {{"path", path}, {"name", std::string("Plane")}}));

    Document back;
    CHECK(run(back, "goz_import", {{"path", path}}));
    CHECK(back.gozName == "Plane");
    CHECK(back.mesh.vertexCount() == d.mesh.vertexCount());
    CHECK(back.mesh.triangleCount() == d.mesh.triangleCount());
    CHECK(back.mesh.hasMask() && back.mesh.hasColors() && back.mesh.hasPolygroups());
    float maskErr = 0.0f, colorErr = 0.0f;
    for (uint32_t v = 0; v < d.mesh.vertexCount() && v < back.mesh.vertexCount(); ++v) {
        maskErr = std::max(maskErr, std::abs(back.mesh.mask[v] - d.mesh.mask[v]));
        colorErr = std::max(colorErr, glm::length(back.mesh.colors[v] - d.mesh.colors[v]));
    }
    CHECK(maskErr < 0.01f);  // GoZ stores the mask as 16 bits
    CHECK(colorErr < 0.02f); // and polypaint as 8-bit sRGB
    size_t groupsDiffer = 0;
    for (uint32_t t = 0; t < d.mesh.triangleCount() && t < back.mesh.triangleCount(); ++t)
        groupsDiffer += (back.mesh.polygroupAt(t) == back.mesh.polygroupAt(0)) != (d.mesh.polygroupAt(t) == d.mesh.polygroupAt(0));
    CHECK(groupsDiffer == 0);

    // Deleting the hidden half compacts every attribute, and the result still exports.
    CHECK(run(back, "hide_polygroup", {{"group", double(back.mesh.polygroupAt(0))}}));
    CHECK(run(back, "delete_hidden"));
    CHECK(back.mesh.triangleCount() > 0 && back.mesh.triangleCount() < d.mesh.triangleCount());
    CHECK(back.mesh.mask.size() == back.mesh.vertexCount());
    CHECK(back.mesh.colors.size() == back.mesh.vertexCount());
    CHECK(back.mesh.polygroups.empty() || back.mesh.polygroups.size() == back.mesh.triangleCount());
    CHECK(run(back, "goz_export", {{"path", path}}));
    Mesh again;
    std::string err;
    CHECK(loadGoz(path, again, err));
    CHECK(again.triangleCount() == back.mesh.triangleCount());
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

// The new commands are all in one registry (what the CLI, Python and MCP list).
TEST(v02_commands_registered_together) {
    for (const char* name :
         {"mask_clear", "mask_all", "mask_invert", "mask_blur", "mask_sharpen", "mask_grow", "mask_shrink",
          "mask_by_cavity", "mask_by_polygroup", "mask_rect", "mask_lasso", "polygroup_auto_by_normals", "polygroup_by_connectivity",
          "polygroup_from_mask", "polygroup_clear", "polygroup_merge_visible", "hide_polygroup", "isolate_polygroup",
          "show_all", "invert_visibility", "hide_rect", "hide_lasso", "delete_hidden", "brush_set_alpha", "set_color", "paint_fill",
          "paint_clear", "paint_from_cavity", "export_ply", "goz_import", "goz_export", "goz_send", "goz_receive",
          "export_stl", "save_project", "open_project"})
        CHECK(registry().find(name) != nullptr);
}
