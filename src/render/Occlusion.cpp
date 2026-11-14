// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/Occlusion.h"

#include <SDL3/SDL_log.h>

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>

namespace sf {

namespace {

constexpr GLenum kDepthComponent = 0x1902;
constexpr GLbitfield kDepthBit = 0x00000100;
constexpr GLenum kZero = 0, kSrcColor = 0x0300; // GL_ZERO, GL_SRC_COLOR: the composite multiplies the view
constexpr GLenum kNone = 0;

const char* kVS = R"(#version 330 core
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

// Shared by the passes: the view's depth and positions, and the half-res texels' packed depth.
#define SF_OCC_COMMON                                                                                              \
    "uniform sampler2D uDepth;\n"                                                                                  \
    "uniform mat4 uInvProj;\n"                                                                                     \
    "uniform vec4 uViewport;\n"                                                                                    \
    "uniform vec2 uFull;\n"                                                                                        \
    "uniform float uRange;\n"                                                                                      \
    "float depthAt(ivec2 p) { return texelFetch(uDepth, clamp(p, ivec2(0), ivec2(uFull) - 1), 0).r; }\n"          \
    "vec3 posAt(ivec2 p, float d) {\n"                                                                             \
    "    vec2 ndc = (vec2(p) + 0.5 - uViewport.xy) / uViewport.zw * 2.0 - 1.0;\n"                                 \
    "    vec4 q = uInvProj * vec4(ndc, d * 2.0 - 1.0, 1.0);\n"                                                     \
    "    return q.xyz / q.w;\n"                                                                                    \
    "}\n"                                                                                                          \
    "vec2 packDepth(float z) { float x = clamp(z, 0.0, 1.0) * 255.0; return vec2(floor(x) / 255.0, fract(x)); }\n" \
    "float unpackDepth(vec2 v) { return (v.x * 255.0 + v.y) / 255.0; }\n"

// Half resolution: r = ambient occlusion (0 open .. 1 hidden), g = shadow (0 lit .. 1 in shadow), ba = linear depth.
const char* kOccFS = "#version 330 core\n" SF_OCC_COMMON R"(
uniform mat4 uProj;
uniform int uOrtho;
uniform int uAo, uShadows;
uniform float uRadius, uShadowLen, uShadowSoft, uThick;
uniform vec3 uLight;
uniform sampler2D uColor; // the drawn view: alpha = the shading normal's facing to uLight (0.5 + 0.5 N.L)
out vec4 fragColor;
// World units a full-res pixel spans at view depth z (z < 0 in front of the eye).
float pixelSize(float z) { return (uOrtho == 1 ? 2.0 : 2.0 * max(-z, 1e-6)) / (uProj[1][1] * uViewport.w); }
vec2 toPixel(vec3 P) {
    vec4 c = uProj * vec4(P, 1.0);
    return uViewport.xy + (c.xy / c.w * 0.5 + 0.5) * uViewport.zw;
}
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy) * 2;
    float d = depthAt(p);
    if (d >= 1.0) {
        fragColor = vec4(0.0, 0.0, 1.0, 1.0);
        return;
    }
    vec3 P = posAt(p, d);
    // The normal from the neighbours on the side nearer in depth (no smearing across a silhouette), three pixels out:
    // fine wrinkles and pores would tilt a one-pixel normal and their flanks would read as occluded (blotches).
    const int K = 3;
    vec3 pr = posAt(p + ivec2(K, 0), depthAt(p + ivec2(K, 0))), pl = posAt(p - ivec2(K, 0), depthAt(p - ivec2(K, 0)));
    vec3 pu = posAt(p + ivec2(0, K), depthAt(p + ivec2(0, K))), pd = posAt(p - ivec2(0, K), depthAt(p - ivec2(0, K)));
    vec3 dx = abs(pr.z - P.z) < abs(P.z - pl.z) ? pr - P : P - pl;
    vec3 dy = abs(pu.z - P.z) < abs(P.z - pd.z) ? pu - P : P - pd;
    vec3 n = cross(dx, dy);
    vec3 toEye = uOrtho == 1 ? vec3(0.0, 0.0, 1.0) : normalize(-P);
    n = dot(n, n) > 1e-30 ? normalize(n) : toEye;
    if (dot(n, toEye) < 0.0) n = -n;
    float px = pixelSize(P.z);
    // Interleaved gradient noise (Jimenez 2014): a per-pixel turn of the sample pattern the blur then smooths away.
    float noise = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    float ao = 0.0;
    if (uAo == 1) {
        float rPx = clamp(uRadius / px, 3.0, 0.12 * uViewport.w);
        float r = rPx * px, r2 = r * r;
        const int N = 12;
        for (int i = 0; i < N; ++i) {
            float a = (float(i) + 0.5) / float(N);
            float ang = a * 43.98 + noise * 6.2831853; // 7 turns of a spiral
            vec2 o = vec2(cos(ang), sin(ang)) * (rPx * a);
            ivec2 q = p + ivec2(o);
            float dq = depthAt(q);
            if (dq >= 1.0) continue;
            vec3 v = posAt(q, dq) - P;
            float vv = dot(v, v);
            // (less a horizon bias of ~9 degrees: surface detail smaller than the radius doesn't occlude)
            float cosine = (dot(v, n) - 0.02 * r) / sqrt(vv + 1e-12) - 0.15;
            ao += max(cosine, 0.0) * max(1.0 - vv / r2, 0.0);
        }
        // (1.5x the gain of the first version, owner 11-13 "more AO", less a small floor: the creases go darker and the
        // open surfaces, whose few grazing samples add up to little, stay clean)
        ao = clamp((ao * 5.6 / float(N) - 0.06) / 0.94, 0.0, 1.0);
    }
    float shadow = 0.0;
    // (the mesh's own smooth normal, not the faceted one from the depth: a low-poly mesh shades smooth, and its facets
    // would otherwise stripe the shadow's edge along a grazing light)
    float facing = texelFetch(uColor, p, 0).a * 2.0 - 1.0;
    if (uShadows == 1 && facing > -0.25) {
        // A cone around the light, wider with Softness: the penumbra. Three rays a pixel, a third of a turn apart in the
        // cone (each pixel's set turned by its noise), so the edge is a smooth gradient after the blur, not grain.
        vec3 t1 = normalize(abs(uLight.z) < 0.9 ? cross(uLight, vec3(0.0, 0.0, 1.0)) : cross(uLight, vec3(1.0, 0.0, 0.0)));
        vec3 t2 = cross(uLight, t1);
        const int R = 3, S = 16;
        for (int r = 0; r < R; ++r) {
            float u = fract(noise + float(r) / float(R));
            float ang = u * 6.2831853, spread = uShadowSoft * 0.16 * sqrt((float(r) + 0.5) / float(R));
            vec3 L = normalize(uLight + (t1 * cos(ang) + t2 * sin(ang)) * spread);
            vec3 start = P + n * (px * 2.0) + L * px;
            float jitter = fract(noise * 7.13 + float(r) * 0.618);
            for (int i = 0; i < S; ++i) {
                float t = (float(i) + jitter) / float(S);
                vec3 Q = start + L * (t * t * uShadowLen + px * float(i)); // denser near the point: contact shadows
                vec2 sp = toPixel(Q);
                if (any(lessThan(sp, uViewport.xy)) || any(greaterThanEqual(sp, uViewport.xy + uViewport.zw))) break;
                ivec2 q = ivec2(sp);
                float dq = depthAt(q);
                if (dq >= 1.0) continue;
                float dz = posAt(q, dq).z - Q.z; // > 0: the drawn surface there is in front of the ray
                // (the bias grows along the ray: a grazing ray over a faceted surface doesn't shadow itself)
                if (dz > pixelSize(Q.z) * 2.0 + 0.04 * distance(Q, P) && dz < uThick) {
                    shadow += (1.0 - smoothstep(0.75, 1.0, t)) / float(R);
                    break;
                }
            }
        }
        shadow *= smoothstep(-0.25, 0.3, facing); // (turned away it is dark already: no double darkening)
    }
    fragColor = vec4(ao, shadow, packDepth(-P.z / uRange));
}
)";

