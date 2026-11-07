// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Names for copies (Oslen on Discord): Head -> Head.01, Head.02, ... and a copy of Head.01 is Head.02 (the number is
// replaced, never stacked as "Head copy copy"). Used by Duplicate and Mirror copy for Parts, paint layers and texture
// layers.
#pragma once

#include <cctype>
#include <cstdio>
#include <string>

namespace sf {

// `name` without a trailing ".NN" (a dot and two or more digits): Head.01 -> Head.
inline std::string copyNameBase(const std::string& name) {
    const size_t dot = name.rfind('.');
    if (dot == std::string::npos || dot == 0 || name.size() - dot < 3) return name;
    for (size_t i = dot + 1; i < name.size(); ++i)
        if (!std::isdigit(static_cast<unsigned char>(name[i]))) return name;
    return name.substr(0, dot);
}

// The first of base.01, base.02, ... that `taken` says is free.
template <class Taken>
std::string copyName(const std::string& name, Taken&& taken) {
    const std::string base = copyNameBase(name);
    for (int n = 1;; ++n) {
        char suffix[16];
        std::snprintf(suffix, sizeof suffix, ".%02d", n);
        std::string candidate = base + suffix;
        if (!taken(candidate)) return candidate;
    }
}

} // namespace sf
