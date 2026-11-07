// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/Mesh.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace sf {

// Orbit camera around a target. Turntable orbiting keeps the world upright (yaw / pitch); free orbiting
// turns about the screen's axes and can roll over the top, which shows up as roll.
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
    // Lens shift in normalised device units: where the target lands off the view's centre. frame() sets it to the
    // centre of the free area it fits into (the canvas less the bands that cover it), so orbiting still turns about
    // the model and opening or closing a band doesn't move the view.
    glm::vec2 shift{0.0f};

    glm::quat orientation() const;
    glm::vec3 position() const;
    glm::vec3 forward() const;
    glm::mat4 view() const;
    glm::mat4 projection() const;

    void orbit(float dxPixels, float dyPixels);     // turntable
    void orbitFree(float dxPixels, float dyPixels); // trackball about the screen axes
    // Orbit lock X or Z: the view turns about one world axis only. The drag's part across the axis on screen turns
    // it (as free orbiting would), `rollRadians` (the cursor's sweep around the view's centre) when it points at you.
    void orbitAbout(const glm::vec3& worldAxis, float dxPixels, float dyPixels, float rollRadians);
    void setOrientation(const glm::quat& q);        // as yaw / pitch / roll
    // The nearest of the 24 views looking along a world axis with another world axis up (the classic
    // Shift while rotating).
    static glm::quat snapped(const glm::quat& q);
    void pan(float dxPixels, float dyPixels, float viewportHeight);
    void zoom(float steps);
    // The whole box in view, centred. `area` is the part of the view to fit it into, as fractions of the view's
    // width and height from its top left (x0, y0, x1, y1): the box is fitted to that and centred on it.
    void frame(const AABB& box, const glm::vec4& area = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));

    // Ray through a window coordinate (origin top-left).
    void screenRay(float x, float y, float width, float height, glm::vec3& origin, glm::vec3& dir) const;
    // The same view as seen from a space placed by `place` (a similarity: rotation, translation, uniform scale;
    // e.g. a linked Part's transform): view() becomes view() * place up to a uniform scale, which the projection
    // ignores, so rays, projections and pixel sizes in that space match the view's.
    Camera inSpaceOf(const glm::mat4& place) const;
    // World units covered by one window pixel at the depth of `point`.
    float worldPerPixel(const glm::vec3& point, float viewportHeight) const;

private:
    float nearPlane() const { return distance * 0.01f; }
    float farPlane() const { return distance * 100.0f; }
};

} // namespace sf
