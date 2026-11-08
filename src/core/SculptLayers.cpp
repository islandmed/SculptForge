// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/SculptLayers.h"

#include "core/Commands.h"
#include "core/Multires.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <sstream>

namespace sf {

std::string uniqueSculptLayerName(const Mesh& m, const std::string& wanted, int except) {
    const std::string stem = wanted.empty() ? std::string("Layer") : wanted;
    auto taken = [&](const std::string& n) {
        for (size_t i = 0; i < m.layers.size(); ++i)
            if (int(i) != except && m.layers[i].name == n) return true;
        return false;
    };
    if (!taken(stem)) return stem;
    for (int k = 2;; ++k)
        if (std::string c = stem + " " + std::to_string(k); !taken(c)) return c;
}

namespace {

bool valid(const Mesh& m, int i) { return i >= 0 && i < int(m.layers.size()); }

// After a layer list change: positions from base + layers, or the base itself once the last layer is gone.
void settle(Mesh& m) {
    if (m.layers.empty()) {
        if (m.base.size() == m.positions.size()) m.positions = m.base;
        m.base.clear();
    } else {
        m.evaluateAll();
    }
}

} // namespace

void moveSculptLayer(Mesh& m, int from, int to) {
    if (!valid(m, from)) return;
    to = std::clamp(to, 0, int(m.layers.size()) - 1);
    if (to == from) return;
    if (to < from) std::rotate(m.layers.begin() + to, m.layers.begin() + from, m.layers.begin() + from + 1);
    else std::rotate(m.layers.begin() + from, m.layers.begin() + from + 1, m.layers.begin() + to + 1);
    m.evaluateAll(); // (the sum is the same; re-evaluated for bit-identical positions in the new order)
}

void renameSculptLayer(Mesh& m, int i, const std::string& name) {
    if (valid(m, i)) m.layers[size_t(i)].name = name;
}

void duplicateSculptLayer(Mesh& m, int i, const std::string& name) {
    if (!valid(m, i)) return;
    SculptLayer copy = m.layers[size_t(i)];
    copy.name = name;
    m.layers.insert(m.layers.begin() + i + 1, std::move(copy));
    m.evaluateAll();
}

void mergeSculptLayerDown(Mesh& m, int i) {
    if (!valid(m, i) || i + 1 >= int(m.layers.size())) return;
    SculptLayer& below = m.layers[size_t(i) + 1];
    const SculptLayer& top = m.layers[size_t(i)];
    const float a = below.visible ? below.weight : 0.0f, b = top.visible ? top.weight : 0.0f;
    for (size_t v = 0; v < below.delta.size() && v < top.delta.size(); ++v) below.delta[v] = a * below.delta[v] + b * top.delta[v];
    below.weight = 1.0f;
    below.visible = true;
    m.layers.erase(m.layers.begin() + i);
    m.evaluateAll();
}

void invertSculptLayer(Mesh& m, int i) {
    if (!valid(m, i)) return;
    for (glm::vec3& d : m.layers[size_t(i)].delta) d = -d;
    m.evaluateAll();
}

void bakeSculptLayers(Mesh& m, const std::vector<bool>& which) {
    if (m.layers.empty()) return;
    std::vector<SculptLayer> kept;
    for (size_t i = 0; i < m.layers.size(); ++i) {
        if (i < which.size() && which[i]) {
            const float f = m.layerFactor(i);
            const std::vector<glm::vec3>& d = m.layers[i].delta;
            if (f != 0.0f)
                for (size_t v = 0; v < m.base.size() && v < d.size(); ++v) m.base[v] += f * d[v];
        } else {
            kept.push_back(std::move(m.layers[i]));
        }
    }
    m.layers = std::move(kept);
    settle(m);
}

void removeSculptLayers(Mesh& m, const std::vector<bool>& which) {
    std::vector<SculptLayer> kept;
    for (size_t i = 0; i < m.layers.size(); ++i)
        if (i >= which.size() || !which[i]) kept.push_back(std::move(m.layers[i]));
    m.layers = std::move(kept);
    settle(m);
}

namespace {

using T = ParamSpec::Type;

const ParamSpec kIndex{"index", T::Number, "Layer index (0-based; default the active layer).", false};
const ParamSpec kIndices{"indices", T::String,
                         "Which layers: 'all', or 0-based indices separated by commas, e.g. '0,2' (default the active "
                         "layer).",
                         false};

// The layer `index` names, or the active layer. -1 when there is none.
int layerIndex(const Document& doc, const Args& a) {
    const int i = a.contains("index") ? int(argNumber(a, "index", -1)) : doc.activeLayer;
    return valid(doc.mesh, i) ? i : -1;
}

// Marks the layers `indices` names (or the active layer). Empty on an error (in `error`).
std::vector<bool> layerSet(const Document& doc, const Args& a, std::string& error) {
    const size_t n = doc.mesh.layers.size();
    std::vector<bool> which(n, false);
    if (n == 0) {
        error = "There are no sculpt layers";
        return {};
    }
    const std::string text = argString(a, "indices");
    if (text.empty()) {
        const int i = layerIndex(doc, a);
        if (i < 0) {
            error = "Choose a layer";
            return {};
        }
        which[size_t(i)] = true;
        return which;
    }
    if (text == "all") return std::vector<bool>(n, true);
    std::stringstream in(text);
    std::string item;
    bool any = false;
    while (std::getline(in, item, ',')) {
        char* end = nullptr;
        const long v = std::strtol(item.c_str(), &end, 10);
        if (end == item.c_str() || v < 0 || size_t(v) >= n) {
            error = "No layer '" + item + "'";
            return {};
        }
        which[size_t(v)] = true;
        any = true;
    }
    if (!any) error = "Choose a layer";
    return any ? which : std::vector<bool>{};
}

// Runs `op` on the active Part's layers on every subdivision level, as one undo step. `settingsOnly`: `op` only
// changes weights, visibility or the strength for all layers, which the levels pick up by themselves
// (subdivCommit), so only the working mesh changes.
void editLayers(Document& doc, const std::string& label, const std::function<void(Mesh&)>& op, bool settingsOnly = false) {
    Mesh m = doc.mesh;
    if (settingsOnly) op(m);
    else subdivApplyLayerOp(m, op);
    m.recomputeAllNormals();
    doc.replaceMeshUndoable(std::move(m), label);
}

int countMarked(const std::vector<bool>& which) { return int(std::count(which.begin(), which.end(), true)); }

std::string layersWord(int n) { return std::to_string(n) + (n == 1 ? " layer" : " layers"); }

} // namespace

void registerSculptLayerCommands(CommandRegistry& reg) {
    reg.add({"layer_move",
             "Move a sculpt layer in the list (undoable). Layer 0 is the top of the list.",
             {kIndex, {"direction", T::String, "'up', 'down', 'top' or 'bottom'.", true}},
             [](Document& doc, const Args& a) -> CommandResult {
                 const int i = layerIndex(doc, a);
                 if (i < 0) return {false, "Choose a layer"};
                 const std::string dir = argString(a, "direction");
                 const int last = int(doc.mesh.layers.size()) - 1;
                 int to = i;
                 if (dir == "up") to = i - 1;
                 else if (dir == "down") to = i + 1;
                 else if (dir == "top") to = 0;
                 else if (dir == "bottom") to = last;
                 else return {false, "Direction is up, down, top or bottom"};
                 to = std::clamp(to, 0, last);
                 if (to == i) return {true, "Already there"};
                 const bool wasActive = doc.activeLayer == i;
                 const int active = doc.activeLayer;
                 editLayers(doc, "Move Layer", [&](Mesh& m) { moveSculptLayer(m, i, to); });
                 // The active layer follows its layer.
                 if (wasActive) doc.activeLayer = to;
                 else if (active >= 0) doc.activeLayer = active + (i < active && to >= active ? -1 : i > active && to <= active ? 1 : 0);
                 return {true, "Moved '" + doc.mesh.layers[size_t(to)].name + "' " + dir};
             }});

    reg.add({"layer_set",
             "Change a sculpt layer: rename it, its weight (-1 to 1: 1 = as sculpted, 0 = off, negative = the "
             "opposite way) or show / hide it (undoable).",
             {kIndex,
              {"name", T::String, "New name.", false},
              {"weight", T::Number, "Weight -1 to 1.", false},
              {"visible", T::Bool, "Show (true) or hide (false) the layer.", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 const int i = layerIndex(doc, a);
                 if (i < 0) return {false, "Choose a layer"};
                 const SculptLayer& cur = doc.mesh.layers[size_t(i)];
                 std::string name = cur.name;
                 if (a.contains("name")) {
                     const std::string wanted = argString(a, "name");
                     if (wanted.empty()) return {false, "Give the layer a name"};
                     name = uniqueSculptLayerName(doc.mesh, wanted, i);
                 }
                 float weight = cur.weight;
                 if (a.contains("weight")) {
                     const double w = argNumber(a, "weight", 1.0);
                     if (!std::isfinite(w)) return {false, "Weight is a number from -1 to 1"};
                     weight = float(std::clamp(w, -1.0, 1.0));
                 }
                 const bool visible = argBool(a, "visible", cur.visible);
                 if (name == cur.name && weight == cur.weight && visible == cur.visible) return {true, "No change"};
                 const bool renamed = name != cur.name;
                 editLayers(doc, renamed ? "Rename Layer" : "Layer Settings", [&](Mesh& m) {
                     if (!valid(m, i)) return;
                     m.layers[size_t(i)].name = name; // every level, so the levels still match it by name
                     m.layers[size_t(i)].weight = weight;
                     m.layers[size_t(i)].visible = visible;
                     m.evaluateAll();
                 }, !renamed);
                 const SculptLayer& l = doc.mesh.layers[size_t(i)];
                 char w[32];
                 std::snprintf(w, sizeof w, "%.2f", double(l.weight));
                 return {true, "Layer '" + l.name + "': weight " + w + (l.visible ? "" : ", hidden")};
             }});

    reg.add({"layer_strength",
             "One strength for all sculpt layers of the active Part: scales every layer's weight (-1 to 1; 1 = as "
             "sculpted, 0 = all layers off). Undoable.",
             {{"strength", T::Number, "Strength -1 to 1.", true}},
             [](Document& doc, const Args& a) -> CommandResult {
                 if (doc.mesh.layers.empty()) return {false, "There are no sculpt layers"};
                 const double s = argNumber(a, "strength", 1.0);
                 if (!std::isfinite(s)) return {false, "Strength is a number from -1 to 1"};
                 const float strength = float(std::clamp(s, -1.0, 1.0));
                 if (strength == doc.mesh.layerStrength) return {true, "No change"};
                 editLayers(doc, "Strength for All Layers", [&](Mesh& m) {
                     m.layerStrength = strength;
                     m.evaluateAll();
                 }, true);
                 char t[48];
                 std::snprintf(t, sizeof t, "All layers at strength %.2f", double(strength));
                 return {true, t};
             }});

    reg.add({"layer_duplicate", "Copy a sculpt layer (offsets and settings) to a new layer below it (undoable).", {kIndex},
             [](Document& doc, const Args& a) -> CommandResult {
                 const int i = layerIndex(doc, a);
                 if (i < 0) return {false, "Choose a layer"};
                 const std::string name = uniqueSculptLayerName(doc.mesh, doc.mesh.layers[size_t(i)].name + " copy");
                 editLayers(doc, "Duplicate Layer", [&](Mesh& m) { duplicateSculptLayer(m, i, name); });
                 doc.activeLayer = i + 1;
                 return {true, "Duplicated into '" + name + "'"};
             }});

    reg.add({"layer_merge_down",
             "Merge a sculpt layer into the one below it, keeping how both show (the merged layer gets weight 1). "
             "Undoable.",
             {kIndex},
             [](Document& doc, const Args& a) -> CommandResult {
                 const int i = layerIndex(doc, a);
                 if (i < 0) return {false, "Choose a layer"};
                 if (i + 1 >= int(doc.mesh.layers.size()))
                     return {false, "The bottom layer has no layer below it (Bake puts it into the base mesh)"};
                 editLayers(doc, "Merge Layer Down", [&](Mesh& m) { mergeSculptLayerDown(m, i); });
                 doc.activeLayer = i;
                 return {true, "Merged into '" + doc.mesh.layers[size_t(i)].name + "'"};
             }});

    reg.add({"layer_invert", "Turn a sculpt layer's offsets the opposite way (bumps become dents). Undoable.", {kIndex},
             [](Document& doc, const Args& a) -> CommandResult {
                 const int i = layerIndex(doc, a);
                 if (i < 0) return {false, "Choose a layer"};
                 editLayers(doc, "Invert Layer", [&](Mesh& m) { invertSculptLayer(m, i); });
                 return {true, "Inverted '" + doc.mesh.layers[size_t(i)].name + "'"};
             }});

    reg.add({"layer_bake",
             "Bake sculpt layers into the base mesh as they show now and remove them; the shape doesn't change. "
             "Undoable.",
             {kIndices, kIndex},
             [](Document& doc, const Args& a) -> CommandResult {
                 std::string err;
                 const std::vector<bool> which = layerSet(doc, a, err);
                 if (which.empty()) return {false, err};
                 const int n = countMarked(which);
                 editLayers(doc, n == int(which.size()) ? "Bake All Layers" : "Bake Layers",
                            [&](Mesh& m) { bakeSculptLayers(m, which); });
                 doc.activeLayer = -1;
                 return {true, "Baked " + layersWord(n) + " into the base mesh"};
             }});

    reg.add({"layer_remove_many", "Delete sculpt layers and their offsets (undoable).", {kIndices, kIndex},
             [](Document& doc, const Args& a) -> CommandResult {
                 std::string err;
                 const std::vector<bool> which = layerSet(doc, a, err);
                 if (which.empty()) return {false, err};
                 const int n = countMarked(which);
                 editLayers(doc, n == int(which.size()) ? "Delete All Layers" : "Delete Layers",
                            [&](Mesh& m) { removeSculptLayers(m, which); });
                 doc.activeLayer = -1;
                 return {true, "Deleted " + layersWord(n)};
             }});
}

} // namespace sf
