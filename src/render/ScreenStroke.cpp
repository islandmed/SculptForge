// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/ScreenStroke.h"

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace sf {

namespace {

constexpr GLenum kDepthComponent = 0x1902;
constexpr GLbitfield kDepthBit = 0x00000100;
constexpr GLenum kRgba16f = 0x881A, kSamples = 0x80A9;

// Keep the kernel in step with ghostMove (render/Ghost.cpp, Standard) and the shading with the mesh shader's Lit
// Sphere branch (Renderer.cpp); tools/optimistic bench: app/OptimisticBench.cpp.
//
// Two passes over the dabs' rectangle. Field: for each drawn pixel q, where its displaced surface point lands
// (an offset in pixels) and its bent normal, every dab evaluated once with its gradient in closed form. Warp: for
// each pixel p the q that lands there, by fixed-point iteration over the field (one fetch a step), re-shaded by the
// ratio of the Lit Sphere lookups at the bent and the original normal.
const char* kCommon = R"(
uniform sampler2D uDepth; // the view's depth around the pass (pixel p of the view at p - uOrigin, uLocal of it)
uniform mat4 uInvProj, uProj;
uniform vec4 uViewport;   // x, y, w, h in target pixels
uniform ivec2 uOrigin, uLocal;
ivec2 local(ivec2 p) { return clamp(p - uOrigin, ivec2(0), uLocal - 1); }
bool surface(ivec2 p) { return texelFetch(uDepth, local(p), 0).r < 1.0; }
vec3 viewPos(ivec2 p) {
    p = uOrigin + local(p);
    float d = texelFetch(uDepth, p - uOrigin, 0).r;
    vec2 ndc = (vec2(p) + 0.5 - uViewport.xy) / uViewport.zw * 2.0 - 1.0;
    vec4 q = uInvProj * vec4(ndc, d * 2.0 - 1.0, 1.0);
    return q.xyz / q.w;
}
vec2 toPixel(vec3 P) {
    vec4 c = uProj * vec4(P, 1.0);
    return uViewport.xy + (c.xy / c.w * 0.5 + 0.5) * uViewport.zw - 0.5;
}
// The surface normal from depth, smoothed over baselines of 1-3 pixels (the view holds facets, the mesh shader
// interpolates vertex normals), each pair skipped across a depth jump.
vec3 normalAt(ivec2 p) {
    vec3 P = viewPos(p), sum = vec3(0.0);
    for (int k = 1; k <= 3; ++k) {
        vec3 l = viewPos(p - ivec2(k, 0)), r = viewPos(p + ivec2(k, 0)), d = viewPos(p - ivec2(0, k)), u = viewPos(p + ivec2(0, k));
        float jump = 0.05 * abs(P.z);
        if (abs(r.z - l.z) > jump || abs(u.z - d.z) > jump) continue;
        sum += cross(r - l, u - d);
    }
    if (dot(sum, sum) < 1e-30) {
        vec3 l = viewPos(p - ivec2(1, 0)), r = viewPos(p + ivec2(1, 0)), d = viewPos(p - ivec2(0, 1)), u = viewPos(p + ivec2(0, 1));
        sum = cross(abs(r.z - P.z) < abs(P.z - l.z) ? r - P : P - l, abs(u.z - P.z) < abs(P.z - d.z) ? u - P : P - d);
    }
    vec3 n = normalize(sum);
    return dot(n, P) > 0.0 ? -n : n;
}
vec2 signs(vec2 v) { return vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0); }
vec2 octEncode(vec3 n) {
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    return n.z >= 0.0 ? n.xy : (1.0 - abs(n.yx)) * signs(n.xy);
}
vec3 octDecode(vec2 e) {
    vec3 n = vec3(e, 1.0 - abs(e.x) - abs(e.y));
    if (n.z < 0.0) n.xy = (1.0 - abs(n.yx)) * signs(n.xy);
    return normalize(n);
}
)";

const char* kVS = R"(#version 330 core
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

