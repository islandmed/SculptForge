// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Forge Night: the colour tokens the desktop and touch layouts share (one design system). Packed 0xAABBGGRR as
// IM_COL32 makes them, so they drop into ImDrawList calls. Change a value here, never a local copy.
#pragma once

#include <imgui.h>

namespace sf::tokens {

constexpr ImU32 kNight = IM_COL32(0x0f, 0x0f, 0x12, 0xff);   // window background
constexpr ImU32 kCanvas = IM_COL32(0x18, 0x18, 0x1d, 0xff);  // empty viewport
constexpr ImU32 kPanel = IM_COL32(0x14, 0x14, 0x18, 0xff);   // flat panels, the caption bar (desktop)
constexpr ImU32 kGlass = IM_COL32(0x1a, 0x1a, 0x20, 0xc7);   // floating chrome over the model (touch), 78%
constexpr ImU32 kSurface = IM_COL32(0x20, 0x20, 0x28, 0xff); // sheets, cards
constexpr ImU32 kRaised = IM_COL32(0x2a, 0x2a, 0x34, 0xff);  // selected segment, switch track
constexpr ImU32 kInk = IM_COL32(0xec, 0xec, 0xf1, 0xff);     // text
constexpr ImU32 kInkDim = IM_COL32(0x9a, 0x9a, 0xa6, 0xff);  // secondary text
constexpr ImU32 kLine = IM_COL32(0xff, 0xff, 0xff, 0x14);    // hairlines, 8%
constexpr ImU32 kLine2 = IM_COL32(0xff, 0xff, 0xff, 0x24);   // stronger borders, 14%
constexpr ImU32 kForge = IM_COL32(0xf2, 0x8c, 0x3c, 0xff);   // active state only (and the Sculpt room)
constexpr ImU32 kForgeSoft = IM_COL32(0xff, 0xb3, 0x7a, 0xff); // values, readouts
constexpr ImU32 kForgeBg = IM_COL32(0xf2, 0x8c, 0x3c, 0x24);  // active fill, 14%
constexpr ImU32 kOnForge = IM_COL32(0x1a, 0x0d, 0x03, 0xff); // text on a forge button
constexpr ImU32 kMask = IM_COL32(0x6f, 0x9b, 0xff, 0xff);
constexpr ImU32 kOk = IM_COL32(0x4c, 0xc3, 0x8a, 0xff);

// Room hues (icons only; orange stays the active colour everywhere).
constexpr ImU32 kRoomSculpt = IM_COL32(0xf2, 0x8c, 0x3c, 0xff);
constexpr ImU32 kRoomPaint = IM_COL32(0xec, 0x6f, 0x9a, 0xff);
constexpr ImU32 kRoomUv = IM_COL32(0x45, 0xc4, 0xb0, 0xff);
constexpr ImU32 kRoomModel = IM_COL32(0xa4, 0x8b, 0xff, 0xff);

// Touch layout sizes in dp (times the UI scale): hit target, hold key.
constexpr float kHitDp = 48.0f;
constexpr float kHoldDp = 56.0f;

} // namespace sf::tokens