// Separable, depth-aware blur at half resolution: occlusion along one axis, each channel with its own width.
const char* kBlurFS = R"(#version 330 core
uniform sampler2D uSrc;
uniform ivec2 uDir;
uniform vec2 uSigma; // AO, shadow (texels)
uniform int uTaps;
uniform int uStride; // texels between taps (wide blurs keep their tap count)
out vec4 fragColor;
float unpackDepth(vec2 v) { return (v.x * 255.0 + v.y) / 255.0; }
void main() {
    ivec2 size = textureSize(uSrc, 0);
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec4 c = texelFetch(uSrc, p, 0);
    float zc = unpackDepth(c.ba);
    if (zc >= 1.0) {
        fragColor = c;
        return;
    }
    vec2 sum = c.rg, w = vec2(1.0);
    for (int k = -uTaps; k <= uTaps; ++k) {
        if (k == 0) continue;
        vec4 s = texelFetch(uSrc, clamp(p + uDir * (k * uStride), ivec2(0), size - 1), 0);
        float z = unpackDepth(s.ba);
        float wz = max(0.0, 1.0 - abs(z - zc) / (zc * 0.03 + 1e-4));
        float fk = float(k * k * uStride * uStride);
        vec2 wk = exp(-fk / (2.0 * uSigma * uSigma)) * wz;
        sum += s.rg * wk;
        w += wk;
    }
    fragColor = vec4(sum / w, c.ba);
}
)";

