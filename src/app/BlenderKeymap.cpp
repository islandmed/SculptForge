// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/BlenderKeymap.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace sf {

namespace {

constexpr size_t kMaxName = 128;     // keymap and operator names
constexpr size_t kMaxDict = 512;     // an event dict: {"type": 'A', "value": 'PRESS', "shift": True}
constexpr size_t kMaxProps = 8192;   // an item's {"properties": [...]} block

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\\'; } // '\' continues a line

size_t skipSpace(std::string_view t, size_t i) {
    while (i < t.size() && isSpace(t[i])) ++i;
    return i;
}

// "name" at i (no escapes, no line breaks); npos if there isn't one.
size_t readQuoted(std::string_view t, size_t i, std::string& out) {
    if (i >= t.size() || t[i] != '"') return std::string_view::npos;
    const size_t end = t.find('"', i + 1);
    if (end == std::string_view::npos || end - i - 1 > kMaxName) return std::string_view::npos;
    out.assign(t.substr(i + 1, end - i - 1));
    if (out.find('\n') != std::string::npos || out.find('\\') != std::string::npos) return std::string_view::npos;
    return end + 1;
}

// A {...} block at i: braces may nest (sets in property values), quoted text is skipped over. Returns the index
// after the closing brace, or npos when it is unbalanced or longer than `limit`.
size_t readBraces(std::string_view t, size_t i, size_t limit, std::string_view& out) {
    if (i >= t.size() || t[i] != '{') return std::string_view::npos;
    int depth = 0;
    for (size_t j = i; j < t.size() && j - i <= limit; ++j) {
        const char c = t[j];
        if (c == '\'' || c == '"') {
            const size_t end = t.find(c, j + 1);
            if (end == std::string_view::npos) return std::string_view::npos;
            j = end;
        } else if (c == '{') {
            ++depth;
        } else if (c == '}' && --depth == 0) {
            out = t.substr(i, j + 1 - i);
            return j + 1;
        }
    }
    return std::string_view::npos;
}

// The value after `"key":` in a dict ('PRESS' -> PRESS, True, -1, 0.9), or "" when the key isn't there.
std::string dictValue(std::string_view dict, std::string_view key) {
    const std::string needle = "\"" + std::string(key) + "\":";
    const size_t at = dict.find(needle);
    if (at == std::string_view::npos) return {};
    size_t i = skipSpace(dict, at + needle.size());
    if (i < dict.size() && dict[i] == '\'') {
        const size_t end = dict.find('\'', i + 1);
        return end == std::string_view::npos ? std::string() : std::string(dict.substr(i + 1, end - i - 1));
    }
    size_t end = i;
    while (end < dict.size() && dict[end] != ',' && dict[end] != '}' && !isSpace(dict[end])) ++end;
    return std::string(dict.substr(i, end - i));
}

// An operator property, ("name", value), from the item's properties block: 'FRONT' -> FRONT, True, -1, 0.9.
std::string propValue(std::string_view props, std::string_view name) {
    const std::string needle = "(\"" + std::string(name) + "\",";
    const size_t at = props.find(needle);
    if (at == std::string_view::npos) return {};
    size_t i = skipSpace(props, at + needle.size());
    if (i < props.size() && (props[i] == '\'' || props[i] == '"')) {
        const size_t end = props.find(props[i], i + 1);
        return end == std::string_view::npos ? std::string() : std::string(props.substr(i + 1, end - i - 1));
    }
    size_t end = i;
    while (end < props.size() && props[end] != ')' && props[end] != ',' && !isSpace(props[end])) ++end;
    return std::string(props.substr(i, end - i));
}

double number(const std::string& s, double fallback) {
    if (s.empty()) return fallback;
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    return end && *end == '\0' ? v : fallback;
}

// Blender's event type -> SDL key; SDLK_UNKNOWN for mouse buttons, the wheel, the trackpad and NDOF.
SDL_Keycode keyFromType(const std::string& type) {
    if (type.size() == 1 && type[0] >= 'A' && type[0] <= 'Z') return SDL_Keycode(type[0] - 'A' + 'a');
    static const char* kDigits[] = {"ZERO", "ONE", "TWO", "THREE", "FOUR", "FIVE", "SIX", "SEVEN", "EIGHT", "NINE"};
    for (int d = 0; d < 10; ++d)
        if (type == kDigits[d]) return SDL_Keycode('0' + d);
    if (type.rfind("NUMPAD_", 0) == 0) {
        const std::string k = type.substr(7);
        if (k.size() == 1 && k[0] >= '1' && k[0] <= '9') return SDL_Keycode(SDLK_KP_1 + (k[0] - '1'));
        if (k == "0") return SDLK_KP_0;
        if (k == "PERIOD") return SDLK_KP_PERIOD;
        if (k == "PLUS") return SDLK_KP_PLUS;
        if (k == "MINUS") return SDLK_KP_MINUS;
        if (k == "SLASH") return SDLK_KP_DIVIDE;
        if (k == "ASTERIX") return SDLK_KP_MULTIPLY;
        if (k == "ENTER") return SDLK_KP_ENTER;
        return SDLK_UNKNOWN;
    }
    if (type.size() >= 2 && type.size() <= 3 && type[0] == 'F' && std::isdigit(static_cast<unsigned char>(type[1]))) {
        const int n = std::atoi(type.c_str() + 1);
        if (n >= 1 && n <= 12) return SDL_Keycode(SDLK_F1 + (n - 1));
        return SDLK_UNKNOWN;
    }
    static const std::pair<const char*, SDL_Keycode> kKeys[] = {
        {"HOME", SDLK_HOME},         {"END", SDLK_END},           {"PAGE_UP", SDLK_PAGEUP},
        {"PAGE_DOWN", SDLK_PAGEDOWN}, {"INSERT", SDLK_INSERT},     {"DEL", SDLK_DELETE},
        {"BACK_SPACE", SDLK_BACKSPACE}, {"RET", SDLK_RETURN},      {"TAB", SDLK_TAB},
        {"SPACE", SDLK_SPACE},       {"LEFT_BRACKET", SDLK_LEFTBRACKET}, {"RIGHT_BRACKET", SDLK_RIGHTBRACKET},
        {"COMMA", SDLK_COMMA},       {"PERIOD", SDLK_PERIOD},     {"MINUS", SDLK_MINUS},
        {"EQUAL", SDLK_EQUALS},      {"SEMI_COLON", SDLK_SEMICOLON}, {"QUOTE", SDLK_APOSTROPHE},
        {"SLASH", SDLK_SLASH},       {"BACK_SLASH", SDLK_BACKSLASH}, {"ACCENT_GRAVE", SDLK_GRAVE},
        {"LEFT_ARROW", SDLK_LEFT},   {"RIGHT_ARROW", SDLK_RIGHT}, {"UP_ARROW", SDLK_UP},
        {"DOWN_ARROW", SDLK_DOWN},
    };
    for (const auto& [name, key] : kKeys)
        if (type == name) return key;
    return SDLK_UNKNOWN;
}

// Blender's sculpt tools (paint.brush_select, up to 4.2) and essentials brush assets (brush.asset_activate,
// 4.3 on) -> SculptForge's nearest brush. Scrape / Fill flatten, as SculptForge has no separate scrape.
std::string brushAction(std::string tool) {
    for (auto& c : tool) c = char(std::toupper(static_cast<unsigned char>(c)));
    for (auto& c : tool)
        if (c == ' ') c = '_';
    static const std::pair<const char*, const char*> kBrushes[] = {
        {"DRAW", "brush.standard"},       {"DRAW_SHARP", "brush.damstandard"}, {"CLAY", "brush.clay"},
        {"CLAY_STRIPS", "brush.claybuildup"}, {"LAYER", "brush.layer"},      {"INFLATE", "brush.inflate"},
        {"INFLATE/DEFLATE", "brush.inflate"}, {"CREASE", "brush.crease"},    {"CREASE_POLISH", "brush.crease"},
        {"CREASE_SHARP", "brush.crease"}, {"SMOOTH", "brush.smooth"},        {"FLATTEN", "brush.flatten"},
        {"FLATTEN/CONTRAST", "brush.flatten"}, {"SCRAPE", "brush.flatten"},  {"SCRAPE/FILL", "brush.flatten"},
        {"PINCH", "brush.pinch"},         {"PINCH/MAGNIFY", "brush.pinch"},  {"GRAB", "brush.move"},
        {"SNAKE_HOOK", "brush.snakehook"}, {"NUDGE", "brush.nudge"},         {"MASK", "mask.brush"},
    };
    for (const auto& [name, id] : kBrushes)
        if (tool == name) return id;
    return {};
}

// The SculptForge action for one keymap item, or "".
std::string actionFor(const std::string& op, std::string_view props) {
    auto prop = [&](const char* name) { return propValue(props, name); };
    if (op == "view3d.view_axis") {
        if (prop("align_active") == "True" || prop("relative") == "True") return {};
        const std::string t = prop("type");
        if (t == "FRONT") return "view.front";
        if (t == "BACK") return "view.back";
        if (t == "LEFT") return "view.left";
        if (t == "RIGHT") return "view.right";
        if (t == "TOP") return "view.top";
        if (t == "BOTTOM") return "view.bottom";
        return {};
    }
    if (op == "view3d.view_persportho") return "view.ortho";
    if (op == "view3d.view_selected") return prop("use_all_regions") == "True" ? "" : "view.frame_selected";
    if (op == "view3d.view_all") return prop("use_all_regions") == "True" ? "" : "view.frame";
    if (op == "view3d.view_orbit") {
        const std::string t = prop("type");
        if (std::abs(number(prop("angle"), 0.0) - 3.14159265) < 0.01) return "view.opposite";
        if (t == "ORBITLEFT") return "view.orbit_left";
        if (t == "ORBITRIGHT") return "view.orbit_right";
        if (t == "ORBITUP") return "view.orbit_up";
        if (t == "ORBITDOWN") return "view.orbit_down";
        return {};
    }
    if (op == "view3d.zoom") {
        const double d = number(prop("delta"), 0.0);
        return d > 0 ? "view.zoom_in" : d < 0 ? "view.zoom_out" : "";
    }
    if (op == "view3d.toggle_shading") return prop("type") == "WIREFRAME" ? "view.wireframe" : "";
    if (op == "paint.brush_select") return brushAction(prop("sculpt_tool"));
    if (op == "brush.asset_activate") {
        const std::string id = prop("relative_asset_identifier");
        const size_t at = id.find("/Brush/");
        return at == std::string::npos ? std::string() : brushAction(id.substr(at + 7));
    }
    if (op == "wm.radial_control") {
        const std::string path = prop("data_path_primary");
        if (path.find("sculpt") == std::string::npos) return {};
        if (path.size() >= 5 && path.compare(path.size() - 5, 5, ".size") == 0) return "ui.size_popup";
        if (path.size() >= 9 && path.compare(path.size() - 9, 9, ".strength") == 0) return "ui.strength_popup";
        return {};
    }
    if (op == "paint.mask_flood_fill") {
        const std::string mode = prop("mode");
        if (mode == "INVERT") return "mask.invert";
        if (mode == "VALUE") return number(prop("value"), 0.0) > 0.5 ? "mask.all" : "mask.clear";
        return {};
    }
    if (op == "paint.hide_show_all") return prop("action") == "SHOW" ? "visibility.show_all" : "";
    if (op == "sculpt.face_set_change_visibility" || op == "sculpt.face_sets_change_visibility") {
        const std::string mode = prop("mode");
        return mode == "HIDE_ACTIVE" ? "visibility.hide_group" : mode == "TOGGLE" ? "visibility.isolate_group" : "";
    }
    if (op == "object.subdivision_set") {
        if (prop("relative") != "True") return {};
        const double level = number(prop("level"), 0.0);
        return level < 0 ? "geometry.lower_res" : level > 0 ? "geometry.higher_res" : "";
    }
    if (op == "brush.scale_size") {
        const double s = number(prop("scalar"), 1.0);
        return s < 1.0 ? "brush.size_down" : s > 1.0 ? "brush.size_up" : "";
    }
    if (op == "paint.brush_colors_flip") return "color.switch";
    if (op == "sculpt.sample_color") return "color.pick";
    if (op == "wm.context_toggle") {
        const std::string path = prop("data_path");
        const std::string_view tail = "use_smooth_stroke";
        return path.size() >= tail.size() && path.compare(path.size() - tail.size(), tail.size(), tail) == 0 ? "stroke.lazy"
                                                                                                             : "";
    }
    if (op == "ed.undo") return "edit.undo";
    if (op == "ed.redo") return "edit.redo";
    return {};
}

int keymapPriority(const std::string& keymap) {
    if (keymap == "Sculpt") return 2;
    if (keymap == "3D View") return 1;
    if (keymap == "Screen") return 0;
    return -1; // not read
}

} // namespace

