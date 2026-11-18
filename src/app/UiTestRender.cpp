// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// UI tests for "the model is drawn" (0.12.15: a 48M-triangle mesh was invisible in PBR, and no test noticed, because the tests
// checked that nothing crashed). docs/testing-flow.md, "Render matrix"; the cells are tests/render_matrix/*.sfui.
//
//   gl_limits texels=N       the buffer-texture limit the renderer reads (and the most one micro buffer holds) is at most N
//                            texels, as on a small GPU, so a mesh runs the path past the limits on any machine; 0: the driver's.
//                            Tests only: no setting or environment reaches it.
//   gl_limits copies=0|1|auto partial buffer updates as GPU copies (1) or BufferSubData (0) whatever the driver; auto: as the
//                            driver needs (gl::copyUploads: copies on OpenGL over D3D12). Tests only, as texels=.
//   render_cell NAME mode=lit|pbr|flat|wire|groups [min=F] [size=PX] [splats=1]   (groups: Lit Sphere with Show Face Groups)
//                            the scene drawn offscreen (256 px square) with that shading: at least F (default 0.25) of the pixels
//                            inside the model's projected box differ from the background, and they are not all one colour. A
//                            failing cell is listed by NAME (the script's "mode x path") with its picture in --ui-out, and
//                            the script goes on, so every empty cell of a run is named. splats=1: the loaded splats alone (the
//                            box is theirs); when they carry view-dependent colour (SH degree >= 1) it must show: drawn again
//                            with their plain colour only, at least 10% of the box changes; drawn with that colour per
//                            quad corner (not the once-a-splat pre-pass), no channel differs by more than 1/255.
//                            After the frame the context must still draw (gl::contextHealthy): a cell fails when the
//                            GPU dropped the device.
// Value: render_coverage (the last cell's share, 0-1).
#include "app/App.h"
#include "app/Demo.h"
#include "app/RenderCoverage.h"
#include "core/Image.h"
#include "render/GL.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>