// Full resolution: the blurred occlusion brought up with the depth (the nearest half-res texels at this pixel's
// depth), multiplied into the view's colour (display encoded, so the factor is too).
const char* kCompFS = "#version 330 core\n" SF_OCC_COMMON R"(
uniform sampler2D uOcc;
uniform vec2 uStrength; // AO strength, shadow darkness (0 when off)
out vec4 fragColor;
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    float d = depthAt(p);
    if (d >= 1.0) discard;
    float z = -posAt(p, d).z / uRange;
    vec2 h = (vec2(p) + 0.5) * 0.5 - 0.5;
    ivec2 b = ivec2(floor(h));
    vec2 f = h - floor(h);
    ivec2 size = textureSize(uOcc, 0);
    vec2 sum = vec2(0.0);
    float wsum = 0.0;
    for (int j = 0; j < 2; ++j)
        for (int i = 0; i < 2; ++i) {
            vec4 s = texelFetch(uOcc, clamp(b + ivec2(i, j), ivec2(0), size - 1), 0);
            float wb = (i == 0 ? 1.0 - f.x : f.x) * (j == 0 ? 1.0 - f.y : f.y) + 1e-3;
            float w = wb / (abs(unpackDepth(s.ba) - z) * 400.0 / max(z, 1e-3) + 1e-2);
            sum += s.rg * w;
            wsum += w;
        }
    vec2 o = sum / max(wsum, 1e-6);
    float k = (1.0 - uStrength.x * o.x) * (1.0 - uStrength.y * o.y);
    fragColor = vec4(vec3(pow(max(k, 0.0), 1.0 / 2.2)), 1.0);
}
)";

GLuint buildProgram(const char* fs) {
    auto compile = [](GLenum type, const char* src) {
        GLuint s = gl::CreateShader(type);
        gl::ShaderSource(s, 1, &src, nullptr);
        gl::CompileShader(s);
        GLint ok = 0;
        gl::GetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[1024] = {};
            gl::GetShaderInfoLog(s, sizeof log - 1, nullptr, log);
            SDL_Log("View shadows and AO: shader compile error: %s", log);
        }
        return s;
    };
    GLuint vs = compile(GL_VERTEX_SHADER, kVS), f = compile(GL_FRAGMENT_SHADER, fs);
    GLuint prog = gl::CreateProgram();
    gl::AttachShader(prog, vs);
    gl::AttachShader(prog, f);
    gl::LinkProgram(prog);
    gl::DeleteShader(vs);
    gl::DeleteShader(f);
    GLint ok = 0;
    gl::GetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        gl::DeleteProgram(prog);
        return 0;
    }
    return prog;
}

