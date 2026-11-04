// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Names older SculptForge versions wrote into projects, brush files, configs and scripts, before the brushes got
// their own names. They are read only, mapped to today's name, and never shown or written
// (docs/legal/trademarks.md, tools/marks_allow.txt).
#pragma once

#include <string_view>

namespace sf::legacy {

struct Rename {
    std::string_view old, now;
};

// Brush stored names (projects, .sfbrush, `stroke brush=`), compared squashed and lower-case.
inline constexpr Rename kBrushNames[] = {{"damstandard", "SharpCrease"}, {"claybuildup", "ClayBlock"},
                                         {"snakehook", "Tendril"}};

// Action ids (hotkeys, shelves and menus saved in configs).
inline constexpr Rename kActionIds[] = {{"brush.damstandard", "brush.sharpcrease"},
                                        {"brush.claybuildup", "brush.clayblock"},
                                        {"brush.snakehook", "brush.tendril"}};

// User library folders (Documents/SculptForge/<folder>): moved to today's name once, when the library opens.
inline constexpr Rename kLibraryFolders[] = {{"MatCaps", "Lit Spheres"}};

// Today's name for an old one, or "" when `name` isn't an old name.
inline std::string_view renamed(const Rename* begin, const Rename* end, std::string_view name) {
    for (const Rename* r = begin; r != end; ++r)
        if (r->old == name) return r->now;
    return {};
}
template <size_t N> std::string_view renamed(const Rename (&table)[N], std::string_view name) {
    return renamed(table, table + N, name);
}

} // namespace sf::legacy
