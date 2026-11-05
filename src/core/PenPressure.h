// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pen pressure per brush (#122): each brush's Size and Strength follow the global switches (Preferences > Tablet,
// the shelf's Pressure > Size / Strength), or are always on or always off for that brush.
#pragma once

#include <string>

namespace sf {

enum class PenPressure { Global, On, Off };

inline bool penPressureOn(PenPressure p, bool global) { return p == PenPressure::Global ? global : p == PenPressure::On; }
inline const char* penPressureId(PenPressure p) { return p == PenPressure::On ? "on" : p == PenPressure::Off ? "off" : "global"; }
// "on", "off", "global" (anything else is `fallback`).
inline PenPressure penPressureFromId(const std::string& s, PenPressure fallback = PenPressure::Global) {
    return s == "on" ? PenPressure::On : s == "off" ? PenPressure::Off : s == "global" ? PenPressure::Global : fallback;
}

} // namespace sf
