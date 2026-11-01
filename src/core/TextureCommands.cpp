// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Commands for texture painting (core/Texture.h) and automatic UVs (core/AutoUv.h), plus the texture side
// of the paint_layer_* commands.
#include "core/AutoUv.h"
#include "core/Polypaint.h"
#include "core/Texture.h"
#include "core/TextureTransfer.h"
#include "core/Undo.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace sf {

namespace {

using T = ParamSpec::Type;

// "2048", "2k", "4K" -> texels; 0 when not one of the offered sizes.
int parseTextureSize(const std::string& text) {
    std::string s;
    for (char c : text)
        if (!std::isspace(static_cast<unsigned char>(c))) s += char(std::tolower(static_cast<unsigned char>(c)));
    if (s.empty()) return 0;
    int n = 0;
    try {
        n = s.back() == 'k' ? std::stoi(s.substr(0, s.size() - 1)) * 1024 : std::stoi(s);
    } catch (...) {
        return 0;
    }
    return validTextureSize(n) ? n : 0;
}

std::string sizeLabel(const PaintTexture& t) {
    if (t.width == t.height && t.width % 1024 == 0) return std::to_string(t.width / 1024) + "K";
    return std::to_string(t.width) + "x" + std::to_string(t.height);
}

std::string percent(float x) { return std::to_string(int(std::lround(x * 100.0f))) + "%"; }

std::string blendList() {
    std::string s;
    for (int i = 0; i < kBlendModeCount; ++i) s += std::string(i ? ", " : "") + blendModeId(BlendMode(i));
    return s;
}

std::string layerName(const PaintTexture& t, int i) { return i < 0 ? std::string("Base") : t.layers[size_t(i)].name; }

// The layer named by "layer" or "index" (as the polypaint commands), the active one by default; -2 + error if none.
int layerArg(const PaintTexture& t, const Args& a, std::string& error) {
    if (a.contains("layer")) {
        const std::string name = argString(a, "layer");
        const int i = findTextureLayer(t, name);
        if (i == -2) error = "No texture layer called '" + name + "'";
        return i;
    }
    if (a.contains("index")) {
        const int i = int(argNumber(a, "index", -1));
        if (i < -1 || i >= int(t.layers.size())) {
            error = "No texture layer " + std::to_string(i) + " (-1 = Base, 0-" + std::to_string(int(t.layers.size()) - 1) +
                    " above it)";
            return -2;
        }
        return i;
    }
    return t.activeLayer >= -1 && t.activeLayer < int(t.layers.size()) ? t.activeLayer : -1;
}

const char* kNoUvs = "The Part has no UVs: run Auto UV first (Tool > Texture Map > Auto UV, or uv_auto)";
const char* kNoTexture = "The Part has no texture: create one first (Tool > Texture Map > New, or texture_create)";

} // namespace