const char* kFieldFS = R"(
uniform float uFocal;
uniform int uCount;
uniform int uKind;      // 1 Standard, 2 Clay (GhostKind order is not used: the shader's own)
uniform vec4 uDabC[64]; // view-space centre, radius
uniform vec4 uDabN[64]; // view-space area normal, signed strength (strength, negative inverted)
uniform vec4 uDabA[64]; // view-space area centre, weight
out vec4 fragColor;     // xy: where the pixel's surface point lands, in pixels from it; zw: its bent normal (octahedral),
                        // or 9 where no dab reaches it
// How far dab i moves P along its area normal (ghostMove's Standard and Clay), and the gradient of that in P.
float amount(int i, vec3 P, out vec3 grad) {
    grad = vec3(0.0);
    vec3 d = P - uDabC[i].xyz;
    float dist = length(d), rad = uDabC[i].w;
    if (dist >= rad) return 0.0;
    float t = 1.0 - dist / rad, u = t, dudt = 1.0;
    if (uFocal != 0.0) {
        float e = exp2(2.0 * clamp(uFocal, -1.0, 1.0));
        u = pow(t, e), dudt = e * pow(max(t, 1e-3), e - 1.0);
    }
    float w = uDabA[i].w, f = u * u * (3.0 - 2.0 * u) * w;
    vec3 dfdP = 6.0 * u * (1.0 - u) * dudt * (-1.0 / rad) * w * (dist > 0.0 ? d / dist : vec3(0.0));
    float s = uDabN[i].w;
    if (uKind == 1) {
        grad = s * rad * 0.06 * dfdP;
        return s * rad * 0.06 * f;
    }
    vec3 n = uDabN[i].xyz;
    float h = dot(P - (uDabA[i].xyz + n * (rad * 0.05 * s)), n); // Clay: up to its plane
    if (!(s > 0.0 ? h < 0.0 : h > 0.0)) return 0.0;
    grad = -(n * f + h * dfdP);
    return -h * f;
}
void main() {
    ivec2 q = ivec2(gl_FragCoord.xy) + uOrigin; // (drawn into the local field)
    if (!surface(q)) {
        fragColor = vec4(0.0);
        return;
    }
    vec3 P = viewPos(q);
    bool touched = false;
    for (int i = 0; i < uCount && !touched; ++i) touched = distance(P, uDabC[i].xyz) < uDabC[i].w;
    if (!touched) { // (most of the rectangle: nothing moves it, the warp passes it through)
        fragColor = vec4(0.0, 0.0, 9.0, 9.0);
        return;
    }
    vec3 n0 = normalAt(q), m = vec3(0.0), g = vec3(0.0);
    for (int i = 0; i < uCount; ++i) {
        vec3 grad;
        float a = amount(i, P, grad);
        m += uDabN[i].xyz * a;
        g += (grad - n0 * dot(grad, n0)) * dot(uDabN[i].xyz, n0); // the move's slope along the surface bends the normal
    }
    fragColor = vec4(toPixel(P + m) - vec2(q), octEncode(normalize(n0 - g)));
}
)";

