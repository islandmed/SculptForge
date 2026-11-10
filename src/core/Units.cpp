// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Units.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace sf {

namespace {

constexpr LengthUnit kUnits[] = {{"mm", 1000.0}, {"cm", 100.0}, {"m", 1.0}, {"in", 1.0 / 0.0254}};

// About four significant digits, no trailing zeros: 50, 12.5, 0.254, 1234.
std::string number(double v) {
    const double a = std::fabs(v);
    const int decimals = a >= 1000.0 ? 0 : a >= 100.0 ? 1 : a >= 10.0 ? 2 : a >= 1.0 ? 3 : 4;
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    std::string s = buf;
    if (s.find('.') != std::string::npos) {
        while (s.back() == '0') s.pop_back();
        if (s.back() == '.') s.pop_back();
    }
    return s == "-0" ? "0" : s;
}

// Significant digits in the shortest decimal form that reads back as `v` (9 always do for a float). Not
// std::to_chars: its float overloads need macOS 13.3, above our deployment target. Digits, not characters: %g
// writes 697130 at 5 digits as 6.9713e+05.
size_t shortestLength(float v) {
    char buf[32];
    for (int digits = 1; digits < 9; ++digits)
        if (std::snprintf(buf, sizeof buf, "%.*g", digits, double(v)) > 0 && std::strtof(buf, nullptr) == v)
            return size_t(digits);
    return 9;
}

} // namespace

float fromFileUnits(float value, double perMetre) { return float(double(value) / perMetre); }

float toFileUnits(float metres, double perMetre) {
    if (perMetre == 1.0) return metres;
    const float nearest = float(double(metres) * perMetre);
    if (!std::isfinite(nearest)) return nearest;
    float best = nearest;
    size_t bestLength = shortestLength(nearest);
    for (const float c : {std::nextafter(nearest, -INFINITY), std::nextafter(nearest, INFINITY)}) {
        if (fromFileUnits(c, perMetre) != metres) continue;
        const size_t length = shortestLength(c);
        if (length < bestLength) best = c, bestLength = length;
    }
    return best;
}

glm::vec3 toFileUnits(glm::vec3 metres, double perMetre) {
    return {toFileUnits(metres.x, perMetre), toFileUnits(metres.y, perMetre), toFileUnits(metres.z, perMetre)};
}

std::span<const LengthUnit> lengthUnits() { return kUnits; }

const LengthUnit* findLengthUnit(std::string_view id) {
    std::string lower(id);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (lower == "inch" || lower == "inches") lower = "in";
    for (const LengthUnit& u : kUnits)
        if (lower == u.id) return &u;
    return nullptr;
}

const LengthUnit& millimetres() { return kUnits[0]; }
const LengthUnit& metres() { return kUnits[2]; }

const LengthUnit* unitForScale(double perMetre) {
    for (const LengthUnit& u : kUnits)
        if (std::fabs(u.perMetre - perMetre) <= 1e-9 * u.perMetre) return &u;
    return nullptr;
}

std::string formatLength(double metres, const LengthUnit& unit) { return number(metres * unit.perMetre) + " " + unit.id; }

std::string formatSize(glm::vec3 metres, const LengthUnit& unit) {
    return number(metres.x * unit.perMetre) + " x " + number(metres.y * unit.perMetre) + " x " +
           number(metres.z * unit.perMetre) + " " + unit.id;
}

} // namespace sf
