// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Micro.h"
#include "core/PathStroke.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace sf {

bool parseVec3(const std::string& text, glm::vec3& out) {
    const char* s = text.c_str();
    for (int i = 0; i < 3; ++i) {
        char* end = nullptr;
        const float v = std::strtof(s, &end);
        if (end == s || !std::isfinite(v)) return false;
        out[i] = v;
        s = end;
        if (i < 2) {
            if (*s != ',') return false;
            ++s;
        }
    }
    return *s == '\0';
}

bool parsePathPoints(const std::string& text, std::vector<glm::vec3>& out, std::string& error) {
    out.clear();
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && (std::isspace(static_cast<unsigned char>(text[i])) || text[i] == '|')) ++i;
        if (i >= text.size()) break;
        size_t j = i;
        while (j < text.size() && !std::isspace(static_cast<unsigned char>(text[j])) && text[j] != '|') ++j;
        glm::vec3 p;
        if (!parseVec3(text.substr(i, j - i), p)) {
            error = "Bad point '" + text.substr(i, j - i) + "' (expected x,y,z)";
            return false;
        }
        out.push_back(p);
        i = j;
    }
    if (out.empty()) {
        error = "No points given (expected \"x,y,z x,y,z ...\")";
        return false;
    }
    return true;
}

std::vector<glm::vec3> samplePath(const std::vector<glm::vec3>& points, bool curve, float step) {
    std::vector<glm::vec3> out;
    if (points.empty()) return out;
    if (points.size() == 1 || step <= 0.0f) return {points.front()};

    // Dense polyline first (a centripetal-free uniform Catmull-Rom is plenty for hand-placed points).
    std::vector<glm::vec3> line;
    const size_t n = points.size();
    for (size_t i = 0; i + 1 < n; ++i) {
        const glm::vec3 p0 = points[i ? i - 1 : 0], p1 = points[i], p2 = points[i + 1], p3 = points[std::min(i + 2, n - 1)];
        const int sub = curve && n > 2 ? 32 : 1;
        for (int k = 0; k < sub; ++k) {
            const float t = float(k) / float(sub), t2 = t * t, t3 = t2 * t;
            line.push_back(0.5f * (2.0f * p1 + (p2 - p0) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                                   (3.0f * p1 - p0 - 3.0f * p2 + p3) * t3));
        }
    }
    line.push_back(points.back());

    // Then points at even arc length along it.
    out.push_back(line.front());
    float carry = 0.0f; // distance walked since the last sample
    for (size_t i = 0; i + 1 < line.size(); ++i) {
        const glm::vec3 a = line[i], b = line[i + 1];
        const float len = glm::length(b - a);
        float at = step - carry;
        while (at <= len) {
            out.push_back(a + (b - a) * (at / len));
            at += step;
        }
        carry = len - (at - step);
    }
    return out;
}