const char* kWarpFS = R"(
uniform sampler2D uColor, uField, uLitSphereTex;
uniform mat3 uLightRot;
uniform vec2 uProjScale;
uniform vec4 uRect;      // the field's pixels: x0, y0, x1, y1 (outside it nothing moves)
uniform int uCount;
uniform vec4 uDabPx[64]; // the dabs' centres in pixels (xy)
out vec4 fragColor;
bool inField(ivec2 p) { return p.x >= int(uRect.x) && p.y >= int(uRect.y) && p.x < int(uRect.z) && p.y < int(uRect.w); }
// Where the surface at q lands, from q (interpolated between the field's pixels, so the iteration converges below a
// pixel; outside the field nothing moves).
vec2 offsetAt(vec2 q) {
    return inField(ivec2(floor(q + 0.5))) ? texture(uField, (q - vec2(uOrigin) + 0.5) / vec2(textureSize(uField, 0))).xy : vec2(0.0);
}
vec3 lit(vec3 n, vec3 v) {
    vec3 bx = normalize(vec3(v.z, 0.0, -v.x));
    vec3 by = cross(v, bx);
    vec3 ln = transpose(uLightRot) * n;
    vec2 uv = vec2(dot(bx, ln), dot(by, ln)) * 0.495 + 0.5;
    return pow(texture(uLitSphereTex, uv).rgb, vec3(2.2));
}
void main() {
    vec2 p = gl_FragCoord.xy - 0.5;
    ivec2 ip = ivec2(p);
    // Inverse warp: the drawn pixel q whose displaced surface point lands on p (fixed point q = p - offset(q)), from
    // p and from points towards the nearest dab (where the surface grows over the background); the closest landing.
    vec2 toward = vec2(0.0);
    float near = 1e30;
    for (int i = 0; i < uCount; ++i)
        if (distance(uDabPx[i].xy, p) < near) near = distance(uDabPx[i].xy, p), toward = uDabPx[i].xy - p;
    vec2 best = vec2(-1.0);
    float bestErr = 4.0;
    for (int s = 0; s < 4 && bestErr > 0.04; ++s) {
        vec2 q = p + toward * (0.12 * float(s));
        for (int k = 0; k < 16; ++k) {
            if (!surface(ivec2(floor(q + 0.5)))) break;
            vec2 err = q + offsetAt(q) - p;
            float e = dot(err, err);
            if (e < bestErr) bestErr = e, best = q - err;
            if (e < 0.04) break;
            q -= err * (k < 8 ? 1.0 : 0.5);
        }
    }
    if (best.x < 0.0) {
        if (!surface(ip)) discard; // background stays as drawn
        best = p;
    }
    ivec2 iq = ivec2(floor(best + 0.5));
    vec4 f = inField(iq) ? texelFetch(uField, iq - uOrigin, 0) : vec4(0.0, 0.0, 9.0, 9.0);
    if (f.z > 2.0 && iq == ip) discard; // untouched and not moved: as drawn
    vec3 n0 = normalAt(iq), n1 = f.z > 2.0 ? n0 : octDecode(f.zw);
    vec2 ndc = (gl_FragCoord.xy - uViewport.xy) / uViewport.zw * 2.0 - 1.0;
    vec3 v = normalize(vec3(-ndc / uProjScale, 1.0)); // the mesh shader's viewRay
    vec3 ratio = lit(n1, v) / max(lit(n0, v), vec3(1e-3));
    vec4 c = texture(uColor, (best - vec2(uOrigin) + 0.5) / vec2(textureSize(uColor, 0)));
    fragColor = vec4(pow(pow(c.rgb, vec3(2.2)) * ratio, vec3(1.0 / 2.2)), 1.0);
}
)";

GLuint compile(GLenum type, const char* body, bool common) {
    const char* src[3] = {"#version 330 core\n", common ? kCommon : "", body};
    GLuint s = gl::CreateShader(type);
    gl::ShaderSource(s, 3, src, nullptr);
    gl::CompileShader(s);
    return s;
}

GLuint link(const char* fragmentBody) {
    GLuint vs = compile(GL_VERTEX_SHADER, kVS + std::strlen("#version 330 core\n"), false), fs = compile(GL_FRAGMENT_SHADER, fragmentBody, true);
    GLuint prog = gl::CreateProgram();
    gl::AttachShader(prog, vs);
    gl::AttachShader(prog, fs);
    gl::LinkProgram(prog);
    GLint ok = 0;
    gl::GetProgramiv(prog, GL_LINK_STATUS, &ok);
    gl::DeleteShader(vs);
    gl::DeleteShader(fs);
    if (!ok) {
        char log[2048] = {};
        gl::GetProgramInfoLog(prog, sizeof log, nullptr, log);
        std::fprintf(stderr, "Screen stroke pass: %s\n", log);
        gl::DeleteProgram(prog);
        return 0;
    }
    return prog;
}

