// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Radial menus (#45): the slot a flick points at (wedges, the dead zone) and the strict JSON reading of ui.json
// and exported menu files, including hostile input (huge arrays, unknown ids, long strings, wrong types).
#include "TestHarness.h"

#include "core/RadialMenu.h"

#include <cmath>
#include <string>

using namespace sf;

namespace {

RadialVocabulary vocab() {
    RadialVocabulary v;
    v.action = [](std::string_view id) { return id == "brush.clay" || id == "brush.move" || id == "mask.invert"; };
    v.icon = [](std::string_view id) { return id == "brush" || id == "eraser"; };
    return v;
}

bool validUtf8(const std::string& s) { return radialCleanText(s, s.size() + 1) == s; }

} // namespace

TEST(radial_slot_dead_zone) {
    CHECK(radialSlotAt(0.0f, 0.0f, 8, 20.0f) == -1);
    CHECK(radialSlotAt(10.0f, -10.0f, 8, 20.0f) == -1); // 14 px out: still inside
    CHECK(radialSlotAt(0.0f, -21.0f, 8, 20.0f) == 0);
    CHECK(radialSlotAt(0.0f, -100.0f, 0, 20.0f) == -1); // no slots
    CHECK(radialSlotAt(NAN, 50.0f, 8, 20.0f) == -1);
    CHECK(radialSlotAt(INFINITY, 0.0f, 8, 20.0f) == -1);
}

TEST(radial_slot_from_angle) {
    // Eight slots: 0 up, then clockwise (screen y points down).
    CHECK(radialSlotAt(0.0f, -100.0f, 8, 10.0f) == 0);
    CHECK(radialSlotAt(70.0f, -70.0f, 8, 10.0f) == 1);
    CHECK(radialSlotAt(100.0f, 0.0f, 8, 10.0f) == 2);
    CHECK(radialSlotAt(70.0f, 70.0f, 8, 10.0f) == 3);
    CHECK(radialSlotAt(0.0f, 100.0f, 8, 10.0f) == 4);
    CHECK(radialSlotAt(-70.0f, 70.0f, 8, 10.0f) == 5);
    CHECK(radialSlotAt(-100.0f, 0.0f, 8, 10.0f) == 6);
    CHECK(radialSlotAt(-70.0f, -70.0f, 8, 10.0f) == 7);
    // Wedges are centred on their slot: just either side of straight up is still slot 0 (not 7 or 1).
    CHECK(radialSlotAt(-15.0f, -100.0f, 8, 10.0f) == 0);
    CHECK(radialSlotAt(15.0f, -100.0f, 8, 10.0f) == 0);
    // Fewer slots: wider wedges. Two slots split top and bottom; four make a cross.
    CHECK(radialSlotAt(90.0f, -10.0f, 2, 10.0f) == 0);
    CHECK(radialSlotAt(90.0f, 10.0f, 2, 10.0f) == 1);
    CHECK(radialSlotAt(100.0f, 20.0f, 4, 10.0f) == 1);
    CHECK(radialSlotAt(-20.0f, 100.0f, 4, 10.0f) == 2);
    CHECK(radialSlotAt(5.0f, 100.0f, 1, 10.0f) == 0); // one slot owns every direction
    // Every slot's own direction picks that slot, for every menu size.
    for (int count = 1; count <= kRadialSlots; ++count)
        for (int slot = 0; slot < count; ++slot) {
            float x = 0.0f, y = 0.0f;
            radialSlotDirection(slot, count, x, y);
            CHECK(std::abs(x * x + y * y - 1.0f) < 1e-4f);
            CHECK(radialSlotAt(x * 80.0f, y * 80.0f, count, 20.0f) == slot);
        }
}

TEST(radial_json_round_trip) {
    RadialMenu m;
    m.name = "Brushes";
    m.button = RadialButton::PenUpper;
    m.slots = {{"brush.clay", "Clay", "brush"}, {"mask.invert", "", ""}};
    RadialMenu back;
    int dropped = 0;
    CHECK(radialMenuFromJson(toJson(m), vocab(), back, &dropped));
    CHECK(dropped == 0);
    CHECK(back.name == "Brushes");
    CHECK(back.button == RadialButton::PenUpper);
    CHECK(back.slots.size() == 2);
    CHECK(back.slots[0].action == "brush.clay" && back.slots[0].label == "Clay" && back.slots[0].icon == "brush");
    CHECK(back.slots[1].action == "mask.invert" && back.slots[1].label.empty() && back.slots[1].icon.empty());

    // A whole file, and the menus array of ui.json.
    RadialMenu fromFile;
    std::string err;
    CHECK(parseRadialMenuFile(radialMenuFileText(m), vocab(), fromFile, err));
    CHECK(fromFile.slots.size() == 2 && fromFile.name == "Brushes");
    const std::vector<RadialMenu> menus = radialMenusFromJson(toJson(std::vector<RadialMenu>{m, m}), vocab());
    CHECK(menus.size() == 2);
    for (int b = 0; b < int(RadialButton::Count); ++b)
        CHECK(radialButtonFromId(radialButtonId(RadialButton(b))) == RadialButton(b));
}

