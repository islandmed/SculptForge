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

// One pass over a dab's screen rectangle: every dab of the stroke so far displaces the canvas's surface point along
// its area normal by the Standard kernel (core/Sculpt.cpp, as render/Ghost.cpp's ghostMove), the normal bends by the
// kernels' gradients, and the colour is re-shaded by the ratio of the Lit Sphere lookups (the mesh shader's, view
// space) at the bent and the original normal, so cavity, material and paint stay as drawn.
const char* kVS = R"(#version 330 core
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";
const char* kFS = R"(#version 330 core
uniform sampler2D uColor, uDepth, uMatcapTex;
uniform mat4 uInvProj, uProj;
uniform vec2 uProjScale, uSize;
uniform int uCount;
uniform vec4 uDabC[64]; // view-space centre, radius
uniform vec4 uDabN[64]; // view-space area normal, amplitude (strength x radius x 0.06)
out vec4 fragColor;
bool surface(ivec2 p) { return texelFetch(uDepth, clamp(p, ivec2(0), ivec2(uSize) - 1), 0).r < 1.0; }
vec3 viewPos(ivec2 p) {
    p = clamp(p, ivec2(0), ivec2(uSize) - 1);
    float d = texelFetch(uDepth, p, 0).r;
    vec4 q = uInvProj * vec4((vec2(p) + 0.5) / uSize * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    return q.xyz / q.w;
}
vec3 lit(vec3 n, vec3 v) {
    vec3 bx = normalize(vec3(v.z, 0.0, -v.x));
    vec3 by = cross(v, bx);
    vec2 uv = vec2(dot(bx, n), dot(by, n)) * 0.495 + 0.5;
    return pow(texture(uMatcapTex, uv).rgb, vec3(2.2));
}
// The displacement of the surface point P by every dab (the Standard kernel along each area normal), and the
// tangential gradient that bends a normal n.
vec3 displace(vec3 P, vec3 n, out vec3 g) {
    vec3 m = vec3(0.0);
    g = vec3(0.0);
    for (int i = 0; i < uCount; ++i) {
        vec3 o = P - uDabC[i].xyz;
        float dist = length(o), rad = uDabC[i].w;
        if (dist >= rad || dist < 1e-9) continue;
        float t = 1.0 - dist / rad;
        m += uDabN[i].xyz * (uDabN[i].w * t * t * (3.0 - 2.0 * t));
        vec3 grad = uDabN[i].w * (-(6.0 * t - 6.0 * t * t) / rad) * o / dist;
        g += (grad - n * dot(grad, n)) * dot(uDabN[i].xyz, n);
    }
    return m;
}
// The surface normal from depth, smoothed: central differences over baselines of 1-3 pixels summed (the canvas holds
// the mesh's facets, the mesh shader interpolates vertex normals), each pair skipped across a depth jump.
vec3 normalAt(ivec2 p) {
    vec3 P = viewPos(p), sum = vec3(0.0);
    for (int k = 1; k <= 3; ++k) {
        vec3 l = viewPos(p - ivec2(k, 0)), r = viewPos(p + ivec2(k, 0)), d = viewPos(p - ivec2(0, k)), u = viewPos(p + ivec2(0, k));
        float jump = 0.05 * abs(P.z);
        if (abs(r.z - l.z) > jump || abs(u.z - d.z) > jump) continue;
        sum += cross(r - l, u - d);
    }
    if (dot(sum, sum) < 1e-30) { // (at an edge: the one-pixel step on the smaller side)
        vec3 l = viewPos(p - ivec2(1, 0)), r = viewPos(p + ivec2(1, 0)), d = viewPos(p - ivec2(0, 1)), u = viewPos(p + ivec2(0, 1));
        sum = cross(abs(r.z - P.z) < abs(P.z - l.z) ? r - P : P - l, abs(u.z - P.z) < abs(P.z - d.z) ? u - P : P - d);
    }
    vec3 n = normalize(sum);
    return dot(n, P) > 0.0 ? -n : n;
}
vec2 toPixel(vec3 P) {
    vec4 c = uProj * vec4(P, 1.0);
    return (c.xy / c.w * 0.5 + 0.5) * uSize - 0.5;
}
void main() {
    vec2 p = gl_FragCoord.xy - 0.5;
    // Inverse warp: the canvas pixel q whose displaced surface point lands on p (fixed point q = p - offset(q)),
    // from p and, for pixels the surface may grow over, from points towards the nearest dab.
    vec2 best = vec2(-1.0);
    vec2 toward = vec2(0.0);
    float near = 1e30;
    for (int i = 0; i < uCount; ++i) {
        vec2 c = toPixel(uDabC[i].xyz);
        if (distance(c, p) < near) near = distance(c, p), toward = c - p;
    }
    float bestErr = 4.0; // (accept a landing within 2 px; the closest one found)
    for (int s = 0; s < 4 && bestErr > 0.04; ++s) {
        vec2 q = p + toward * (0.12 * float(s));
        for (int k = 0; k < 16; ++k) {
            ivec2 iq = ivec2(floor(q + 0.5));
            if (!surface(iq)) break;
            vec3 g;
            vec3 P = viewPos(iq);
            vec2 err = toPixel(P + displace(P, vec3(0.0), g)) - p; // (the bend isn't needed here)
            float e = dot(err, err);
            if (e < bestErr) bestErr = e, best = q - err;
            if (e < 0.04) break;
            q -= err * (k < 8 ? 1.0 : 0.5); // (damped later: where the warp folds, a full step can oscillate)
        }
    }
    ivec2 ip = ivec2(p);
    if (best.x < 0.0) {
        if (!surface(ip)) { fragColor = texelFetch(uColor, ip, 0); return; } // background stays
        best = p; // (no landing point found: shade in place)
    }
    ivec2 iq = ivec2(floor(best + 0.5));
    vec3 n0 = normalAt(iq);
    vec3 g;
    displace(viewPos(iq), n0, g);
    vec3 n1 = normalize(n0 - g);
    vec2 ndc = (gl_FragCoord.xy / uSize) * 2.0 - 1.0;
    vec3 v = normalize(vec3(-ndc / uProjScale, 1.0)); // the mesh shader's viewRay
    vec3 ratio = lit(n1, v) / max(lit(n0, v), vec3(1e-3));
    vec4 c = texture(uColor, (best + 0.5) / uSize); // (bilinear at the landing point: no stair-steps where it stretches)
    fragColor = vec4(pow(pow(c.rgb, vec3(2.2)) * ratio, vec3(1.0 / 2.2)), c.a);
}
)";

