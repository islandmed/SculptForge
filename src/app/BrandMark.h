// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The app's mark (assets/branding, made by tools/brand/make_icons.py) drawn in the UI: the detailed tile from
// 48 px up, the simplified small-size mark below.
#pragma once

#include <imgui.h>

namespace sf::brand {

// The mark `size` px wide at `at` (screen pixels), `alpha` faded. Needs the GL context (draws from a texture
// uploaded on first use); without a texture it draws nothing.
void mark(ImDrawList* dl, ImVec2 at, float size, float alpha = 1.0f);

// The mark as an ImGui item `size` px wide at the cursor (title bars, dialogs).
void markItem(float size, float alpha = 1.0f);

} // namespace sf::brand