TEST(radial_json_drops_unknown_and_wrong_types) {
    Json j;
    std::string err;
    CHECK(Json::parse(R"({"name": 42, "button": "launch_missiles", "slots": [
        {"action": "brush.clay"},
        {"action": "cmd.run_script", "label": "evil"},
        {"action": "brush.move", "icon": "../../etc/passwd"},
        {"action": ["brush.clay"]},
        "brush.clay",
        17, null, true,
        {"label": "no action"},
        {"action": "mask.invert", "label": {"x": 1}, "icon": 5}
    ]})", j, err));
    RadialMenu m;
    int dropped = 0;
    CHECK(radialMenuFromJson(j, vocab(), m, &dropped));
    CHECK(m.name.empty());                  // not a string: no name
    CHECK(m.button == RadialButton::None);  // unknown button
    CHECK(m.slots.size() == 3);
    CHECK(m.slots[0].action == "brush.clay");
    CHECK(m.slots[1].action == "brush.move" && m.slots[1].icon.empty()); // unknown icon dropped, slot kept
    CHECK(m.slots[2].action == "mask.invert" && m.slots[2].label.empty() && m.slots[2].icon.empty());
    CHECK(dropped == 8); // 7 slots and 1 icon

    // Not a menu at all.
    CHECK(!radialMenuFromJson(Json(3.0), vocab(), m));
    CHECK(!radialMenuFromJson(Json::array(), vocab(), m));
    CHECK(radialMenusFromJson(Json("menus"), vocab()).empty());
    Json slotsObject;
    CHECK(Json::parse(R"({"name": "x", "slots": {"action": "brush.clay"}})", slotsObject, err));
    dropped = 0;
    CHECK(radialMenuFromJson(slotsObject, vocab(), m, &dropped));
    CHECK(m.slots.empty() && dropped == 1);
}

TEST(radial_json_caps_counts) {
    // A huge array of slots and of menus: only the caps are kept, the rest counted as dropped.
    std::string text = R"({"name": "big", "slots": [)";
    for (int i = 0; i < 100000; ++i) text += std::string(i ? "," : "") + R"({"action": "brush.clay"})";
    text += "]}";
    Json j;
    std::string err;
    CHECK(Json::parse(text, j, err));
    RadialMenu m;
    int dropped = 0;
    CHECK(radialMenuFromJson(j, vocab(), m, &dropped));
    CHECK(m.slots.size() == size_t(kRadialSlots));
    CHECK(dropped == 100000 - kRadialSlots);

    Json menus = Json::array();
    for (int i = 0; i < 50; ++i) menus.push(j);
    dropped = 0;
    const std::vector<RadialMenu> all = radialMenusFromJson(menus, vocab(), &dropped);
    CHECK(all.size() == size_t(kRadialMenus));
    for (const RadialMenu& menu : all) CHECK(menu.slots.size() == size_t(kRadialSlots));
    CHECK(dropped > 0);

    // A file over the size cap is refused before parsing.
    RadialMenu fromFile;
    CHECK(!parseRadialMenuFile(std::string(kRadialFileMax + 1, ' '), vocab(), fromFile, err));
    CHECK(!err.empty());
}

TEST(radial_json_long_and_odd_strings) {
    RadialMenu m;
    m.name = std::string(10000, 'N');
    m.slots = {{"brush.clay", std::string(5000, 'L'), ""}};
    RadialMenu back;
    CHECK(radialMenuFromJson(toJson(m), vocab(), back));
    CHECK(back.name.size() == kRadialNameMax);
    CHECK(back.slots.size() == 1 && back.slots[0].label.size() == kRadialLabelMax);

    // A very long action id is never looked up (and isn't one).
    Json j = Json::object();
    Json slots = Json::array();
    Json slot = Json::object();
    slot.set("action", "brush.clay" + std::string(kRadialIdMax, 'x'));
    slots.push(slot);
    j.set("slots", slots);
    int dropped = 0;
    CHECK(radialMenuFromJson(j, vocab(), back, &dropped));
    CHECK(back.slots.empty() && dropped == 1);

    // Control characters go; UTF-8 is cut on a character boundary; broken UTF-8 is dropped.
    CHECK(radialCleanText("a\nb\tc\x1b[31md\x7f", 32) == "abc[31md");
    const std::string accents = "\xC3\xA9\xC3\xA9\xC3\xA9"; // three e-acute, 2 bytes each
    CHECK(radialCleanText(accents, 5) == "\xC3\xA9\xC3\xA9");
    CHECK(radialCleanText("\xF0\x9F\x8E\xA8 art", 3).empty()); // a 4-byte emoji doesn't fit in 3
    CHECK(radialCleanText("ok\xC3", 32) == "ok");           // cut-off sequence
    CHECK(radialCleanText("\x80\xBFok\xFF", 32) == "ok");    // stray continuation and invalid bytes
    CHECK(radialCleanText("\xED\xA0\x80x", 32) == "x");      // a UTF-16 surrogate
    CHECK(radialCleanText("\xC0\xAFx", 32) == "x");          // an overlong '/'
    std::string junk;
    for (int i = 0; i < 4096; ++i) junk += char((i * 37) & 0xFF);
    const std::string cleaned = radialCleanText(junk, kRadialLabelMax);
    CHECK(cleaned.size() <= kRadialLabelMax);
    CHECK(validUtf8(cleaned));
}

TEST(radial_menu_file_rejects_other_json) {
    RadialMenu m;
    std::string err;
    CHECK(!parseRadialMenuFile("not json", vocab(), m, err));
    CHECK(!parseRadialMenuFile("[1, 2, 3]", vocab(), m, err));
    CHECK(!parseRadialMenuFile(R"({"layout": {}, "prefs": {}})", vocab(), m, err)); // a UI layout, not a menu
    CHECK(!parseRadialMenuFile(R"({"format": "sculptforge-radial-menu", "menu": "x"})", vocab(), m, err));
    std::string deep;
    for (int i = 0; i < 100000; ++i) deep += '[';
    CHECK(!parseRadialMenuFile(deep, vocab(), m, err)); // over the size cap (and too deep anyway)
    std::string nested = R"({"format": "sculptforge-radial-menu", "menu": {"name": "n", "slots": )";
    for (int i = 0; i < 1000; ++i) nested += '[';
    CHECK(!parseRadialMenuFile(nested, vocab(), m, err)); // nesting refused by the parser
}
