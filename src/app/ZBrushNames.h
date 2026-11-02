// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// SculptForge's own names for features ZBrush users know by ZBrush's names (Forge Spheres: ZSpheres, Parts:
// SubTools...), so the command search finds a feature by the name a ZBrush user types, shown as
// "Forge Spheres (ZBrush users: ZSpheres)".
#pragma once

#include <cctype>
#include <string>
#include <string_view>

namespace sf {

struct ZBrushName {
    const char* ours;   // as SculptForge's interface writes it
    const char* theirs; // the ZBrush name
    bool wholeLabel;    // only when it is the whole label ("Add", "Intensity": too common inside other labels)
};

inline constexpr ZBrushName kZBrushNames[] = {
    {"Forge Sphere", "ZSpheres", false},
    {"Library", "LightBox", false},
    {"Vertex Paint", "Polypaint", false},
    {"vertex paint", "Polypaint", false},
    {"Part", "SubTool", false},
    {"Send to ZBrush", "GoZ", false},
    {"Add", "Zadd", true},
    {"Sub", "Zsub", true},
    {"Intensity", "Z Intensity", true},
    {"Intensity at cursor", "Z Intensity", true},
    {"Smooth Intensity", "Smooth Z Intensity", true},
    {"Voxel Remesh", "DynaMesh", false},
    {"Quad Remesh", "ZRemesher", false},
    {"Dynamic", "Sculptris Pro", true},
    {"Dynamic Topology", "Sculptris Pro", false},
    {"Snapshot", "QuickSave", false},
    {"Gizmo", "Gizmo 3D, Transpose", false},
    {"Lazy Stroke", "LazyMouse", false},
    {"Steady Stroke", "LazyMouse", false},
    {"Wire", "PolyF", true},
    {"Wireframe", "PolyFrame", false},
    {"Face Group", "Polygroups", false},
    {"face group", "Polygroups", false},
    {"Sharp Crease", "DamStandard", false},
    {"Clay Block", "ClayBuildup", false},
    {"Elastic Tendril", "KelvinSnake", false},
    {"Tendril", "SnakeHook", false},
    {"Falloff", "Focal Shift", false},
    {"Drag Stamp", "DragRect", false},
    {"Slide Stamp", "DragDot", false},
    {"Quick Menu", "QuickMenu", false},
    {"Project All", "ProjectAll", false},
    {"Lit Sphere", "MatCap", false},
    {"Mask Pen", "MaskPen", false},
    {"Mask Lasso", "MaskLasso", false},
    {"Mask Rectangle", "MaskRect", false},
    {"Select Rectangle", "SelectRect", false},
    {"Select Lasso", "SelectLasso", false},
};

// SculptForge's own earlier names, which search still finds (never shown as a ZBrush name).
inline constexpr ZBrushName kEarlierNames[] = {
    {"Quad Remesh", "QRemesh", false},
};

// The ZBrush names of the renamed features `label` (and its palette path) names, joined by ", "; "" when none.
// Matches whole words, so "Part" finds "Parts" and "Tool > Parts" but not "Partial". `earlier` adds SculptForge's
// own earlier names (for search).
inline std::string zbrushNamesIn(std::string_view label, std::string_view palette = {}, bool earlier = false) {
    std::string out;
    auto add = [&](const char* theirs) {
        if (out.find(theirs) != std::string::npos) return;
        if (!out.empty()) out += ", ";
        out += theirs;
    };
    auto isWord = [](unsigned char c) { return std::isalnum(c) != 0; };
    auto lowered = [](std::string_view s) {
        std::string r(s);
        for (auto& c : r) c = char(std::tolower(static_cast<unsigned char>(c)));
        return r;
    };
    auto contains = [&](std::string_view original, std::string_view name) {
        // Names of two words or more also match in sentence case ("Forge sphere set" from a command's name).
        const bool anyCase = name.find(' ') != std::string_view::npos;
        const std::string textStore = anyCase ? lowered(original) : std::string(original);
        const std::string wordStore = anyCase ? lowered(name) : std::string(name);
        const std::string_view text = textStore, word = wordStore;
        for (size_t at = text.find(word); at != std::string_view::npos; at = text.find(word, at + 1)) {
            const size_t end = at + word.size();
            const bool startOk = at == 0 || !isWord(static_cast<unsigned char>(text[at - 1]));
            // "Parts", "Snapshots", "Face Groups": a plural s is still the word.
            const bool endOk = end >= text.size() || !isWord(static_cast<unsigned char>(text[end])) ||
                               (text[end] == 's' && (end + 1 >= text.size() || !isWord(static_cast<unsigned char>(text[end + 1]))));
            if (startOk && endOk) return true;
        }
        return false;
    };
    auto scan = [&](const auto& table) {
        for (const ZBrushName& n : table)
            if (n.wholeLabel ? label == n.ours : (contains(label, n.ours) || contains(palette, n.ours))) add(n.theirs);
    };
    scan(kZBrushNames);
    if (earlier) scan(kEarlierNames);
    return out;
}

} // namespace sf
