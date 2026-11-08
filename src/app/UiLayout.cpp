// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/UiLayout.h"

#include "core/LegacyNames.h"

#include <algorithm>

namespace sf {

namespace {

Json stringArray(const std::vector<std::string>& v) {
    Json a = Json::array();
    for (const auto& s : v) a.push(s);
    return a;
}

void readStrings(const Json& j, std::vector<std::string>& out) {
    if (!j.isArray()) return;
    out.clear();
    for (size_t i = 0; i < j.size(); ++i)
        if (j[i].isString() && !j[i].asString().empty()) {
            out.push_back(legacy::modernize(j[i].asString()));
        }
}

} // namespace

UiLayout standardLayout() {
    // docs/ui-design.md §3: what a classic user expects to find around the canvas.
    UiLayout l;
    l.rightTray = {"tool"};
    l.openSubpalettes = {"tool.part"};
    l.topShelf = {"ui.library",       "sep",           "tool.draw",            "tool.move",           "tool.scale",
                  "tool.rotate",       "tool.gizmo_all",   "sep",           "draw.sculpt",          "draw.paint",          "sep",
                  "draw.raise",         "draw.lower",     "draw.rgb",             "draw.paint_vertex",   "draw.paint_texture",   "draw.new_texture",     "sep",                 "widget.draw_size",
                  "widget.strength",   "row",           "ui.brush_lab",         "draw.front_faces",    "sep",
                  "draw.dynamic",      "widget.dyntopo_detail", "sep",
                  "transform.symmetry_x",   "transform.symmetry_y", "transform.symmetry_z",    "transform.symmetry_radial", "sep",                 "draw.pressure_strength",
                  "draw.pressure_size", "widget.points"};
    l.leftShelf = {"widget.tile.brush", "widget.tile.stroke", "stroke.popup", "widget.tile.alpha", "widget.tile.material",
                   "widget.color"};
    l.rightShelf = {"edit.undo",  "edit.redo",       "sep",
                    "view.frame", "widget.nav.move", "widget.nav.zoom", "widget.nav.rotate", "sep",
                    "view.wire", "view.facegroups", "view.mask",       "view.vertexpaint",    "sep",
                    "part.solo", "view.splats"};
    l.bottomShelf = {"brush.standard", "brush.clay",    "brush.clayblock", "brush.sharpcrease", "brush.move",
                     "brush.smooth",   "brush.inflate", "brush.pinch",       "brush.flatten",     "brush.crease",
                     "brush.trim",     "brush.tendril", "brush.layer",     "brush.nudge",       "brush.kelvingrab",
                     "brush.detailclone", "brush.paint",
                     "widget.add_brush"};
    return l;
}

UiLayout standardLayout(int shellLayout) {
    UiLayout l = standardLayout();
    if (shellLayout != 1) return l;
    // The SculptForge layout: one tool row (the mode, the transform tools, add / subtract, the two sliders,
    // dynamic topology, symmetry). Library, Brush Lab, Front, the pressure options and the triangle counts moved to
    // the menus (Sculpt > Draw) and the status bar.
    l.topShelf = {"draw.sculpt",          "draw.paint",           "sep",
                  "tool.draw",            "tool.move",            "tool.scale",           "tool.rotate",       "tool.gizmo_all", "sep",
                  "draw.raise",            "draw.lower",            "draw.rgb",             "draw.paint_vertex", "draw.paint_texture",
                  "draw.new_texture",     "sep",                  "widget.draw_size",     "widget.strength",   "sep",
                  "draw.dynamic",         "widget.dyntopo_detail", "sep",
                  "transform.symmetry_x", "transform.symmetry_y", "transform.symmetry_z", "transform.symmetry_radial"};
    l.rightShelf = {"edit.undo",  "edit.redo",       "sep",
                    "view.frame", "widget.nav.move", "widget.nav.zoom", "widget.nav.rotate", "sep",
                    "view.wire", "view.facegroups", "view.mask",       "view.vertexpaint",    "sep",
                    "part.solo", "view.splats"};
    return l;
}

Json toJson(const UiLayout& l) {
    Json j = Json::object();
    j.set("version", double(l.version));
    j.set("shelvesVisible", l.shelvesVisible);
    j.set("paletteBarVisible", l.paletteBarVisible);
    j.set("leftHanded", l.leftHanded);
    j.set("leftTrayOpen", l.leftTrayOpen);
    j.set("rightTrayOpen", l.rightTrayOpen);
    j.set("leftTrayWidth", double(l.leftTrayWidth));
    j.set("rightTrayWidth", double(l.rightTrayWidth));
    j.set("leftTray", stringArray(l.leftTray));
    j.set("rightTray", stringArray(l.rightTray));
    j.set("openSubpalettes", stringArray(l.openSubpalettes));
    j.set("topShelf", stringArray(l.topShelf));
    j.set("leftShelf", stringArray(l.leftShelf));
    j.set("rightShelf", stringArray(l.rightShelf));
    j.set("bottomShelf", stringArray(l.bottomShelf));
    j.set("consoleOpen", l.consoleOpen);
    j.set("consoleHeight", double(l.consoleHeight));
    j.set("libraryOpen", l.libraryOpen);
    j.set("libraryRows", double(l.libraryRows));
    j.set("libraryTile", double(l.libraryTile));
    j.set("libraryTab", l.libraryTab);
    j.set("libraryBottom", l.libraryBottom);
    j.set("shelfTiles", double(l.shelfTiles));
    return j;
}

bool shelfEmpty(const std::vector<std::string>& items) {
    return std::all_of(items.begin(), items.end(), [](const std::string& id) { return id == "sep" || id == "row"; });
}

bool fromJson(const Json& j, UiLayout& l, std::string& error, int shellLayout) {
    if (!j.isObject()) {
        error = "The UI configuration is not a JSON object";
        return false;
    }
    UiLayout out = standardLayout(shellLayout);
    auto flag = [&](const char* key, bool& v) { v = j[key].asBool(v); };
    auto number = [&](const char* key, float& v) { v = float(j[key].asNumber(v)); };
    flag("shelvesVisible", out.shelvesVisible);
    flag("paletteBarVisible", out.paletteBarVisible);
    flag("leftHanded", out.leftHanded);
    flag("leftTrayOpen", out.leftTrayOpen);
    flag("rightTrayOpen", out.rightTrayOpen);
    number("leftTrayWidth", out.leftTrayWidth);
    number("rightTrayWidth", out.rightTrayWidth);
    readStrings(j["leftTray"], out.leftTray);
    readStrings(j["rightTray"], out.rightTray);
    readStrings(j["openSubpalettes"], out.openSubpalettes);
    readStrings(j["topShelf"], out.topShelf);
    // Version 2 (0.8) added Dynamic and its Detail slider: put them into stored top shelves, before symmetry.
    if (int(j["version"].asNumber(1)) < 2 && j["topShelf"].isArray() &&
        std::find(out.topShelf.begin(), out.topShelf.end(), "draw.dynamic") == out.topShelf.end()) {
        const auto at = std::find(out.topShelf.begin(), out.topShelf.end(), "transform.symmetry_x");
        out.topShelf.insert(at, {"draw.dynamic", "widget.dyntopo_detail", "sep"});
    }
    readStrings(j["leftShelf"], out.leftShelf);
    // Version 4 added the stroke popup's button under the Stroke tile: put it into stored left shelves.
    if (int(j["version"].asNumber(1)) < 4 && j["leftShelf"].isArray() &&
        std::find(out.leftShelf.begin(), out.leftShelf.end(), "stroke.popup") == out.leftShelf.end()) {
        const auto tile = std::find(out.leftShelf.begin(), out.leftShelf.end(), "widget.tile.stroke");
        if (tile != out.leftShelf.end()) out.leftShelf.insert(tile + 1, "stroke.popup");
    }
    readStrings(j["rightShelf"], out.rightShelf);
    // Version 3 added Undo and Redo at the top of the right shelf (#43): put them into stored ones.
    if (int(j["version"].asNumber(1)) < 3 && j["rightShelf"].isArray() &&
        std::find(out.rightShelf.begin(), out.rightShelf.end(), "edit.undo") == out.rightShelf.end() &&
        std::find(out.rightShelf.begin(), out.rightShelf.end(), "edit.redo") == out.rightShelf.end())
        out.rightShelf.insert(out.rightShelf.begin(), {"edit.undo", "edit.redo", "sep"});
    readStrings(j["bottomShelf"], out.bottomShelf);
    flag("consoleOpen", out.consoleOpen);
    number("consoleHeight", out.consoleHeight);
    flag("libraryOpen", out.libraryOpen);
    out.libraryRows = int(j["libraryRows"].asNumber(out.libraryRows));
    out.libraryTile = std::clamp(float(j["libraryTile"].asNumber(out.libraryTile)), kLibraryTileMin, kLibraryTileMax);
    if (j["libraryTab"].isString()) out.libraryTab = j["libraryTab"].asString();
    flag("libraryBottom", out.libraryBottom);
    out.shelfTiles = std::clamp(int(j["shelfTiles"].asNumber(out.shelfTiles)), 0, 2);
    l = std::move(out);
    return true;
}

} // namespace sf
