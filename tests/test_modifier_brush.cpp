// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Shift and Ctrl brushes (core/ModifierBrush.h): rebinding a slot, going back, which slot the keys pick, the
// preferences round trip, and the classic alternate smooth (#40) that stays Smooth's.
#include "TestHarness.h"

#include "core/ModifierBrush.h"

#include <cmath>

using namespace sf;

TEST(modifier_brush_defaults) {
    ModifierBrushes m;
    CHECK(!m.bound(ModifierSlot::Shift));
    CHECK(!m.bound(ModifierSlot::Ctrl));
    CHECK(modifierBrushLabel(m, ModifierSlot::Shift) == "Smooth");
    CHECK(modifierBrushLabel(m, ModifierSlot::Ctrl) == "Mask");
    // Unbound, Shift and Ctrl keep their usual meanings: no slot is picked.
    CHECK(heldModifierSlot(m, true, false, false, false) == ModifierSlot::None);
    CHECK(heldModifierSlot(m, false, true, false, false) == ModifierSlot::None);
}

TEST(modifier_brush_slot_swap) {
    ModifierBrushes m;
    CHECK(setModifierBrush(m, ModifierSlot::Shift, int(BrushType::Trim)) == "Shift brush: Trim");
    CHECK(m.shift == int(BrushType::Trim));
    CHECK(m.ctrl == -1); // the other slot stays
    CHECK(setModifierBrush(m, ModifierSlot::Ctrl, int(BrushType::Flatten)) == "Ctrl brush: Flatten");
    CHECK(m.shift == int(BrushType::Trim));
    CHECK(heldModifierSlot(m, true, false, false, false) == ModifierSlot::Shift);
    CHECK(heldModifierSlot(m, true, false, true, false) == ModifierSlot::Shift); // Shift+Alt: the Shift brush inverted
    CHECK(heldModifierSlot(m, false, true, false, false) == ModifierSlot::Ctrl);
    CHECK(heldModifierSlot(m, false, true, true, false) == ModifierSlot::None);  // Ctrl+Alt still unmasks
    CHECK(heldModifierSlot(m, true, true, false, false) == ModifierSlot::None);  // Ctrl+Shift: the selection keys
    CHECK(heldModifierSlot(m, false, false, false, false) == ModifierSlot::None);
    CHECK(heldModifierSlot(m, true, false, false, true) == ModifierSlot::None);  // Paint: Shift smooths, Ctrl masks
    CHECK(heldModifierSlot(m, false, true, false, true) == ModifierSlot::None);
    // Rebinding a bound slot replaces it.
    setModifierBrush(m, ModifierSlot::Shift, int(BrushType::Clay));
    CHECK(m.shift == int(BrushType::Clay));
}

TEST(modifier_brush_reset) {
    ModifierBrushes m;
    setModifierBrush(m, ModifierSlot::Shift, int(BrushType::Trim));
    // Shift+click Smooth goes back to the default (not a "Smooth" binding: #40 stays).
    CHECK(setModifierBrush(m, ModifierSlot::Shift, int(BrushType::Smooth)) == "Shift brush: Smooth (default)");
    CHECK(!m.bound(ModifierSlot::Shift));
    setModifierBrush(m, ModifierSlot::Ctrl, int(BrushType::Trim));
    CHECK(setModifierBrush(m, ModifierSlot::Ctrl, -1) == "Ctrl brush: Mask (default)");
    CHECK(!m.bound(ModifierSlot::Ctrl));
    // Smooth on Ctrl is a real binding (Ctrl smooths, Shift stays as it is).
    setModifierBrush(m, ModifierSlot::Ctrl, int(BrushType::Smooth));
    CHECK(m.ctrl == int(BrushType::Smooth));
    // Out of range: the default.
    setModifierBrush(m, ModifierSlot::Ctrl, int(BrushType::Count) + 3);
    CHECK(!m.bound(ModifierSlot::Ctrl));
    CHECK(setModifierBrush(m, ModifierSlot::None, int(BrushType::Trim)).empty());
}

TEST(modifier_brush_script_names) {
    int t = 99;
    CHECK(modifierBrushFromName("trim", ModifierSlot::Shift, t) && t == int(BrushType::Trim));
    CHECK(modifierBrushFromName("Clay Block", ModifierSlot::Ctrl, t) && t == int(BrushType::ClayBlock));
    CHECK(modifierBrushFromName("smooth", ModifierSlot::Shift, t) && t == -1);
    CHECK(modifierBrushFromName("smooth", ModifierSlot::Ctrl, t) && t == int(BrushType::Smooth));
    CHECK(modifierBrushFromName("mask", ModifierSlot::Ctrl, t) && t == -1);
    CHECK(modifierBrushFromName("DEFAULT", ModifierSlot::Shift, t) && t == -1);
    CHECK(modifierBrushFromName("reset", ModifierSlot::Ctrl, t) && t == -1);
    CHECK(!modifierBrushFromName("mask", ModifierSlot::Shift, t));
    CHECK(!modifierBrushFromName("no such brush", ModifierSlot::Shift, t));
}

TEST(modifier_brush_persistence) {
    ModifierBrushes m;
    setModifierBrush(m, ModifierSlot::Shift, int(BrushType::Trim));
    setModifierBrush(m, ModifierSlot::Ctrl, int(BrushType::Flatten));
    m.shiftIntensity = 0.25f;
    m.ctrlIntensity = 0.8f;
    const Json j = toJson(m);
    CHECK(j["shift"].asString() == "Trim"); // by name, not by number
    Json parsed;
    std::string err;
    CHECK(Json::parse(j.dump(), parsed, err));
    ModifierBrushes back;
    fromJson(parsed, back);
    CHECK(back.shift == int(BrushType::Trim));
    CHECK(back.ctrl == int(BrushType::Flatten));
    CHECK(std::abs(back.shiftIntensity - 0.25f) < 1e-5f);
    CHECK(std::abs(back.ctrlIntensity - 0.8f) < 1e-5f);
    // Defaults round-trip as defaults; nonsense and missing keys leave the defaults.
    ModifierBrushes d, d2;
    fromJson(toJson(d), d2);
    CHECK(!d2.bound(ModifierSlot::Shift) && !d2.bound(ModifierSlot::Ctrl));
    Json bad = Json::object();
    bad.set("shift", std::string("not a brush"));
    bad.set("ctrlIntensity", 7.0);
    ModifierBrushes d3;
    fromJson(bad, d3);
    CHECK(!d3.bound(ModifierSlot::Shift));
    CHECK(d3.ctrlIntensity == 1.0f);
    fromJson(Json(), d3); // not an object: nothing changes
    CHECK(!d3.bound(ModifierSlot::Shift));
}

TEST(modifier_brush_alternate_smooth_unchanged) {
    // #40: a Smooth stroke begun with Shift that lets go of Shift switches once; nothing else does.
    CHECK(alternateSmoothStarts(BrushType::Smooth, true, false, false, false));
    CHECK(!alternateSmoothStarts(BrushType::Smooth, true, false, false, true));  // Shift still held
    CHECK(!alternateSmoothStarts(BrushType::Smooth, true, true, false, false));  // already switched
    CHECK(!alternateSmoothStarts(BrushType::Smooth, false, false, false, false)); // Smooth picked, no Shift
    CHECK(!alternateSmoothStarts(BrushType::Smooth, true, false, true, false));  // a mask stroke
    // A rebound Shift brush's stroke is not Smooth: letting go of Shift changes nothing.
    CHECK(!alternateSmoothStarts(BrushType::Trim, true, false, false, false));
}