void texture2D(GLuint& tex, GLenum internal, int w, int h, GLenum format, GLenum type) {
    gl::GenTextures(1, &tex);
    gl::BindTexture(GL_TEXTURE_2D, tex);
    gl::TexImage2D(GL_TEXTURE_2D, 0, GLint(internal), w, h, 0, format, type, nullptr);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GLint(GL_NEAREST));
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GLint(GL_NEAREST));
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GLint(GL_CLAMP_TO_EDGE));
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GLint(GL_CLAMP_TO_EDGE));
}

} // namespace

OcclusionPass::~OcclusionPass() = default; // (GL objects go in release(), with the context current)

void OcclusionPass::release() {
    for (GLuint* p : {&occProg_, &blurProg_, &compProg_})
        if (*p) gl::DeleteProgram(*p), *p = 0;
    if (vao_) gl::DeleteVertexArrays(1, &vao_);
    if (depthFbo_) gl::DeleteFramebuffers(1, &depthFbo_);
    if (depth_) gl::DeleteTextures(1, &depth_);
    if (colorFbo_) gl::DeleteFramebuffers(1, &colorFbo_);
    if (color_) gl::DeleteTextures(1, &color_);
    if (halfFbo_[0]) gl::DeleteFramebuffers(2, halfFbo_);
    if (half_[0]) gl::DeleteTextures(2, half_);
    vao_ = depthFbo_ = depth_ = colorFbo_ = color_ = 0;
    halfFbo_[0] = halfFbo_[1] = half_[0] = half_[1] = 0;
    w_ = h_ = 0;
}

bool OcclusionPass::ensure(int width, int height) {
    if (failed_) return false;
    if (!occProg_) {
        occProg_ = buildProgram(kOccFS);
        blurProg_ = buildProgram(kBlurFS);
        compProg_ = buildProgram(kCompFS);
        if (!occProg_ || !blurProg_ || !compProg_) {
            SDL_Log("View shadows and AO: not supported by this driver (shaders), left off");
            failed_ = true;
            return false;
        }
        gl::GenVertexArrays(1, &vao_);
    }
    if (depthFbo_ && width == w_ && height == h_) return true;
    if (depthFbo_) {
        gl::DeleteFramebuffers(1, &depthFbo_);
        gl::DeleteTextures(1, &depth_);
        gl::DeleteFramebuffers(1, &colorFbo_);
        gl::DeleteTextures(1, &color_);
        gl::DeleteFramebuffers(2, halfFbo_);
        gl::DeleteTextures(2, half_);
    }
    for (int i = 0; i < 16 && gl::GetError() != GL_NO_ERROR; ++i) {
    }
    // (DEPTH_COMPONENT24 with UNSIGNED_INT: the one depth texture GLES 3 and desktop GL both take, and the view's
    // depth renderbuffer's format, which a blit needs)
    texture2D(depth_, GL_DEPTH_COMPONENT24, width, height, kDepthComponent, GL_UNSIGNED_INT);
    gl::GenFramebuffers(1, &depthFbo_);
    gl::BindFramebuffer(GL_FRAMEBUFFER, depthFbo_);
    gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth_, 0);
    gl::DrawBuffers(1, &kNone);
    bool ok = gl::CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    texture2D(color_, GL_RGBA8, width, height, GL_RGBA, GL_UNSIGNED_BYTE);
    gl::GenFramebuffers(1, &colorFbo_);
    gl::BindFramebuffer(GL_FRAMEBUFFER, colorFbo_);
    gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_, 0);
    ok = ok && gl::CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    const int hw = (width + 1) / 2, hh = (height + 1) / 2;
    gl::GenFramebuffers(2, halfFbo_);
    for (int i = 0; i < 2; ++i) {
        texture2D(half_[i], GL_RGBA8, hw, hh, GL_RGBA, GL_UNSIGNED_BYTE);
        gl::BindFramebuffer(GL_FRAMEBUFFER, halfFbo_[i]);
        gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, half_[i], 0);
        ok = ok && gl::CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    }
    gl::BindTexture(GL_TEXTURE_2D, 0);
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    ok = ok && gl::GetError() == GL_NO_ERROR;
    w_ = width, h_ = height;
    if (!ok) {
        SDL_Log("View shadows and AO: not supported by this driver (render targets), left off");
        failed_ = true;
    }
    return ok;
}

