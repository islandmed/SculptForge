// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The brand in the UI (assets/branding, made by tools/brand/make_icons.py; docs/brand.md): the cube is the app icon
// (the title bars, like the window icon); the bust is the illustration (About, What's New, the support card).
#pragma once

#include <imgui.h>

namespace sf::brand {

// The cube app icon `size` px wide at `at` (screen pixels); its small cut up to 32 px. Needs the GL context (draws
// from a texture uploaded on first use); without a texture it draws nothing.
void appMark(ImDrawList* dl, ImVec2 at, float size, float alpha = 1.0f);
void appMarkItem(float size, float alpha = 1.0f); // as an ImGui item at the cursor

// The bust illustration, `size` px wide at `at`.
void bust(ImDrawList* dl, ImVec2 at, float size, float alpha = 1.0f);
void bustItem(float size, float alpha = 1.0f); // as an ImGui item at the cursor

// Community icon `index` (0..kSocialCount-1, the order of kSocialLinks in Support.h), `size` px wide at `at`, in `color`
// (the strip is white; it is tinted). Draws nothing without a texture.
void socialIcon(ImDrawList* dl, int index, ImVec2 at, float size, ImU32 color);

} // namespace sf::brand