int strokePath(Document& doc, const BrushRecipe* recipe, BrushType type, const PathStroke& stroke, std::string& error) {
    Mesh& mesh = doc.mesh;
    if (mesh.triangleCount() == 0) {
        error = "The active Part is empty";
        return -1;
    }
    if (stroke.points.empty()) {
        error = "No points given";
        return -1;
    }
    const float viewLen = glm::length(stroke.view);
    if (!(viewLen > 1e-6f)) {
        error = "The view direction must not be zero";
        return -1;
    }
    const glm::vec3 view = stroke.view / viewLen;
    doc.syncBvh();

    const AABB box = mesh.bounds();
    const float diag = std::max(glm::length(box.extent()), 1e-4f);
    const float radius = stroke.radius > 0.0f ? stroke.radius : diag * 0.04f;
    const float spacing = std::max(stroke.spacing >= 0.0f ? stroke.spacing : recipe ? recipe->spacing : 0.15f, 0.02f);
    const std::vector<glm::vec3> samples = samplePath(stroke.points, stroke.curve, radius * spacing);

    // Screen axes of the projection: jitter scatters in them, alphas point "up" along `up`.
    const glm::vec3 helper = std::abs(view.y) > 0.9f ? glm::vec3(0.0f, 0.0f, -1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3 right = glm::normalize(glm::cross(view, helper));
    const glm::vec3 up = glm::cross(right, view);

    const std::string label = recipe ? recipe->name : brushLabel(type);
    if (!doc.sculptor.beginStroke(mesh, label, doc.activeLayer)) {
        error = "Active layer weight is 0 - raise it to sculpt into the layer";
        return -1;
    }

    uint32_t rng = stroke.seed ? stroke.seed : 1u;
    auto rnd = [&rng] { // -1..1, same xorshift as viewport strokes
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return float(rng & 0xffff) / 65535.0f * 2.0f - 1.0f;
    };
    auto project = [&](glm::vec3 p, RayHit& hit) {
        const glm::vec3 origin = p - view * (glm::dot(p - box.center(), view) + diag);
        return surfaceRaycast(mesh, doc.bvh, origin, view, hit);
    };

    DabParams base;
    base.viewDir = view;
    base.radius = radius;
    base.strength = std::clamp(stroke.strength, 0.0f, 1.0f);
    base.pressure = std::clamp(stroke.pressure, 0.0f, 1.0f);
    base.invert = stroke.invert;
    base.frontFacesOnly = recipe && recipe->frontFacesOnly;
    base.alpha = &doc.brushAlpha;
    base.alphaUp = up;
    base.vdm = doc.brushVdm.get();

    int dabs = 0;
    if (type == BrushType::Move && !recipe) {
        // Move grabs the region under the first point and drags it along the path.
        RayHit hit;
        if (project(samples.front(), hit)) {
            DabParams p = base;
            p.center = hit.position;
            doc.sculptor.beginGrab(mesh, doc.bvh, p, stroke.symmetry);
            for (size_t i = 1; i < samples.size(); ++i) doc.sculptor.grab(mesh, doc.bvh, samples[i] - samples[i - 1]);
            dabs = int(samples.size());
        }
    } else if ((type == BrushType::SnakeHook || type == BrushType::KelvinGrab || type == BrushType::KelvinSnake) && !recipe) {
        // Tendril, Elastic grab and Elastic Tendril: the grabbed point follows the path (in the view plane, as the
        // cursor drags it in App::snakeHookTo; a ray would hit the surface behind a pulled shape, or miss past the
        // silhouette), in small steps.
        RayHit hit;
        if (project(samples.front(), hit)) {
            DabParams p = base;
            glm::vec3 tip = hit.position;
            const glm::vec3 offset = hit.position - samples.front();
            for (size_t i = 1; i < samples.size(); ++i) {
                p.center = tip;
                p.strokeDelta = samples[i] + offset - tip;
                doc.sculptor.dab(mesh, doc.bvh, type, p, stroke.symmetry);
                tip += p.strokeDelta;
            }
            if (samples.size() == 1) {
                p.center = tip;
                doc.sculptor.dab(mesh, doc.bvh, type, p, stroke.symmetry);
            }
            dabs = int(samples.size());
        }
    } else if ((type == BrushType::KelvinTwist || type == BrushType::KelvinScale) && !recipe && samples.size() > 1) {
        // Twist and Scale act at the first point; moving right along the screen turns clockwise / swells (as a
        // drag in the viewport does), by the distance in brush radii.
        RayHit hit;
        if (project(samples.front(), hit)) {
            KelvinletStep step;
            step.center = hit.position;
            step.radius = radius;
            step.viewDir = view;
            step.axis = -view;
            const float amount = std::min(1.0f, 2.0f * base.strength) * base.pressure * (stroke.invert ? -1.0f : 1.0f);
            for (size_t i = 1; i < samples.size(); ++i) {
                const float dx = glm::dot(samples[i] - samples[i - 1], right) / radius * amount;
                if (type == BrushType::KelvinTwist) step.angle = -dx * 1.5707963f;
                else step.logScale = dx * 0.6f;
                doc.sculptor.kelvinletStep(mesh, doc.bvh, type, step, stroke.symmetry);
            }
            dabs = int(samples.size());
        }
    } else {
        glm::vec3 last{0.0f};
        bool haveLast = false;
        for (glm::vec3 s : samples) {
            if (recipe && recipe->jitter > 0.0f) s += (right * rnd() + up * rnd()) * (recipe->jitter * radius);
            RayHit hit;
            if (!project(s, hit)) continue;
            DabParams p = base;
            p.center = hit.position;
            p.strokeDelta = haveLast ? hit.position - last : glm::vec3(0.0f);
            last = hit.position;
            haveLast = true;
            if (recipe) doc.sculptor.dabRecipe(mesh, doc.bvh, *recipe, p, stroke.symmetry);
            else doc.sculptor.dab(mesh, doc.bvh, type, p, stroke.symmetry);
            ++dabs;
        }
    }
    doc.sculptor.flushDetailClone(mesh, doc.bvh, stroke.symmetry); // a click with Detail Clone
    UndoRecord record = doc.sculptor.endStroke();
    if (!record.empty()) doc.undo.push(std::move(record));
    return dabs;
}

} // namespace sf
