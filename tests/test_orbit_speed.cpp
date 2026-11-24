// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
// Mouse orbit speed (Preferences > Input, app/OrbitSpeed.h): the clamp, and the angle per pixel that the speed scales
// in each kind of rotation.
#include "TestHarness.h"

#include "app/OrbitSpeed.h"
#include "render/Camera.h"

#include <glm/gtc/quaternion.hpp>

using namespace sf;

namespace {
// The angle (radians) a camera has turned through from the front.
float turned(const Camera& c) { return 2.0f * std::acos(std::min(1.0f, std::abs(glm::normalize(c.orientation()).w))); }
} // namespace

TEST(orbit_speed_clamp) {
    CHECK(clampMouseOrbitSpeed(1.0f) == 1.0f);
    CHECK(clampMouseOrbitSpeed(0.0f) == kMouseOrbitSpeedMin);
    CHECK(clampMouseOrbitSpeed(-3.0f) == kMouseOrbitSpeedMin);
    CHECK(clampMouseOrbitSpeed(50.0f) == kMouseOrbitSpeedMax);
    CHECK(clampMouseOrbitSpeed(std::nanf("")) == 1.0f); // (a damaged ui.json)
}

// App::orbitBy hands each rotation kind its pixels times the speed: the turn is that much of the 1x turn.
TEST(orbit_speed_scales_every_rotation) {
    const float px = 120.0f;
    for (float speed : {0.1f, 0.3f, 0.5f, 2.0f}) {
        Camera a, b;
        a.orbit(px, 0.0f);
        b.orbit(px * speed, 0.0f);
        CHECK(std::abs(turned(b) - turned(a) * speed) < 1e-4f); // turntable
        Camera c, d;
        c.orbitFree(px, 0.0f);
        d.orbitFree(px * speed, 0.0f);
        CHECK(std::abs(turned(d) - turned(c) * speed) < 1e-4f); // free
        Camera e, f;
        e.orbitAbout(glm::vec3(0, 1, 0), px, 0.0f, 0.0f);
        f.orbitAbout(glm::vec3(0, 1, 0), px * speed, 0.0f, 0.0f);
        CHECK(std::abs(turned(f) - turned(e) * speed) < 1e-4f); // about a locked axis
    }
    Camera one;
    one.orbit(100.0f, 0.0f);
    CHECK(std::abs(turned(one) - 0.8f) < 1e-4f); // 1x is still 0.008 radians per pixel
}
