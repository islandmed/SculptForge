// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/SculptLayers.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/Multires.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <sstream>

namespace sf {

namespace {

using T = ParamSpec::Type;

bool validLayer(const Document& doc, int index, std::string& message) {
    if (index >= 0 && index < int(doc.mesh.layers.size())) return true;
    message = doc.mesh.layers.empty() ? "This Part has no sculpt layers" : "No layer " + std::to_string(index);
    return false;
}

// A change to the layers' offsets or list: on every subdivision level, one undo step (the mesh before it).
void changeEveryLevel(Document& doc, const std::function<void(Mesh&)>& op, std::string label) {
    const int active = doc.activeLayer;
    const std::string before =
        active >= 0 && active < int(doc.mesh.layers.size()) ? doc.mesh.layers[size_t(active)].name : std::string();
    Mesh m = doc.mesh;
    subdivForEachLevel(m, op);
    m.recomputeAllNormals();
    doc.replaceMeshUndoable(std::move(m), std::move(label));
    if (UndoRecord* r = doc.undo.lastUndo()) {
        r->hasActiveLayer = true; // (the undo command fills in activeLayerAfter when it undoes the step)
        r->activeLayerBefore = before;
    }
}

// A change to the settings only (weights, eyes, Strength): the levels share them (subdivCommit passes them on).
void changeSettings(Document& doc, const std::function<void(Mesh&)>& op, std::string label) {
    LayerSettingsUndo before = LayerSettingsUndo::capture(doc.mesh);
    op(doc.mesh);
    doc.mesh.evaluateAll();
    doc.mesh.recomputeAllNormals();
    doc.bvhDirty = true;
    pushLayerSettingsUndo(doc, std::move(before), std::move(label));
}

std::string layerName(const Document& doc, int index) { return "'" + doc.mesh.layers[size_t(index)].name + "'"; }

std::string number(double v) {
    std::ostringstream s;
    s.precision(3);
    s << v;
    return s.str();
}

} // namespace

void pushLayerSettingsUndo(Document& doc, LayerSettingsUndo before, std::string label) {
    UndoRecord r;
    r.label = std::move(label);
    r.layerSettings = std::make_unique<LayerSettingsUndo>(std::move(before));
    doc.undo.push(std::move(r));
    doc.modified = true;
}

bool moveSculptLayers(Document& doc, std::vector<int> layers, const std::string& where, int to, std::string& message,
                      std::vector<int>* moved) {
    const int n = int(doc.mesh.layers.size());
    std::sort(layers.begin(), layers.end());
    layers.erase(std::unique(layers.begin(), layers.end()), layers.end());
    if (layers.empty()) {
        message = n ? "Choose the layers to move" : "This Part has no sculpt layers";
        return false;
    }
    for (int i : layers)
        if (!validLayer(doc, i, message)) return false;
    std::vector<uint8_t> chosen(size_t(n), 0);
    for (int i : layers) chosen[size_t(i)] = 1;
    std::vector<int> order(static_cast<size_t>(n));
    for (int k = 0; k < n; ++k) order[size_t(k)] = k;
    if (where == "up" || where == "down") {
        // Each chosen layer one place, unless the place is taken by another chosen one that can't move.
        const bool up = where == "up";
        for (int s = 0; s < n; ++s) {
            const int k = up ? s : n - 1 - s, j = up ? k - 1 : k + 1;
            if (j < 0 || j >= n || !chosen[size_t(order[size_t(k)])] || chosen[size_t(order[size_t(j)])]) continue;
            std::swap(order[size_t(k)], order[size_t(j)]);
        }
    } else if (where == "top" || where == "bottom" || where == "to") {
        std::vector<int> rest;
        for (int k = 0; k < n; ++k)
            if (!chosen[size_t(k)]) rest.push_back(k);
        const int at = where == "top" ? 0 : where == "bottom" ? int(rest.size()) : std::clamp(to, 0, int(rest.size()));
        order = rest;
        order.insert(order.begin() + at, layers.begin(), layers.end());
    } else {
        message = "Move where? to=, up, down, top or bottom";
        return false;
    }
    const std::string what = layers.size() == 1 ? "layer " + layerName(doc, layers[0])
                                                : std::to_string(layers.size()) + " layers";
    std::vector<int> now;
    for (int k = 0; k < n; ++k)
        if (chosen[size_t(order[size_t(k)])]) now.push_back(k);
    if (moved) *moved = now;
    bool same = true;
    for (int k = 0; k < n; ++k) same = same && order[size_t(k)] == k;
    if (same) {
        const bool atTop = where == "up" || where == "top";
        std::string subject = what;
        subject[0] = char(std::toupper(static_cast<unsigned char>(subject[0])));
        const std::string is = layers.size() == 1 ? " is" : " are";
        message = where == "to" ? subject + (layers.size() == 1 ? " stays where it is" : " stay where they are")
                                : subject + is + (atTop ? " already at the top" : " already at the bottom");
        return where == "to";
    }
    changeEveryLevel(doc, [&](Mesh& m) { m.reorderLayers(order); }, layers.size() == 1 ? "Move Layer" : "Move Layers");
    for (int k = 0; k < n; ++k)
        if (order[size_t(k)] == doc.activeLayer) {
            doc.activeLayer = k;
            break;
        }
    message = "Moved " + what + (where == "top" ? " to the top" : where == "bottom" ? " to the bottom"
                                 : now.size() == 1 ? " to place " + std::to_string(now[0] + 1) + " of " + std::to_string(n)
                                                   : "");
    return true;
}

bool moveSculptLayer(Document& doc, int from, int to, std::string& message) {
    if (!validLayer(doc, from, message)) return false;
    if (to < 0 || to >= int(doc.mesh.layers.size())) {
        message = "Layer " + layerName(doc, from) + (to < 0 ? " is already at the top" : " is already at the bottom");
        return false;
    }
    return moveSculptLayers(doc, {from}, "to", to, message);
}

bool duplicateSculptLayer(Document& doc, int index, std::string& message) {
    if (!validLayer(doc, index, message)) return false;
    const std::string base = doc.mesh.layers[size_t(index)].name + " copy";
    std::string name = base;
    auto taken = [&](const std::string& x) {
        for (const SculptLayer& l : doc.mesh.layers)
            if (l.name == x) return true;
        return false;
    };
    for (int k = 2; taken(name); ++k) name = base + " " + std::to_string(k);
    changeEveryLevel(doc, [&](Mesh& m) { m.duplicateLayer(index, name); }, "Duplicate Layer");
    doc.activeLayer = index;
    message = "Duplicated layer as '" + name + "'";
    return true;
}

bool renameSculptLayer(Document& doc, int index, const std::string& name, std::string& message) {
    if (!validLayer(doc, index, message)) return false;
    if (name.empty() || name.size() > 256) {
        message = name.empty() ? "Give the layer a name" : "That name is too long (256 characters at most)";
        return false;
    }
    if (doc.mesh.layers[size_t(index)].name == name) {
        message = "Layer is already called '" + name + "'";
        return true;
    }
    const std::string old = layerName(doc, index);
    changeEveryLevel(doc, [&](Mesh& m) {
        if (index < int(m.layers.size())) m.layers[size_t(index)].name = name;
    }, "Rename Layer");
    message = "Renamed layer " + old + " to '" + name + "'";
    return true;
}

bool mergeSculptLayerDown(Document& doc, int index, std::string& message) {
    if (!validLayer(doc, index, message)) return false;
    if (index + 1 >= int(doc.mesh.layers.size())) {
        message = "Layer " + layerName(doc, index) + " is the bottom layer: nothing below to merge into";
        return false;
    }
    if (doc.mesh.layers[size_t(index)].visible != doc.mesh.layers[size_t(index) + 1].visible) {
        message = "Show (or hide) both layers to merge them";
        return false;
    }
    const std::string top = layerName(doc, index), below = layerName(doc, index + 1);
    const int active = doc.activeLayer; // (replacing the mesh drops an index past the shorter list)
    changeEveryLevel(doc, [&](Mesh& m) { m.mergeLayerDown(index); }, "Merge Layer Down");
    doc.activeLayer = active > index ? active - 1 : active; // the layer itself is now the merged one
    message = "Merged layer " + top + " into " + below + " (weight 1)";
    return true;
}

bool invertSculptLayer(Document& doc, int index, std::string& message) {
    if (!validLayer(doc, index, message)) return false;
    changeEveryLevel(doc, [&](Mesh& m) { m.invertLayer(index); }, "Invert Layer");
    message = "Inverted layer " + layerName(doc, index);
    return true;
}

bool bakeSculptLayer(Document& doc, int index, std::string& message) {
    if (index < 0) {
        int shown = 0;
        for (const SculptLayer& l : doc.mesh.layers) shown += l.visible ? 1 : 0;
        if (shown == 0) {
            message = doc.mesh.layers.empty() ? "This Part has no sculpt layers" : "No layer is shown: nothing to bake";
            return false;
        }
        const int kept = int(doc.mesh.layers.size()) - shown;
        changeEveryLevel(doc, [](Mesh& m) { m.bakeShownLayers(); }, "Bake All Layers");
        doc.activeLayer = -1;
        message = "Baked " + std::to_string(shown) + (shown == 1 ? " layer" : " layers") + " into the base mesh";
        if (kept) message += "; " + std::to_string(kept) + " hidden " + (kept == 1 ? "layer stays" : "layers stay");
        return true;
    }
    if (!validLayer(doc, index, message)) return false;
    if (!doc.mesh.layers[size_t(index)].visible) {
        message = "Layer " + layerName(doc, index) + " is hidden: show it to bake it";
        return false;
    }
    const std::string name = layerName(doc, index);
    const int active = doc.activeLayer; // (replacing the mesh drops an index past the shorter list)
    changeEveryLevel(doc, [&](Mesh& m) { m.bakeLayer(index); }, "Bake Layer");
    doc.activeLayer = active == index ? -1 : active > index ? active - 1 : active;
    message = "Baked layer " + name + " into the base mesh";
    return true;
}

bool clearSculptLayers(Document& doc, std::string& message) {
    if (doc.mesh.layers.empty()) {
        message = "This Part has no sculpt layers";
        return false;
    }
    const size_t n = doc.mesh.layers.size();
    changeEveryLevel(doc, [](Mesh& m) { m.clearLayers(); }, "Clear All Layers");
    doc.activeLayer = -1;
    message = "Removed " + std::to_string(n) + (n == 1 ? " layer" : " layers") + " (undo brings them back)";
    return true;
}

bool removeSculptLayer(Document& doc, int index, std::string& message) {
    if (!validLayer(doc, index, message)) return false;
    const std::string name = layerName(doc, index);
    changeEveryLevel(doc, [&](Mesh& m) {
        if (index >= int(m.layers.size())) return;
        m.layers.erase(m.layers.begin() + index);
        if (m.layers.empty()) {
            m.positions = m.base;
            m.base.clear();
        } else {
            m.evaluateAll();
        }
    }, "Remove Layer");
    doc.activeLayer = -1;
    message = "Removed layer " + name;
    return true;
}

bool setSculptLayerWeight(Document& doc, int index, float weight, std::string& message) {
    if (!validLayer(doc, index, message)) return false;
    if (!std::isfinite(weight)) {
        message = "The weight must be a number";
        return false;
    }
    changeSettings(doc, [&](Mesh& m) { m.layers[size_t(index)].weight = weight; }, "Layer Weight");
    message = "Layer " + layerName(doc, index) + " weight " + number(weight);
    return true;
}

bool setSculptLayerVisible(Document& doc, int index, bool visible, std::string& message) {
    if (!validLayer(doc, index, message)) return false;
    if (doc.mesh.layers[size_t(index)].visible != visible)
        changeSettings(doc, [&](Mesh& m) { m.layers[size_t(index)].visible = visible; },
                       visible ? "Show Layer" : "Hide Layer");
    message = (visible ? "Showing layer " : "Hid layer ") + layerName(doc, index);
    return true;
}

bool setSculptLayerStrength(Document& doc, float strength, std::string& message) {
    if (doc.mesh.layers.empty()) {
        message = "This Part has no sculpt layers";
        return false;
    }
    if (!std::isfinite(strength)) {
        message = "The strength must be a number";
        return false;
    }
    strength = std::clamp(strength, 0.0f, 2.0f);
    changeSettings(doc, [&](Mesh& m) { m.layerStrength = strength; }, "Layers Strength");
    message = "All layers at strength " + number(strength);
    return true;
}

void registerSculptLayerCommands(CommandRegistry& reg) {
    auto done = [](bool ok, std::string message) { return CommandResult{ok, std::move(message)}; };
    const ParamSpec indexParam{"index", T::Number, "Layer index (0 = the top layer).", true};

    reg.add({"layer_add",
             "Add a sculpt layer (blend shape / morph target). New strokes go into it until another layer is made active.",
             {{"name", T::String, "Layer name, e.g. 'Smile' or 'BlinkL'.", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 if (doc.mesh.micro) // (core/Micro: a micro level can't sit on sculpt layers yet)
                     return {false, "This Part has a micro level: sculpt layers can't go under it yet (delete the micro "
                                    "level first)"};
                 const std::string before = doc.activeLayer >= 0 && doc.activeLayer < int(doc.mesh.layers.size())
                                                ? doc.mesh.layers[size_t(doc.activeLayer)].name
                                                : std::string();
                 Mesh m = doc.mesh;
                 int idx = m.addLayer(argString(a, "name", "Layer " + std::to_string(doc.mesh.layers.size() + 1)));
                 doc.replaceMeshUndoable(std::move(m), "Add Layer");
                 if (UndoRecord* r = doc.undo.lastUndo()) {
                     r->hasActiveLayer = true;
                     r->activeLayerBefore = before;
                 }
                 doc.activeLayer = idx;
                 return {true, "Added layer '" + doc.mesh.layers[idx].name + "' (index " + std::to_string(idx) + ")"};
             }});

    reg.add({"layer_remove", "Delete a sculpt layer and its offsets, on every subdivision level.", {indexParam},
             [done](Document& doc, const Args& a) {
                 std::string msg;
                 const bool ok = removeSculptLayer(doc, int(argNumber(a, "index", -1)), msg);
                 return done(ok, msg);
             }});

    reg.add({"layer_set_weight",
             "Set how strongly a sculpt layer is applied (0 = off, 1 = as sculpted, -1 = the opposite shape; beyond "
             "-1..1 exaggerates). Undoable.",
             {indexParam, {"weight", T::Number, "Blend weight, typically -1 to 1.", true}},
             [done](Document& doc, const Args& a) {
                 std::string msg;
                 const bool ok =
                     setSculptLayerWeight(doc, int(argNumber(a, "index", -1)), float(argNumber(a, "weight", 1.0)), msg);
                 return done(ok, msg);
             }});

    reg.add({"layer_set_visible",
             "Show or hide a sculpt layer (the eye in Tool > Layers). A hidden layer keeps its weight and offsets.",
             {indexParam, {"visible", T::Bool, "true = shown (default), false = hidden.", false}},
             [done](Document& doc, const Args& a) {
                 std::string msg;
                 const bool ok =
                     setSculptLayerVisible(doc, int(argNumber(a, "index", -1)), argBool(a, "visible", true), msg);
                 return done(ok, msg);
             }});

    reg.add({"layer_strength",
             "Scale every sculpt layer of the Part at once (Tool > Layers > Strength): 1 = as set, 0 = the base mesh "
             "alone, up to 2.",
             {{"strength", T::Number, "0 to 2 (default 1).", true}},
             [done](Document& doc, const Args& a) {
                 std::string msg;
                 const bool ok = setSculptLayerStrength(doc, float(argNumber(a, "strength", 1.0)), msg);
                 return done(ok, msg);
             }});

    reg.add({"layer_move",
             "Move sculpt layers in the list (drag in Tool > Layers, Up / Down, Top / Bottom); several move together. "
             "The shape doesn't change.",
             {{"index", T::Number, "Layer index (0 = top).", false},
              {"indices", T::String, "Instead of index: several layers, e.g. '0,2', moved together.", false},
              {"to", T::Number, "New index (0 = top) of the (first) layer.", false},
              {"direction", T::Number, "Instead of to: -1 = one place up, 1 = one place down.", false},
              {"place", T::String, "Instead of to: 'top' or 'bottom'.", false}},
             [done](Document& doc, const Args& a) {
                 std::vector<int> layers;
                 if (a.contains("indices")) {
                     const std::string list = argString(a, "indices");
                     size_t at = 0;
                     while (at < list.size()) {
                         const size_t comma = std::min(list.find(',', at), list.size());
                         try {
                             layers.push_back(std::stoi(list.substr(at, comma - at)));
                         } catch (...) {
                             return done(false, "indices: numbers separated by commas, e.g. '0,2'");
                         }
                         at = comma + 1;
                     }
                 } else {
                     layers.push_back(int(argNumber(a, "index", -1)));
                 }
                 std::string where = "to", msg;
                 int to = 0;
                 if (a.contains("place")) where = argString(a, "place");
                 else if (a.contains("to")) to = int(argNumber(a, "to", 0));
                 else where = argNumber(a, "direction", 1) < 0 ? "up" : "down";
                 const bool ok = moveSculptLayers(doc, layers, where, to, msg);
                 return done(ok, msg);
             }});

    reg.add({"layer_duplicate", "Duplicate a sculpt layer: a copy just above it ('Smile copy'), made active.",
             {indexParam},
             [done](Document& doc, const Args& a) {
                 std::string msg;
                 const bool ok = duplicateSculptLayer(doc, int(argNumber(a, "index", -1)), msg);
                 return done(ok, msg);
             }});

    reg.add({"layer_rename", "Rename a sculpt layer (the blend shape / morph target name in exports).",
             {indexParam, {"name", T::String, "New name.", true}},
             [done](Document& doc, const Args& a) {
                 std::string msg;
                 const bool ok = renameSculptLayer(doc, int(argNumber(a, "index", -1)), argString(a, "name"), msg);
                 return done(ok, msg);
             }});

    reg.add({"layer_merge_down",
             "Merge a sculpt layer into the one below it, each at its weight: the shape stays the same and the "
             "merged layer (with the lower layer's name) has weight 1.",
             {indexParam},
             [done](Document& doc, const Args& a) {
                 std::string msg;
                 const bool ok = mergeSculptLayerDown(doc, int(argNumber(a, "index", -1)), msg);
                 return done(ok, msg);
             }});

    reg.add({"layer_invert", "Invert a sculpt layer: negate its offsets, so it pushes where it pulled.", {indexParam},
             [done](Document& doc, const Args& a) {
                 std::string msg;
                 const bool ok = invertSculptLayer(doc, int(argNumber(a, "index", -1)), msg);
                 return done(ok, msg);
             }});

    reg.add({"layer_bake",
             "Bake a sculpt layer into the base mesh as it shows now (weight and Strength) and remove the layer; "
             "all=true bakes every shown layer (hidden ones stay). The shape stays the same.",
             {{"index", T::Number, "Layer index (0 = top).", false},
              {"all", T::Bool, "Bake every shown layer (default false).", false}},
             [done](Document& doc, const Args& a) {
                 const bool all = argBool(a, "all", false);
                 if (!all && !a.contains("index")) return done(false, "Give index= or all=true");
                 std::string msg;
                 const bool ok = bakeSculptLayer(doc, all ? -1 : std::max(0, int(argNumber(a, "index", 0))), msg);
                 return done(ok, msg);
             }});

    reg.add({"layer_clear", "Delete every sculpt layer of the Part (one undo step).", {},
             [done](Document& doc, const Args&) {
                 std::string msg;
                 const bool ok = clearSculptLayers(doc, msg);
                 return done(ok, msg);
             }});

    reg.add({"layer_set_active",
             "Choose which sculpt layer new strokes go into (-1 = base mesh).",
             {{"index", T::Number, "Layer index, or -1 for the base mesh.", true}},
             [](Document& doc, const Args& a) -> CommandResult {
                 int idx = int(argNumber(a, "index", -1));
                 if (idx < -1 || idx >= int(doc.mesh.layers.size())) return {false, "No layer " + std::to_string(idx)};
                 doc.activeLayer = idx;
                 return {true, idx < 0 ? "Sculpting on base mesh" : "Sculpting on layer " + doc.mesh.layers[idx].name};
             }});

    reg.add({"layer_info", "List the active Part's sculpt layers: name, weight, shown or hidden, and the Strength.", {},
             [](Document& doc, const Args&) -> CommandResult {
                 const Mesh& m = doc.mesh;
                 if (m.layers.empty()) return {true, "No sculpt layers"};
                 std::string s = "Strength " + number(m.layerStrength);
                 for (size_t i = 0; i < m.layers.size(); ++i)
                     s += "\n" + std::to_string(i) + ": " + m.layers[i].name + " weight " + number(m.layers[i].weight) +
                          (m.layers[i].visible ? "" : " (hidden)") + (doc.activeLayer == int(i) ? " (active)" : "");
                 return {true, s};
             }});
}

} // namespace sf