namespace sf {

bool Demo::parseRenderStep(int line, const std::vector<std::string>& w, std::string& error) {
    const std::string& verb = w[0];
    if (verb == "gl_limits") {
        if (w.size() < 2) {
            error = "'gl_limits' needs texels=N (0: the driver's limits) or copies=0|1|auto";
            return false;
        }
        for (size_t i = 1; i < w.size(); ++i) {
            if (w[i].rfind("texels=", 0) == 0) {
                const unsigned long texels = std::strtoul(w[i].c_str() + 7, nullptr, 10);
                run([texels] { gl::setTestTexelLimit(uint32_t(texels)); });
            } else if (w[i] == "copies=0" || w[i] == "copies=1" || w[i] == "copies=auto") {
                const int copies = w[i] == "copies=auto" ? -1 : w[i] == "copies=1" ? 1 : 0;
                run([copies] { gl::setTestCopyUploads(copies); });
            } else {
                error = "'gl_limits': unknown argument '" + w[i] + "' (texels=N, copies=0|1|auto)";
                return false;
            }
        }
        return true;
    }
    // render_cell NAME mode=... [min=F] [size=PX] [splats=1] [target=X,Y,Z distance=D: a close-up, the camera D from that point]
    if (w.size() < 3) {
        error = "'render_cell' needs NAME mode=lit|pbr|flat|wire|groups";
        return false;
    }
    std::string mode;
    double floor = kRenderCoverageFloor;
    int size = 256;
    float distance = 0.0f; // 0 = frame the scene
    glm::vec3 target(0.0f);
    bool splats = false;
    for (size_t i = 2; i < w.size(); ++i) {
        if (w[i].rfind("mode=", 0) == 0) mode = w[i].substr(5);
        else if (w[i].rfind("min=", 0) == 0) floor = std::strtod(w[i].c_str() + 4, nullptr);
        else if (w[i].rfind("size=", 0) == 0) size = std::clamp(std::atoi(w[i].c_str() + 5), 64, 1024);
        else if (w[i].rfind("distance=", 0) == 0) distance = std::max(float(std::strtod(w[i].c_str() + 9, nullptr)), 0.0f);
        else if (w[i].rfind("target=", 0) == 0) {
            if (std::sscanf(w[i].c_str() + 7, "%f,%f,%f", &target.x, &target.y, &target.z) != 3) {
                error = "'render_cell': target wants X,Y,Z";
                return false;
            }
        }
        else if (w[i] == "splats=1") splats = true;
        else {
            error = "'render_cell': unknown argument '" + w[i] + "'";
            return false;
        }
    }
    if (mode != "lit" && mode != "pbr" && mode != "flat" && mode != "wire" && mode != "groups") {
        error = "'render_cell' needs mode=lit|pbr|flat|wire|groups";
        return false;
    }
    const std::string name = w[1];
    steps_.push_back([this, line, name, mode, floor, size, splats, distance, target](float t) {
        if (t == 0.0f) return false; // a settled frame
        App& app = app_;
        const RenderSettings saved = app.renderSettings_;
        RenderSettings& s = app.renderSettings_;
        s.shading = mode == "pbr" ? RenderSettings::Shading::Pbr : RenderSettings::Shading::LitSphere;
        s.flatShading = mode == "flat";
        s.wireframe = mode == "wire";
        s.showFaceGroups = mode == "groups";
        s.gradientBackground = false;
        s.backgroundColor = glm::vec3(0.06f, 0.30f, 0.12f); // (unlike any shaded surface's colour)
        s.dimInactive = false;
        s.fullDetail = true;
        s.showMesh = !splats;
        s.showSplats = true;
        s.splatViewColor = true;
        const bool forced = app.splatViewColorForced_;
        app.splatViewColorForced_ = splats; // (the floor tier draws plain colour; CI's software GL is one)
        app.syncScene();
        Camera cam;
        cam.aspect = 1.0f;
        cam.yaw = glm::radians(30.0f);
        cam.pitch = glm::radians(20.0f);
        const AABB bounds = splats ? app.doc_.splats.bounds(0.9f) : app.sceneBounds();
        if (bounds.valid()) cam.frame(bounds);
        if (distance > 0.0f) { // a close-up: looking down -z at `target` from `distance` away (the micro levels' draw-above path)
            cam.yaw = cam.pitch = 0.0f;
            cam.target = target;
            cam.distance = distance;
        }
        Image img;
        img.width = img.height = size;
        std::string err;
        bool ok = true;
        // Focus Detail and the environment stream in over frames: draw until they have (as the view does while it is open).
        for (int k = 0; k < 24 && ok; ++k) {
            if (app.doc_.mesh.micro && app.doc_.mesh.micro->focus) {
                app.updateFocusDetail(float(size));
                app.syncScene();
            }
            ok = app.renderer_.renderToImage(cam, size, size, s, img.rgba, err);
            if (k >= 1 && !app.renderer_.environmentPending() && !(app.doc_.mesh.micro && app.doc_.mesh.micro->focus)) break;
        }
        // The same view with the splats' plain colour only: the share of the box that changes.
        double viewColorShare = -1.0;
        int prepassDiff = -1; // the largest channel difference, pre-pass vs per corner (0-255)
        if (ok && splats && app.doc_.splats.shDegree > 0) {
            Image plain;
            plain.width = plain.height = size;
            s.splatViewColor = false;
            ok = app.renderer_.renderToImage(cam, size, size, s, plain.rgba, err);
            const RenderCoverage box = measureRenderCoverage(img, cam, bounds);
            size_t changed = 0;
            for (size_t p = 0; ok && p + 3 < img.rgba.size(); p += 4)
                for (int ch = 0; ch < 3; ++ch)
                    if (std::abs(int(img.rgba[p + size_t(ch)]) - int(plain.rgba[p + size_t(ch)])) > 6) {
                        ++changed;
                        break;
                    }
            viewColorShare = box.boxW * box.boxH > 0 ? double(changed) / double(box.boxW * box.boxH) : 0.0;
            // The same view with that colour per quad corner (the old path): the pre-pass's once a splat must match it.
            Image corner;
            corner.width = corner.height = size;
            s.splatViewColor = true;
            s.splatShPerVertex = true;
            ok = ok && app.renderer_.renderToImage(cam, size, size, s, corner.rgba, err);
            for (size_t p = 0; ok && p < img.rgba.size(); ++p)
                prepassDiff = std::max(prepassDiff, std::abs(int(img.rgba[p]) - int(corner.rgba[p])));
            if (!app.renderer_.splatColorPrepass()) prepassDiff = 256; // (both drawn per corner: nothing compared)
        }
        app.renderSettings_ = saved;
        app.splatViewColorForced_ = forced;
        app.syncScene();
        // ... and the GPU still has its device after the frame (Mesa's OpenGL over D3D12 removed it after micro strokes:
        // render/BufferUpload.h); a lost device draws nothing again, so it fails here by its name, not as an empty cell.
        gl::Finish();
        const bool healthy = gl::contextHealthy();
        std::string why;
        RenderCoverage r;
        if (!healthy) why = "the GPU dropped the device (GL context lost) by this frame", ok = false;
        else if (!ok) why = "the render failed: " + err;
        else if (!bounds.valid()) why = splats ? "no splats are loaded" : "the scene has no model";
        else {
            r = measureRenderCoverage(img, cam, bounds);
            lastRenderCoverage_ = r.coverage;
            char buf[200];
            if (r.coverage < floor) {
                std::snprintf(buf, sizeof buf, "the model is not drawn: %.0f%% of its %dx%d px box differs from the background, wanted %.0f%%",
                              r.coverage * 100.0, r.boxW, r.boxH, floor * 100.0);
                why = buf;
            } else if (r.colors < 6) {
                std::snprintf(buf, sizeof buf, "the model is one flat colour (%d distinct colours in %.0f%% of its box)", r.colors, r.coverage * 100.0);
                why = buf;
            } else if (viewColorShare >= 0.0 && viewColorShare < 0.1) {
                std::snprintf(buf, sizeof buf, "the view-dependent colour is not drawn: %.1f%% of the box changes without it, wanted 10%%",
                              viewColorShare * 100.0);
                why = buf;
            } else if (prepassDiff > 255) {
                why = "the view colour pre-pass did not run (drawn per corner)";
            } else if (prepassDiff > 1) {
                std::snprintf(buf, sizeof buf, "the view colour pre-pass differs from the per-corner colour by %d/255, wanted <= 1",
                              prepassDiff);
                why = buf;
            }
        }
        if (ok) lastRenderCoverage_ = r.coverage;
        std::printf("render_cell %-40s coverage %5.1f%%, %d colours", name.c_str(), r.coverage * 100.0, r.colors);
        if (viewColorShare >= 0.0) std::printf(", view colour %.1f%%, pre-pass diff %d", viewColorShare * 100.0, prepassDiff);
        std::printf("%s\n", why.empty() ? "" : "  FAIL");
        if (!why.empty()) {
            std::string path;
            if (!shotDir_.empty() && ok) {
                std::string file = name;
                for (char& c : file)
                    if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
                path = shotDir_ + "/FAIL-cell-" + file + ".png";
                std::string perr;
                if (!img.savePng(path, perr)) path.clear();
            }
            failures_.push_back(scriptName_ + ":" + std::to_string(line) + ": render cell " + name + ": " + why +
                                (path.empty() ? "" : " (picture: " + path + ")"));
            std::printf("FAIL %s\n", failures_.back().c_str());
        }
        return true;
    });
    return true;
}

double Demo::queryRender(const std::string& name, bool& known) const {
    if (name == "render_coverage") return lastRenderCoverage_;
    known = false;
    return 0.0;
}

} // namespace sf
