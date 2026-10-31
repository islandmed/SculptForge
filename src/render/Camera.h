// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/Mesh.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace sf {

// Orbit camera around a target. Turntable orbiting keeps the world upright (yaw / pitch); free orbiting
// (ZBrush) turns about the screen's axes and can roll over the top, which shows up as roll.
class Camera {
public:
    glm::vec3 target{0.0f};
    float distance = 4.0f;
    float yaw = 0.0f;   // radians around world Y
    float pitch = 0.0f; // radians around the camera's X; a turntable clamps it short of +-90 degrees
    float roll = 0.0f;  // radians around the view direction; only free orbiting makes it non-zero
    float fovY = glm::radians(35.0f);
    float aspect = 1.0f;
    bool ortho = false; // orthographic: shows what a perspective view would at the target's depth (Numpad 5)

    glm::quat orientation() const;
    glm::vec3 position() const;
    glm::vec3 forward() const;
    glm::mat4 view() const;
    glm::mat4 projection() const;

    void orbit(float dxPixels, float dyPixels);     // turntable
    void orbitFree(float dxPixels, float dyPixels); // trackball about the screen axes (ZBrush)
    void setOrientation(const glm::quat& q);        // as yaw / pitch / roll
    // The nearest of the 24 views looking along a world axis with another world axis up (ZBrush's
    // Shift while rotating).
    static glm::quat snapped(const glm::quat& q);
    void pan(float dxPixels, float dyPixels, float viewportHeight);
    void zoom(float steps);
    void frame(const AABB& box);

    // Ray through a window coordinate (origin top-left).
    void screenRay(float x, float y, float width, float height, glm::vec3& origin, glm::vec3& dir) const;
    // World units covered by one window pixel at the depth of `point`.
    float worldPerPixel(const glm::vec3& point, float viewportHeight) const;

private:
    float nearPlane() const { return distance * 0.01f; }
    float farPlane() const { return distance * 100.0f; }
};

} // namespace sf
