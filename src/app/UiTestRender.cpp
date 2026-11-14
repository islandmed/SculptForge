// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// UI tests for "the model is drawn" (0.12.15: a 48M-triangle mesh was invisible in PBR, and no test noticed, because the tests
// checked that nothing crashed). docs/testing-flow.md, "Render matrix"; the cells are tests/render_matrix/*.sfui.
//
//   gl_limits texels=N       the buffer-texture limit the renderer reads (and the most one micro buffer holds) is at most N
//                            texels, as on a small GPU, so a mesh runs the path past the limits on any machine; 0: the driver's.
//                            Tests only: no setting or environment reaches it.
//   render_cell NAME mode=lit|pbr|flat|wire [min=F] [size=PX]
//                            the scene drawn offscreen (256 px square) with that shading: at least F (default 0.25) of the pixels
//                            inside the model's projected box differ from the background, and they are not all one colour. A
//                            failing cell is listed by NAME (the script's "mode x path") with its picture in --ui-out, and
//                            the script goes on, so every empty cell of a run is named.
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
        if (w.size() < 2 || w[1].rfind("texels=", 0) != 0) {
            error = "'gl_limits' needs texels=N (0: the driver's limits)";
            return false;
        }
        const unsigned long texels = std::strtoul(w[1].c_str() + 7, nullptr, 10);
        run([texels] { gl::setTestTexelLimit(uint32_t(texels)); });
        return true;
    }
    // render_cell NAME mode=... [min=F] [size=PX]
    if (w.size() < 3) {
        error = "'render_cell' needs NAME mode=lit|pbr|flat|wire";
        return false;
    }
    std::string mode;
    double floor = kRenderCoverageFloor;
    int size = 256;
    for (size_t i = 2; i < w.size(); ++i) {
        if (w[i].rfind("mode=", 0) == 0) mode = w[i].substr(5);
        else if (w[i].rfind("min=", 0) == 0) floor = std::strtod(w[i].c_str() + 4, nullptr);
        else if (w[i].rfind("size=", 0) == 0) size = std::clamp(std::atoi(w[i].c_str() + 5), 64, 1024);
        else {
            error = "'render_cell': unknown argument '" + w[i] + "'";
            return false;
        }
    }
    if (mode != "lit" && mode != "pbr" && mode != "flat" && mode != "wire") {
        error = "'render_cell' needs mode=lit|pbr|flat|wire";
        return false;
    }
    const std::string name = w[1];
    steps_.push_back([this, line, name, mode, floor, size](float t) {
        if (t == 0.0f) return false; // a settled frame
        App& app = app_;
        const RenderSettings saved = app.renderSettings_;
        RenderSettings& s = app.renderSettings_;
        s.shading = mode == "pbr" ? RenderSettings::Shading::Pbr : RenderSettings::Shading::LitSphere;
        s.flatShading = mode == "flat";
        s.wireframe = mode == "wire";
        s.gradientBackground = false;
        s.backgroundColor = glm::vec3(0.06f, 0.30f, 0.12f); // (unlike any shaded surface's colour)
        s.dimInactive = false;
        s.fullDetail = true;
        s.showMesh = true;
        app.syncScene();
        Camera cam;
        cam.aspect = 1.0f;
        cam.yaw = glm::radians(30.0f);
        cam.pitch = glm::radians(20.0f);
        const AABB bounds = app.sceneBounds();
        if (bounds.valid()) cam.frame(bounds);
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
        app.renderSettings_ = saved;
        app.syncScene();
        std::string why;
        RenderCoverage r;
        if (!ok) why = "the render failed: " + err;
        else if (!bounds.valid()) why = "the scene has no model";
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
            }
        }
        if (ok) lastRenderCoverage_ = r.coverage;
        std::printf("render_cell %-40s coverage %5.1f%%, %d colours%s\n", name.c_str(), r.coverage * 100.0, r.colors, why.empty() ? "" : "  FAIL");
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
