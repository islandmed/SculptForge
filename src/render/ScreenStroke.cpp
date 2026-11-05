// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/ScreenStroke.h"

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace sf {

namespace {

constexpr GLenum kDepthComponent = 0x1902;
constexpr GLbitfield kDepthBit = 0x00000100;

const char* kVS = R"(#version 330 core
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

// Keep the kernel in step with ghostMove (render/Ghost.cpp, Standard) and the shading with the mesh shader's Lit
// Sphere branch (Renderer.cpp); tools/optimistic bench: app/OptimisticBench.cpp.
const char* kFS = R"(#version 330 core
uniform sampler2D uColor, uDepth, uLitSphereTex;
uniform mat4 uInvProj, uProj;
uniform mat3 uLightRot;
uniform vec4 uViewport; // x, y, w, h in target pixels
uniform vec2 uProjScale, uSize;
uniform float uFocal;
uniform int uCount;
uniform int uKind;      // 1 Standard, 2 Clay (GhostKind order is not used: the shader's own)
uniform vec4 uDabC[64]; // view-space centre, radius
uniform vec4 uDabN[64]; // view-space area normal, signed strength (strength, negative inverted)
uniform vec4 uDabA[64]; // view-space area centre, weight
out vec4 fragColor;
bool surface(ivec2 p) { return texelFetch(uDepth, clamp(p, ivec2(0), ivec2(uSize) - 1), 0).r < 1.0; }
vec3 viewPos(ivec2 p) {
    p = clamp(p, ivec2(0), ivec2(uSize) - 1);
    float d = texelFetch(uDepth, p, 0).r;
    vec2 ndc = (vec2(p) + 0.5 - uViewport.xy) / uViewport.zw * 2.0 - 1.0;
    vec4 q = uInvProj * vec4(ndc, d * 2.0 - 1.0, 1.0);
    return q.xyz / q.w;
}
vec2 toPixel(vec3 P) {
    vec4 c = uProj * vec4(P, 1.0);
    return uViewport.xy + (c.xy / c.w * 0.5 + 0.5) * uViewport.zw - 0.5;
}
float falloff(float dist, float rad) {
    float t = clamp(1.0 - dist / rad, 0.0, 1.0);
    if (uFocal != 0.0) t = pow(t, exp2(2.0 * clamp(uFocal, -1.0, 1.0)));
    return t * t * (3.0 - 2.0 * t);
}
// How far dab i moves P along its area normal (ghostMove's Standard and Clay).
float amount(int i, vec3 P) {
    float dist = length(P - uDabC[i].xyz), rad = uDabC[i].w;
    if (dist >= rad) return 0.0;
    float f = falloff(dist, rad) * uDabA[i].w, s = uDabN[i].w;
    if (uKind == 1) return s * rad * 0.06 * f;
    float h = dot(P - (uDabA[i].xyz + uDabN[i].xyz * (rad * 0.05 * s)), uDabN[i].xyz); // Clay: up to its plane
    return (s > 0.0 ? h < 0.0 : h > 0.0) ? -h * f : 0.0;
}
// The dabs' displacement of P, and (bend) the tangential gradient of each move that bends the normal n.
vec3 displace(vec3 P, vec3 n, bool bend, out vec3 g) {
    vec3 m = vec3(0.0);
    g = vec3(0.0);
    vec3 t1 = normalize(abs(n.x) < 0.9 ? cross(n, vec3(1.0, 0.0, 0.0)) : cross(n, vec3(0.0, 1.0, 0.0))), t2 = cross(n, t1);
    for (int i = 0; i < uCount; ++i) {
        if (distance(P, uDabC[i].xyz) >= uDabC[i].w) continue;
        m += uDabN[i].xyz * amount(i, P);
        if (bend) {
            float e = uDabC[i].w * 2e-3;
            vec3 grad = t1 * (amount(i, P + t1 * e) - amount(i, P - t1 * e)) / (2.0 * e) +
                        t2 * (amount(i, P + t2 * e) - amount(i, P - t2 * e)) / (2.0 * e);
            g += grad * dot(uDabN[i].xyz, n);
        }
    }
    return m;
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
    for (int i = 0; i < uCount; ++i) {
        vec2 c = toPixel(uDabC[i].xyz);
        if (distance(c, p) < near) near = distance(c, p), toward = c - p;
    }
    vec2 best = vec2(-1.0);
    float bestErr = 4.0;
    vec3 g;
    for (int s = 0; s < 4 && bestErr > 0.04; ++s) {
        vec2 q = p + toward * (0.12 * float(s));
        for (int k = 0; k < 16; ++k) {
            ivec2 iq = ivec2(floor(q + 0.5));
            if (!surface(iq)) break;
            vec3 P = viewPos(iq);
            vec2 err = toPixel(P + displace(P, vec3(0.0), false, g)) - p;
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
    vec3 n0 = normalAt(iq);
    displace(viewPos(iq), n0, true, g);
    vec3 n1 = normalize(n0 - g);
    vec2 ndc = (gl_FragCoord.xy - uViewport.xy) / uViewport.zw * 2.0 - 1.0;
    vec3 v = normalize(vec3(-ndc / uProjScale, 1.0)); // the mesh shader's viewRay
    vec3 ratio = lit(n1, v) / max(lit(n0, v), vec3(1e-3));
    vec4 c = texture(uColor, (best + 0.5) / uSize);
    fragColor = vec4(pow(pow(c.rgb, vec3(2.2)) * ratio, vec3(1.0 / 2.2)), 1.0);
}
)";

GLuint compile(GLenum type, const char* src) {
    GLuint s = gl::CreateShader(type);
    gl::ShaderSource(s, 1, &src, nullptr);
    gl::CompileShader(s);
    return s;
}

} // namespace

ScreenStroke::~ScreenStroke() = default; // (GL objects go in release(), with the context current)

void ScreenStroke::release() {
    if (prog_) gl::DeleteProgram(prog_);
    if (vao_) gl::DeleteVertexArrays(1, &vao_);
    if (fbo_) gl::DeleteFramebuffers(1, &fbo_);
    if (color_) gl::DeleteTextures(1, &color_);
    if (depth_) gl::DeleteTextures(1, &depth_);
    prog_ = vao_ = fbo_ = color_ = depth_ = 0;
    w_ = h_ = 0;
}

bool ScreenStroke::ensure(int width, int height) {
    if (failed_) return false;
    if (!prog_) {
        GLuint vs = compile(GL_VERTEX_SHADER, kVS), fs = compile(GL_FRAGMENT_SHADER, kFS);
        prog_ = gl::CreateProgram();
        gl::AttachShader(prog_, vs);
        gl::AttachShader(prog_, fs);
        gl::LinkProgram(prog_);
        GLint ok = 0;
        gl::GetProgramiv(prog_, GL_LINK_STATUS, &ok);
        gl::DeleteShader(vs);
        gl::DeleteShader(fs);
        if (!ok) {
            failed_ = true;
            return false;
        }
        gl::GenVertexArrays(1, &vao_);
    }
    if (fbo_ && width == w_ && height == h_) return true;
    if (fbo_) {
        gl::DeleteFramebuffers(1, &fbo_);
        gl::DeleteTextures(1, &color_);
        gl::DeleteTextures(1, &depth_);
    }
    gl::GenTextures(1, &color_);
    gl::BindTexture(GL_TEXTURE_2D, color_);
    gl::TexImage2D(GL_TEXTURE_2D, 0, GLint(GL_RGBA8), width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GLint(GL_LINEAR));
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GLint(GL_LINEAR));
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GLint(GL_CLAMP_TO_EDGE));
    gl::GenTextures(1, &depth_);
    gl::BindTexture(GL_TEXTURE_2D, depth_);
    gl::TexImage2D(GL_TEXTURE_2D, 0, GLint(GL_DEPTH_COMPONENT24), width, height, 0, kDepthComponent, GL_FLOAT, nullptr);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GLint(GL_NEAREST));
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GLint(GL_NEAREST));
    gl::BindTexture(GL_TEXTURE_2D, 0);
    gl::GenFramebuffers(1, &fbo_);
    gl::BindFramebuffer(GL_FRAMEBUFFER, fbo_);
    gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_, 0);
    gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth_, 0);
    const bool ok = gl::CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    w_ = width, h_ = height;
    if (!ok) failed_ = true;
    return ok;
}

