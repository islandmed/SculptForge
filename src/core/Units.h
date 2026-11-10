// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Real-world lengths. A SculptForge unit is one metre; sizes show in the unit picked in Preferences (mm, cm, m,
// in), and unit-less files (OBJ, PLY; STL is millimetres by convention) are read in a unit too, which the scene
// remembers so exporting writes the numbers it was opened with (Document::unitlessScale).
#pragma once

#include <glm/glm.hpp>

#include <span>
#include <string>
#include <string_view>

namespace sf {

struct LengthUnit {
    const char* id;     // "mm", "cm", "m", "in"
    double perMetre;    // numbers in this unit per metre
};

// mm, cm, m, in.
std::span<const LengthUnit> lengthUnits();
// The unit called `id` (case-insensitive; "inch"/"inches" too), or null.
const LengthUnit* findLengthUnit(std::string_view id);
const LengthUnit& millimetres();
const LengthUnit& metres();
// The unit with this many numbers per metre (mm for 1000, ...), or null.
const LengthUnit* unitForScale(double perMetre);

// A file number in a unit to metres (`perMetre` numbers per metre), rounded once from double.
float fromFileUnits(float value, double perMetre);
// Metres to a number in the file unit: the float nearest metres * perMetre, or a neighbour that reads back as the
// same metres with a shorter decimal form, so a file value such as 12.7 that was read with fromFileUnits is
// written back exactly (any value with up to about 7 significant digits).
float toFileUnits(float metres, double perMetre);
glm::vec3 toFileUnits(glm::vec3 metres, double perMetre);

// "12.5 mm", "1.8 m", "2.36 in": `metres` in the unit, with about four significant digits.
std::string formatLength(double metres, const LengthUnit& unit);
// "50 x 20 x 12.5 mm": an extent (x, y, z) in the unit.
std::string formatSize(glm::vec3 metres, const LengthUnit& unit);

} // namespace sf
