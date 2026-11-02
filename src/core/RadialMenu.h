// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Radial (pie) menus (#45), independent of SDL and ImGui so they can be unit tested: the menus a user defines
// (up to kRadialMenus, each with up to kRadialSlots), which slot a flick points at, and their JSON in ui.json
// and in an exported menu file. A slot only names an action id from the action registry; nothing in a menu
// is ever run as code. Reading is strict: unknown actions and icons are dropped, counts and string lengths are
// capped, wrong types fall back to defaults. The app side (opening, drawing, Preferences > Input > Radial Menus)
// is src/app/RadialMenu.cpp.
#pragma once

#include "core/Json.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace sf {

constexpr int kRadialMenus = 8;          // menus a user can define
constexpr int kRadialSlots = 8;          // slots in one menu
constexpr size_t kRadialNameMax = 32;    // bytes in a menu's name
constexpr size_t kRadialLabelMax = 32;   // bytes in a slot's label
constexpr size_t kRadialIdMax = 96;      // bytes in an action or icon id (longer ones are never real ids)
constexpr size_t kRadialFileMax = 64 * 1024; // an imported menu file larger than this is refused unread

// What opens a menu besides its key: a mouse button or a pen side button, held like the key.
// MouseRight (#74): a plain right-drag opens the menu instead of turning the view (Maya's marking menus); the
// view then turns with Shift+right-drag or the style's other ways. Mouse buttons open a menu only with no modifier
// held (Alt+middle still pans, Alt+right zooms in the Maya style). Stored by id, so the order may change.
enum class RadialButton { None, MouseMiddle, MouseBack, MouseForward, PenLower, PenUpper, PenThird, MouseRight, Count };
const char* radialButtonId(RadialButton b);    // "mouse_middle" ... (stored in ui.json)
const char* radialButtonLabel(RadialButton b); // "Middle mouse button" ... (Preferences > Input > Radial Menus)
RadialButton radialButtonFromId(std::string_view id); // unknown: None

struct RadialSlot {
    std::string action; // ActionRegistry id: "brush.clay", "mask.invert"
    std::string label;  // "" = the action's own label
    std::string icon;   // a name from the app's icon list; "" = the action's own icon
};

struct RadialMenu {
    std::string name;
    std::vector<RadialSlot> slots; // at most kRadialSlots; slot 0 points up, the rest go clockwise
    RadialButton button = RadialButton::None;
};

// Which slot the pointer at (dx, dy) from the menu's centre points at (screen coordinates, y down), for a menu of
// COUNT slots: slot 0 is centred straight up and the others follow clockwise, each owning an equal wedge. -1 when
// the pointer is within DEADZONE pixels of the centre (or there are no slots).
int radialSlotAt(float dx, float dy, int count, float deadZone);
// The unit direction (screen coordinates, y down) of slot SLOT's centre in a menu of COUNT slots.
void radialSlotDirection(int slot, int count, float& x, float& y);

// What a reader may accept (the app checks against its action registry and icon list).
struct RadialVocabulary {
    std::function<bool(std::string_view)> action; // a known action that a slot may run
    std::function<bool(std::string_view)> icon;   // a known icon name
};

// A string from a file, made safe to show: control characters dropped, cut to MAXBYTES without splitting a UTF-8
// sequence (invalid UTF-8 bytes are dropped too).
std::string radialCleanText(std::string_view text, size_t maxBytes);

Json toJson(const RadialMenu& menu);
Json toJson(const std::vector<RadialMenu>& menus);
// One menu. False when J isn't a menu object at all; otherwise MENU holds what was valid and DROPPED counts the
// slots (and the icons) that were not.
bool radialMenuFromJson(const Json& j, const RadialVocabulary& vocab, RadialMenu& menu, int* dropped = nullptr);
// The menus array of ui.json: at most kRadialMenus menus; anything else in J is ignored.
std::vector<RadialMenu> radialMenusFromJson(const Json& j, const RadialVocabulary& vocab, int* dropped = nullptr);

// An exported menu file: {"format": "sculptforge-radial-menu", "version": 1, "menu": {...}}.
std::string radialMenuFileText(const RadialMenu& menu);
// Reads TEXT (the whole file) as an exported menu; false with ERROR when it isn't one.
bool parseRadialMenuFile(const std::string& text, const RadialVocabulary& vocab, RadialMenu& menu, std::string& error,
                         int* dropped = nullptr);

} // namespace sf
