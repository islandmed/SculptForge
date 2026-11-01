// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/UvView.h"

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>

namespace sf {

namespace {

constexpr GLenum kViewport = 0x0BA2;           // GL_VIEWPORT
constexpr GLenum kFramebufferBinding = 0x8CA6; // GL_FRAMEBUFFER_BINDING
constexpr GLenum kRepeat = 0x2901;             // GL_REPEAT

// A full-view quad: the tile grid, the checker or the texture.
const char* kBgVS = R"(#version 330 core
uniform vec2 uMin, uMax;
out vec2 vUv;
void main() {
    vec2 p = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    vUv = mix(uMin, uMax, p);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";
const char* kBgFS = R"(#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform int uMode;          // 0 nothing, 1 checker, 2 texture
uniform sampler2D uImage;   // sRGB: samples come back linear
uniform float uOpacity;
uniform float uChecker;     // squares per UV unit
uniform vec2 uPixel;        // UV units per pixel
uniform ivec2 uTiles;       // UDIM tiles in use (columns, rows)
// Distance in pixels to the nearest line of a grid with `step` UV units.
float gridDist(vec2 uv, float step) {
    vec2 d = abs(fract(uv / step + 0.5) - 0.5) * step / uPixel;
    return min(d.x, d.y);
}
void main() {
    vec2 tile = floor(vUv);
    bool used = tile.x >= 0.0 && tile.y >= 0.0 && tile.x < float(uTiles.x) && tile.y < float(uTiles.y);
    bool home = tile.x == 0.0 && tile.y == 0.0;
    vec3 c = vec3(0.098, 0.098, 0.106);
    if (used) c = vec3(0.137, 0.137, 0.149);
    if (home) c = vec3(0.157, 0.157, 0.172);
    if (uMode == 1 && used) {
        vec2 cell = floor(vUv * uChecker);
        float odd = mod(cell.x + cell.y, 2.0);
        c = mix(c, mix(vec3(0.62), vec3(0.30), odd), uOpacity);
    } else if (uMode == 2 && home) {
        c = mix(c, pow(texture(uImage, vUv).rgb, vec3(1.0 / 2.2)), uOpacity);
    }
    // Tenths inside tiles (fading out when they get too close), tile borders on top.
    float tenthsPx = 0.1 / max(uPixel.x, 1e-9);
    float fine = (1.0 - smoothstep(0.0, 1.0, gridDist(vUv, 0.1))) * smoothstep(6.0, 18.0, tenthsPx);
    float border = 1.0 - smoothstep(0.5, 1.5, gridDist(vUv, 1.0));
    c = mix(c, vec3(0.30, 0.30, 0.34), fine * (used ? 0.55 : 0.25));
    c = mix(c, used ? vec3(0.50, 0.50, 0.56) : vec3(0.26, 0.26, 0.29), border);
    fragColor = vec4(c, 1.0);
}
)";
// Triangles (fill) and lines (edges) of the UV layout.
const char* kMeshVS = R"(#version 330 core
layout(location = 0) in vec2 aUv;
layout(location = 1) in vec4 aFace;
layout(location = 2) in vec4 aEdge;
layout(location = 3) in float aMove;
uniform vec2 uMin, uMax;
uniform mat3 uMove;
uniform int uLines;
flat out vec4 vColor;
void main() {
    vec2 p = aUv;
    if (aMove > 0.5) p = (uMove * vec3(p, 1.0)).xy;
    gl_Position = vec4((p - uMin) / (uMax - uMin) * 2.0 - 1.0, 0.0, 1.0);
    vColor = uLines == 1 ? aEdge : aFace;
}
)";
const char* kMeshFS = R"(#version 330 core
flat in vec4 vColor;
out vec4 fragColor;
void main() { fragColor = vColor; }
)";

GLuint compileShader(GLenum type, const char* src, std::string& error) {
    GLuint s = gl::CreateShader(type);
    gl::ShaderSource(s, 1, &src, nullptr);
    gl::CompileShader(s);
    GLint ok = 0;
    gl::GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        gl::GetShaderInfoLog(s, sizeof log, nullptr, log);
        error = std::string("UV view shader: ") + log;
        gl::DeleteShader(s);
        return 0;
    }
    return s;
}

GLuint linkProgram(const char* vs, const char* fs, std::string& error) {
    const GLuint v = compileShader(GL_VERTEX_SHADER, vs, error);
    if (!v) return 0;
    const GLuint f = compileShader(GL_FRAGMENT_SHADER, fs, error);
    if (!f) {
        gl::DeleteShader(v);
        return 0;
    }
    GLuint p = gl::CreateProgram();
    gl::AttachShader(p, v);
    gl::AttachShader(p, f);
    gl::LinkProgram(p);
    gl::DeleteShader(v);
    gl::DeleteShader(f);
    GLint ok = 0;
    gl::GetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        error = "UV view shader: link failed";
        gl::DeleteProgram(p);
        return 0;
    }
    return p;
}

} // namespace

