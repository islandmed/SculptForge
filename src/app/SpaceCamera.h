// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Hold Space and drag to move the camera (#43; Preferences > Input, off by default since ZBrush's Space opens
// the QuickMenu): what a press does while Space is held.
#pragma once

namespace sf {

enum class SpaceCameraMove { None, Orbit, Pan, Zoom };

// `button`: 1 left, 2 middle, 3 right (SDL's numbering). Space+left orbits, Space+right or middle pans, and with
// Ctrl held as well any of them zooms.
inline SpaceCameraMove spaceCameraMove(int button, bool ctrl) {
    if (button < 1 || button > 3) return SpaceCameraMove::None;
    if (ctrl) return SpaceCameraMove::Zoom;
    return button == 1 ? SpaceCameraMove::Orbit : SpaceCameraMove::Pan;
}

// Whether a Space press starts holding: never while a text field has the keyboard (typing a space), and only with
// the preference on.
inline bool spaceCameraKeyDown(bool enabled, bool typing) { return enabled && !typing; }

} // namespace sf
