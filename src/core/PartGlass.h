// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Glass, water and diamond as a Part's own look (Part::glass): how see-through it is, how strongly it bends light
// (the index of refraction, which sets how much of the environment it reflects at a glancing angle), and the colour
// it takes on through its thickness. The viewport draws it as clear, Fresnel-weighted glass in front of what is
// behind it (render/Renderer.cpp); it does not bend the view behind it and it does not trace rays: that is what
// the Render Bridge is for. Saved in projects and written to glTF (KHR_materials_transmission, _ior, _volume).
#pragma once

#include "core/Json.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace sf {

struct PartGlass {
    float transmission = 0.0f;     // 0 = not glass (an ordinary Part) .. 1 = clear
    float ior = 1.5f;              // index of refraction, 1.0 .. 2.5
    glm::vec3 tint{1.0f};          // linear colour the glass reaches at its full depth; white = clear
    float depth = 1.0f;            // how far light goes before it has the full tint, in quarter-sizes of the Part (0.05 .. 8)
    bool active() const { return transmission > 0.001f; }
    bool operator==(const PartGlass& o) const {
        return transmission == o.transmission && ior == o.ior && tint == o.tint && depth == o.depth;
    }
    bool operator!=(const PartGlass& o) const { return !(*this == o); }
};

constexpr float kGlassIorMin = 1.0f, kGlassIorMax = 2.5f;
constexpr float kGlassDepthMin = 0.05f, kGlassDepthMax = 8.0f;
// The thickness a typical wall has, as a fraction of the Part's size (its bounding box diagonal): depth 1 means the
// full tint is reached after this much glass. glTF's volume extension is written with the same lengths.
constexpr float kGlassWallFraction = 0.25f;

enum class GlassPreset { Water, Glass, Diamond };
constexpr int kGlassPresetCount = 3;
const char* glassPresetName(GlassPreset p); // "water", "glass", "diamond"
float glassPresetIor(GlassPreset p);        // 1.33, 1.5, 2.42
bool parseGlassPreset(const std::string& name, GlassPreset& out);
// The preset's look: transmission 1, its index, a clear tint (water a faint blue-green).
PartGlass glassFromPreset(GlassPreset p);

// The share of light a face looking straight at the eye reflects: ((n - 1) / (n + 1))^2. The shader adds the
// Schlick term for the glancing angle on top of it.
float glassF0(float ior);
// The same Fresnel the viewport uses: Schlick's approximation.
float glassFresnel(float ior, float cosTheta);

// Values clamped into their ranges (NaN becomes the default).
PartGlass clampedGlass(PartGlass g);

// {"transmission", "ior", "tint": [r,g,b] (display sRGB), "depth"}.
Json glassToJson(const PartGlass& g);
// Reads what is there (missing keys keep `out`'s values); false when `j` isn't an object.
bool glassFromJson(const Json& j, PartGlass& out);

// The order to draw see-through Parts in: farthest first. `distances` are each Part's centre's distance in front of
// the eye; the result lists indices into it (ties keep their order).
std::vector<size_t> glassDrawOrder(const std::vector<float>& distances);

} // namespace sf