bool UvViewRenderer::init(std::string& error) {
    if (meshProgram_) return true;
    bgProgram_ = linkProgram(kBgVS, kBgFS, error);
    meshProgram_ = bgProgram_ ? linkProgram(kMeshVS, kMeshFS, error) : 0;
    if (!meshProgram_) return false;
    auto at = [](GLuint p, const char* n) { return gl::GetUniformLocation(p, n); };
    bgMin_ = at(bgProgram_, "uMin"), bgMax_ = at(bgProgram_, "uMax"), bgMode_ = at(bgProgram_, "uMode");
    bgImage_ = at(bgProgram_, "uImage"), bgOpacity_ = at(bgProgram_, "uOpacity"), bgChecker_ = at(bgProgram_, "uChecker");
    bgPixel_ = at(bgProgram_, "uPixel"), bgTiles_ = at(bgProgram_, "uTiles");
    mMin_ = at(meshProgram_, "uMin"), mMax_ = at(meshProgram_, "uMax"), mMove_ = at(meshProgram_, "uMove");
    mLines_ = at(meshProgram_, "uLines");
    gl::GenVertexArrays(1, &vao_);
    gl::GenVertexArrays(1, &emptyVao_);
    GLuint buffers[5];
    gl::GenBuffers(5, buffers);
    uvBuffer_ = buffers[0], faceBuffer_ = buffers[1], edgeBuffer_ = buffers[2], moveBuffer_ = buffers[3], indexBuffer_ = buffers[4];
    gl::BindVertexArray(vao_);
    gl::BindBuffer(GL_ARRAY_BUFFER, uvBuffer_);
    gl::EnableVertexAttribArray(0);
    gl::VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    gl::BindBuffer(GL_ARRAY_BUFFER, faceBuffer_);
    gl::EnableVertexAttribArray(1);
    gl::VertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, nullptr);
    gl::BindBuffer(GL_ARRAY_BUFFER, edgeBuffer_);
    gl::EnableVertexAttribArray(2);
    gl::VertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, nullptr);
    gl::BindBuffer(GL_ARRAY_BUFFER, moveBuffer_);
    gl::EnableVertexAttribArray(3);
    gl::VertexAttribPointer(3, 1, GL_UNSIGNED_BYTE, GL_FALSE, 0, nullptr);
    gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer_);
    gl::BindVertexArray(0);
    gl::BindBuffer(GL_ARRAY_BUFFER, 0);
    return true;
}

void UvViewRenderer::shutdown() {
    if (!meshProgram_) return;
    gl::DeleteProgram(bgProgram_);
    gl::DeleteProgram(meshProgram_);
    const GLuint buffers[5] = {uvBuffer_, faceBuffer_, edgeBuffer_, moveBuffer_, indexBuffer_};
    gl::DeleteBuffers(5, buffers);
    gl::DeleteVertexArrays(1, &vao_);
    gl::DeleteVertexArrays(1, &emptyVao_);
    if (fbo_) gl::DeleteFramebuffers(1, &fbo_);
    if (msFbo_) gl::DeleteFramebuffers(1, &msFbo_);
    if (msColor_) gl::DeleteRenderbuffers(1, &msColor_);
    if (texture_) gl::DeleteTextures(1, &texture_);
    *this = UvViewRenderer();
}

void UvViewRenderer::setUvs(const std::vector<glm::vec2>& uvs) {
    if (!meshProgram_) return;
    if (uvs.size() != corners_) colorsValid_ = false;
    corners_ = uvs.size();
    gl::BindBuffer(GL_ARRAY_BUFFER, uvBuffer_);
    gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(uvs.size() * sizeof(glm::vec2)), uvs.empty() ? nullptr : uvs.data(), GL_DYNAMIC_DRAW);
    gl::BindBuffer(GL_ARRAY_BUFFER, 0);
}

void UvViewRenderer::setColors(const std::vector<uint32_t>& face, const std::vector<uint32_t>& edge, const std::vector<uint8_t>& moving) {
    if (!meshProgram_) return;
    colorsValid_ = face.size() == corners_ && edge.size() == corners_ && moving.size() == corners_;
    if (!colorsValid_) return;
    gl::BindBuffer(GL_ARRAY_BUFFER, faceBuffer_);
    gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(face.size() * 4), face.data(), GL_DYNAMIC_DRAW);
    gl::BindBuffer(GL_ARRAY_BUFFER, edgeBuffer_);
    gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(edge.size() * 4), edge.data(), GL_DYNAMIC_DRAW);
    gl::BindBuffer(GL_ARRAY_BUFFER, moveBuffer_);
    gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(moving.size()), moving.data(), GL_DYNAMIC_DRAW);
    gl::BindBuffer(GL_ARRAY_BUFFER, 0);
}

void UvViewRenderer::setEdges(const std::vector<uint32_t>& corners) {
    if (!meshProgram_) return;
    // Each line as (next corner, corner): the last vertex is the provoking one, so the edge takes the colour of
    // the corner it starts at.
    std::vector<uint32_t> idx(corners.size() * 2);
    for (size_t i = 0; i < corners.size(); ++i) {
        const uint32_t c = corners[i];
        idx[i * 2] = c - c % 3 + (c % 3 + 1) % 3;
        idx[i * 2 + 1] = c;
    }
    edgeCount_ = corners.size();
    gl::BindVertexArray(vao_);
    gl::BufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(idx.size() * sizeof(uint32_t)), idx.empty() ? nullptr : idx.data(), GL_DYNAMIC_DRAW);
    gl::BindVertexArray(0);
}

