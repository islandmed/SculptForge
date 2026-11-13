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
            out.push_back(legacy::modernizeId(j[i].asString())); // (a custom brush's name is the user's text)
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
                    "view.frame", "widget.nav.move", "widget.nav.zoom", "widget.nav.rotate", "view.turntable",
                    "view.ortho", "view.floor_grid", "sep",
                    "view.wire", "view.facegroups", "view.mask",       "view.vertexpaint",    "view.flat_shading", "sep",
                    "part.solo", "view.splats"};
    l.bottomShelf = {"brush.standard", "brush.clay",    "brush.clayblock", "brush.sharpcrease", "brush.move",
                     "brush.smooth",   "brush.inflate", "brush.pinch",       "brush.flatten",     "brush.crease",
                     "brush.trim",     "brush.tendril", "brush.layer",     "brush.nudge",       "brush.kelvingrab",
                     "brush.cloth",    "brush.detailclone", "brush.paint",
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
                    "view.frame", "widget.nav.move", "widget.nav.zoom", "widget.nav.rotate", "view.turntable",
                    "view.ortho", "view.floor_grid", "sep",
                    "view.wire", "view.facegroups", "view.mask",       "view.vertexpaint",    "view.flat_shading", "sep",
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
    if (!l.myPanel.empty()) j.set("myPanel", stringArray(l.myPanel));
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
    if (!l.brushIds.empty()) {
        Json ids = Json::object();
        for (const auto& [name, id] : l.brushIds) ids.set(name, id);
        j.set("brushIds", ids);
    }
    return j;
}

MyPanelEntry myPanelEntry(const std::string& entry) {
    MyPanelEntry e;
    if (entry.rfind("action:", 0) == 0) e.section = false, e.id = entry.substr(7);
    else if (entry.rfind("section:", 0) == 0) e.section = true, e.id = entry.substr(8);
    return e;
}

bool shelfEmpty(const std::vector<std::string>& items) {
    return std::all_of(items.begin(), items.end(), [](const std::string& id) { return id == "sep" || id == "row"; });
}

UiLayoutPair layoutsFromUiJson(const Json& ui) {
    UiLayoutPair p;
    if (!ui.isObject()) return p;
    if (ui["layout"].isObject()) p.current = ui["layout"];
    if (ui.has("layoutSaved")) p.saved = ui["layoutSaved"].isObject() ? ui["layoutSaved"] : Json();
    else p.saved = p.current; // before the autosave: the one layout was the saved one
    return p;
}

void layoutsToUiJson(Json& ui, const Json& current, const Json& saved) {
    if (current.isObject()) ui.set("layout", current);
    ui.set("layoutSaved", saved.isObject() ? saved : Json());
}

Json toAutosaveJson(UiLayout l) {
    l.consoleOpen = false;
    l.libraryOpen = false;
    l.customize = false;
    return toJson(l);
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
    readStrings(j["myPanel"], out.myPanel);
    // Only well-formed entries, no repeats, at most kMyPanelMax (a hand-edited or shared file).
    {
        std::vector<std::string> kept;
        for (const std::string& e : out.myPanel)
            if (myPanelEntry(e).valid() && e.size() < 96 && std::find(kept.begin(), kept.end(), e) == kept.end() &&
                kept.size() < kMyPanelMax)
                kept.push_back(e);
        out.myPanel = std::move(kept);
    }
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
    // Version 5 added the view toggles (foirgairwolf on Discord): Turntable, Ortho and Floor under Rotate, Flat under
    // Paint, in stored right shelves that still have those neighbours.
    if (int(j["version"].asNumber(1)) < 5 && j["rightShelf"].isArray()) {
        auto has = [&](const char* id) { return std::find(out.rightShelf.begin(), out.rightShelf.end(), id) != out.rightShelf.end(); };
        auto after = [&](const char* anchor, std::initializer_list<const char*> ids) {
            const auto at = std::find(out.rightShelf.begin(), out.rightShelf.end(), anchor);
            if (at == out.rightShelf.end()) return;
            std::vector<std::string> add;
            for (const char* id : ids)
                if (!has(id)) add.push_back(id);
            out.rightShelf.insert(at + 1, add.begin(), add.end());
        };
        after("widget.nav.rotate", {"view.turntable", "view.ortho", "view.floor_grid"});
        after("view.vertexpaint", {"view.flat_shading"});
    }
    readStrings(j["bottomShelf"], out.bottomShelf);
    flag("consoleOpen", out.consoleOpen);
    number("consoleHeight", out.consoleHeight);
    flag("libraryOpen", out.libraryOpen);
    out.libraryRows = int(j["libraryRows"].asNumber(out.libraryRows));
    out.libraryTile = std::clamp(float(j["libraryTile"].asNumber(out.libraryTile)), kLibraryTileMin, kLibraryTileMax);
    if (j["libraryTab"].isString()) out.libraryTab = j["libraryTab"].asString();
    flag("libraryBottom", out.libraryBottom);
    out.shelfTiles = std::clamp(int(j["shelfTiles"].asNumber(out.shelfTiles)), 0, 2);
    if (j["brushIds"].isObject()) // a hand-edited file: short names and ids only, at most 256
        for (const auto& [name, id] : j["brushIds"].members())
            if (id.isString() && !id.asString().empty() && id.asString().size() <= 64 && name.size() <= 64 && out.brushIds.size() < 256)
                out.brushIds[name] = id.asString();
    l = std::move(out);
    return true;
}

Json panelPlacementsToJson(const std::vector<PanelPlacement>& panels) {
    Json list = Json::array();
    for (const PanelPlacement& p : panels) {
        Json e = Json::object();
        e.set("id", p.id);
        e.set("w", double(p.w));
        e.set("h", double(p.h));
        if (p.placed) {
            e.set("x", double(p.x));
            e.set("y", double(p.y));
        }
        list.push(std::move(e));
    }
    Json j = Json::object();
    j.set("version", 1.0);
    j.set("panels", std::move(list));
    return j;
}

std::vector<PanelPlacement> panelPlacementsFromJson(const Json& j) {
    std::vector<PanelPlacement> out;
    const Json& list = j["panels"];
    for (size_t i = 0; i < list.size() && out.size() < 32; ++i) {
        const Json& e = list[i];
        if (!e["id"].isString()) continue;
        PanelPlacement p;
        p.id = e["id"].asString();
        if (p.id.empty() || p.id.size() > 64 ||
            std::any_of(out.begin(), out.end(), [&](const PanelPlacement& q) { return q.id == p.id; }))
            continue;
        p.w = std::clamp(int(e["w"].asNumber(p.w)), 160, 16384);
        p.h = std::clamp(int(e["h"].asNumber(p.h)), 160, 16384);
        if (e["x"].isNumber() && e["y"].isNumber()) {
            p.x = std::clamp(int(e["x"].asNumber()), -100000, 100000);
            p.y = std::clamp(int(e["y"].asNumber()), -100000, 100000);
            p.placed = true;
        }
        out.push_back(std::move(p));
    }
    return out;
}

bool panelOnDisplays(const PanelPlacement& p, const std::vector<DisplayRect>& displays) {
    if (!p.placed) return false;
    return std::any_of(displays.begin(), displays.end(), [&](const DisplayRect& r) {
        return p.x + 100 > r.x && p.x < r.x + r.w - 100 && p.y >= r.y - 10 && p.y < r.y + r.h - 50;
    });
}

} // namespace sf
