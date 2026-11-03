// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Focus Detail in the app (prototype; core/FocusDetail.h): opening a .sfdetail file (or `--focus-detail FILE`,
// or the `standard-1b` benchmark scene) and, once a frame, telling the focus where the camera and brush are.
#include "app/App.h"

#include "core/FocusDetail.h"
#include "core/Micro.h"

#include <chrono>
#include <cmath>
#include <filesystem>

namespace sf {

void App::openFocusDetail(const std::string& path) {
    Mesh m;
    std::string error;
    if (!FocusDetail::open(path, m, error)) {
        status_ = "Focus Detail: " + error;
        return;
    }
    const size_t tris = microTriangleCount(m);
    doc_.setMesh(std::move(m), std::filesystem::path(path).stem().string());
    frameActiveSubtool();
    char buf[160];
    std::snprintf(buf, sizeof buf, "Focus Detail (experimental): %.2f billion micro triangles; detail streams in where you look and sculpt",
                  double(tris) / 1e9);
    status_ = buf;
}

void App::updateFocusDetail() {
    Mesh& m = doc_.mesh;
    if (!m.micro || !m.micro->focus || solverBusy()) return;
    FocusDetail& fd = *m.micro->focus;
    const float h = float(canvasHeight());
    if (!(h > 0.0f)) return;
    FocusView view;
    view.valid = true;
    view.eye = camera_.position();
    view.viewProj = camera_.projection() * camera_.view();
    // pixels per world unit at distance 1 (ortho: as a perspective view would at the target's depth)
    view.focalPx = glm::distance(view.eye, camera_.target) / std::max(camera_.worldPerPixel(camera_.target, h), 1e-12f);
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    FocusBrush brush;
    RayHit hit;
    const bool busy = (asyncStroke_ && drag_ == Drag::Stroke) || doc_.bvhDirty || previewLod_ >= 0;
    if (!busy && pick(mouseX_, mouseY_, hit)) {
        const float dt = float(now - focusBrushTime_);
        const glm::vec3 v = focusBrushValid_ && dt > 0.0f && dt < 0.25f ? (hit.position - focusBrushPos_) / dt : glm::vec3(0.0f);
        focusBrushVel_ = glm::mix(focusBrushVel_, v, 0.5f); // smoothed over a couple of frames
        focusBrushPos_ = hit.position;
        focusBrushRadius_ = radiusPx_ * camera_.worldPerPixel(hit.position, h);
        focusBrushTime_ = now;
        focusBrushValid_ = true;
    } else if (!(drag_ == Drag::Stroke) || now - focusBrushTime_ > 1.0) {
        focusBrushValid_ = drag_ == Drag::Stroke && focusBrushValid_; // (a stroke keeps the last brush; its dabs promote their own tiles)
    }
    if (focusBrushValid_) {
        brush.present = true;
        brush.position = focusBrushPos_;
        brush.velocity = focusBrushVel_;
        brush.radius = focusBrushRadius_;
        brush.mirrorX = symmetry_.x;
    }
    fd.update(m, view, brush, 3.0);
}

} // namespace sf
