// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Preferences > Interface > Mask colour in ui.json (core/MaskColour.h): written as display values, read back clamped;
// anything that isn't three numbers is not a colour (the default darkening look stays).
#include "TestHarness.h"

#include "core/MaskColour.h"

#include <cmath>

using namespace sf;

TEST(mask_colour_round_trip) {
    glm::vec3 c{0.0f};
    CHECK(maskColorFromJson(maskColorToJson({0.8f, 0.1f, 0.25f}), c));
    CHECK(std::abs(c.x - 0.8f) < 1e-4f && std::abs(c.y - 0.1f) < 1e-4f && std::abs(c.z - 0.25f) < 1e-4f);
}

TEST(mask_colour_clamped_and_strict) {
    Json a = Json::array();
    a.push(2.0), a.push(-1.0), a.push(0.5);
    glm::vec3 c{0.3f};
    CHECK(maskColorFromJson(a, c));
    CHECK(c.x == 1.0f && c.y == 0.0f && c.z == 0.5f);

    glm::vec3 keep{0.2f, 0.3f, 0.4f};
    Json two = Json::array();
    two.push(1.0), two.push(1.0);
    CHECK(!maskColorFromJson(two, keep));          // two numbers
    CHECK(!maskColorFromJson(Json::object(), keep)); // not an array
    CHECK(keep.x == 0.2f && keep.y == 0.3f && keep.z == 0.4f);
}