CommandResult textureLayerCommand(Document& doc, const std::string& verb, const Args& a) {
    Mesh& m = doc.mesh;
    if (!hasTexture(m)) return {false, kNoTexture};
    std::string err;
    const PaintTexture& cur = *m.texture;
    if (verb == "list") {
        std::string out;
        for (int i = int(cur.layers.size()) - 1; i >= -1; --i) {
            out += (i == (cur.layers.empty() ? -1 : cur.activeLayer) ? "* " : "  ") + std::to_string(i) + " " + layerName(cur, i);
            if (i >= 0) {
                const TextureLayer& l = cur.layers[size_t(i)];
                out += std::string(" (") + blendModeId(l.blend) + ", " + percent(l.opacity) + (l.visible ? "" : ", hidden") +
                       (l.locked ? ", locked" : "") + ")";
            }
            if (i > -1) out += "\n";
        }
        return {true, "Texture layers:\n" + out};
    }
    if (verb == "select") {
        if (!a.contains("index") && !a.contains("layer")) return {false, "Give index or layer"};
        const int i = layerArg(cur, a, err);
        if (i == -2) return {false, err};
        m.texture->activeLayer = cur.layers.empty() ? -1 : i; // a setting, shared copies may see it too
        return {true, "Painting on texture layer " + layerName(cur, i)};
    }
    if (verb == "add") {
        BlendMode blend = BlendMode::Normal;
        if (a.contains("blend") && !parseBlendMode(argString(a, "blend"), blend))
            return {false, "Unknown blend mode (use " + blendList() + ")"};
        UndoRecord r = captureTexture(m, "Add Texture Layer");
        PaintTexture& t = *m.texture;
        const int at = addTextureLayer(t, argString(a, "name"), t.activeLayer >= 0 && t.activeLayer < int(t.layers.size()) ? t.activeLayer : -2);
        TextureLayer& l = t.layers[size_t(at)];
        l.blend = blend;
        l.opacity = std::clamp(float(argNumber(a, "opacity", 1.0)), 0.0f, 1.0f);
        finishTextureUndo(r, m);
        doc.undo.push(std::move(r));
        return {true, "Added texture layer '" + l.name + "' (index " + std::to_string(at) + ", " + blendModeLabel(blend) + ")"};
    }
    const int i = layerArg(cur, a, err);
    if (i == -2) return {false, err};
    if (verb == "set") {
        if (i < 0) return {false, "Base is always visible, opaque and Normal; add a layer to blend"};
        BlendMode blend = cur.layers[size_t(i)].blend;
        if (a.contains("blend") && !parseBlendMode(argString(a, "blend"), blend))
            return {false, "Unknown blend mode (use " + blendList() + ")"};
        std::string name = cur.layers[size_t(i)].name;
        if (a.contains("name")) {
            const std::string wanted = argString(a, "name");
            if (wanted.empty() || wanted == "Base") return {false, "Choose another layer name"};
            if (wanted != name) name = uniqueTextureLayerName(cur, wanted);
        }
        UndoRecord r = captureTexture(m, "Texture Layer Settings");
        PaintTexture& t = *m.texture;
        TextureLayer& l = t.layers[size_t(i)];
        l.name = name;
        l.blend = blend;
        if (a.contains("opacity")) l.opacity = std::clamp(float(argNumber(a, "opacity", 1.0)), 0.0f, 1.0f);
        l.visible = argBool(a, "visible", l.visible);
        l.locked = argBool(a, "locked", l.locked);
        compositeTextureAll(t);
        finishTextureUndo(r, m);
        doc.undo.push(std::move(r));
        return {true, "Texture layer '" + l.name + "': " + blendModeLabel(l.blend) + ", " + percent(l.opacity) +
                          (l.visible ? "" : ", hidden") + (l.locked ? ", locked" : "")};
    }
    if (verb == "duplicate") {
        UndoRecord r = captureTexture(m, "Duplicate Texture Layer");
        const int at = duplicateTextureLayer(*m.texture, i);
        finishTextureUndo(r, m);
        doc.undo.push(std::move(r));
        return {true, "Duplicated into '" + m.texture->layers[size_t(at)].name + "'"};
    }
    if (verb == "delete") {
        if (i < 0) return {false, "Base can't be deleted (paint_clear clears the texture)"};
        const std::string name = layerName(cur, i);
        UndoRecord r = captureTexture(m, "Delete Texture Layer");
        removeTextureLayer(*m.texture, i);
        finishTextureUndo(r, m);
        doc.undo.push(std::move(r));
        return {true, "Deleted texture layer '" + name + "'"};
    }
    if (verb == "move") {
        const std::string dir = argString(a, "direction");
        if (dir != "up" && dir != "down") return {false, "direction must be up or down"};
        if (i < 0) return {false, "Base stays at the bottom"};
        const int d = dir == "up" ? 1 : -1;
        if (i + d < 0 || i + d >= int(cur.layers.size()))
            return {false, "'" + layerName(cur, i) + "' is already at the " + (d > 0 ? "top" : "bottom")};
        UndoRecord r = captureTexture(m, "Move Texture Layer");
        moveTextureLayer(*m.texture, i, d);
        finishTextureUndo(r, m);
        doc.undo.push(std::move(r));
        return {true, "Moved '" + layerName(*m.texture, i + d) + "' " + dir};
    }
    if (verb == "merge") {
        if (i < 0) return {false, "Base has nothing below it to merge into"};
        const std::string name = layerName(cur, i), below = layerName(cur, i - 1);
        UndoRecord r = captureTexture(m, "Merge Texture Layer");
        if (!mergeTextureLayerDown(*m.texture, i, err)) {
            r.texture->swapWith(m); // put the untouched original back
            return {false, err};
        }
        finishTextureUndo(r, m);
        doc.undo.push(std::move(r));
        return {true, "Merged '" + name + "' into '" + below + "'"};
    }
    if (verb == "flatten") {
        if (cur.layers.empty()) return {true, "There are no texture layers to flatten"};
        UndoRecord r = captureTexture(m, "Flatten Texture Layers");
        flattenTextureLayers(*m.texture);
        finishTextureUndo(r, m);
        doc.undo.push(std::move(r));
        return {true, "Flattened the texture layers into Base"};
    }
    if (verb == "clear") {
        if (i >= 0 && cur.layers[size_t(i)].locked) return {false, "Layer '" + layerName(cur, i) + "' is locked"};
        UndoRecord r = captureTexture(m, "Clear Texture Layer");
        clearTextureLayer(*m.texture, i);
        finishTextureUndo(r, m);
        doc.undo.push(std::move(r));
        return {true, "Cleared texture " + layerName(*m.texture, i)};
    }
    return {false, "Unknown texture layer operation '" + verb + "'"};
}

