// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/Camera.h"

#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>

#include <algorithm>
#include <cmath>

namespace sf {

glm::quat Camera::orientation() const {
    return glm::angleAxis(yaw, glm::vec3(0, 1, 0)) * glm::angleAxis(pitch, glm::vec3(1, 0, 0)) *
           glm::angleAxis(roll, glm::vec3(0, 0, 1));
}

glm::vec3 Camera::position() const { return target + orientation() * glm::vec3(0, 0, distance); }

glm::vec3 Camera::forward() const { return orientation() * glm::vec3(0, 0, -1); }

glm::mat4 Camera::view() const {
    // The inverse of translate(position) * rotate(orientation), written out (no general 4x4 inverse and its
    // rounding: at extreme zoom the view and the brush's rays, screenRay, agree to a fraction of a pixel).
    const glm::quat q = glm::normalize(orientation());
    return glm::translate(glm::mat4_cast(glm::conjugate(q)), -position());
}

glm::mat4 Camera::projection() const {
    if (!ortho) return glm::perspective(fovY, aspect, nearPlane(), farPlane());
    // As tall as the perspective view at the target; the depth range reaches behind the eye, as in Blender, so
    // zooming in never clips the model.
    const float h = distance * std::tan(fovY * 0.5f);
    return glm::ortho(-h * aspect, h * aspect, -h, h, -farPlane(), farPlane());
}

void Camera::orbit(float dx, float dy) {
    if (roll != 0.0f) { // coming from free orbiting: stand the view upright again
        const glm::vec3 f = forward();
        yaw = std::atan2(-f.x, -f.z);
        pitch = std::asin(std::clamp(f.y, -1.0f, 1.0f));
        roll = 0.0f;
    }
    yaw -= dx * 0.008f;
    pitch = std::clamp(pitch - dy * 0.008f, -glm::radians(89.0f), glm::radians(89.0f));
}

void Camera::orbitFree(float dx, float dy) {
    // Turning the camera about its own up and right axes: the model follows the cursor on screen.
    setOrientation(orientation() * glm::angleAxis(-dx * 0.008f, glm::vec3(0, 1, 0)) *
                   glm::angleAxis(-dy * 0.008f, glm::vec3(1, 0, 0)));
}

void Camera::setOrientation(const glm::quat& q) {
    glm::extractEulerAngleYXZ(glm::mat4_cast(glm::normalize(q)), yaw, pitch, roll);
    if (std::abs(roll) < 1e-6f) roll = 0.0f;
}

glm::quat Camera::snapped(const glm::quat& q) {
    auto nearestAxis = [](const glm::vec3& v, const glm::vec3* avoid) {
        glm::vec3 best(0.0f);
        float bestDot = -2.0f;
        for (int axis = 0; axis < 3; ++axis)
            for (float sign : {1.0f, -1.0f}) {
                glm::vec3 a(0.0f);
                a[axis] = sign;
                if (avoid && std::abs(glm::dot(a, *avoid)) > 0.5f) continue;
                if (glm::dot(v, a) > bestDot) bestDot = glm::dot(v, a), best = a;
            }
        return best;
    };
    const glm::vec3 back = nearestAxis(q * glm::vec3(0, 0, 1), nullptr); // camera +Z points away from the view
    const glm::vec3 up = nearestAxis(q * glm::vec3(0, 1, 0), &back);
    return glm::quat_cast(glm::mat3(glm::cross(up, back), up, back));
}

void Camera::pan(float dx, float dy, float viewportHeight) {
    float scale = 2.0f * distance * std::tan(fovY * 0.5f) / std::max(viewportHeight, 1.0f);
    glm::quat q = orientation();
    target += q * glm::vec3(-dx * scale, dy * scale, 0.0f);
}

void Camera::zoom(float steps) { distance = std::clamp(distance * std::pow(0.88f, steps), 1e-4f, 1e6f); }

void Camera::frame(const AABB& box) {
    if (!box.valid()) return;
    target = box.center();
    float radius = std::max(glm::length(box.extent()) * 0.5f, 1e-4f);
    distance = radius / std::sin(fovY * 0.5f) * 1.1f;
}

void Camera::screenRay(float x, float y, float width, float height, glm::vec3& origin, glm::vec3& dir) const {
    // From the camera's own axes rather than by inverting projection * view: the same ray, without the matrix
    // inverse's rounding, so the brush lands under the cursor to a fraction of a pixel at any zoom.
    const float nx = 2.0f * x / std::max(width, 1e-6f) - 1.0f;
    const float ny = 1.0f - 2.0f * y / std::max(height, 1e-6f);
    const float t = std::tan(fovY * 0.5f);
    const glm::quat q = orientation();
    if (!ortho) {
        origin = position() + q * (glm::vec3(nx * t * aspect, ny * t, -1.0f) * nearPlane()); // on the near plane
        dir = glm::normalize(q * glm::vec3(nx * t * aspect, ny * t, -1.0f));
        return;
    }
    const float h = distance * t; // projection(): the depth range starts behind the eye
    origin = position() + q * glm::vec3(nx * h * aspect, ny * h, farPlane());
    dir = forward();
}

float Camera::worldPerPixel(const glm::vec3& point, float viewportHeight) const {
    if (ortho) return 2.0f * distance * std::tan(fovY * 0.5f) / std::max(viewportHeight, 1.0f);
    float depth = std::max(glm::dot(point - position(), forward()), nearPlane());
    return 2.0f * depth * std::tan(fovY * 0.5f) / std::max(viewportHeight, 1.0f);
}

} // namespace sf
