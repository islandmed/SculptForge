// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Preferences > Input > Start strokes off the model (#62): which left presses on the background wait to become a
// stroke (App::airStrokeMouseDown) instead of moving the view.
#pragma once

namespace sf {

// enabled: the preference. keyboardAlt: Alt on the keyboard itself (Alt+drag on the background still pans, and
// with Blender or Maya keys it orbits). ctrlShift: Ctrl+Shift, the hide / show selection shapes, left alone.
// busy: a LOD preview, or Append > Insert waiting for its click.
inline bool airStrokeStarts(bool enabled, bool keyboardAlt, bool ctrlShift, bool busy) {
    return enabled && !keyboardAlt && !ctrlShift && !busy;
}

} // namespace sf
