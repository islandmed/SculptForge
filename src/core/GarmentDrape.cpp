// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/GarmentDrape.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/PathStroke.h"
#include "core/StableMath.h"
#include "core/Units.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>

namespace sf {
namespace garment {

float patternScale(const Pattern& pat) { return pat.unitsPerMetre > 0.0f ? pat.unitsPerMetre : 1.0f; }

SeamGap widestSeamGap(const GarmentDrapeScene& scene, const std::vector<glm::vec3>& metres) {
    SeamGap g;
    for (const ClothSeam& s : scene.setup.seams) {
        if (s.a >= metres.size() || s.b >= metres.size()) continue;
        const float d = glm::length(metres[s.a] - metres[s.b]) * scene.scale;
        if (d > g.gap) g = {d, scene.mesh.panelOf[s.a], scene.mesh.panelOf[s.b]};
    }
    return g;
}

size_t particlesInside(const GarmentDrapeScene& scene, const std::vector<glm::vec3>& metres) {
    size_t n = 0;
    for (const glm::vec3& x : metres) {
        glm::vec3 point, normal;
        if (scene.colliders.closest(x, 0.05f, point, normal) && glm::dot(x - point, normal) < 0.0f) ++n;
    }
    return n;
}

std::string seamGapText(const Pattern& pat, const GarmentDrapeScene& scene, const std::vector<glm::vec3>& metres) {
    const SeamGap g = widestSeamGap(scene, metres);
    char buf[200];
    const auto name = [&](uint32_t p) { return p < pat.panels.size() ? pat.panels[p].name.c_str() : "?"; };
    std::snprintf(buf, sizeof buf, "widest seam gap %.0f mm (%s / %s)", double(g.gap / scene.scale * 1000.0f), name(g.panelA),
                  name(g.panelB));
    return buf;
}

float defaultWrap(const Pattern& pat, uint32_t panel) {
    if (panel >= pat.panels.size()) return 0.0f;
    for (const Seam& s : pat.seams)
        if (s.a.panel == panel && s.b.panel == panel) {
            // Nearly closed: its own seam's edges start 1.5 particle spacings apart, on the far side from the click.
            const float width = std::max(1.0f, panelMax(pat.panels[panel]).x - panelMin(pat.panels[panel]).x);
            return std::clamp(360.0f * (1.0f - 1.5f * pat.particleDistance / width), 300.0f, 355.0f);
        }
    return 0.0f;
}

glm::vec2 panelAnchor(const Panel& panel) {
    const std::vector<glm::vec2> line = outline(panel, 1.0f);
    const glm::vec2 lo = panelMin(panel), hi = panelMax(panel);
    const glm::vec2 centre = 0.5f * (lo + hi);
    constexpr int kRows = 200;
    std::vector<float> width(kRows), at(kRows);
    float widest = 0.0f;
    for (int r = 0; r < kRows; ++r) {
        const float y = lo.y + (hi.y - lo.y) * (float(r) + 0.5f) / float(kRows);
        float x0 = std::numeric_limits<float>::max(), x1 = std::numeric_limits<float>::lowest();
        for (size_t i = 0; i < line.size(); ++i) {
            const glm::vec2 a = line[i], b = line[(i + 1) % line.size()];
            if ((a.y <= y) == (b.y <= y)) continue;
            const float x = a.x + (b.x - a.x) * (y - a.y) / (b.y - a.y);
            x0 = std::min(x0, x);
            x1 = std::max(x1, x);
        }
        width[size_t(r)] = std::max(0.0f, x1 - x0);
        at[size_t(r)] = y;
        widest = std::max(widest, width[size_t(r)]);
    }
    // The widest band: rows within 1% of the widest. Its top is the panel's chest line when it is a band that
    // narrows above (a bodice under its armholes, a sleeve under its cap); a panel as wide all the way up (a
    // rectangle) or widest only at its foot (a flared skirt) goes by its middle.
    int first = -1, last = -1;
    for (int r = 0; r < kRows; ++r)
        if (width[size_t(r)] >= 0.99f * widest) {
            if (first < 0) first = r;
            last = r;
        }
    if (first < 0 || last - first > kRows * 9 / 10 || last < kRows / 5) return centre;
    return {centre.x, at[size_t(last)]};
}

glm::vec3 placedPoint(const Panel& panel, glm::vec2 q, float scale, const glm::vec2* anchor, glm::vec3* normal) {
    const Placement& pl = panel.place;
    const glm::vec3 f = glm::normalize(pl.facing);
    glm::vec3 up = pl.up - f * glm::dot(pl.up, f);
    up = glm::length(up) > 1e-6f ? glm::normalize(up) : glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3 right0 = glm::cross(up, f); // right x up = facing: the sheet seen from its right side
    const float turn = glm::radians(pl.turnDegrees);
    // (stableSin / stableCos: the same bits on every OS, so a pattern drapes alike everywhere)
    const float ct = stableCos(turn), st = stableSin(turn);
    const glm::vec3 right = ct * right0 + st * up;
    const glm::vec3 up2 = -st * right0 + ct * up;
    const glm::vec2 lo = panelMin(panel), hi = panelMax(panel);
    const glm::vec2 local = q - (anchor ? *anchor : panelAnchor(panel));
    const float mm = 0.001f * scale;
    const glm::vec3 centre = pl.point + f * (pl.gapMm * mm);
    const float wrap = glm::radians(std::clamp(pl.wrapDegrees, 0.0f, 360.0f));
    if (wrap < 1e-3f) {
        if (normal) *normal = f;
        return centre + (right * local.x + up2 * local.y) * mm;
    }
    // Curled back around an axis behind the centre, along the sheet's y: the width becomes the arc.
    const float r = std::max(1.0f, hi.x - lo.x) / wrap;
    const float a = local.x / r;
    const float sa = stableSin(a), ca = stableCos(a);
    if (normal) *normal = right * sa + f * ca; // (out from the curl's axis)
    return centre + (right * (r * sa) + f * (r * (ca - 1.0f)) + up2 * local.y) * mm;
}

bool placedPositions(const Pattern& pat, const GarmentMesh& m, std::vector<glm::vec3>& world, std::string& error,
                     std::vector<glm::vec3>* normals) {
    const float scale = patternScale(pat);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    world.assign(m.vertexCount(), glm::vec3(nan));
    if (normals) normals->assign(m.vertexCount(), glm::vec3(0.0f));
    std::vector<glm::vec2> anchors;
    for (const Panel& panel : pat.panels) anchors.push_back(panelAnchor(panel));
    bool all = true;
    for (size_t v = 0; v < m.vertexCount(); ++v) {
        const uint32_t p = m.panelOf[v];
        if (p >= pat.panels.size()) continue;
        const Panel& panel = pat.panels[p];
        if (!panel.place.placed) {
            if (all) error = "Place " + panel.name + " on the body first";
            all = false;
            continue;
        }
        world[v] = placedPoint(panel, m.flat[v], scale, &anchors[p], normals ? &(*normals)[v] : nullptr);
    }
    return all;
}

ClothSettings garmentSettings(const Pattern&) {
    ClothSettings s;
    s.substeps = 16;
    return s;
}

bool garmentStep(ClothSim& sim, int frame, const ClothSettings& base, const Fabric& fabric, int sewFrames) {
    ClothSettings s = base;
    if (frame < sewFrames) s.gravity = glm::vec3(0.0f);
    if (frame == 0 || frame == sewFrames) {
        sim.setSettings(s);
        Fabric f = fabric;
        // Garments keep 7 mm off the body (a sheet keeps its fabric's 2 mm): closer, the body's own vertices poked
        // up between the particles at the hips, shoulders and armpits, through the finished Part's 2 mm skin (at 5 mm
        // still 3-4 mm at the front of a shoulder and the side of the neck).
        f.thickness = std::max(f.thickness, kGarmentContact);
        if (frame < sewFrames) f.damping = std::max(f.damping, gSewDamping);
        sim.setFabric(f);
    }
    const bool moving = sim.step();
    return moving || frame < sewFrames;
}

DrapeResult garmentDrapeToRest(const GarmentDrapeScene& scene, const ClothSettings& base, int maxFrames, int sewFrames) {
    DrapeResult r;
    const auto t0 = std::chrono::steady_clock::now();
    ClothSim sim;
    if (!sim.init(scene.setup, scene.fabric, base)) return r;
    sim.setColliders(&scene.colliders);
    bool moving = true;
    while (moving && r.frames < maxFrames) moving = garmentStep(sim, r.frames++, base, scene.fabric, sewFrames);
    r.rested = !moving;
    r.positions = sim.positions();
    r.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return r;
}

bool garmentDrapeScene(const Document& doc, const Pattern& pat, GarmentDrapeScene& out, std::string& error,
                       uint32_t skipPart) {
    if (pat.panels.empty()) return error = "No garment pattern: start one from Templates", false;
    if (!triangulatePattern(pat, out.mesh, error)) return false;
    std::vector<glm::vec3> world;
    if (!placedPositions(pat, out.mesh, world, error, &out.outward)) return false;
    out.scale = patternScale(pat);
    const float inv = 1.0f / out.scale;
    FabricPreset preset = FabricPreset::Cotton;
    fabricFromName(pat.fabric, preset);
    out.fabric = fabricPreset(preset);
    ClothSetup& s = out.setup;
    s = ClothSetup{};
    s.positions.resize(world.size());
    for (size_t v = 0; v < world.size(); ++v) s.positions[v] = world[v] * inv;
    s.triangles = out.mesh.triangles;
    flatRest(out.mesh, s.rest);
    for (const SeamLink& l : out.mesh.links) s.seams.push_back({l.a, l.b});
    s.outward = out.outward; // (a particle placed inside the body leaves it the way its panel faces)
    out.colliders = ClothColliders{};
    std::vector<glm::vec3> scaled;
    for (int i = 0; i < doc.partCount(); ++i) {
        const Part& part = doc.parts[size_t(i)];
        if (!part.visible || part.id == skipPart) continue;
        const Mesh& other = doc.partMesh(i);
        if (other.micro || other.triangleCount() == 0 || !other.forgespheres.empty()) continue;
        scaled.resize(other.vertexCount());
        for (size_t v = 0; v < other.vertexCount(); ++v)
            scaled[v] = glm::vec3(part.transform * glm::vec4(other.positions[v], 1.0f)) * inv;
        if (!other.hasHidden()) {
            out.colliders.add(scaled, other.triangles);
        } else {
            std::vector<glm::uvec3> shown;
            for (uint32_t t = 0; t < other.triangleCount(); ++t)
                if (!other.triangleHidden(t)) shown.push_back(other.triangles[t]);
            out.colliders.add(scaled, shown);
        }
    }
    out.colliders.build();
    return true;
}

Mesh previewMesh(const GarmentMesh& m, const std::vector<glm::vec3>& world) {
    Mesh out;
    if (world.size() != m.vertexCount()) return out;
    std::vector<uint32_t> remap(world.size(), ~0u);
    for (size_t v = 0; v < world.size(); ++v)
        if (std::isfinite(world[v].x)) {
            remap[v] = uint32_t(out.positions.size());
            out.positions.push_back(world[v]);
        }
    for (const glm::uvec3& t : m.triangles) {
        const glm::uvec3 w(remap[t.x], remap[t.y], remap[t.z]);
        if (w.x == ~0u || w.y == ~0u || w.z == ~0u) continue;
        out.triangles.push_back(w);
        out.facegroups.push_back(m.panelOf[t.x] + 1);
    }
    out.rebuildTopology();
    out.recomputeAllNormals();
    return out;
}

Mesh drapedMesh(const Pattern& pat, const GarmentMesh& m, const std::vector<glm::vec3>& metres) {
    Mesh out = garmentToMesh(m, metres, pat.thickness);
    const float scale = patternScale(pat);
    for (glm::vec3& p : out.positions) p *= scale; // (a uniform scale: the normals stay)
    return out;
}

int addGarmentPart(Document& doc, Mesh mesh, uint32_t previewId) {
    int i = previewId ? doc.partIndex(previewId) : -1;
    if (i >= 0 && i != doc.activePart) {
        Part& part = doc.parts[size_t(i)];
        part.name = doc.uniquePartName("Garment");
        part.transform = glm::mat4(1.0f);
        PartData& s = *part.data;
        s.mesh = std::move(mesh);
        s.mesh.topologyDirty = true;
        s.bvh.build(s.mesh);
        s.bvhDirty = false;
    } else {
        i = doc.addPart(std::move(mesh), doc.uniquePartName("Garment"), false, doc.activePart + 1);
    }
    UndoRecord r;
    r.label = "Garment";
    r.part = std::make_shared<PartStep>();
    r.part->id = doc.parts[size_t(i)].id;
    r.part->at = i;
    doc.undo.push(std::move(r));
    doc.modified = true;
    return i;
}

} // namespace garment

namespace {

using namespace garment;
using T = ParamSpec::Type;

bool vec3Arg(const Args& a, const std::string& key, glm::vec3& out, std::string& error) {
    if (!a.count(key)) return true;
    glm::vec3 v;
    if (!parseVec3(argString(a, key), v) || !std::isfinite(v.x + v.y + v.z)) return error = "Bad " + key + " (expected x,y,z)", false;
    out = v;
    return true;
}

} // namespace

void registerGarmentDrapeCommands(CommandRegistry& reg) {
    reg.add({"garment_place",
             "Garments: put a panel on the body for draping (you choose where: nothing is placed for you). The panel's "
             "centre sits gap_mm in front of `point` along `facing`, its top toward `up`. Place each panel slightly in "
             "front of the body at its widest point.",
             {{"panel", T::String, "Panel name or number (1-based)", true},
              {"point", T::String, "x,y,z: the body point (world units)", false},
              {"facing", T::String, "x,y,z: out of the body, toward the viewer (default 0,0,1)", false},
              {"up", T::String, "x,y,z: the panel's top (default 0,1,0)", false},
              {"turn", T::Number, "Degrees the panel turns about `facing` (default 0)", false},
              {"wrap", T::Number, "Degrees the panel curls round (0 flat; a sleeve 300, the default for a panel sewn to itself)", false},
              {"gap_mm", T::Number, "How far in front of the point (default 30)", false},
              {"units_per_metre", T::Number, "The body's world units per metre (default from the scene's size: 100 over 20 units, 1000 over 600, else 1)", false},
              {"clear", T::Bool, "Take the panel off the body", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 Pattern pat;
                 if (!documentPattern(doc, pat)) return {false, "No garment pattern: run garment_new first"};
                 uint32_t i = 0;
                 std::string error;
                 if (!findPanelRef(pat, argString(a, "panel"), i, error)) return {false, error};
                 Panel& p = pat.panels[i];
                 if (argBool(a, "clear", false)) {
                     p.place = {};
                     setDocumentPattern(doc, pat);
                     return {true, p.name + " taken off the body"};
                 }
                 Placement pl = p.place;
                 if (!pl.placed) {
                     pl = Placement{};
                     pl.wrapDegrees = defaultWrap(pat, i);
                     if (!a.count("point")) return {false, "garment_place: give the body point (point=x,y,z)"};
                 }
                 if (!vec3Arg(a, "point", pl.point, error) || !vec3Arg(a, "facing", pl.facing, error) ||
                     !vec3Arg(a, "up", pl.up, error))
                     return {false, error};
                 if (glm::length(pl.facing) < 1e-6f) return {false, "garment_place: facing can't be 0,0,0"};
                 pl.facing = glm::normalize(pl.facing);
                 if (glm::length(glm::cross(pl.facing, pl.up)) < 1e-4f) return {false, "garment_place: up can't be along facing"};
                 pl.up = glm::normalize(pl.up);
                 pl.turnDegrees = std::clamp(float(argNumber(a, "turn", pl.turnDegrees)), -360.0f, 360.0f);
                 pl.wrapDegrees = std::clamp(float(argNumber(a, "wrap", pl.wrapDegrees)), 0.0f, 360.0f);
                 pl.gapMm = std::clamp(float(argNumber(a, "gap_mm", pl.gapMm)), 0.0f, 1000.0f);
                 pl.placed = true;
                 p.place = pl;
                 if (a.count("units_per_metre")) {
                     pat.unitsPerMetre = std::clamp(float(argNumber(a, "units_per_metre", 1.0)), 1e-4f, 1e4f);
                 } else if (pat.unitsPerMetre <= 0.0f) {
                     pat.unitsPerMetre = sceneUnitsPerMetre(doc);
                 }
                 setDocumentPattern(doc, pat);
                 int placed = 0;
                 for (const Panel& q : pat.panels) placed += q.place.placed ? 1 : 0;
                 char msg[200];
                 std::snprintf(msg, sizeof msg, "Placed %s (%d of %d placed; %g units per metre)", p.name.c_str(), placed,
                               int(pat.panels.size()), double(pat.unitsPerMetre));
                 return {true, msg};
             }});
    reg.add({"garment_drape",
             "Garments: drape the sewn pattern on the body (every panel placed with garment_place): the seams pull "
             "closed while it settles on the visible Parts, then it becomes the Part \"Garment\" (UVs from the flat "
             "pattern, the pattern's thickness). One undo step.",
             {{"frames", T::Number, "Most simulation frames (1/60 s each; default 900)", false},
              {"fabric", T::String, "silk, cotton, denim or leather (default: the pattern's)", false},
              {"sew_frames", T::Number, "Frames sewn with gravity off before it settles (default 60)", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 Pattern pat;
                 if (!documentPattern(doc, pat)) return {false, "No garment pattern: run garment_new first"};
                 if (a.count("fabric")) {
                     FabricPreset f;
                     if (!fabricFromName(argString(a, "fabric"), f)) return {false, "garment_drape: no fabric called " + argString(a, "fabric")};
                     pat.fabric = fabricPreset(f).id;
                 }
                 GarmentDrapeScene scene;
                 std::string error;
                 if (!garmentDrapeScene(doc, pat, scene, error)) return {false, "garment_drape: " + error};
                 const DrapeResult r = garmentDrapeToRest(scene, garmentSettings(pat),
                                                          std::clamp(int(argNumber(a, "frames", 900.0)), 1, 20000),
                                                          std::clamp(int(argNumber(a, "sew_frames", kSewFrames)), 0, 2000));
                 Mesh mesh = drapedMesh(pat, scene.mesh, r.positions);
                 if (mesh.triangleCount() == 0) return {false, "garment_drape: the garment came out empty"};
                 const size_t tris = mesh.triangleCount();
                 const int i = addGarmentPart(doc, std::move(mesh));
                 char msg[200];
                 std::snprintf(msg, sizeof msg, "Draped %s: %s after %d frames, %.1f s (%zu triangles), ",
                               doc.parts[size_t(i)].name.c_str(), r.rested ? "at rest" : "stopped", r.frames,
                               r.milliseconds / 1000.0, tris);
                 return {true, msg + seamGapText(pat, scene, r.positions)};
             }});
}

} // namespace sf