void registerTextureCommands(CommandRegistry& reg) {
    reg.add({"uv_auto",
             "Unwrap the active Part automatically (xatlas): cuts it into UV islands and packs them into the 0-1 "
             "square, so it can carry a painted texture. Replaces any existing UVs; a painted texture and its layers "
             "are moved onto the new UVs (one undo step). Can take a while on millions of triangles.",
             {{"resolution", T::Number, "Texture size the islands are packed for (padding in texels). Default: the "
                                        "texture's size, or 2048.", false},
              {"keep_texture", T::Bool, "Move a painted texture onto the new UVs. Default true; false leaves the "
                                        "texels where they are (they no longer line up).", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 if (doc.mesh.triangleCount() == 0) return {false, "There is no mesh to unwrap"};
                 AutoUvResult result;
                 std::string err;
                 const int res = a.contains("resolution") ? int(argNumber(a, "resolution", kDefaultTextureSize))
                                 : doc.mesh.texture                ? std::max(doc.mesh.texture->width, doc.mesh.texture->height)
                                                                   : kDefaultTextureSize;
                 if (!computeAutoUv(doc.mesh.positions, doc.mesh.triangles, res, result, err)) return {false, err};
                 Mesh m = doc.mesh;
                 const std::string note = setUvsKeepingTexture(m, std::move(result.uvs), argBool(a, "keep_texture", true));
                 doc.replaceMeshUndoable(std::move(m), "Auto UV");
                 return {true, "Auto UV: " + std::to_string(result.charts) + " UV islands" + note};
             }});

    reg.add({"texture_create",
             "Give the active Part a texture to paint into (texture painting on its UVs: the colour resolution "
             "comes from the texture, not the mesh). The Paint brush, fills and paint layers then work on the texture. "
             "Needs UVs (uv_auto makes them). Replaces an existing texture (undoable).",
             {{"size", T::String, "1024, 2048 (default), 4096 or 8192 (or 1k, 2k, 4k, 8k).", false},
              {"from_polypaint", T::Bool, "Start from the Part's vertex paint instead of white. Default false.", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 Mesh& m = doc.mesh;
                 if (m.triangleCount() == 0) return {false, "There is no mesh to paint"};
                 if (!m.hasUvs()) return {false, kNoUvs};
                 std::string sizeText = argString(a, "size", "");
                 if (sizeText.empty() && a.contains("size")) sizeText = std::to_string(int(argNumber(a, "size", 2048)));
                 const int size = sizeText.empty() ? kDefaultTextureSize : parseTextureSize(sizeText);
                 if (!size) return {false, "size must be 1024, 2048, 4096 or 8192"};
                 UndoRecord r = captureTexture(m, "New Texture");
                 m.texture = makeTexture(size, size);
                 m.paintToTexture = true;
                 if (argBool(a, "from_polypaint", false) && m.hasColors()) polypaintToTexture(m);
                 m.facesDirty = true;
                 finishTextureUndo(r, m);
                 doc.undo.push(std::move(r));
                 return {true, "New " + sizeLabel(*m.texture) + " texture: painting now goes into the texture"};
             }});

    reg.add({"texture_delete", "Remove the active Part's texture (undoable). Painting goes back to the vertices.", {},
             [](Document& doc, const Args&) -> CommandResult {
                 Mesh& m = doc.mesh;
                 if (!m.texture) return {true, "The Part has no texture"};
                 UndoRecord r = captureTexture(m, "Delete Texture");
                 m.texture.reset();
                 m.paintToTexture = false;
                 m.facesDirty = true;
                 finishTextureUndo(r, m);
                 doc.undo.push(std::move(r));
                 return {true, "Texture deleted"};
             }});

    reg.add({"paint_target",
             "Where painting goes on the active Part: vertex (vertex paint) or texture (its texture map). Applies to the "
             "Paint brush, Rgb sculpting, fills and the paint_layer_* commands.",
             {{"target", T::String, "vertex or texture.", true}},
             [](Document& doc, const Args& a) -> CommandResult {
                 const std::string t = argString(a, "target");
                 if (t == "vertex" || t == "vertices" || t == "polypaint") {
                     doc.mesh.paintToTexture = false;
                     return {true, "Painting goes into the vertices (vertex paint)"};
                 }
                 if (t != "texture") return {false, "target must be vertex or texture"};
                 if (!doc.mesh.hasUvs()) return {false, kNoUvs};
                 if (!hasTexture(doc.mesh)) return {false, kNoTexture};
                 doc.mesh.paintToTexture = true;
                 return {true, "Painting goes into the " + sizeLabel(*doc.mesh.texture) + " texture"};
             }});

    reg.add({"texture_from_polypaint",
             "Bake the Part's vertex paint (vertex colours, all paint layers composited) into the active texture layer "
             "(undoable). Creates a 2K texture when there is none.",
             {{"size", T::String, "Size for a new texture: 1024, 2048, 4096 or 8192. Default 2048.", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 Mesh& m = doc.mesh;
                 if (!m.hasUvs()) return {false, kNoUvs};
                 if (!m.hasColors()) return {false, "The Part has no vertex paint to convert"};
                 const int size = parseTextureSize(argString(a, "size", "2048"));
                 if (!size) return {false, "size must be 1024, 2048, 4096 or 8192"};
                 UndoRecord r = captureTexture(m, "Vertex Paint to Texture");
                 if (!hasTexture(m)) {
                     m.texture = makeTexture(size, size);
                     m.facesDirty = true;
                 }
                 m.paintToTexture = true;
                 polypaintToTexture(m);
                 finishTextureUndo(r, m);
                 doc.undo.push(std::move(r));
                 return {true, "Vertex Paint baked into the " + sizeLabel(*m.texture) + " texture"};
             }});

    reg.add({"polypaint_from_texture",
             "Set the vertex paint (the active paint layer) of every vertex to the texture's colour under it (undoable). "
             "Detail finer than the mesh is lost; vertices on UV seams average their sides.",
             {},
             [](Document& doc, const Args&) -> CommandResult {
                 Mesh& m = doc.mesh;
                 if (!hasTexture(m)) return {false, kNoTexture};
                 if (!m.paintLayers.empty() && m.activePaintLayer >= 0 && !paintLayerEditable(m, m.activePaintLayer))
                     return {false, "Paint layer '" + m.paintLayers[size_t(m.activePaintLayer)].name + "' is locked or hidden"};
                 UndoRecord r;
                 if (m.hasColors()) {
                     r = UndoRecord::captureColors(m, "Texture to Vertex Paint");
                 } else {
                     r.label = "Texture to Vertex Paint";
                     r.mesh = std::make_unique<Mesh>(m);
                 }
                 textureToPolypaint(m);
                 doc.undo.push(std::move(r));
                 doc.modified = true;
                 return {true, "Texture copied into the vertex paint of " + std::to_string(m.vertexCount()) + " vertices"};
             }});

    reg.add({"texture_export",
             "Save the active Part's texture (all visible layers composited) as a PNG image.",
             {{"path", T::String, "Output .png path (UTF-8).", true}},
             [](Document& doc, const Args& a) -> CommandResult {
                 if (!hasTexture(doc.mesh)) return {false, kNoTexture};
                 const std::string path = argString(a, "path");
                 if (path.empty()) return {false, "Give a path"};
                 std::string err;
                 if (!textureImage(*doc.mesh.texture).savePng(path, err)) return {false, err};
                 return {true, "Saved the " + sizeLabel(*doc.mesh.texture) + " texture to " + path};
             }});

    reg.add({"texture_import",
             "Load an image (PNG, JPEG, TGA, ...) as the active Part's texture (undoable; its layers are replaced). "
             "The image is laid on the UVs as it is (v = 0 at the bottom of the image).",
             {{"path", T::String, "Image path (UTF-8).", true}},
             [](Document& doc, const Args& a) -> CommandResult {
                 Mesh& m = doc.mesh;
                 if (!m.hasUvs()) return {false, kNoUvs};
                 Image img;
                 std::string err;
                 if (!Image::load(argString(a, "path"), img, err)) return {false, err};
                 auto tex = makeTexture(1, 1);
                 if (!loadTextureImage(*tex, img, err)) return {false, err};
                 UndoRecord r = captureTexture(m, "Import Texture");
                 m.texture = std::move(tex);
                 m.paintToTexture = true;
                 m.facesDirty = true;
                 finishTextureUndo(r, m);
                 doc.undo.push(std::move(r));
                 return {true, "Texture " + sizeLabel(*m.texture) + " loaded from " + argString(a, "path")};
             }});
}

} // namespace sf
