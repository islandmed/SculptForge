// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Plain words that find a feature in the command search (F3) when its label says it another way: "retopo" finds
// Quad Remesh, "multires" finds Divide, "kitbash" finds Append. Generic sculpting words only, never another
// product's feature names (docs/legal/trademarks.md). A hit shows as "Quad Remesh (also: retopo)".
#pragma once

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace sf {

struct SearchAlias {
    const char* ours;    // a word or phrase of SculptForge's labels or palette paths
    const char* words;   // the other words for it, joined by ", "
    bool wholeLabel;     // only when it is the whole label ("Dynamic": too common inside other labels)
};

inline constexpr SearchAlias kSearchAliases[] = {
    {"Voxel Remesh", "remesh, voxel, block out, rebuild, uniform mesh", false},
    {"Quad Remesh", "retopology, retopo, quads, auto retopo", false},
    {"Dynamic", "dynamic topology, adaptive detail, tessellation", true},
    {"Dynamic Topology", "adaptive detail, tessellation", false},
    {"Face Group", "groups, regions", false},
    {"face group", "groups, regions", false},
    {"Cut", "clip, trim, slice, boolean", false},
    {"Clip", "trim, slice, flatten", true},
    {"Part", "folders, objects, meshes", false},
    {"Divide", "subdivide, subdivision, multires, levels", false},
    {"Higher Res", "subdivision, multires, levels", true},
    {"Lower Res", "subdivision, multires, levels", true},
    {"Vertex Paint", "vertex color, vertex colour, color, colour", false},
    {"vertex paint", "vertex color, vertex colour", false},
    {"Append", "insert, kitbash, add mesh, import mesh", true},
    {"Library", "assets, browser", false},
    {"Forge Sphere", "armature, skeleton, base mesh", false},
    {"Lit Sphere", "sphere material, shader", false},
    {"Close Holes", "fill holes, cap, watertight", false},
    {"Snapshot", "checkpoint, backup", false},
    {"Steady Stroke", "stabilizer, smoothing", false},
    {"Falloff", "softness, hardness", false},
    {"Bake", "normal map, displacement map", false},
    {"Gizmo", "manipulator, transform handle", false},
};

namespace aliases {

inline std::string lowered(std::string_view s) {
    std::string r(s);
    for (auto& c : r) c = char(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

// Whether `text` holds `word` as whole words (a plural s still counts: "Parts", "Face Groups").
inline bool holdsWord(std::string_view text, std::string_view word) {
    auto isWord = [](unsigned char c) { return std::isalnum(c) != 0; };
    // Phrases of two words or more also match in sentence case ("Forge sphere set" from a command's name).
    const bool anyCase = word.find(' ') != std::string_view::npos;
    const std::string textStore = anyCase ? lowered(text) : std::string(text);
    const std::string wordStore = anyCase ? lowered(word) : std::string(word);
    const std::string_view t = textStore, w = wordStore;
    for (size_t at = t.find(w); at != std::string_view::npos; at = t.find(w, at + 1)) {
        const size_t end = at + w.size();
        const bool startOk = at == 0 || !isWord(static_cast<unsigned char>(t[at - 1]));
        const bool endOk = end >= t.size() || !isWord(static_cast<unsigned char>(t[end])) ||
                           (t[end] == 's' && (end + 1 >= t.size() || !isWord(static_cast<unsigned char>(t[end + 1]))));
        if (startOk && endOk) return true;
    }
    return false;
}

} // namespace aliases

// The other words for what `label` (and its palette path) names, one per entry, without repeats; empty when none.
// Matches whole words, so "Part" finds "Parts" and "Tool > Parts" but not "Partial".
inline std::vector<std::string> searchAliasesIn(std::string_view label, std::string_view palette = {}) {
    std::vector<std::string> out;
    for (const SearchAlias& a : kSearchAliases) {
        const bool hit = a.wholeLabel ? label == a.ours
                                      : (aliases::holdsWord(label, a.ours) || aliases::holdsWord(palette, a.ours));
        if (!hit) continue;
        std::string_view rest = a.words;
        while (!rest.empty()) {
            const size_t comma = rest.find(',');
            std::string w(rest.substr(0, comma));
            rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
            while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
            bool seen = false;
            for (const auto& o : out) seen = seen || o == w;
            if (!seen) out.push_back(std::move(w));
        }
    }
    return out;
}

} // namespace sf