GLuint compile(GLenum type, const char* src, std::string& error) {
    GLuint s = gl::CreateShader(type);
    gl::ShaderSource(s, 1, &src, nullptr);
    gl::CompileShader(s);
    GLint ok = 0;
    gl::GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048] = {};
        gl::GetShaderInfoLog(s, sizeof log, nullptr, log);
        error = log;
    }
    return s;
}

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
    GLuint prog = 0, vao = 0;
    if (code == 0) {
        GLuint vs = compile(GL_VERTEX_SHADER, kVS, error), fs = compile(GL_FRAGMENT_SHADER, kFS, error);
        prog = gl::CreateProgram();
        gl::AttachShader(prog, vs);
        gl::AttachShader(prog, fs);
        gl::LinkProgram(prog);
        GLint ok = 0;
        gl::GetProgramiv(prog, GL_LINK_STATUS, &ok);
        gl::DeleteShader(vs);
        gl::DeleteShader(fs);
        if (!ok) {
            std::fprintf(stderr, "optimistic pass: %s\n", error.c_str());
            code = 1;
        }
        gl::GenVertexArrays(1, &vao);
    }
    std::ostringstream md;
    md << "## Optimistic screen-space strokes (N1), " << reinterpret_cast<const char*>(gl::GetString(GL_RENDERER)) << "\n\n"
       << "Canvas " << kW << "x" << kH << ", Standard, " << o.dabs << " dabs of " << o.radiusPx << " px, strength 0.5.\n\n"
       << "| Mesh | pass per dab (GPU, median / max) | whole stroke on the canvas | mesh: Standard dabs (CPU) + upload | "
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
        // Optimistic: each dab one pass over its rectangle (all dabs so far evaluated), timed GPU-side by Finish.
        gl::BindFramebuffer(GL_READ_FRAMEBUFFER, canvas.fbo);
        gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, out.fbo);
        gl::BlitFramebuffer(0, 0, kW, kH, 0, 0, kW, kH, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        gl::UseProgram(prog);
        gl::BindVertexArray(vao);
        gl::Uniform1i(gl::GetUniformLocation(prog, "uColor"), 0);
        gl::Uniform1i(gl::GetUniformLocation(prog, "uDepth"), 1);
        gl::Uniform1i(gl::GetUniformLocation(prog, "uMatcapTex"), 2);
        gl::UniformMatrix4fv(gl::GetUniformLocation(prog, "uInvProj"), 1, GL_FALSE, glm::value_ptr(invProj));
        gl::UniformMatrix4fv(gl::GetUniformLocation(prog, "uProj"), 1, GL_FALSE, glm::value_ptr(proj));
        gl::Uniform2f(gl::GetUniformLocation(prog, "uProjScale"), proj[0][0], proj[1][1]);
        gl::Uniform2f(gl::GetUniformLocation(prog, "uSize"), float(kW), float(kH));
        gl::ActiveTexture(GL_TEXTURE0 + 1);
        gl::BindTexture(GL_TEXTURE_2D, canvas.depth);
        gl::ActiveTexture(GL_TEXTURE0 + 2);
        gl::BindTexture(GL_TEXTURE_2D, renderer.matcapTexture(rs.matcap));
        gl::ActiveTexture(GL_TEXTURE0);
        gl::BindTexture(GL_TEXTURE_2D, canvas.color); // (each pass re-shades its rectangle from the drawn canvas, all dabs so far)
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GLint(GL_LINEAR));
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GLint(GL_LINEAR));
        std::vector<glm::vec4> uc, un;
        std::vector<double> passMs;
        const auto all0 = Clock::now();
        gl::Enable(GL_SCISSOR_TEST);
        for (size_t i = 0; i < dabs.size(); ++i) {
            uc.push_back(glm::vec4(dabs[i].cView, dabs[i].radius));
            un.push_back(glm::vec4(dabs[i].nView, 0.5f * dabs[i].radius * 0.06f));
            gl::Finish();
            const auto t = Clock::now();
            gl::Uniform1i(gl::GetUniformLocation(prog, "uCount"), GLint(uc.size()));
            gl::Uniform4fv(gl::GetUniformLocation(prog, "uDabC"), GLsizei(uc.size()), glm::value_ptr(uc[0]));
            gl::Uniform4fv(gl::GetUniformLocation(prog, "uDabN"), GLsizei(un.size()), glm::value_ptr(un[0]));
            // The dab's screen rectangle (its radius projected at its depth, plus a margin).
            const glm::vec4 cc = proj * glm::vec4(dabs[i].cView, 1.0f);
            const float sx = (cc.x / cc.w * 0.5f + 0.5f) * kW, sy = (cc.y / cc.w * 0.5f + 0.5f) * kH;
            const int rp = int(std::ceil(o.radiusPx * 1.5f)) + 2; // (the surface may grow past the radius)
            gl::BindFramebuffer(GL_FRAMEBUFFER, out.fbo);
            gl::Viewport(0, 0, kW, kH);
            gl::Scissor(int(sx) - rp, int(sy) - rp, 2 * rp, 2 * rp);
            gl::DrawArrays(GL_TRIANGLES, 0, 3);
            gl::Finish();
            passMs.push_back(msSince(t));
        }
        gl::Disable(GL_SCISSOR_TEST);
        const double allMs = msSince(all0);
        gl::UseProgram(0);
        const std::vector<uint8_t> optimistic = readColor(out);
        // The geometry preview (render/Ghost.h, Background Preview): the same dabs moved in the vertex shader. Its
        // cost is the frame with it less the frame without (median of 5 each).
        GhostStroke ghost;
        ghost.kind = GhostKind::Standard;
        for (const Dab& d : dabs) {
            GhostDab g;
            g.center = glm::vec3(invView * glm::vec4(d.cView, 1.0f));
            g.normal = glm::normalize(glm::vec3(invView * glm::vec4(d.nView, 0.0f)));
            g.areaCenter = g.center;
            g.radius = d.radius;
            g.strength = 0.5f;
            ghost.dabs.push_back(g);
        }
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
        char row[512];
        std::snprintf(row, sizeof row, "| %.1fM | %.2f / %.2f ms | %.1f ms | %.0f ms | %.1f / %.0f, %.1f%% | %.1f / %.0f, %.1f%% | %zu | %zu: %.1f vs %.1f |\n",
                      double(m.triangleCount()) / 1e6, median(passMs), *std::max_element(passMs.begin(), passMs.end()), allMs,
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
    md << "\nPass: one GPU pass over the dab's rectangle evaluating every dab so far (its cost is the brush's pixels, not "
          "the mesh). Mesh: the Standard brush on the CPU plus the upload, i.e. what the optimistic canvas hides. Pop: "
          "per-pixel max channel difference over the pixels the stroke changed in either image.\n";
    if (prog) gl::DeleteProgram(prog);
    if (vao) gl::DeleteVertexArrays(1, &vao);
    renderer.shutdown();
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (o.outPath.empty()) std::printf("%s", md.str().c_str());
    else std::ofstream(o.outPath) << md.str();
    return code;
}

} // namespace sf