bool UvViewRenderer::ensureTarget(int w, int h) {
    if (w == width_ && h == height_ && fbo_) return true;
    if (!fbo_) {
        gl::GenFramebuffers(1, &fbo_);
        gl::GenFramebuffers(1, &msFbo_);
        gl::GenRenderbuffers(1, &msColor_);
        gl::GenTextures(1, &texture_);
    }
    GLint maxSamples = 0;
    gl::GetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    samples_ = std::clamp(maxSamples, 1, 4);
    gl::BindRenderbuffer(GL_RENDERBUFFER, msColor_);
    gl::RenderbufferStorageMultisample(GL_RENDERBUFFER, samples_, GL_RGBA8, w, h);
    gl::BindRenderbuffer(GL_RENDERBUFFER, 0);
    gl::BindFramebuffer(GL_FRAMEBUFFER, msFbo_);
    gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msColor_);
    const bool msOk = gl::CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    gl::BindTexture(GL_TEXTURE_2D, texture_);
    gl::TexImage2D(GL_TEXTURE_2D, 0, GLint(GL_RGBA8), w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl::BindTexture(GL_TEXTURE_2D, 0);
    gl::BindFramebuffer(GL_FRAMEBUFFER, fbo_);
    gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_, 0);
    const bool ok = gl::CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (!msOk) samples_ = 0; // draw straight into the texture
    width_ = w, height_ = h;
    return ok;
}

GLuint UvViewRenderer::draw(int w, int h, const View& v) {
    if (!meshProgram_ || w <= 0 || h <= 0) return 0;
    GLint oldFbo = 0, oldViewport[4] = {0, 0, 0, 0};
    gl::GetIntegerv(kFramebufferBinding, &oldFbo);
    gl::GetIntegerv(kViewport, oldViewport);
    if (!ensureTarget(w, h)) {
        gl::BindFramebuffer(GL_FRAMEBUFFER, GLuint(oldFbo));
        return 0;
    }
    gl::BindFramebuffer(GL_FRAMEBUFFER, samples_ > 0 ? msFbo_ : fbo_);
    gl::Viewport(0, 0, w, h);
    gl::Disable(GL_DEPTH_TEST);
    gl::Disable(GL_CULL_FACE);
    gl::Disable(GL_SCISSOR_TEST);
    gl::ClearColor(0.098f, 0.098f, 0.106f, 1.0f);
    gl::Clear(GL_COLOR_BUFFER_BIT);
    // Background.
    gl::UseProgram(bgProgram_);
    gl::Uniform2f(bgMin_, v.min.x, v.min.y);
    gl::Uniform2f(bgMax_, v.max.x, v.max.y);
    gl::Uniform1i(bgMode_, v.imageMode == 2 && !v.image ? 0 : v.imageMode);
    gl::Uniform1f(bgOpacity_, v.imageOpacity);
    gl::Uniform1f(bgChecker_, v.checkerScale);
    gl::Uniform2f(bgPixel_, (v.max.x - v.min.x) / float(w), (v.max.y - v.min.y) / float(h));
    gl::Uniform2i(bgTiles_, std::max(v.tiles.x, 1), std::max(v.tiles.y, 1));
    gl::Uniform1i(bgImage_, 0);
    gl::ActiveTexture(GL_TEXTURE0);
    gl::BindTexture(GL_TEXTURE_2D, v.image);
    if (v.image) {
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GLint(kRepeat));
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GLint(kRepeat));
    }
    gl::BindVertexArray(emptyVao_);
    gl::DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    gl::BindTexture(GL_TEXTURE_2D, 0);
    // The layout.
    if (corners_ && colorsValid_) {
        gl::Enable(GL_BLEND);
        gl::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        gl::UseProgram(meshProgram_);
        gl::Uniform2f(mMin_, v.min.x, v.min.y);
        gl::Uniform2f(mMax_, v.max.x, v.max.y);
        gl::UniformMatrix3fv(mMove_, 1, GL_FALSE, glm::value_ptr(v.move));
        gl::BindVertexArray(vao_);
        if (v.fill) {
            gl::Uniform1i(mLines_, 0);
            gl::DrawArrays(GL_TRIANGLES, 0, GLsizei(corners_));
        }
        if (v.edges && edgeCount_) {
            gl::Uniform1i(mLines_, 1);
            gl::DrawElements(GL_LINES, GLsizei(edgeCount_ * 2), GL_UNSIGNED_INT, nullptr);
        }
        gl::BindVertexArray(0);
        gl::Disable(GL_BLEND);
    }
    gl::UseProgram(0);
    if (samples_ > 0) {
        gl::BindFramebuffer(GL_READ_FRAMEBUFFER, msFbo_);
        gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_);
        gl::BlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    gl::BindFramebuffer(GL_FRAMEBUFFER, GLuint(oldFbo));
    gl::Viewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
    return texture_;
}

} // namespace sf