bool parseBlenderKeymap(std::string_view t, BlenderKeymap& out, std::string& error) {
    out = BlenderKeymap{};
    if (t.find("keyconfig_data") == std::string_view::npos) {
        error = "Not a Blender keymap: export one in Blender with Preferences > Keymap > Export";
        return false;
    }
    struct Found {
        BlenderKeyBinding b;
        size_t order;
    };
    std::vector<Found> found;
    std::string keymap;
    bool modal = false;
    size_t i = 0;
    while ((i = t.find("(\"", i)) != std::string_view::npos) {
        std::string name;
        size_t j = readQuoted(t, i + 1, name);
        ++i;
        if (j == std::string_view::npos) continue;
        j = skipSpace(t, j);
        if (j >= t.size() || t[j] != ',') continue;
        j = skipSpace(t, j + 1);
        std::string_view dict;
        const size_t afterDict = readBraces(t, j, kMaxDict, dict);
        if (afterDict == std::string_view::npos) continue;
        const std::string_view inner = dict.substr(1);
        const size_t firstKey = skipSpace(inner, 0);
        if (inner.compare(firstKey, 12, "\"space_type\"") == 0) { // a keymap: ("Sculpt", {"space_type": ...
            keymap = name;
            modal = dictValue(dict, "modal") == "True";
            i = afterDict;
            continue;
        }
        if (inner.compare(firstKey, 6, "\"type\"") != 0) continue;
        // An item: ("operator.name", {event}, None or {"properties": [...]})
        size_t k = skipSpace(t, afterDict);
        if (k >= t.size() || t[k] != ',') continue;
        k = skipSpace(t, k + 1);
        std::string_view props;
        if (t.compare(k, 4, "None") == 0) {
            k += 4;
        } else if (readBraces(t, k, kMaxProps, props) == std::string_view::npos) {
            continue;
        } else {
            k += props.size();
        }
        i = k;
        if (modal || keymapPriority(keymap) < 0) continue;
        const std::string value = dictValue(dict, "value");
        if (value != "PRESS" && value != "CLICK") continue;
        Chord chord;
        chord.key = keyFromType(dictValue(dict, "type"));
        if (chord.key == SDLK_UNKNOWN) continue; // mouse, wheel, trackpad, NDOF: not keys
        if (!dictValue(dict, "key_modifier").empty()) continue; // two-key chords: SculptForge has none
        if (props.find("\"active\":False") != std::string_view::npos ||
            props.find("\"active\": False") != std::string_view::npos)
            continue;
        if (dictValue(dict, "any") != "True") {
            chord.shift = dictValue(dict, "shift") == "True";
            chord.ctrl = dictValue(dict, "ctrl") == "True" || dictValue(dict, "oskey") == "True"; // Cmd on a Mac
            chord.alt = dictValue(dict, "alt") == "True";
        }
        ++out.items;
        const std::string action = actionFor(name, props);
        if (action.empty()) {
            ++out.skipped;
            continue;
        }
        found.push_back({{action, chord, keymapPriority(keymap)}, found.size()});
    }
    if (out.items == 0) {
        error = "The keymap has no keys for the 3D View or Sculpt mode (export it with \"All Keymaps\" on, or edit a "
                "sculpt key first)";
        return false;
    }
    // Blender looks in Sculpt mode's keymap before the 3D View's: one action per key, the higher keymap's.
    std::stable_sort(found.begin(), found.end(), [](const Found& a, const Found& b) {
        return a.b.priority != b.b.priority ? a.b.priority > b.b.priority : a.order < b.order;
    });
    for (const Found& f : found) {
        const bool taken = std::any_of(out.bindings.begin(), out.bindings.end(),
                                       [&](const BlenderKeyBinding& b) { return b.chord == f.b.chord; });
        if (taken) {
            if (std::none_of(out.bindings.begin(), out.bindings.end(),
                             [&](const BlenderKeyBinding& b) { return b.chord == f.b.chord && b.action == f.b.action; }))
                ++out.skipped; // shadowed by Sculpt mode's own use of the key
            continue;
        }
        out.bindings.push_back(f.b);
    }
    return true;
}

} // namespace sf