GLuint makeTexture(GLenum internalFormat, GLenum format, GLenum type, int width, int height, GLenum filter) {
    GLuint t = 0;
    gl::GenTextures(1, &t);
    gl::BindTexture(GL_TEXTURE_2D, t);
    gl::TexImage2D(GL_TEXTURE_2D, 0, GLint(internalFormat), width, height, 0, format, type, nullptr);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GLint(filter));
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GLint(filter));
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GLint(GL_CLAMP_TO_EDGE));
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GLint(GL_CLAMP_TO_EDGE));
    return t;
}

} // namespace

ScreenStroke::~ScreenStroke() = default; // (GL objects go in release(), with the context current)

void ScreenStroke::release() {
    if (fieldProg_) gl::DeleteProgram(fieldProg_);
    if (warpProg_) gl::DeleteProgram(warpProg_);
    if (vao_) gl::DeleteVertexArrays(1, &vao_);
    if (resolveFbo_) gl::DeleteFramebuffers(1, &resolveFbo_), gl::DeleteTextures(1, &resolveDepth_);
    resolveFbo_ = resolveDepth_ = 0;
    resolveW_ = resolveH_ = 0;
    releaseTargets();
    fieldProg_ = warpProg_ = vao_ = 0;
}

void ScreenStroke::releaseTargets() {
    if (fbo_) gl::DeleteFramebuffers(1, &fbo_);
    if (fieldFbo_) gl::DeleteFramebuffers(1, &fieldFbo_);
    if (color_) gl::DeleteTextures(1, &color_);
    if (depth_) gl::DeleteTextures(1, &depth_);
    if (field_) gl::DeleteTextures(1, &field_);
    fbo_ = fieldFbo_ = color_ = depth_ = field_ = 0;
    w_ = h_ = 0;
}

bool ScreenStroke::ensure(int width, int height) {
    if (failed_) return false;
    if (!fieldProg_) {
        fieldProg_ = link(kFieldFS);
        warpProg_ = link(kWarpFS);
        if (!fieldProg_ || !warpProg_) {
            failed_ = true;
            return false;
        }
        gl::GenVertexArrays(1, &vao_);
    }
    if (width <= 0 || (fbo_ && width <= w_ && height <= h_)) return true;
    width = std::max(width, w_), height = std::max(height, h_); // (only grows: the pass's rectangle and a margin)
    releaseTargets();
    color_ = makeTexture(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, width, height, GL_LINEAR);
    depth_ = makeTexture(GL_DEPTH_COMPONENT24, kDepthComponent, GL_FLOAT, width, height, GL_NEAREST);
    field_ = makeTexture(kRgba16f, GL_RGBA, GL_FLOAT, width, height, GL_LINEAR); // (the warp interpolates the offsets)
    gl::BindTexture(GL_TEXTURE_2D, 0);
    gl::GenFramebuffers(1, &fbo_);
    gl::BindFramebuffer(GL_FRAMEBUFFER, fbo_);
    gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_, 0);
    gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth_, 0);
    bool ok = gl::CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    gl::GenFramebuffers(1, &fieldFbo_);
    gl::BindFramebuffer(GL_FRAMEBUFFER, fieldFbo_);
    gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, field_, 0);
    ok = ok && gl::CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    w_ = width, h_ = height;
    if (!ok) failed_ = true;
    return ok;
}