bool ScreenStroke::apply(const GhostStroke& g, GLuint drawFbo, GLuint resolveFbo, int width, int height, const glm::mat4& view,
                         const glm::mat4& proj, const glm::vec4& viewport, GLuint litsphere, const glm::mat3& lightRot) {
    if (!supports(g) || !litsphere || !ensure(width, height)) return false;
    // The pass's inputs: the resolved colour and the view's depth (a multisample depth resolves by nearest sample).
    gl::BindFramebuffer(GL_READ_FRAMEBUFFER, resolveFbo);
    gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_);
    gl::BlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    gl::BindFramebuffer(GL_READ_FRAMEBUFFER, drawFbo);
    gl::BlitFramebuffer(0, 0, width, height, 0, 0, width, height, kDepthBit, GL_NEAREST);
    // The dabs in view space, and the screen rectangle they reach (grown: the surface may move past the radius).
    std::vector<glm::vec4> c, n, a;
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    const float sign = g.invert ? -1.0f : 1.0f;
    for (const GhostDab& d : g.dabs) {
        if (c.size() >= 64) break;
        const glm::vec3 cv = glm::vec3(view * glm::vec4(d.center, 1.0f));
        const glm::vec3 nv = glm::normalize(glm::mat3(view) * d.normal);
        c.emplace_back(cv, d.radius);
        n.emplace_back(nv, sign * d.strength);
        a.emplace_back(glm::vec3(view * glm::vec4(d.areaCenter, 1.0f)), d.weight);
        const glm::vec4 cc = proj * glm::vec4(cv, 1.0f);
        if (cc.w <= 0.0f) continue;
        const float sx = viewport.x + (cc.x / cc.w * 0.5f + 0.5f) * viewport.z, sy = viewport.y + (cc.y / cc.w * 0.5f + 0.5f) * viewport.w;
        const float rp = 1.5f * d.radius * proj[1][1] / cc.w * 0.5f * viewport.w + 2.0f;
        x0 = std::min(x0, sx - rp), y0 = std::min(y0, sy - rp), x1 = std::max(x1, sx + rp), y1 = std::max(y1, sy + rp);
    }
    if (c.empty() || x1 <= x0) return false;
    gl::BindFramebuffer(GL_FRAMEBUFFER, resolveFbo);
    gl::Viewport(0, 0, width, height);
    gl::Enable(GL_SCISSOR_TEST);
    const int sx0 = std::max(0, int(x0)), sy0 = std::max(0, int(y0));
    gl::Scissor(sx0, sy0, std::min(width, int(x1) + 1) - sx0, std::min(height, int(y1) + 1) - sy0);
    gl::Disable(GL_DEPTH_TEST);
    gl::UseProgram(prog_);
    auto at = [&](const char* name) { return gl::GetUniformLocation(prog_, name); };
    gl::Uniform1i(at("uColor"), 0);
    gl::Uniform1i(at("uDepth"), 1);
    gl::Uniform1i(at("uLitSphereTex"), 2);
    const glm::mat4 invProj = glm::inverse(proj);
    gl::UniformMatrix4fv(at("uInvProj"), 1, GL_FALSE, glm::value_ptr(invProj));
    gl::UniformMatrix4fv(at("uProj"), 1, GL_FALSE, glm::value_ptr(proj));
    gl::UniformMatrix3fv(at("uLightRot"), 1, GL_FALSE, glm::value_ptr(lightRot));
    gl::Uniform4fv(at("uViewport"), 1, glm::value_ptr(viewport));
    gl::Uniform2f(at("uProjScale"), proj[0][0], proj[1][1]);
    gl::Uniform2f(at("uSize"), float(width), float(height));
    gl::Uniform1f(at("uFocal"), g.focal);
    gl::Uniform1i(at("uCount"), GLint(c.size()));
    gl::Uniform4fv(at("uDabC"), GLsizei(c.size()), glm::value_ptr(c[0]));
    gl::Uniform4fv(at("uDabN"), GLsizei(n.size()), glm::value_ptr(n[0]));
    gl::Uniform4fv(at("uDabA"), GLsizei(a.size()), glm::value_ptr(a[0]));
    gl::Uniform1i(at("uKind"), g.kind == GhostKind::Clay ? 2 : 1);
    gl::ActiveTexture(GL_TEXTURE0 + 2);
    gl::BindTexture(GL_TEXTURE_2D, litsphere);
    gl::ActiveTexture(GL_TEXTURE0 + 1);
    gl::BindTexture(GL_TEXTURE_2D, depth_);
    gl::ActiveTexture(GL_TEXTURE0);
    gl::BindTexture(GL_TEXTURE_2D, color_);
    gl::BindVertexArray(vao_);
    gl::DrawArrays(GL_TRIANGLES, 0, 3);
    gl::BindVertexArray(0);
    gl::UseProgram(0);
    gl::Disable(GL_SCISSOR_TEST);
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

} // namespace sf
