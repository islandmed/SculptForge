// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Polypaint.h"
#include "core/LayerStack.h"
#include "core/MaterialRecipe.h"
#include "core/MeshMaps.h"
#include "core/Uv.h"
#include "core/MapExport.h"

#include "core/Commands.h"
#include "core/SubTools.h"
#include "core/Texture.h"
#include "core/Undo.h"
#include "io/PlyIO.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <memory>
#include <sstream>

namespace sf {

namespace {

struct NamedColor {
    const char* name;
    glm::vec3 srgb;
};

const NamedColor kNamedColors[] = {
    {"white", {1.0f, 1.0f, 1.0f}},      {"black", {0.0f, 0.0f, 0.0f}},     {"gray", {0.5f, 0.5f, 0.5f}},
    {"grey", {0.5f, 0.5f, 0.5f}},       {"red", {0.85f, 0.12f, 0.1f}},     {"green", {0.2f, 0.65f, 0.25f}},
    {"blue", {0.15f, 0.35f, 0.85f}},    {"yellow", {0.95f, 0.85f, 0.2f}},  {"orange", {0.95f, 0.5f, 0.12f}},
    {"purple", {0.5f, 0.25f, 0.7f}},    {"pink", {0.95f, 0.6f, 0.7f}},     {"brown", {0.45f, 0.28f, 0.16f}},
    {"cyan", {0.2f, 0.8f, 0.85f}},      {"magenta", {0.85f, 0.2f, 0.75f}}, {"skin", {0.87f, 0.66f, 0.55f}},
    {"bone", {0.89f, 0.85f, 0.75f}},    {"gold", {0.83f, 0.66f, 0.25f}},   {"blood", {0.45f, 0.03f, 0.03f}},
};

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = char(std::tolower(static_cast<unsigned char>(c)));
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

std::string trimLower(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    std::string out = s.substr(b, e - b);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return out;
}

bool parseHex(const std::string& s, glm::vec3& srgb) {
    if (s.size() != 3 && s.size() != 6) return false;
    for (char c : s)
        if (hexDigit(c) < 0) return false;
    for (int k = 0; k < 3; ++k) {
        const int v = s.size() == 3 ? hexDigit(s[size_t(k)]) * 17 : hexDigit(s[size_t(2 * k)]) * 16 + hexDigit(s[size_t(2 * k + 1)]);
        srgb[k] = float(v) / 255.0f;
    }
    return true;
}

// Vertex weight for a fill over `area`, before hidden faces are excluded.
float areaWeight(const Mesh& mesh, uint32_t v, PaintArea area) {
    switch (area) {
    case PaintArea::Unmasked: return 1.0f - mesh.maskAt(v);
    case PaintArea::Masked: return mesh.maskAt(v);
    default: return 1.0f;
    }
}

} // namespace

bool parseColor(const std::string& text, glm::vec3& linear) {
    const std::string s = trimLower(text);
    if (s.empty()) return false;
    for (const auto& n : kNamedColors)
        if (s == n.name) {
            linear = srgbToLinear(n.srgb);
            return true;
        }
    glm::vec3 srgb;
    if (parseHex(s[0] == '#' ? s.substr(1) : s, srgb)) {
        linear = srgbToLinear(srgb);
        return true;
    }
    // "r,g,b" or "r g b": 0..1, or 0..255 when any component exceeds 1.
    std::string list = s;
    std::replace(list.begin(), list.end(), ',', ' ');
    std::istringstream in(list);
    float c[3];
    if (!(in >> c[0] >> c[1] >> c[2])) return false;
    std::string rest;
    if (in >> rest) return false;
    const float scale = std::max({c[0], c[1], c[2]}) > 1.0f ? 1.0f / 255.0f : 1.0f;
    for (int k = 0; k < 3; ++k) {
        if (!std::isfinite(c[k]) || c[k] < 0.0f) return false;
        srgb[k] = std::min(c[k] * scale, 1.0f);
    }
    linear = srgbToLinear(srgb);
    return true;
}

std::string colorToHex(const glm::vec3& linear) {
    const glm::vec3 c = glm::clamp(linearToSrgb(linear), 0.0f, 1.0f);
    char buf[8];
    std::snprintf(buf, sizeof buf, "#%02x%02x%02x", int(std::lround(c.x * 255.0f)), int(std::lround(c.y * 255.0f)),
                  int(std::lround(c.z * 255.0f)));
    return buf;
}

size_t paintDab(Sculptor& sculptor, Mesh& mesh, const BVH& bvh, const DabParams& p, const glm::vec3& color,
                Symmetry sym) {
    if (!sculptor.strokeActive() || mesh.vertexCount() == 0 || p.radius <= 0.0f) return 0;
    if (!paintLayerEditable(mesh, mesh.paintLayers.empty() ? -1 : mesh.activePaintLayer)) return 0; // locked / hidden layer
    mesh.ensureColors();
    const float s = std::clamp(p.strength, 0.0f, 1.0f) * std::clamp(p.pressure, 0.0f, 1.0f);
    static thread_local std::vector<uint32_t> verts;
    auto clock = std::chrono::steady_clock::now();
    size_t changed = 0;
    for (const SymCopy& m : Sculptor::copies(sym)) {
        const glm::vec3 center = m.point(p.center), viewDir = m.dir(p.viewDir);
        static thread_local ConnectedScratch connected;
        brushRegion(mesh, bvh, center, viewDir, p.radius, p.connectedOnly, p.viewFalloff, verts, connected);
        for (uint32_t v : verts) {
            if (p.frontFacesOnly && v < mesh.normals.size() && glm::dot(mesh.normals[v], viewDir) > 0.0f) continue;
            const float w = brushFalloff(brushDistance(mesh.positions[v], center, viewDir, p.viewFalloff), p.radius, p.focal) * s *
                            vertexInfluence(mesh, v);
            if (w <= 0.0f) continue;
            sculptor.recordColor(mesh, v);
            paintVertex(mesh, v, color, w, p.invert); // Alt paints Base back to white, erases a layer
            ++changed;
        }
        sculptor.timings.vertices += verts.size();
    }
    sculptor.timings.kernelMs += lapMs(clock);
    ++sculptor.timings.dabs;
    return changed;
}

size_t paintFill(Mesh& mesh, const glm::vec3& color, PaintArea area, float amount) {
    if (mesh.vertexCount() == 0) return 0;
    if (!paintLayerEditable(mesh, mesh.paintLayers.empty() ? -1 : mesh.activePaintLayer)) return 0;
    mesh.ensureColors();
    amount = std::clamp(amount, 0.0f, 1.0f);
    size_t changed = 0;
    for (uint32_t v = 0; v < mesh.vertexCount(); ++v) {
        if (mesh.hasHidden() && mesh.vertexHidden(v)) continue;
        const float w = areaWeight(mesh, v, area) * amount;
        if (w <= 0.0f) continue;
        paintVertex(mesh, v, color, w);
        ++changed;
    }
    mesh.markAllDirty();
    return changed;
}

std::vector<float> cavityMap(const Mesh& mesh, int smoothing) {
    const size_t nv = mesh.vertexCount();
    std::vector<float> curv(nv, 0.0f);
    if (mesh.normals.size() != nv) return curv;
    // Signed curvature as in Renderer::updateCurvature: > 0 in creases.
    for (uint32_t v = 0; v < nv; ++v) {
        const auto ring = mesh.neighbors(v);
        if (ring.empty()) continue;
        glm::vec3 avg(0.0f);
        float edge = 0.0f;
        const glm::vec3 p = mesh.positions[v];
        for (uint32_t n : ring) {
            avg += mesh.positions[n];
            edge += glm::distance(mesh.positions[n], p);
        }
        avg /= float(ring.size());
        edge /= float(ring.size());
        curv[v] = edge > 0.0f ? glm::dot(mesh.normals[v], avg - p) / edge : 0.0f;
    }
    std::vector<float> tmp(nv);
    for (int it = 0; it < smoothing; ++it) {
        for (uint32_t v = 0; v < nv; ++v) {
            const auto ring = mesh.neighbors(v);
            float sum = curv[v];
            for (uint32_t n : ring) sum += curv[n];
            tmp[v] = sum / float(ring.size() + 1);
        }
        curv.swap(tmp);
    }
    for (float& c : curv) {
        const float t = std::clamp(c / 0.12f, 0.0f, 1.0f);
        c = t * t * (3.0f - 2.0f * t);
    }
    return curv;
}

size_t paintFromCavity(Mesh& mesh, const glm::vec3& color, float strength, int smoothing) {
    if (mesh.vertexCount() == 0) return 0;
    if (!paintLayerEditable(mesh, mesh.paintLayers.empty() ? -1 : mesh.activePaintLayer)) return 0;
    const std::vector<float> cavity = cavityMap(mesh, std::clamp(smoothing, 0, 50));
    mesh.ensureColors();
    strength = std::clamp(strength, 0.0f, 1.0f);
    size_t changed = 0;
    for (uint32_t v = 0; v < mesh.vertexCount(); ++v) {
        const float w = cavity[v] * strength * vertexInfluence(mesh, v);
        if (w <= 0.0f) continue;
        paintVertex(mesh, v, color, w);
        ++changed;
    }
    mesh.markAllDirty();
    return changed;
}

glm::vec3 sampleColor(const Mesh& mesh, uint32_t triangle, const glm::vec3& point) {
    if (triangle >= mesh.triangleCount()) return glm::vec3(1.0f);
    const glm::uvec3 t = mesh.triangles[triangle];
    const glm::vec3 a = mesh.positions[t.x], b = mesh.positions[t.y], c = mesh.positions[t.z];
    // Barycentric coordinates of the point projected onto the triangle.
    const glm::vec3 v0 = b - a, v1 = c - a, v2 = point - a;
    const float d00 = glm::dot(v0, v0), d01 = glm::dot(v0, v1), d11 = glm::dot(v1, v1);
    const float d20 = glm::dot(v2, v0), d21 = glm::dot(v2, v1);
    const float denom = d00 * d11 - d01 * d01;
    if (std::abs(denom) < 1e-20f) return mesh.colorAt(t.x);
    float wb = std::clamp((d11 * d20 - d01 * d21) / denom, 0.0f, 1.0f);
    float wc = std::clamp((d00 * d21 - d01 * d20) / denom, 0.0f, 1.0f);
    if (wb + wc > 1.0f) {
        const float k = 1.0f / (wb + wc);
        wb *= k;
        wc *= k;
    }
    return mesh.colorAt(t.x) * (1.0f - wb - wc) + mesh.colorAt(t.y) * wb + mesh.colorAt(t.z) * wc;
}

namespace {

// A colour from "color" (any parseColor form) or numeric "r"/"g"/"b" (sRGB 0..1).
// Returns false with `error` empty when no colour was given.
bool colorArg(const Args& a, glm::vec3& linear, std::string& error) {
    if (a.contains("color")) {
        const std::string text = argString(a, "color");
        if (parseColor(text, linear)) return true;
        error = "Unrecognised colour '" + text + "' (use #rrggbb, r,g,b or a name such as red)";
        return false;
    }
    if (a.contains("r") || a.contains("g") || a.contains("b")) {
        glm::vec3 srgb(float(argNumber(a, "r", 0.0)), float(argNumber(a, "g", 0.0)), float(argNumber(a, "b", 0.0)));
        if (std::max({srgb.x, srgb.y, srgb.z}) > 1.0f) srgb /= 255.0f;
        linear = srgbToLinear(glm::clamp(srgb, 0.0f, 1.0f));
        return true;
    }
    return false;
}

// Undo step for a whole-mesh colour edit, pushed before the edit: the old
// colours, or a mesh snapshot when there is no polypaint yet, so undo removes
// the attribute again instead of leaving the mesh painted white.
UndoRecord colorUndo(const Mesh& mesh, std::string label) {
    if (mesh.hasColors()) return UndoRecord::captureColors(mesh, std::move(label));
    UndoRecord r;
    r.label = std::move(label);
    r.mesh = std::make_unique<Mesh>(mesh);
    return r;
}

// Why the active paint layer can't be painted ("" = it can).
std::string paintBlocked(const Mesh& mesh) {
    if (paintsTexture(mesh)) {
        const PaintTexture& t = *mesh.texture;
        if (t.layers.empty() || t.activeLayer < 0 || t.activeLayer >= int(t.layers.size())) return {};
        const TextureLayer& l = t.layers[size_t(t.activeLayer)];
        if (l.locked) return "Texture layer '" + l.name + "' is locked";
        if (!l.visible) return "Texture layer '" + l.name + "' is hidden";
        return {};
    }
    if (mesh.paintLayers.empty() || mesh.activePaintLayer < 0) return {};
    const PaintLayer& l = mesh.paintLayers[size_t(mesh.activePaintLayer)];
    if (l.locked) return "Paint layer '" + l.name + "' is locked";
    if (!l.visible) return "Paint layer '" + l.name + "' is hidden";
    return {};
}

const char* kColorHelp = "Colour: rrggbb or #rrggbb (quote it in scripts), r,g,b (0-1 or 0-255) or a name (white, black, red, skin, bone, ...).";

} // namespace

void registerPaintCommands(CommandRegistry& registry) {
    using T = ParamSpec::Type;

    registry.add({"set_color",
                  "Set the current vertex paint colour used by the Paint brush and paint_fill.",
                  {{"color", T::String, kColorHelp, false},
                   {"r", T::Number, "Red 0-1 (sRGB), instead of color.", false},
                   {"g", T::Number, "Green 0-1 (sRGB).", false},
                   {"b", T::Number, "Blue 0-1 (sRGB).", false}},
                  [](Document& doc, const Args& a) -> CommandResult {
                      glm::vec3 c;
                      std::string err;
                      if (!colorArg(a, c, err)) return {false, err.empty() ? "Give a color (or r, g, b)" : err};
                      doc.paintColor = c;
                      return {true, "Paint colour " + colorToHex(c)};
                  }});

    registry.add({"paint_fill",
                  "Fill the active paint layer (of the texture when the Part paints into one) with a colour (undoable). By default the mask is respected: only "
                  "unmasked areas are painted. Hidden faces are never painted.",
                  {{"color", T::String, std::string(kColorHelp) + " Default: the current paint colour.", false},
                   {"area", T::String, "unmasked (default), masked (only the masked area) or all (ignore the mask).", false},
                   {"strength", T::Number, "Blend amount 0-1. Default 1.", false}},
                  [](Document& doc, const Args& a) -> CommandResult {
                      if (doc.mesh.vertexCount() == 0) return {false, "There is no mesh to paint"};
                      glm::vec3 c = doc.paintColor;
                      std::string err;
                      if (!colorArg(a, c, err) && !err.empty()) return {false, err};
                      const std::string areaName = argString(a, "area", "unmasked");
                      PaintArea area;
                      if (areaName == "unmasked") area = PaintArea::Unmasked;
                      else if (areaName == "masked") area = PaintArea::Masked;
                      else if (areaName == "all") area = PaintArea::All;
                      else return {false, "area must be unmasked, masked or all"};
                      if (area == PaintArea::Masked && !doc.mesh.hasMask()) return {false, "Nothing is masked"};
                      if (std::string why = paintBlocked(doc.mesh); !why.empty()) return {false, why};
                      if (paintsTexture(doc.mesh)) {
                          Mesh& m = doc.mesh;
                          const float amount = std::clamp(float(argNumber(a, "strength", 1.0)), 0.0f, 1.0f);
                          std::vector<float> w(m.vertexCount());
                          for (uint32_t v = 0; v < m.vertexCount(); ++v)
                              w[v] = (area == PaintArea::Unmasked ? 1.0f - m.maskAt(v)
                                      : area == PaintArea::Masked ? m.maskAt(v)
                                                                  : 1.0f) * amount;
                          UndoRecord r = captureTexture(m, "Fill Texture");
                          const size_t n = textureFill(m, c, w);
                          finishTextureUndo(r, m);
                          doc.undo.push(std::move(r));
                          return {true, "Filled " + std::to_string(n) + " texels with " + colorToHex(c)};
                      }
                      doc.undo.push(colorUndo(doc.mesh, "Fill Color"));
                      const size_t n = paintFill(doc.mesh, c, area, float(argNumber(a, "strength", 1.0)));
                      doc.modified = true;
                      return {true, "Filled " + std::to_string(n) + " vertices with " + colorToHex(c)};
                  }});

    registry.add({"paint_clear", "Remove all vertex paint from the mesh, paint layers included (undoable).", {},
                  [](Document& doc, const Args&) -> CommandResult {
                      if (paintsTexture(doc.mesh)) {
                          UndoRecord r = captureTexture(doc.mesh, "Clear Texture");
                          PaintTexture& t = *doc.mesh.texture;
                          t.layers.clear();
                          t.base.clear();
                          t.activeLayer = -1;
                          clearTextureLayer(t, -1);
                          finishTextureUndo(r, doc.mesh);
                          doc.undo.push(std::move(r));
                          return {true, "Texture cleared to white (its layers too)"};
                      }
                      if (!doc.mesh.hasColors()) return {true, "The mesh has no vertex paint"};
                      // A snapshot, so redo removes the attribute again instead of leaving white paint.
                      Mesh cleared = doc.mesh;
                      cleared.clearColors();
                      cleared.paintLayers.clear();
                      cleared.paintBase.clear();
                      cleared.activePaintLayer = -1;
                      doc.replaceMeshUndoable(std::move(cleared), "Clear Vertex Paint");
                      return {true, "Vertex Paint cleared"};
                  }});

    registry.add({"paint_from_cavity",
                  "Darken (or tint) the creases and cavities of the sculpt with vertex paint, like ZBrush's cavity "
                  "masking plus fill (undoable, respects the mask).",
                  {{"color", T::String, std::string(kColorHelp) + " Default: a dark brown.", false},
                   {"strength", T::Number, "0-1. Default 0.7.", false},
                   {"smoothing", T::Number, "Blur passes over the cavity map (0-50). Default 2.", false}},
                  [](Document& doc, const Args& a) -> CommandResult {
                      if (doc.mesh.vertexCount() == 0) return {false, "There is no mesh to paint"};
                      glm::vec3 c = srgbToLinear(glm::vec3(0.16f, 0.1f, 0.08f));
                      std::string err;
                      if (!colorArg(a, c, err) && !err.empty()) return {false, err};
                      if (std::string why = paintBlocked(doc.mesh); !why.empty()) return {false, why};
                      if (paintsTexture(doc.mesh)) {
                          Mesh& m = doc.mesh;
                          std::vector<float> w = cavityMap(m, std::clamp(int(argNumber(a, "smoothing", 2)), 0, 50));
                          const float strength = std::clamp(float(argNumber(a, "strength", 0.7)), 0.0f, 1.0f);
                          for (uint32_t v = 0; v < m.vertexCount(); ++v) w[v] *= strength * (1.0f - m.maskAt(v));
                          UndoRecord r = captureTexture(m, "Paint Cavity");
                          const size_t n = textureFill(m, c, w);
                          finishTextureUndo(r, m);
                          doc.undo.push(std::move(r));
                          return {true, "Painted cavities on " + std::to_string(n) + " texels"};
                      }
                      doc.undo.push(colorUndo(doc.mesh, "Paint Cavity"));
                      const size_t n = paintFromCavity(doc.mesh, c, float(argNumber(a, "strength", 0.7)),
                                                       int(argNumber(a, "smoothing", 2)));
                      doc.modified = true;
                      return {true, "Painted cavities on " + std::to_string(n) + " vertices"};
                  }});

    registry.add({"export_ply",
                  "Export the visible Parts as one binary PLY mesh with vertex colours (vertex paint), for scan tools, MeshLab, Blender.",
                  {{"path", T::String, "Output .ply path (UTF-8).", true}},
                  [](Document& doc, const Args& a) -> CommandResult {
                      const std::string path = argString(a, "path");
                      std::string err;
                      Mesh merged; // no copy for a single SubTool
                      const Mesh& mesh = doc.subtoolCount() == 1 ? doc.mesh : (merged = mergedVisibleMesh(doc));
                      if (mesh.triangleCount() == 0) return {false, "Nothing visible to export"};
                      if (!savePlyMesh(path, mesh, err)) return {false, err};
                      return {true, "Exported " + path + (mesh.hasColors() ? " (with vertex paint)" : "")};
                  }});
    registerPaintLayerCommands(registry); // paint_layer_* (core/PaintLayer.cpp)
    registerTextureCommands(registry);    // texture_*, uv_auto, paint_target (core/TextureCommands.cpp)
    registerUvCommands(registry);         // uv_unwrap, uv_pack, uv_seam, uv_select, ... (core/UvCommands.cpp)
    registerTextureChannelCommands(registry); // texture_channels, paint_material, channel_view (core/TextureChannelCommands.cpp)
    registerMeshMapCommands(registry);        // mesh_maps (core/MeshMaps.cpp)
    registerLayerStackCommands(registry);     // fill_layer (core/LayerStack.cpp)
    registerMaterialRecipeCommands(registry); // material_recipe (core/MaterialRecipe.cpp)
    registerMapExportCommands(registry);      // export_maps (core/MapExport.cpp)
}

} // namespace sf
