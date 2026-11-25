// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A number typed into a picker pop-up (U / I Intensity, Size, ...): the decimal separator is '.' or ',' (0,5 as Spanish and many
// European users type it), whatever the system locale.
#pragma once

#include <cstdlib>
#include <optional>
#include <string>

namespace sf {

struct TypedNumber {
    float value = 0.0f;
    bool percent = false; // written with a trailing %
};

// "0,5" == "0.5", ",5" == 0.5, "1," == 1, "-2,5" == -2.5, "50%" is 50 with percent set. Nothing when there is no number
// ("", ".", "-", ",") or the text is ambiguous: more than one separator, or a '.' together with a ',' ("1,000.5", "1,000,000").
inline std::optional<TypedNumber> parseTypedNumber(std::string text) {
    TypedNumber out;
    if (!text.empty() && text.back() == '%') {
        out.percent = true;
        text.pop_back();
    }
    int separators = 0, digits = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c >= '0' && c <= '9') ++digits;
        else if (c == '.' || c == ',') { ++separators; text[i] = '.'; }
        else if (!(c == '-' && i == 0)) return std::nullopt;
    }
    if (digits == 0 || separators > 1) return std::nullopt;
    if (text[0] == '.' || (text[0] == '-' && text.size() > 1 && text[1] == '.')) text.insert(text[0] == '-' ? 1 : 0, "0");
    out.value = std::strtof(text.c_str(), nullptr); // (the text is only digits, one '.' and a leading '-': the app never sets a locale)
    return out;
}

// A typed Intensity (U / I pop-ups) as a 0-1 value: a percentage (1 -> 1%, 30 -> 30%), except a number below 1 written with a
// decimal point (0.5 or 0,5 -> 50%, 0.05 -> 5%), which is a fraction; a trailing % is always a percentage. Nothing for text that
// is not a number (the pop-up keeps waiting).
inline std::optional<float> typedIntensity(const std::string& typed) {
    const auto n = parseTypedNumber(typed);
    if (!n) return std::nullopt;
    const bool decimal = typed.find_first_of(".,") != std::string::npos;
    return n->value < 1.0f && decimal && !n->percent ? n->value : n->value / 100.0f;
}

} // namespace sf
