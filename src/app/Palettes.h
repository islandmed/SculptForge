// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Small helpers the palette bodies and the right-tray panels share, so every
// section is laid out the same way (docs/ui-design.md §10, the tray rules).
#pragma once

#include <imgui.h>

namespace sf {
class App;
}

namespace sf::tray {

// The content of an open sub-palette, indented to line up with its header title. Scoped: construct it right after
// subpalette() returns true; it unindents and leaves a little room before the next header when it goes.
struct Section {
    Section();
    ~Section();
    Section(const Section&) = delete;
    Section& operator=(const Section&) = delete;
};

// The palette bodies' quiet controls: check marks in the text colour, not the accent (the accent marks the
// current tool or mode). Scoped like Section.
struct Style {
    Style();
    ~Style();
    Style(const Style&) = delete;
    Style& operator=(const Style&) = delete;
};

// A group label inside a section ("Project", "QRemesh", "Tablet"): small dim text flush with the content, then a
// hairline to the end of the line. Replaces ImGui::SeparatorText, whose leading dash made a third header style.
void GroupLabel(const App& app, const char* text);

// ui::SliderInside for an int ("passes 3"): the label inside the frame and a thin accent bar, no grab over the text.
// `format` uses %d.
bool SliderInt(const char* id, int* v, int lo, int hi, const char* format, ImGuiSliderFlags flags = 0);

} // namespace sf::tray