bool ScreenStroke::apply(const GhostStroke& g, GLuint depthFbo, GLuint colorFbo, GLuint targetFbo, int width, int height,
                         const glm::mat4& view, const glm::mat4& proj, const glm::vec4& viewport, GLuint litsphere,
                         const glm::mat3& lightRot, const glm::ivec4* passRect) {
    if (!supports(g) || !litsphere || !ensure(0, 0)) return false;
    // The dabs in view space and the screen circle each one can reach (grown: the surface may move past the radius).
    struct Dab {
        glm::vec4 c, n, a;
        glm::vec2 px;
        float reach;
    };
    std::vector<Dab> dabs;
    const float sign = g.invert ? -1.0f : 1.0f;
    for (const GhostDab& d : g.dabs) {
        const glm::vec3 cv = glm::vec3(view * glm::vec4(d.center, 1.0f));
        const glm::vec4 cc = proj * glm::vec4(cv, 1.0f);
        if (cc.w <= 0.0f) continue;
        Dab k;
        k.c = glm::vec4(cv, d.radius);
        k.n = glm::vec4(glm::normalize(glm::mat3(view) * d.normal), sign * d.strength);
        k.a = glm::vec4(glm::vec3(view * glm::vec4(d.areaCenter, 1.0f)), d.weight);
        k.px = glm::vec2(viewport.x + (cc.x / cc.w * 0.5f + 0.5f) * viewport.z - 0.5f, viewport.y + (cc.y / cc.w * 0.5f + 0.5f) * viewport.w - 0.5f);
        k.reach = kReach * d.radius * proj[1][1] / cc.w * 0.5f * viewport.w + 2.0f;
        dabs.push_back(k);
    }
    // The pass's rectangle (given, or all the dabs'), and only the dabs that reach it.
    glm::ivec4 r;
    if (passRect) r = *passRect;
    else {
        float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
        for (const Dab& d : dabs)
            x0 = std::min(x0, d.px.x - d.reach), y0 = std::min(y0, d.px.y - d.reach), x1 = std::max(x1, d.px.x + d.reach), y1 = std::max(y1, d.px.y + d.reach);
        r = glm::ivec4(int(std::floor(x0)), int(std::floor(y0)), int(std::ceil(x1)) + 1, int(std::ceil(y1)) + 1);
    }
    r = glm::ivec4(std::max(0, r.x), std::max(0, r.y), std::min(width, r.z), std::min(height, r.w));
    if (r.z <= r.x || r.w <= r.y) return false;
    std::vector<glm::vec4> c, n, a, px;
    for (const Dab& d : dabs) {
        if (c.size() >= 64) break;
        const float dx = std::max({float(r.x) - d.px.x, 0.0f, d.px.x - float(r.z)}), dy = std::max({float(r.y) - d.px.y, 0.0f, d.px.y - float(r.w)});
        if (dx * dx + dy * dy > d.reach * d.reach) continue;
        c.push_back(d.c), n.push_back(d.n), a.push_back(d.a), px.emplace_back(d.px, 0.0f, 0.0f);
    }
    if (c.empty()) return false;
    // The inputs, the rectangle and a margin (normals look 3 pixels out): the drawn colour and the view's depth (a
    // multisample depth resolves by nearest sample), copied into textures of the rectangle's size (a full-view copy
    // costs a tile-based GPU a whole-frame load and store per pass).
    const int bx0 = std::max(0, r.x - 4), by0 = std::max(0, r.y - 4), bx1 = std::min(width, r.z + 4), by1 = std::min(height, r.w + 4);
    const int lw = bx1 - bx0, lh = by1 - by0;
    if (!ensure(lw, lh)) return false;
    gl::BindFramebuffer(GL_READ_FRAMEBUFFER, colorFbo);
    gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_);
    gl::BlitFramebuffer(bx0, by0, bx1, by1, 0, 0, lw, lh, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    GLint samples = 0; // (a multisample source can't move: resolve it in place first, then copy the rectangle)
    gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, depthFbo);
    gl::GetIntegerv(kSamples, &samples);
    GLuint depthSource = depthFbo;
    if (samples > 0) {
        if (!resolveFbo_ || resolveW_ != width || resolveH_ != height) {
            if (resolveFbo_) gl::DeleteFramebuffers(1, &resolveFbo_), gl::DeleteTextures(1, &resolveDepth_);
            resolveDepth_ = makeTexture(GL_DEPTH_COMPONENT24, kDepthComponent, GL_FLOAT, width, height, GL_NEAREST);
            gl::GenFramebuffers(1, &resolveFbo_);
            gl::BindFramebuffer(GL_FRAMEBUFFER, resolveFbo_);
            gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, resolveDepth_, 0);
            resolveW_ = width, resolveH_ = height;
        }
        gl::BindFramebuffer(GL_READ_FRAMEBUFFER, depthFbo);
        gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFbo_);
        gl::BlitFramebuffer(bx0, by0, bx1, by1, bx0, by0, bx1, by1, kDepthBit, GL_NEAREST);
        depthSource = resolveFbo_;
    }
    gl::BindFramebuffer(GL_READ_FRAMEBUFFER, depthSource);
    gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_);
    gl::BlitFramebuffer(bx0, by0, bx1, by1, 0, 0, lw, lh, kDepthBit, GL_NEAREST);
    gl::Enable(GL_SCISSOR_TEST);
    gl::Disable(GL_DEPTH_TEST);
    gl::BindVertexArray(vao_);
    const glm::mat4 invProj = glm::inverse(proj);
    auto common = [&](GLuint prog) {
        auto at = [prog](const char* name) { return gl::GetUniformLocation(prog, name); };
        gl::UseProgram(prog);
        gl::Uniform1i(at("uDepth"), 1);
        gl::UniformMatrix4fv(at("uInvProj"), 1, GL_FALSE, glm::value_ptr(invProj));
        gl::UniformMatrix4fv(at("uProj"), 1, GL_FALSE, glm::value_ptr(proj));
        gl::Uniform4fv(at("uViewport"), 1, glm::value_ptr(viewport));
        gl::Uniform2i(at("uOrigin"), bx0, by0);
        gl::Uniform2i(at("uLocal"), lw, lh);
        return at;
    };
    gl::ActiveTexture(GL_TEXTURE0 + 1);
    gl::BindTexture(GL_TEXTURE_2D, depth_);
    // Field.
    {
        auto at = common(fieldProg_);
        gl::Uniform1f(at("uFocal"), g.focal);
        gl::Uniform1i(at("uCount"), GLint(c.size()));
        gl::Uniform4fv(at("uDabC"), GLsizei(c.size()), glm::value_ptr(c[0]));
        gl::Uniform4fv(at("uDabN"), GLsizei(n.size()), glm::value_ptr(n[0]));
        gl::Uniform4fv(at("uDabA"), GLsizei(a.size()), glm::value_ptr(a[0]));
        gl::Uniform1i(at("uKind"), g.kind == GhostKind::Clay ? 2 : 1);
        gl::BindFramebuffer(GL_FRAMEBUFFER, fieldFbo_);
        gl::Viewport(0, 0, w_, h_);
        gl::Scissor(r.x - bx0, r.y - by0, r.z - r.x, r.w - r.y);
        gl::DrawArrays(GL_TRIANGLES, 0, 3);
    }
    // Warp.
    {
        auto at = common(warpProg_);
        gl::Uniform1i(at("uColor"), 0);
        gl::Uniform1i(at("uLitSphereTex"), 2);
        gl::Uniform1i(at("uField"), 3);
        gl::UniformMatrix3fv(at("uLightRot"), 1, GL_FALSE, glm::value_ptr(lightRot));
        gl::Uniform2f(at("uProjScale"), proj[0][0], proj[1][1]);
        const glm::vec4 rect(r);
        gl::Uniform4fv(at("uRect"), 1, glm::value_ptr(rect));
        gl::Uniform1i(at("uCount"), GLint(px.size()));
        gl::Uniform4fv(at("uDabPx"), GLsizei(px.size()), glm::value_ptr(px[0]));
        gl::ActiveTexture(GL_TEXTURE0 + 3);
        gl::BindTexture(GL_TEXTURE_2D, field_);
        gl::ActiveTexture(GL_TEXTURE0 + 2);
        gl::BindTexture(GL_TEXTURE_2D, litsphere);
        gl::ActiveTexture(GL_TEXTURE0);
        gl::BindTexture(GL_TEXTURE_2D, color_);
        gl::BindFramebuffer(GL_FRAMEBUFFER, targetFbo);
        gl::Viewport(0, 0, width, height);
        gl::Scissor(r.x, r.y, r.z - r.x, r.w - r.y);
        gl::DrawArrays(GL_TRIANGLES, 0, 3);
    }
    gl::BindVertexArray(0);
    gl::UseProgram(0);
    gl::Disable(GL_SCISSOR_TEST);
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

} // namespace sf
