// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/PaintLayer.h"

#include "core/Commands.h"
#include "core/Mesh.h"
#include "core/Texture.h"
#include "core/Undo.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <memory>
#include <thread>

namespace sf {

namespace {

struct BlendInfo {
    BlendMode mode;
    const char* id;
    const char* label;
};

const BlendInfo kBlendModes[kBlendModeCount] = {
    {BlendMode::Normal, "normal", "Normal"},         {BlendMode::Multiply, "multiply", "Multiply"},
    {BlendMode::Screen, "screen", "Screen"},         {BlendMode::Overlay, "overlay", "Overlay"},
    {BlendMode::SoftLight, "soft_light", "Soft Light"}, {BlendMode::Add, "add", "Add"},
    {BlendMode::Subtract, "subtract", "Subtract"},   {BlendMode::Darken, "darken", "Darken"},
    {BlendMode::Lighten, "lighten", "Lighten"},      {BlendMode::Difference, "difference", "Difference"},
};

// Lower case without spaces, dashes or underscores ("Soft Light" -> "softlight").
std::string squash(const std::string& s) {
    std::string out;
    for (char c : s)
        if (!std::isspace(static_cast<unsigned char>(c)) && c != '_' && c != '-')
            out += char(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// sRGB <-> linear through tables (with linear interpolation): compositing a
// 2M-vertex stack converts every colour, and std::pow would dominate.
constexpr int kLutSize = 4096;
struct SrgbTables {
    std::array<float, kLutSize + 1> toSrgb, toLinear;
    SrgbTables() {
        for (int i = 0; i <= kLutSize; ++i) {
            const float x = float(i) / kLutSize;
            toSrgb[size_t(i)] = x <= 0.0031308f ? x * 12.92f : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
            toLinear[size_t(i)] = x <= 0.04045f ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f);
        }
    }
};
const SrgbTables& tables() {
    static const SrgbTables t;
    return t;
}
inline float lookup(const std::array<float, kLutSize + 1>& t, float x) {
    x = std::clamp(x, 0.0f, 1.0f) * float(kLutSize);
    const int i = std::min(int(x), kLutSize - 1);
    const float f = x - float(i);
    return t[size_t(i)] + (t[size_t(i) + 1] - t[size_t(i)]) * f;
}
inline glm::vec3 fastToSrgb(const glm::vec3& c) {
    const auto& t = tables().toSrgb;
    return {lookup(t, c.x), lookup(t, c.y), lookup(t, c.z)};
}
inline glm::vec3 fastToLinear(const glm::vec3& c) {
    const auto& t = tables().toLinear;
    return {lookup(t, c.x), lookup(t, c.y), lookup(t, c.z)};
}

inline float softLight(float b, float s) {
    if (s <= 0.5f) return b - (1.0f - 2.0f * s) * b * (1.0f - b);
    const float d = b <= 0.25f ? ((16.0f * b - 12.0f) * b + 4.0f) * b : std::sqrt(b);
    return b + (2.0f * s - 1.0f) * (d - b);
}

inline float overlay(float b, float s) { return b <= 0.5f ? 2.0f * b * s : 1.0f - 2.0f * (1.0f - b) * (1.0f - s); }

bool validLayer(const Mesh& m, int layer) { return layer >= -1 && layer < int(m.paintLayers.size()); }

// Runs fn(begin, end) over [0, n) on several threads for big meshes.
template <class Fn>
void parallelRange(size_t n, Fn fn) {
    const size_t threads = n < 200000 ? 1 : std::clamp<size_t>(std::thread::hardware_concurrency(), 1, 16);
    if (threads <= 1) {
        fn(size_t(0), n);
        return;
    }
    std::vector<std::thread> pool;
    const size_t chunk = (n + threads - 1) / threads;
    for (size_t t = 0; t < threads; ++t) {
        const size_t b = t * chunk, e = std::min(n, b + chunk);
        if (b < e) pool.emplace_back([=] { fn(b, e); });
    }
    for (auto& th : pool) th.join();
}

// Creates the stack: Base = today's polypaint (white when unpainted).
void ensureStack(Mesh& m) {
    if (!m.paintLayers.empty()) return;
    m.ensureColors();
    m.paintBase = m.colors;
    m.activePaintLayer = -1;
}

// Back to plain polypaint once the last layer is gone (colors already holds Base).
void dropStackIfEmpty(Mesh& m) {
    if (!m.paintLayers.empty()) return;
    if (m.paintBase.size() == m.colors.size()) m.colors = m.paintBase;
    std::vector<glm::vec3>().swap(m.paintBase);
    m.activePaintLayer = -1;
    m.markAllDirty();
}

} // namespace

const char* blendModeId(BlendMode mode) {
    const int i = int(mode);
    return i >= 0 && i < kBlendModeCount ? kBlendModes[i].id : "normal";
}

const char* blendModeLabel(BlendMode mode) {
    const int i = int(mode);
    return i >= 0 && i < kBlendModeCount ? kBlendModes[i].label : "Normal";
}

bool parseBlendMode(const std::string& text, BlendMode& mode) {
    const std::string s = squash(text);
    if (s == "lineardodge") {
        mode = BlendMode::Add;
        return true;
    }
    for (const auto& b : kBlendModes)
        if (s == squash(b.id)) {
            mode = b.mode;
            return true;
        }
    return false;
}

glm::vec3 blendColors(BlendMode mode, const glm::vec3& b, const glm::vec3& s) {
    switch (mode) {
    case BlendMode::Normal: return s;
    case BlendMode::Multiply: return b * s;
    case BlendMode::Screen: return b + s - b * s;
    case BlendMode::Overlay: return {overlay(b.x, s.x), overlay(b.y, s.y), overlay(b.z, s.z)};
    case BlendMode::SoftLight: return {softLight(b.x, s.x), softLight(b.y, s.y), softLight(b.z, s.z)};
    case BlendMode::Add: return glm::min(b + s, glm::vec3(1.0f));
    case BlendMode::Subtract: return glm::max(b - s, glm::vec3(0.0f));
    case BlendMode::Darken: return glm::min(b, s);
    case BlendMode::Lighten: return glm::max(b, s);
    case BlendMode::Difference: return glm::abs(b - s);
    }
    return s;
}

bool hasPaintLayers(const Mesh& mesh) { return !mesh.paintLayers.empty(); }
int paintLayerCount(const Mesh& mesh) { return int(mesh.paintLayers.size()); }

glm::vec3 compositePaintVertex(const Mesh& m, uint32_t v) {
    const glm::vec3 base = m.paintBase[v];
    glm::vec3 c(0.0f);
    bool blended = false; // untouched vertices keep Base exactly (no sRGB round trip)
    for (const PaintLayer& l : m.paintLayers) {
        if (!l.visible) continue;
        const float a = std::min(l.alpha[v] * l.opacity, 1.0f);
        if (a <= 0.0f) continue;
        if (!blended) c = fastToSrgb(base);
        blended = true;
        const glm::vec3 src = fastToSrgb(l.color[v]);
        c = glm::mix(c, glm::clamp(blendColors(l.blend, c, src), 0.0f, 1.0f), a);
    }
    return blended ? fastToLinear(c) : base;
}

void compositePaint(Mesh& m, std::span<const uint32_t> verts) {
    if (m.paintLayers.empty()) {
        for (uint32_t v : verts) m.markDirty(v);
        return;
    }
    m.ensureColors();
    for (uint32_t v : verts) {
        m.colors[v] = compositePaintVertex(m, v);
        m.markDirty(v);
    }
}

void compositePaintAll(Mesh& m) {
    if (m.paintLayers.empty()) {
        m.markAllDirty();
        return;
    }
    m.ensureColors();
    parallelRange(m.vertexCount(), [&m](size_t b, size_t e) {
        for (size_t v = b; v < e; ++v) m.colors[v] = compositePaintVertex(m, uint32_t(v));
    });
    m.markAllDirty();
}

bool paintLayerEditable(const Mesh& m, int layer) {
    if (layer == -1) return true;
    if (!validLayer(m, layer)) return false;
    const PaintLayer& l = m.paintLayers[size_t(layer)];
    return !l.locked && l.visible;
}

void paintVertex(Mesh& m, uint32_t v, const glm::vec3& color, float w, bool erase) {
    w = std::clamp(w, 0.0f, 1.0f);
    if (w <= 0.0f) return;
    if (m.paintLayers.empty()) {
        m.colors[v] = glm::mix(m.colors[v], erase ? glm::vec3(1.0f) : color, w);
        m.markDirty(v);
        return;
    }
    const int active = m.activePaintLayer;
    if (active < 0) {
        m.paintBase[v] = glm::mix(m.paintBase[v], erase ? glm::vec3(1.0f) : color, w);
    } else {
        if (!paintLayerEditable(m, active)) return;
        PaintLayer& l = m.paintLayers[size_t(active)];
        const float a = l.alpha[v];
        if (erase) {
            l.alpha[v] = a * (1.0f - w);
        } else {
            // "Source over" onto the layer's own coverage.
            const float na = a + w * (1.0f - a);
            l.color[v] = (l.color[v] * (a * (1.0f - w)) + color * w) / na;
            l.alpha[v] = na;
        }
    }
    m.colors[v] = compositePaintVertex(m, v);
    m.markDirty(v);
}

std::string uniquePaintLayerName(const Mesh& m, const std::string& wanted) {
    const std::string stem = wanted.empty() ? "Layer" : wanted;
    auto used = [&](const std::string& n) { return findPaintLayer(m, n) != -2; };
    if (!wanted.empty() && !used(stem)) return stem;
    for (int i = 2;; ++i) {
        std::string n = stem + " " + std::to_string(i);
        if (!used(n)) return n;
    }
}

int findPaintLayer(const Mesh& m, const std::string& name) {
    for (size_t i = 0; i < m.paintLayers.size(); ++i)
        if (m.paintLayers[i].name == name) return int(i);
    return name == "Base" ? -1 : -2;
}

int addPaintLayer(Mesh& m, std::string name, int above) {
    ensureStack(m);
    const size_t nv = m.vertexCount();
    PaintLayer l;
    l.name = uniquePaintLayerName(m, name.empty() ? "Layer " + std::to_string(m.paintLayers.size() + 1) : name);
    l.color.assign(nv, glm::vec3(1.0f));
    l.alpha.assign(nv, 0.0f);
    if (above == -2) above = int(m.paintLayers.size()) - 1;
    above = std::clamp(above, -1, int(m.paintLayers.size()) - 1);
    m.paintLayers.insert(m.paintLayers.begin() + (above + 1), std::move(l));
    m.activePaintLayer = above + 1;
    return above + 1; // a transparent layer changes nothing on screen
}

int duplicatePaintLayer(Mesh& m, int layer) {
    if (!validLayer(m, layer)) return -2;
    ensureStack(m);
    PaintLayer copy;
    if (layer < 0) {
        copy.name = "Base";
        copy.color = m.paintBase;
        copy.alpha.assign(m.vertexCount(), 1.0f);
    } else {
        copy = m.paintLayers[size_t(layer)];
        copy.locked = false;
    }
    copy.name = uniquePaintLayerName(m, copy.name + " copy");
    m.paintLayers.insert(m.paintLayers.begin() + (layer + 1), std::move(copy));
    m.activePaintLayer = layer + 1;
    compositePaintAll(m);
    return layer + 1;
}

bool removePaintLayer(Mesh& m, int layer) {
    if (layer < 0 || layer >= int(m.paintLayers.size())) return false;
    m.paintLayers.erase(m.paintLayers.begin() + layer);
    m.activePaintLayer = std::min(layer, int(m.paintLayers.size()) - 1);
    if (m.activePaintLayer < 0) m.activePaintLayer = -1;
    compositePaintAll(m);
    dropStackIfEmpty(m);
    return true;
}

bool movePaintLayer(Mesh& m, int layer, int direction) {
    const int to = layer + (direction > 0 ? 1 : -1);
    if (layer < 0 || layer >= int(m.paintLayers.size()) || to < 0 || to >= int(m.paintLayers.size())) return false;
    std::swap(m.paintLayers[size_t(layer)], m.paintLayers[size_t(to)]);
    if (m.activePaintLayer == layer) m.activePaintLayer = to;
    else if (m.activePaintLayer == to) m.activePaintLayer = layer;
    compositePaintAll(m);
    return true;
}

bool mergePaintLayerDown(Mesh& m, int layer, std::string& error) {
    if (layer < 0 || layer >= int(m.paintLayers.size())) {
        error = "Choose a layer above Base to merge down";
        return false;
    }
    PaintLayer& up = m.paintLayers[size_t(layer)];
    if (up.locked) {
        error = "Layer '" + up.name + "' is locked";
        return false;
    }
    const size_t nv = m.vertexCount();
    if (layer == 0) {
        // Onto Base: exactly what the screen shows for Base + this layer.
        if (up.visible)
            parallelRange(nv, [&](size_t b, size_t e) {
                for (size_t v = b; v < e; ++v) {
                    const float a = std::min(up.alpha[v] * up.opacity, 1.0f);
                    if (a <= 0.0f) continue;
                    const glm::vec3 c = fastToSrgb(m.paintBase[v]);
                    m.paintBase[v] = fastToLinear(glm::mix(c, glm::clamp(blendColors(up.blend, c, fastToSrgb(up.color[v])), 0.0f, 1.0f), a));
                }
            });
    } else {
        PaintLayer& low = m.paintLayers[size_t(layer - 1)];
        if (low.locked) {
            error = "Layer '" + low.name + "' is locked";
            return false;
        }
        if (!low.visible) {
            error = "Show layer '" + low.name + "' before merging into it";
            return false;
        }
        // W3C/Photoshop compositing of the upper layer onto the lower one's own
        // pixels; the lower opacity is baked into its coverage.
        parallelRange(nv, [&](size_t b, size_t e) {
            for (size_t v = b; v < e; ++v) {
                const float ab = std::min(low.alpha[v] * low.opacity, 1.0f);
                const float as = up.visible ? std::min(up.alpha[v] * up.opacity, 1.0f) : 0.0f;
                if (as <= 0.0f) {
                    low.alpha[v] = ab;
                    continue;
                }
                const glm::vec3 cb = fastToSrgb(low.color[v]), cs = fastToSrgb(up.color[v]);
                const glm::vec3 mixed = glm::clamp(blendColors(up.blend, cb, cs), 0.0f, 1.0f);
                const float ao = as + ab * (1.0f - as);
                const glm::vec3 co = (as * (1.0f - ab) * cs + as * ab * mixed + (1.0f - as) * ab * cb) / ao;
                low.color[v] = fastToLinear(co);
                low.alpha[v] = ao;
            }
        });
        low.opacity = 1.0f;
    }
    m.paintLayers.erase(m.paintLayers.begin() + layer);
    m.activePaintLayer = layer - 1;
    compositePaintAll(m);
    dropStackIfEmpty(m);
    return true;
}

void flattenPaintLayers(Mesh& m) {
    if (m.paintLayers.empty()) return;
    compositePaintAll(m);
    m.paintLayers.clear();
    m.paintBase = m.colors;
    dropStackIfEmpty(m);
}

void clearPaintLayer(Mesh& m, int layer) {
    if (m.paintLayers.empty()) {
        if (m.hasColors()) std::fill(m.colors.begin(), m.colors.end(), glm::vec3(1.0f));
        m.markAllDirty();
        return;
    }
    if (layer < 0) std::fill(m.paintBase.begin(), m.paintBase.end(), glm::vec3(1.0f));
    else if (layer < int(m.paintLayers.size())) {
        PaintLayer& l = m.paintLayers[size_t(layer)];
        std::fill(l.alpha.begin(), l.alpha.end(), 0.0f);
        std::fill(l.color.begin(), l.color.end(), glm::vec3(1.0f));
    }
    compositePaintAll(m);
}

void copyPaintLayers(const Mesh& src, Mesh& dst, std::span<const uint32_t> kept) {
    if (src.paintLayers.empty() || src.paintBase.size() != src.vertexCount()) return;
    dst.paintBase.clear();
    dst.paintBase.reserve(kept.size());
    for (uint32_t v : kept) dst.paintBase.push_back(src.paintBase[v]);
    dst.paintLayers.clear();
    for (const PaintLayer& l : src.paintLayers) {
        PaintLayer nl = l;
        nl.color.clear();
        nl.alpha.clear();
        nl.color.reserve(kept.size());
        nl.alpha.reserve(kept.size());
        for (uint32_t v : kept) {
            nl.color.push_back(l.color[v]);
            nl.alpha.push_back(l.alpha[v]);
        }
        dst.paintLayers.push_back(std::move(nl));
    }
    dst.activePaintLayer = src.activePaintLayer;
}

void subdividePaintLayers(const Mesh& src, Mesh& dst, size_t vertexCount, std::span<const glm::uvec3> midpoints) {
    const size_t nv = src.vertexCount();
    if (src.paintLayers.empty() || src.paintBase.size() != nv) return;
    auto grow = [&](const auto& a) {
        std::remove_cvref_t<decltype(a)> out(vertexCount);
        std::copy(a.begin(), a.end(), out.begin());
        return out;
    };
    dst.paintBase = grow(src.paintBase);
    for (const glm::uvec3& m : midpoints) dst.paintBase[m.x] = (src.paintBase[m.y] + src.paintBase[m.z]) * 0.5f;
    dst.paintLayers.clear();
    for (const PaintLayer& l : src.paintLayers) {
        PaintLayer nl;
        nl.name = l.name;
        nl.opacity = l.opacity;
        nl.blend = l.blend;
        nl.visible = l.visible;
        nl.locked = l.locked;
        nl.color = grow(l.color);
        nl.alpha = grow(l.alpha);
        for (const glm::uvec3& m : midpoints) {
            // Premultiplied average, so unpainted (white, transparent) neighbours don't pale the edge.
            const float a0 = l.alpha[m.y], a1 = l.alpha[m.z], sum = a0 + a1;
            nl.alpha[m.x] = sum * 0.5f;
            nl.color[m.x] = sum > 0.0f ? (l.color[m.y] * a0 + l.color[m.z] * a1) / sum : glm::vec3(1.0f);
        }
        dst.paintLayers.push_back(std::move(nl));
    }
    dst.activePaintLayer = src.activePaintLayer;
    if (dst.colors.size() == vertexCount)
        for (const glm::uvec3& m : midpoints) dst.colors[m.x] = compositePaintVertex(dst, m.x);
}

void sanitizePaintLayers(Mesh& m) {
    const size_t nv = m.positions.size();
    bool ok = !m.paintLayers.empty() && m.paintBase.size() == nv;
    for (const PaintLayer& l : m.paintLayers) ok = ok && l.color.size() == nv && l.alpha.size() == nv;
    if (!ok) {
        // Unknown to whatever changed the topology: keep the flattened look in `colors`.
        if (!m.paintLayers.empty() || !m.paintBase.empty()) {
            std::vector<PaintLayer>().swap(m.paintLayers);
            std::vector<glm::vec3>().swap(m.paintBase);
        }
        m.activePaintLayer = -1;
        return;
    }
    if (!validLayer(m, m.activePaintLayer)) m.activePaintLayer = -1;
    if (m.colors.size() != nv) compositePaintAll(m);
}

// --- Commands ---

namespace {

using T = ParamSpec::Type;

// The layer named by "layer" (a name, or "Base") or "index" (-1 = Base); the active one by default.
// Returns -2 with `error` set when there is no such layer.
int layerArg(const Mesh& m, const Args& a, std::string& error) {
    if (a.contains("layer")) {
        const std::string name = argString(a, "layer");
        const int i = findPaintLayer(m, name);
        if (i == -2) error = "No paint layer called '" + name + "'";
        return i;
    }
    if (a.contains("index")) {
        const int i = int(argNumber(a, "index", -1));
        if (!validLayer(m, i)) {
            error = "No paint layer " + std::to_string(i) + " (-1 = Base, 0-" + std::to_string(int(m.paintLayers.size()) - 1) +
                    " above it)";
            return -2;
        }
        return i;
    }
    return validLayer(m, m.activePaintLayer) ? m.activePaintLayer : -1;
}

const ParamSpec kIndexParam{"index", T::Number, "Layer index: -1 = Base, 0 = the first layer above it. Default: the active layer.", false};
const ParamSpec kLayerParam{"layer", T::String, "Layer name instead of index (\"Base\" for the base layer).", false};

std::string layerName(const Mesh& m, int i) { return i < 0 ? std::string("Base") : m.paintLayers[size_t(i)].name; }

std::string blendList() {
    std::string s;
    for (int i = 0; i < kBlendModeCount; ++i) s += std::string(i ? ", " : "") + kBlendModes[i].id;
    return s;
}

// Undo step for a paint-stack edit, captured before the edit.
UndoRecord stackUndo(const Mesh& m, std::string label, bool propertiesOnly = false) {
    UndoRecord r;
    r.label = std::move(label);
    r.paint = std::make_unique<PaintStackUndo>(PaintStackUndo::capture(m, propertiesOnly));
    return r;
}

std::string percent(float x) { return std::to_string(int(std::lround(x * 100.0f))) + "%"; }

} // namespace

void registerPaintLayerCommands(CommandRegistry& reg) {
    reg.add({"paint_layer_add",
             "Add a vertex paint layer above the active one (Photoshop-style paint layers) and paint into it. "
             "Base is the existing vertex paint; new layers start transparent.",
             {{"name", T::String, "Layer name, e.g. 'Freckles'. Default 'Layer N'.", false},
              {"blend", T::String, "Blend mode: " + blendList() + ". Default normal.", false},
              {"opacity", T::Number, "Layer opacity 0-1. Default 1.", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 if (paintsTexture(doc.mesh)) return textureLayerCommand(doc, "add", a);
                 Mesh& m = doc.mesh;
                 if (m.vertexCount() == 0) return {false, "There is no mesh to paint"};
                 BlendMode blend = BlendMode::Normal;
                 if (a.contains("blend") && !parseBlendMode(argString(a, "blend"), blend))
                     return {false, "Unknown blend mode (use " + blendList() + ")"};
                 UndoRecord r = stackUndo(m, "Add Paint Layer");
                 const bool fresh = m.paintLayers.empty();
                 const int at = addPaintLayer(m, argString(a, "name"), validLayer(m, m.activePaintLayer) ? m.activePaintLayer : -2);
                 PaintLayer& l = m.paintLayers[size_t(at)];
                 l.blend = blend;
                 l.opacity = std::clamp(float(argNumber(a, "opacity", 1.0)), 0.0f, 1.0f);
                 if (fresh) m.markAllDirty(); // colours may have just been allocated
                 doc.undo.push(std::move(r));
                 return {true, "Added paint layer '" + l.name + "' (index " + std::to_string(at) + ", " + blendModeLabel(blend) + ")"};
             }});

    reg.add({"paint_layer_set",
             "Change a paint layer: rename it, its opacity, blend mode, visibility or lock (undoable). "
             "Base only takes name changes back to 'Base' (it is always opaque Normal).",
             {kIndexParam, kLayerParam,
              {"name", T::String, "New name.", false},
              {"opacity", T::Number, "Opacity 0-1.", false},
              {"blend", T::String, "Blend mode: " + blendList() + ".", false},
              {"visible", T::Bool, "Show (true) or hide (false) the layer.", false},
              {"locked", T::Bool, "Lock the layer against painting, fills, clears and merges.", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 if (paintsTexture(doc.mesh)) return textureLayerCommand(doc, "set", a);
                 Mesh& m = doc.mesh;
                 std::string err;
                 const int i = layerArg(m, a, err);
                 if (i == -2) return {false, err};
                 if (i < 0) return {false, "Base is always visible, opaque and Normal; add a layer to blend"};
                 BlendMode blend = m.paintLayers[size_t(i)].blend;
                 if (a.contains("blend") && !parseBlendMode(argString(a, "blend"), blend))
                     return {false, "Unknown blend mode (use " + blendList() + ")"};
                 std::string name = m.paintLayers[size_t(i)].name;
                 if (a.contains("name")) {
                     const std::string wanted = argString(a, "name");
                     if (wanted.empty() || wanted == "Base") return {false, "Choose another layer name"};
                     if (wanted != name) name = uniquePaintLayerName(m, wanted);
                 }
                 UndoRecord r = stackUndo(m, "Paint Layer Settings", true);
                 PaintLayer& l = m.paintLayers[size_t(i)];
                 l.name = name;
                 l.blend = blend;
                 if (a.contains("opacity")) l.opacity = std::clamp(float(argNumber(a, "opacity", 1.0)), 0.0f, 1.0f);
                 l.visible = argBool(a, "visible", l.visible);
                 l.locked = argBool(a, "locked", l.locked);
                 compositePaintAll(m);
                 doc.undo.push(std::move(r));
                 return {true, "Paint layer '" + l.name + "': " + blendModeLabel(l.blend) + ", " + percent(l.opacity) +
                                   (l.visible ? "" : ", hidden") + (l.locked ? ", locked" : "")};
             }});

    reg.add({"paint_layer_select", "Choose the paint layer the Paint brush and fills write into.", {kIndexParam, kLayerParam},
             [](Document& doc, const Args& a) -> CommandResult {
                 if (paintsTexture(doc.mesh)) return textureLayerCommand(doc, "select", a);
                 Mesh& m = doc.mesh;
                 if (!a.contains("index") && !a.contains("layer")) return {false, "Give index or layer"};
                 std::string err;
                 const int i = layerArg(m, a, err);
                 if (i == -2) return {false, err};
                 m.activePaintLayer = m.paintLayers.empty() ? -1 : i;
                 return {true, "Painting on " + layerName(m, i)};
             }});

    reg.add({"paint_layer_duplicate", "Copy a paint layer (Base becomes an opaque layer) to a new layer above it.",
             {kIndexParam, kLayerParam},
             [](Document& doc, const Args& a) -> CommandResult {
                 if (paintsTexture(doc.mesh)) return textureLayerCommand(doc, "duplicate", a);
                 Mesh& m = doc.mesh;
                 if (m.vertexCount() == 0) return {false, "There is no mesh to paint"};
                 std::string err;
                 const int i = layerArg(m, a, err);
                 if (i == -2) return {false, err};
                 UndoRecord r = stackUndo(m, "Duplicate Paint Layer");
                 const int at = duplicatePaintLayer(m, i);
                 doc.undo.push(std::move(r));
                 return {true, "Duplicated into '" + m.paintLayers[size_t(at)].name + "'"};
             }});

    reg.add({"paint_layer_delete", "Delete a paint layer (not Base). With the last layer gone, Base is plain vertex paint again.",
             {kIndexParam, kLayerParam},
             [](Document& doc, const Args& a) -> CommandResult {
                 if (paintsTexture(doc.mesh)) return textureLayerCommand(doc, "delete", a);
                 Mesh& m = doc.mesh;
                 std::string err;
                 const int i = layerArg(m, a, err);
                 if (i == -2) return {false, err};
                 if (i < 0) return {false, "Base can't be deleted (paint_clear removes all vertex paint)"};
                 const std::string name = layerName(m, i);
                 UndoRecord r = stackUndo(m, "Delete Paint Layer");
                 removePaintLayer(m, i);
                 doc.undo.push(std::move(r));
                 return {true, "Deleted paint layer '" + name + "'"};
             }});

    reg.add({"paint_layer_move", "Move a paint layer up or down the stack (Base stays at the bottom).",
             {kIndexParam, kLayerParam, {"direction", T::String, "up or down.", true}},
             [](Document& doc, const Args& a) -> CommandResult {
                 if (paintsTexture(doc.mesh)) return textureLayerCommand(doc, "move", a);
                 Mesh& m = doc.mesh;
                 std::string err;
                 const int i = layerArg(m, a, err);
                 if (i == -2) return {false, err};
                 const std::string dir = argString(a, "direction");
                 if (dir != "up" && dir != "down") return {false, "direction must be up or down"};
                 if (i < 0) return {false, "Base stays at the bottom"};
                 const int d = dir == "up" ? 1 : -1;
                 if (i + d < 0 || i + d >= int(m.paintLayers.size()))
                     return {false, "'" + layerName(m, i) + "' is already at the " + (d > 0 ? "top" : "bottom")};
                 UndoRecord r = stackUndo(m, "Move Paint Layer");
                 movePaintLayer(m, i, d);
                 doc.undo.push(std::move(r));
                 return {true, "Moved '" + layerName(m, i + d) + "' " + dir};
             }});

    reg.add({"paint_layer_merge",
             "Merge a paint layer into the one below it (Merge Down; into Base for the first layer). Exact onto Base; "
             "onto another layer the result keeps that layer's blend mode, as in Photoshop.",
             {kIndexParam, kLayerParam},
             [](Document& doc, const Args& a) -> CommandResult {
                 if (paintsTexture(doc.mesh)) return textureLayerCommand(doc, "merge", a);
                 Mesh& m = doc.mesh;
                 std::string err;
                 const int i = layerArg(m, a, err);
                 if (i == -2) return {false, err};
                 if (i < 0) return {false, "Base has nothing below it to merge into"};
                 const std::string name = layerName(m, i), below = layerName(m, i - 1);
                 UndoRecord r = stackUndo(m, "Merge Paint Layer");
                 if (!mergePaintLayerDown(m, i, err)) return {false, err};
                 doc.undo.push(std::move(r));
                 return {true, "Merged '" + name + "' into '" + below + "'"};
             }});

    reg.add({"paint_layer_flatten", "Flatten every visible paint layer into Base (hidden layers are discarded).", {},
             [](Document& doc, const Args&) -> CommandResult {
                 if (paintsTexture(doc.mesh)) return textureLayerCommand(doc, "flatten", {});
                 Mesh& m = doc.mesh;
                 if (m.paintLayers.empty()) return {true, "There are no paint layers to flatten"};
                 UndoRecord r = stackUndo(m, "Flatten Paint Layers");
                 flattenPaintLayers(m);
                 doc.undo.push(std::move(r));
                 return {true, "Flattened the paint layers into Base"};
             }});

    reg.add({"paint_layer_clear", "Clear a paint layer: a layer becomes transparent, Base white (undoable).",
             {kIndexParam, kLayerParam},
             [](Document& doc, const Args& a) -> CommandResult {
                 if (paintsTexture(doc.mesh)) return textureLayerCommand(doc, "clear", a);
                 Mesh& m = doc.mesh;
                 if (m.vertexCount() == 0) return {false, "There is no mesh to paint"};
                 std::string err;
                 const int i = layerArg(m, a, err);
                 if (i == -2) return {false, err};
                 if (i >= 0 && m.paintLayers[size_t(i)].locked) return {false, "Layer '" + layerName(m, i) + "' is locked"};
                 if (m.paintLayers.empty() && !m.hasColors()) return {true, "The mesh has no vertex paint"};
                 UndoRecord r = UndoRecord::captureColors(m, "Clear Paint Layer", {}, i);
                 clearPaintLayer(m, i);
                 doc.undo.push(std::move(r));
                 return {true, "Cleared " + layerName(m, i)};
             }});

    reg.add({"paint_layer_list", "List the active Part's paint layers, top first.", {},
             [](Document& doc, const Args&) -> CommandResult {
                 if (paintsTexture(doc.mesh)) return textureLayerCommand(doc, "list", {});
                 const Mesh& m = doc.mesh;
                 std::string out;
                 for (int i = int(m.paintLayers.size()) - 1; i >= -1; --i) {
                     out += (i == (m.paintLayers.empty() ? -1 : m.activePaintLayer) ? "* " : "  ") + std::to_string(i) + " " +
                            layerName(m, i);
                     if (i >= 0) {
                         const PaintLayer& l = m.paintLayers[size_t(i)];
                         out += std::string(" (") + blendModeId(l.blend) + ", " + percent(l.opacity) +
                                (l.visible ? "" : ", hidden") + (l.locked ? ", locked" : "") + ")";
                     }
                     if (i > -1) out += "\n";
                 }
                 return {true, out};
             }});
}

} // namespace sf
