// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/OptimisticBench.h"

#include "core/BVH.h"
#include "core/Document.h"
#include "core/HardwareProfile.h"
#include "core/Image.h"
#include "core/Sculpt.h"
#include "core/Symmetry.h"
#include "core/ThreadPool.h"
#include "render/Camera.h"
#include "render/GL.h"
#include "render/Renderer.h"
#include "render/ScreenStroke.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace sf {

namespace {

using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

constexpr GLenum kDepthComponent = 0x1902;
constexpr int kW = 1920, kH = 1080;
constexpr int kMaxDabs = 64; // the pass's uniform arrays

// GL_TIME_ELAPSED queries (3.3 core; not in the app's loader, loaded here like app/Calibration.cpp does).
constexpr GLenum kTimeElapsed = 0x88BF, kQueryResult = 0x8866;
struct Timer {
    void(SF_GLAPI* gen)(GLsizei, GLuint*) = nullptr;
    void(SF_GLAPI* del)(GLsizei, const GLuint*) = nullptr;
    void(SF_GLAPI* begin)(GLenum, GLuint) = nullptr;
    void(SF_GLAPI* end)(GLenum) = nullptr;
    void(SF_GLAPI* result)(GLuint, GLenum, GLuint64*) = nullptr;
    GLuint q = 0;
    bool load() {
        gen = reinterpret_cast<decltype(gen)>(SDL_GL_GetProcAddress("glGenQueries"));
        del = reinterpret_cast<decltype(del)>(SDL_GL_GetProcAddress("glDeleteQueries"));
        begin = reinterpret_cast<decltype(begin)>(SDL_GL_GetProcAddress("glBeginQuery"));
        end = reinterpret_cast<decltype(end)>(SDL_GL_GetProcAddress("glEndQuery"));
        result = reinterpret_cast<decltype(result)>(SDL_GL_GetProcAddress("glGetQueryObjectui64v"));
        if (!(gen && del && begin && end && result)) return false;
        gen(1, &q);
        return true;
    }
};

struct Target {
    GLuint fbo = 0, color = 0, depth = 0;
    void make(bool withDepth) {
        gl::GenTextures(1, &color);
        gl::BindTexture(GL_TEXTURE_2D, color);
        gl::TexImage2D(GL_TEXTURE_2D, 0, GLint(GL_RGBA8), kW, kH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GLint(GL_NEAREST));
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GLint(GL_NEAREST));
        gl::GenFramebuffers(1, &fbo);
        gl::BindFramebuffer(GL_FRAMEBUFFER, fbo);
        gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
        if (withDepth) {
            gl::GenTextures(1, &depth);
            gl::BindTexture(GL_TEXTURE_2D, depth);
            gl::TexImage2D(GL_TEXTURE_2D, 0, GLint(GL_DEPTH_COMPONENT24), kW, kH, 0, kDepthComponent, GL_FLOAT, nullptr);
            gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GLint(GL_NEAREST));
            gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GLint(GL_NEAREST));
            gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth, 0);
        }
        gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    void release() {
        gl::DeleteFramebuffers(1, &fbo);
        gl::DeleteTextures(1, &color);
        if (depth) gl::DeleteTextures(1, &depth);
        *this = {};
    }
};

std::vector<uint8_t> readColor(const Target& t) {
    std::vector<uint8_t> px(size_t(kW) * kH * 4);
    gl::BindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    gl::PixelStorei(GL_PACK_ALIGNMENT, 1);
    gl::ReadPixels(0, 0, kW, kH, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    return px;
}

void savePng(const std::vector<uint8_t>& px, const std::string& path) {
    Image img;
    img.width = kW, img.height = kH;
    img.rgba.resize(px.size());
    for (int y = 0; y < kH; ++y) // (GL rows are bottom-up)
        std::copy_n(px.data() + size_t(kH - 1 - y) * kW * 4, size_t(kW) * 4, img.rgba.data() + size_t(y) * kW * 4);
    std::string err;
    if (!img.savePng(path, err)) std::fprintf(stderr, "%s\n", err.c_str());
}

struct Diff {
    double mean = 0, p99 = 0, over8 = 0; // over the region either image changed (0-255 levels, fraction)
    size_t pixels = 0;
};
// Per-pixel max channel difference of a and b over the pixels where `before` differs from either; `mask` (one per
// pixel, optional) keeps only the pixels it marks.
Diff diff(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, const std::vector<uint8_t>& before,
          const std::vector<uint8_t>* mask = nullptr) {
    std::vector<int> d;
    for (size_t i = 0; i < a.size(); i += 4) {
        if (mask && !(*mask)[i / 4]) continue;
        int ca = 0, cb = 0, ab = 0;
        for (int k = 0; k < 3; ++k) {
            ca = std::max(ca, std::abs(int(a[i + k]) - int(before[i + k])));
            cb = std::max(cb, std::abs(int(b[i + k]) - int(before[i + k])));
            ab = std::max(ab, std::abs(int(a[i + k]) - int(b[i + k])));
        }
        if (ca > 1 || cb > 1) d.push_back(ab);
    }
    Diff r;
    r.pixels = d.size();
    if (d.empty()) return r;
    double sum = 0;
    size_t over = 0;
    for (int v : d) sum += v, over += v > 8;
    std::sort(d.begin(), d.end());
    r.mean = sum / double(d.size());
    r.p99 = d[std::min(d.size() - 1, size_t(double(d.size()) * 0.99))];
    r.over8 = double(over) / double(d.size());
    return r;
}

double median(std::vector<double> v) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

} // namespace