bool OcclusionPass::apply(const OcclusionParams& p, GLuint drawFbo, GLuint resolveFbo, int width, int height,
                          const glm::mat4& proj, const glm::vec4& viewport) {
    if ((!p.ao && !p.shadows) || !ensure(width, height)) return false;
    for (int i = 0; i < 16 && gl::GetError() != GL_NO_ERROR; ++i) {
    }
    // The view's depth (a multisample depth resolves by nearest sample).
    gl::BindFramebuffer(GL_READ_FRAMEBUFFER, drawFbo);
    gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, depthFbo_);
    gl::BlitFramebuffer(0, 0, width, height, 0, 0, width, height, kDepthBit, GL_NEAREST);
    if (p.shadows) { // ... and its colour, for the facing in its alpha
        gl::BindFramebuffer(GL_READ_FRAMEBUFFER, resolveFbo);
        gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, colorFbo_);
        gl::BlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    if (gl::GetError() != GL_NO_ERROR) {
        SDL_Log("View shadows and AO: the view's depth can't be read here, left off");
        failed_ = true;
        gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }
    const int hw = (width + 1) / 2, hh = (height + 1) / 2;
    const glm::mat4 invProj = glm::inverse(proj);
    const bool ortho = proj[3][3] > 0.5f;
    gl::Disable(GL_DEPTH_TEST);
    gl::Disable(GL_BLEND);
    gl::Disable(GL_SCISSOR_TEST);
    gl::DepthMask(GL_FALSE);
    gl::BindVertexArray(vao_);
    auto common = [&](GLuint prog) {
        gl::UseProgram(prog);
        auto at = [prog](const char* n) { return gl::GetUniformLocation(prog, n); };
        gl::Uniform1i(at("uDepth"), 1);
        gl::UniformMatrix4fv(at("uInvProj"), 1, GL_FALSE, glm::value_ptr(invProj));
        gl::Uniform4fv(at("uViewport"), 1, glm::value_ptr(viewport));
        gl::Uniform2f(at("uFull"), float(width), float(height));
        gl::Uniform1f(at("uRange"), std::max(p.depthRange, 1e-4f));
        return at;
    };
    gl::ActiveTexture(GL_TEXTURE0 + 2);
    gl::BindTexture(GL_TEXTURE_2D, color_);
    gl::ActiveTexture(GL_TEXTURE0 + 1);
    gl::BindTexture(GL_TEXTURE_2D, depth_);
    gl::ActiveTexture(GL_TEXTURE0);

    // 1. Occlusion and shadow at half resolution.
    gl::BindFramebuffer(GL_FRAMEBUFFER, halfFbo_[0]);
    gl::Viewport(0, 0, hw, hh);
    {
        auto at = common(occProg_);
        gl::UniformMatrix4fv(at("uProj"), 1, GL_FALSE, glm::value_ptr(proj));
        gl::Uniform1i(at("uOrtho"), ortho ? 1 : 0);
        gl::Uniform1i(at("uColor"), 2);
        gl::Uniform1i(at("uAo"), p.ao ? 1 : 0);
        gl::Uniform1i(at("uShadows"), p.shadows ? 1 : 0);
        gl::Uniform1f(at("uRadius"), p.aoRadius);
        gl::Uniform1f(at("uShadowLen"), p.shadowLength);
        gl::Uniform1f(at("uShadowSoft"), std::clamp(p.shadowSoftness, 0.0f, 1.0f));
        gl::Uniform1f(at("uThick"), p.thickness);
        const glm::vec3 l = glm::normalize(p.light);
        gl::Uniform3fv(at("uLight"), 1, glm::value_ptr(l));
        gl::DrawArrays(GL_TRIANGLES, 0, 3);
    }
    // 2. The blur: across, then down (back into the first target).
    // (in half-res texels, for a view 900 pixels high: the same softness on a bigger or HiDPI view)
    const float scale = std::clamp(viewport.w / 900.0f, 0.5f, 3.0f);
    const float sigmaAo = (0.8f + 4.2f * std::clamp(p.aoSoftness, 0.0f, 1.0f)) * scale;
    const float sigmaSh = (1.0f + 3.5f * std::clamp(p.shadowSoftness, 0.0f, 1.0f)) * scale;
    gl::UseProgram(blurProg_);
    gl::Uniform1i(gl::GetUniformLocation(blurProg_, "uSrc"), 0);
    gl::Uniform2f(gl::GetUniformLocation(blurProg_, "uSigma"), sigmaAo, sigmaSh);
    const int reach = int(std::ceil(2.0f * std::max(sigmaAo, sigmaSh))), stride = (reach + 11) / 12;
    gl::Uniform1i(gl::GetUniformLocation(blurProg_, "uTaps"), (reach + stride - 1) / stride);
    gl::Uniform1i(gl::GetUniformLocation(blurProg_, "uStride"), stride);
    for (int pass = 0; pass < 2; ++pass) {
        gl::BindFramebuffer(GL_FRAMEBUFFER, halfFbo_[1 - pass]);
        gl::BindTexture(GL_TEXTURE_2D, half_[pass]);
        gl::Uniform2i(gl::GetUniformLocation(blurProg_, "uDir"), pass == 0 ? 1 : 0, pass == 0 ? 0 : 1);
        gl::DrawArrays(GL_TRIANGLES, 0, 3);
    }
    // 3. Multiplied into the view, inside its rectangle.
    gl::BindFramebuffer(GL_FRAMEBUFFER, resolveFbo);
    gl::Viewport(0, 0, width, height);
    gl::Enable(GL_SCISSOR_TEST);
    gl::Scissor(int(viewport.x), int(viewport.y), int(viewport.z), int(viewport.w));
    gl::Enable(GL_BLEND);
    gl::BlendFunc(kZero, kSrcColor);
    {
        auto at = common(compProg_);
        gl::Uniform1i(at("uOcc"), 0);
        gl::Uniform2f(at("uStrength"), p.ao ? std::clamp(p.aoStrength, 0.0f, 1.0f) : 0.0f,
                      p.shadows ? std::clamp(p.shadowDarkness, 0.0f, 1.0f) : 0.0f);
        gl::BindTexture(GL_TEXTURE_2D, half_[0]);
        gl::DrawArrays(GL_TRIANGLES, 0, 3);
    }
    gl::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gl::Disable(GL_BLEND);
    if (p.shadows) { // the facing in the alpha back to opaque (the window and saved images read it)
        gl::ColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
        gl::ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        gl::Clear(GL_COLOR_BUFFER_BIT);
        gl::ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }
    gl::Disable(GL_SCISSOR_TEST);
    gl::DepthMask(GL_TRUE);
    gl::Enable(GL_DEPTH_TEST);
    for (int unit : {2, 1, 0}) {
        gl::ActiveTexture(GL_TEXTURE0 + GLenum(unit));
        gl::BindTexture(GL_TEXTURE_2D, 0);
    }
    gl::BindVertexArray(0);
    gl::UseProgram(0);
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

} // namespace sf