bool parseOptimisticBenchArg(int argc, char** argv, int& i, bool& run, OptimisticBenchOptions& o) {
    const std::string a = argv[i];
    auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
    if (a == "--bench-optimistic") run = true;
    else if (a == "--bench-optimistic-sizes") {
        o.sizesMillions.clear();
        std::stringstream ss(next());
        for (std::string s; std::getline(ss, s, ',');)
            if (!s.empty()) o.sizesMillions.push_back(std::atof(s.c_str()));
    } else if (a == "--bench-optimistic-out") o.outPath = next();
    else if (a == "--bench-optimistic-shots") o.shotsDir = next();
    else return false;
    return true;
}

int runOptimisticBench(const OptimisticBenchOptions& o) {
    if (simulatedFloorTier()) ThreadPool::setSharedThreadCount(4); // SF_HW_SIMULATE=floor: the solver on 4 cores
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_Window* window = SDL_CreateWindow("SculptForge optimistic strokes", 320, 200, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    SDL_GLContext context = window ? SDL_GL_CreateContext(window) : nullptr;
    if (!context) {
        std::fprintf(stderr, "OpenGL 3.3 context creation failed: %s\n", SDL_GetError());
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(window, context);
    int code = 0;
    std::string error;
    Renderer renderer;
    if (const char* missing = gl::load([](const char* n) { return reinterpret_cast<gl::GenericProc>(SDL_GL_GetProcAddress(n)); })) {
        std::fprintf(stderr, "Missing OpenGL function: %s\n", missing);
        code = 1;
    } else if (!renderer.init(error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        code = 1;
    }
    ScreenStroke pass; // the live pass (render/ScreenStroke.h)
    Timer timer;
    const bool gpuTimer = code == 0 && timer.load();
    std::ostringstream md;
    md << "## Optimistic screen-space strokes (N1), " << reinterpret_cast<const char*>(gl::GetString(GL_RENDERER)) << "\n\n"
       << "Canvas " << kW << "x" << kH << ", Standard, " << o.dabs << " dabs of " << o.radiusPx << " px, strength 0.5.\n\n"
       << "| Mesh | pass per dab: GPU median / max (wall median) [GPU busy, median; last 8 dabs] | whole stroke on the canvas | mesh: Standard dabs (CPU) + upload | "
          "interior pop: optimistic vs truth (mean / p99 levels, > 8) | interior, doing nothing vs truth | pixels | "
          "outline (coverage changed): pixels, pop vs doing nothing (mean) |\n"
       << "|---|---:|---:|---:|---:|---:|---:|---:|\n";
    std::string ghostRows;
    for (double sizeM : o.sizesMillions) {
        if (code) break;
        Document doc;
        std::fprintf(stderr, "core-200m N1: %gM head...\n", sizeM);
        doc.mesh = makeBenchHead(size_t(sizeM * 1e6));
        doc.bvh.build(doc.mesh);
        Mesh& m = doc.mesh;
        Camera cam;
        cam.aspect = float(kW) / float(kH);
        cam.frame(m.bounds());
        RenderSettings rs;
        std::vector<SceneMesh> scene{SceneMesh{1, &m, true, true}};
        renderer.syncScene(scene);
        Target canvas, out;
        canvas.make(true);
        out.make(false);
        gl::BindFramebuffer(GL_FRAMEBUFFER, canvas.fbo);
        gl::Viewport(0, 0, kW, kH);
        renderer.draw(cam, kW, kH, rs);
        gl::Finish();
        const std::vector<uint8_t> before = readColor(canvas);
        std::vector<float> depth(size_t(kW) * kH);
        gl::BindFramebuffer(GL_FRAMEBUFFER, canvas.fbo);
        gl::ReadPixels(0, 0, kW, kH, kDepthComponent, GL_FLOAT, depth.data());
        gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
        const glm::mat4 view = cam.view(), proj = cam.projection(), invProj = glm::inverse(proj), invView = glm::inverse(view);
        auto viewAt = [&](float x, float y) {
            const int ix = std::clamp(int(x), 0, kW - 1), iy = std::clamp(int(y), 0, kH - 1);
            const float d = depth[size_t(iy) * kW + ix];
            glm::vec4 q = invProj * glm::vec4((x + 0.5f) / kW * 2 - 1, (y + 0.5f) / kH * 2 - 1, d * 2 - 1, 1);
            return std::pair<glm::vec3, bool>(glm::vec3(q) / q.w, d < 1.0f);
        };
        // The stroke, from the canvas alone: a line across the face; each dab's centre from the depth under the
        // pointer, its radius from the pixel size there, its area normal from the depth on a ring around it.
        struct Dab {
            glm::vec3 cView, nView;
            float radius;
        };
        std::vector<Dab> dabs;
        for (int i = 0; i < std::min(o.dabs, kMaxDabs); ++i) {
            const float u = float(i) / float(std::max(1, o.dabs - 1));
            const float x = kW * (0.38f + 0.24f * u), y = kH * (0.55f + 0.04f * std::sin(u * 6.2831853f));
            auto [c, hit] = viewAt(x, y);
            if (!hit) continue;
            const float pxWorld = -c.z * 2.0f * std::tan(cam.fovY * 0.5f) / float(kH);
            const float radius = o.radiusPx * pxWorld;
            glm::vec3 nsum(0.0f);
            for (int k = 0; k < 8; ++k) { // ring at half the radius: the area normal as the solver's averages it
                const float a0 = 6.2831853f * k / 8, a1 = 6.2831853f * (k + 1) / 8, rr = 0.5f * o.radiusPx;
                auto [p0, h0] = viewAt(x + rr * std::cos(a0), y + rr * std::sin(a0));
                auto [p1, h1] = viewAt(x + rr * std::cos(a1), y + rr * std::sin(a1));
                if (h0 && h1) nsum += glm::cross(p0 - c, p1 - c);
            }
            glm::vec3 n = glm::length(nsum) > 0 ? glm::normalize(nsum) : glm::vec3(0, 0, 1);
            if (glm::dot(n, c) > 0) n = -n;
            dabs.push_back({c, n, radius});
        }
        // Optimistic: each dab one live pass (render/ScreenStroke) over its rectangle, the dabs so far that reach it
        // evaluated, reading the drawn canvas and writing the output; GPU time from a timer query, wall time around
        // glFinish (an upper bound, as before).
        gl::BindFramebuffer(GL_READ_FRAMEBUFFER, canvas.fbo);
        gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, out.fbo);
        gl::BlitFramebuffer(0, 0, kW, kH, 0, 0, kW, kH, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        GhostStroke stroke;
        stroke.kind = GhostKind::Standard;
        const GLuint litsphere = renderer.litsphereTexture(rs.litsphere);
        const glm::mat3 lightRot = lightRotation(rs.lightYaw, rs.lightPitch);
        std::vector<double> passMs, gpuMs, warmMs;
        const auto all0 = Clock::now();
        for (size_t i = 0; i < dabs.size(); ++i) {
            GhostDab g;
            g.center = glm::vec3(invView * glm::vec4(dabs[i].cView, 1.0f));
            g.normal = glm::normalize(glm::vec3(invView * glm::vec4(dabs[i].nView, 0.0f)));
            g.areaCenter = g.center;
            g.radius = dabs[i].radius;
            g.strength = 0.5f;
            stroke.dabs.push_back(g);
            // The dab's screen rectangle (its radius projected at its depth, plus a margin).
            const glm::vec4 cc = proj * glm::vec4(dabs[i].cView, 1.0f);
            const float sx = (cc.x / cc.w * 0.5f + 0.5f) * kW, sy = (cc.y / cc.w * 0.5f + 0.5f) * kH;
            const int rp = int(std::ceil(o.radiusPx * kReach)) + 2; // (the surface may grow past the radius)
            const glm::ivec4 rect(int(sx) - rp, int(sy) - rp, int(sx) + rp, int(sy) + rp);
            gl::Finish();
            const auto t = Clock::now();
            if (gpuTimer) timer.begin(kTimeElapsed, timer.q);
            if (!pass.apply(stroke, canvas.fbo, canvas.fbo, out.fbo, kW, kH, view, proj, glm::vec4(0, 0, kW, kH), litsphere, lightRot, &rect))
                std::fprintf(stderr, "core-200m N1: dab %zu not drawn (litsphere %u)\n", i, litsphere);
            if (gpuTimer) timer.end(kTimeElapsed);
            gl::Finish();
            passMs.push_back(msSince(t));
            if (gpuTimer) {
                GLuint64 ns = 0;
                timer.result(timer.q, kQueryResult, &ns);
                gpuMs.push_back(double(ns) / 1e6);
                // Warm: the same pass 5 times back to back (the GPU busy, as inside a frame), per pass.
                timer.begin(kTimeElapsed, timer.q);
                for (int k = 0; k < 5; ++k)
                    pass.apply(stroke, canvas.fbo, canvas.fbo, out.fbo, kW, kH, view, proj, glm::vec4(0, 0, kW, kH), litsphere, lightRot, &rect);
                timer.end(kTimeElapsed);
                timer.result(timer.q, kQueryResult, &ns);
                warmMs.push_back(double(ns) / 5e6);
            }
        }
        const double allMs = msSince(all0);
        const std::vector<uint8_t> optimistic = readColor(out);
        // Live, the pass draws only the dabs the solver hasn't placed yet: the same passes with the last 8 dabs
        // (after the image is read: they draw over it).
        std::vector<double> windowMs;
        if (gpuTimer)
            for (size_t i = 0; i < stroke.dabs.size(); ++i) {
                GhostStroke w = stroke;
                w.dabs.assign(stroke.dabs.begin() + std::ptrdiff_t(i + 1 > 8 ? i + 1 - 8 : 0), stroke.dabs.begin() + std::ptrdiff_t(i + 1));
                const glm::vec4 cc = proj * glm::vec4(dabs[i].cView, 1.0f);
                const float sx = (cc.x / cc.w * 0.5f + 0.5f) * kW, sy = (cc.y / cc.w * 0.5f + 0.5f) * kH;
                const int rp = int(std::ceil(o.radiusPx * kReach)) + 2;
                const glm::ivec4 rect(int(sx) - rp, int(sy) - rp, int(sx) + rp, int(sy) + rp);
                GLuint64 ns = 0;
                timer.begin(kTimeElapsed, timer.q);
                for (int k = 0; k < 5; ++k)
                    pass.apply(w, canvas.fbo, canvas.fbo, out.fbo, kW, kH, view, proj, glm::vec4(0, 0, kW, kH), litsphere, lightRot, &rect);
                timer.end(kTimeElapsed);
                timer.result(timer.q, kQueryResult, &ns);
                windowMs.push_back(double(ns) / 5e6);
            }
        // The geometry preview (render/Ghost.h, Background Preview): the same dabs moved in the vertex shader. Its
        // cost is the frame with it less the frame without (median of 5 each).
        const GhostStroke& ghost = stroke;
        auto frameMs = [&](int repeats) {
            std::vector<double> t;
            for (int k = 0; k < repeats; ++k) {
                gl::BindFramebuffer(GL_FRAMEBUFFER, canvas.fbo);
                gl::Viewport(0, 0, kW, kH);
                gl::Finish();
                const auto t0 = Clock::now();
                renderer.draw(cam, kW, kH, rs);
                gl::Finish();
                t.push_back(msSince(t0));
            }
            return median(t);
        };
        const double plainMs = frameMs(5);
        renderer.setGhost(1, ghost);
        const double ghostFrameMs = frameMs(5);
        const std::vector<uint8_t> ghostImage = readColor(canvas);
        renderer.setGhost(1, GhostStroke());
        // Truth: the same dabs by the Standard brush on the mesh (world space), then drawn.
        Sculptor& sc = doc.sculptor;
        Symmetry sym;
        sym.x = false;
        const auto s0 = Clock::now();
        sc.beginStroke(m, "Standard");
        for (const Dab& d : dabs) {
            DabParams p;
            p.center = glm::vec3(invView * glm::vec4(d.cView, 1.0f));
            p.viewDir = glm::normalize(glm::vec3(invView * glm::vec4(d.cView, 0.0f)));
            p.radius = d.radius;
            p.strength = 0.5f;
            sc.dab(m, doc.bvh, BrushType::Standard, p, sym);
        }
        sc.endStroke();
        renderer.syncScene(scene);
        gl::Finish();
        const double solveMs = msSince(s0);
        gl::BindFramebuffer(GL_FRAMEBUFFER, canvas.fbo);
        gl::Viewport(0, 0, kW, kH);
        renderer.draw(cam, kW, kH, rs);
        gl::Finish();
        const std::vector<uint8_t> truth = readColor(canvas);
        std::vector<float> depthAfter(depth.size());
        gl::BindFramebuffer(GL_FRAMEBUFFER, canvas.fbo);
        gl::ReadPixels(0, 0, kW, kH, kDepthComponent, GL_FLOAT, depthAfter.data());
        gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
        // Interior: surface before and after; outline: the stroke changed what covers the pixel (a re-shade can't).
        std::vector<uint8_t> interior(depth.size()), outline(depth.size());
        for (size_t i = 0; i < depth.size(); ++i) {
            const bool a = depth[i] < 1.0f, b = depthAfter[i] < 1.0f;
            interior[i] = a && b;
            outline[i] = a != b;
        }
        const Diff pop = diff(optimistic, truth, before, &interior), none = diff(before, truth, before, &interior);
        const Diff popEdge = diff(optimistic, truth, before, &outline), noneEdge = diff(before, truth, before, &outline);
        const Diff ghostPop = diff(ghostImage, truth, before, &interior), ghostEdge = diff(ghostImage, truth, before, &outline);
        char row[640];
        std::snprintf(row, sizeof row, "| %.1fM | %.2f / %.2f ms (%.2f) [%.2f; %.2f] | %.1f ms | %.0f ms | %.1f / %.0f, %.1f%% | %.1f / %.0f, %.1f%% | %zu | %zu: %.1f vs %.1f |\n",
                      double(m.triangleCount()) / 1e6, median(gpuMs), gpuMs.empty() ? 0.0 : *std::max_element(gpuMs.begin(), gpuMs.end()),
                      median(passMs), median(warmMs), median(windowMs), allMs,
                      solveMs, pop.mean, pop.p99, pop.over8 * 100, none.mean, none.p99, none.over8 * 100, none.pixels, popEdge.pixels, popEdge.mean, noneEdge.mean);
        md << row;
        std::fprintf(stderr, "%s", row);
        char grow[256];
        std::snprintf(grow, sizeof grow, "| %.1fM | %.2f ms (frame %.2f vs %.2f) | %.1f / %.0f, %.1f%% | %.1f |\n", double(m.triangleCount()) / 1e6,
                      ghostFrameMs - plainMs, ghostFrameMs, plainMs, ghostPop.mean, ghostPop.p99, ghostPop.over8 * 100, ghostEdge.mean);
        ghostRows += grow;
        if (!o.shotsDir.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(o.shotsDir, ec);
            char tag[32];
            std::snprintf(tag, sizeof tag, "%gm", sizeM);
            savePng(before, o.shotsDir + "/n1-" + tag + "-before.png");
            savePng(optimistic, o.shotsDir + "/n1-" + tag + "-optimistic.png");
            savePng(truth, o.shotsDir + "/n1-" + tag + "-truth.png");
            std::vector<uint8_t> heat(truth.size(), 255); // |optimistic - truth| x 4, red on white
            for (size_t i = 0; i < truth.size(); i += 4) {
                int dmax = 0;
                for (int k = 0; k < 3; ++k) dmax = std::max(dmax, std::abs(int(optimistic[i + k]) - int(truth[i + k])));
                const uint8_t v = uint8_t(255 - std::min(255, dmax * 4));
                heat[i + 1] = heat[i + 2] = v;
            }
            savePng(heat, o.shotsDir + "/n1-" + tag + "-pop.png");
            savePng(ghostImage, o.shotsDir + "/n1-" + tag + "-ghost.png");
        }
        canvas.release();
        out.release();
        renderer.syncScene({});
    }
    md << "\nThe geometry preview (Background Preview's ghost) on the same 30 dabs:\n\n| Mesh | extra cost on a frame "
          "(frame with / without) | interior pop vs truth (mean / p99, > 8) | outline pop (mean) |\n|---|---:|---:|---:|\n"
       << ghostRows;
    md << "\nPass: the live pass (render/ScreenStroke: field, then warp) over the dab's rectangle with the dabs so far "
          "that reach it (its cost is the brush's pixels, not the mesh). Mesh: the Standard brush on the CPU plus the upload, i.e. what the optimistic canvas hides. Pop: "
          "per-pixel max channel difference over the pixels the stroke changed in either image.\n";
    pass.release();
    if (gpuTimer) timer.del(1, &timer.q);
    renderer.shutdown();
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (o.outPath.empty()) std::printf("%s", md.str().c_str());
    else std::ofstream(o.outPath) << md.str();
    return code;
}

} // namespace sf
