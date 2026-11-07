// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Stencil.h"

#include "core/BrushAlpha.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/ScreenSelect.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>

namespace sf {

namespace {

constexpr int kMaxSide = 2048; // a stencil spans the view: more detail than an alpha's 512, at most 16 MB
constexpr int kMinLevel = 512;           // the smallest mip level (Stencil::mips)
constexpr int kBuiltinStencilSize = 1024; // a built-in pattern made into a Stencil (BrushAlpha::builtinAt)

// sRGB byte -> linear, once.
const std::array<float, 256>& linearTable() {
    static const std::array<float, 256> table = [] {
        std::array<float, 256> t{};
        for (int i = 0; i < 256; ++i) {
            const float x = float(i) / 255.0f;
            t[size_t(i)] = x <= 0.04045f ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f);
        }
        return t;
    }();
    return table;
}

// The four texels around (u, v) and their bilinear weights (texel centres at (i + 0.5) / size); tiled wraps, else
// clamps to the edge.
struct Taps {
    size_t at[4];
    float w[4];
};
Taps taps(const Image& img, glm::vec2 uv, bool tile) {
    const float x = uv.x * float(img.width) - 0.5f, y = uv.y * float(img.height) - 0.5f;
    const float fx0 = std::floor(x), fy0 = std::floor(y);
    const float fx = x - fx0, fy = y - fy0;
    auto wrap = [tile](int i, int n) { return tile ? ((i % n) + n) % n : std::clamp(i, 0, n - 1); };
    const int x0 = wrap(int(fx0), img.width), x1 = wrap(int(fx0) + 1, img.width);
    const int y0 = wrap(int(fy0), img.height), y1 = wrap(int(fy0) + 1, img.height);
    auto idx = [&](int xi, int yi) { return (size_t(yi) * size_t(img.width) + size_t(xi)) * 4; };
    return {{idx(x0, y0), idx(x1, y0), idx(x0, y1), idx(x1, y1)},
            {(1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy}};
}

// A grey image of an alpha's values (the built-ins are made in code, with no file behind them).
Image alphaImage(const BrushAlpha& a) {
    Image img;
    img.width = a.width;
    img.height = a.height;
    img.rgba.resize(size_t(a.width) * size_t(a.height) * 4);
    for (size_t i = 0; i < a.values.size(); ++i) {
        const uint8_t g = uint8_t(std::lround(std::clamp(a.values[i], 0.0f, 1.0f) * 255.0f));
        img.rgba[i * 4] = img.rgba[i * 4 + 1] = img.rgba[i * 4 + 2] = g;
        img.rgba[i * 4 + 3] = 255;
    }
    return img;
}

std::string fileStem(const std::string& path) {
    const auto stem = std::filesystem::path(std::u8string(path.begin(), path.end())).stem().u8string();
    return std::string(stem.begin(), stem.end());
}

} // namespace

bool Stencil::imageCoord(glm::vec2 canvas, float canvasAspect, glm::vec2& uv) const {
    if (!image || image->empty() || size <= 0.0f) return false;
    // In canvas heights (so a turn keeps its shape on a wide canvas), y down; turned back by the angle (seen
    // counter-clockwise: the screen's y points down).
    const glm::vec2 d((canvas.x - center.x) * canvasAspect, canvas.y - center.y);
    const float c = std::cos(angle), s = std::sin(angle);
    const glm::vec2 local(c * d.x - s * d.y, s * d.x + c * d.y);
    uv = glm::vec2(local.x / (size * aspect()) + 0.5f, local.y / size + 0.5f);
    return tile || (uv.x >= 0.0f && uv.x <= 1.0f && uv.y >= 0.0f && uv.y <= 1.0f);
}

glm::vec2 Stencil::canvasPoint(glm::vec2 uv, float canvasAspect) const {
    const glm::vec2 local((uv.x - 0.5f) * size * aspect(), (uv.y - 0.5f) * size);
    const float c = std::cos(angle), s = std::sin(angle);
    const glm::vec2 d(c * local.x + s * local.y, -s * local.x + c * local.y);
    return glm::vec2(center.x + d.x / std::max(canvasAspect, 1e-6f), center.y + d.y);
}

const Image& Stencil::level(int l) const {
    if (l <= 0 || !mips || mips->empty()) return *image;
    return (*mips)[size_t(std::min(l, int(mips->size()))) - 1];
}

// A disc of 17 taps `spacing` image pixels across (a centre, 8 at half the radius, 8 at the radius turned half a step),
// weighted 4 / 2 / 1, from the level where the taps are still 4 of its pixels apart or more.
template <class T, class Read>
T Stencil::disc(glm::vec2 uv, float spacing, Read&& read) const {
    const int l = std::clamp(int(std::floor(std::log2(spacing / 4.0f))), 0, mips ? int(mips->size()) : 0);
    const Image& img = level(l);
    const glm::vec2 px(0.5f * spacing / float(image->width), 0.5f * spacing / float(image->height)); // the radius, in uv
    T sum = read(img, uv) * 4.0f;
    for (int i = 0; i < 8; ++i) {
        const float a = float(i) * 0.785398163f, b = a + 0.392699082f;
        sum += read(img, uv + 0.5f * px * glm::vec2(std::cos(a), std::sin(a))) * 2.0f;
        sum += read(img, uv + px * glm::vec2(std::cos(b), std::sin(b)));
    }
    return sum * (1.0f / 28.0f);
}

float Stencil::value(glm::vec2 uv, float spacing) const {
    if (!image || image->empty()) return 0.0f;
    if (spacing > 1.5f)
        return disc<float>(uv, spacing, [this](const Image& img, glm::vec2 at) { return valueAt(img, at); });
    return valueAt(*image, uv);
}

float Stencil::valueAt(const Image& img, glm::vec2 uv) const {
    const Taps t = taps(img, uv, tile);
    const uint8_t* px = img.rgba.data();
    float out = 0.0f;
    for (int k = 0; k < 4; ++k) {
        const uint8_t* p = px + t.at[k];
        float l = (0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2]) / 255.0f; // (BrushAlpha::fromImage's)
        if (invert) l = 1.0f - l;
        out += t.w[k] * l * (float(p[3]) / 255.0f);
    }
    return std::clamp(out, 0.0f, 1.0f);
}

glm::vec4 Stencil::color(glm::vec2 uv, float spacing) const {
    if (!image || image->empty()) return glm::vec4(0.0f);
    if (spacing > 1.5f)
        return disc<glm::vec4>(uv, spacing, [this](const Image& img, glm::vec2 at) { return colorAt(img, at); });
    return colorAt(*image, uv);
}

glm::vec4 Stencil::colorAt(const Image& img, glm::vec2 uv) const {
    const Taps t = taps(img, uv, tile);
    const uint8_t* px = img.rgba.data();
    const auto& lin = linearTable();
    glm::vec4 out(0.0f);
    for (int k = 0; k < 4; ++k) {
        const uint8_t* p = px + t.at[k];
        auto ch = [&](int i) { return lin[invert ? 255 - p[i] : p[i]]; };
        out += t.w[k] * glm::vec4(ch(0), ch(1), ch(2), float(p[3]) / 255.0f);
    }
    return out;
}

void Stencil::set(Image img, std::string n, std::string p) {
    if (img.empty()) {
        image.reset();
        mips.reset();
        name.clear();
        path.clear();
        return;
    }
    image = std::make_shared<const Image>(img.downscaled(kMaxSide));
    auto levels = std::make_shared<std::vector<Image>>();
    for (const Image* at = image.get(); std::max(at->width, at->height) / 2 >= kMinLevel; at = &levels->back())
        levels->push_back(at->downscaled(std::max(at->width, at->height) / 2));
    mips = std::move(levels);
    name = std::move(n);
    path = std::move(p);
}

void Stencil::resetPlacement() {
    center = {0.5f, 0.5f};
    size = 0.8f;
    angle = 0.0f;
}

bool StencilView::canvasAt(const glm::vec3& world, glm::vec2& canvas) const {
    glm::vec2 ndc;
    if (!projectToNdc(viewProj, copy.identity() ? world : copy.inversePoint(world), ndc)) return false;
    canvas = glm::vec2((ndc.x + 1.0f) * 0.5f, (1.0f - ndc.y) * 0.5f);
    return true;
}

void StencilView::setSpacing(const glm::vec3& center, const glm::vec3& viewDir, float radius, size_t vertices) {
    spacing = 0.0f;
    if (!active() || vertices == 0 || radius <= 0.0f || stencil->size <= 0.0f) return;
    // A direction across the view: the radius as it shows, in canvas heights (canvasAt maps a symmetry copy back).
    const glm::vec3 d = glm::length(viewDir) > 1e-12f ? glm::normalize(viewDir) : glm::vec3(0, 0, -1);
    const glm::vec3 across = glm::normalize(glm::cross(d, std::abs(d.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0)));
    glm::vec2 a, b;
    if (!canvasAt(center, a) || !canvasAt(center + across * radius, b)) return;
    const float r = glm::length(glm::vec2((b.x - a.x) * canvasAspect, b.y - a.y));
    spacing = r * std::sqrt(3.14159265f / float(vertices)) / stencil->size * float(stencil->image->height);
}

float StencilView::weight(const glm::vec3& world) const {
    glm::vec2 c, uv;
    if (!canvasAt(world, c) || !stencil->imageCoord(c, canvasAspect, uv)) return 0.0f;
    return stencil->value(uv, spacing);
}

glm::vec4 StencilView::paint(const glm::vec3& world, const glm::vec3& paintColor) const {
    glm::vec2 c, uv;
    if (!canvasAt(world, c) || !stencil->imageCoord(c, canvasAspect, uv)) return glm::vec4(paintColor, 0.0f);
    if (!stencil->colors) return glm::vec4(paintColor, stencil->value(uv, spacing));
    const glm::vec4 col = stencil->color(uv, spacing);
    return col.w > 1e-6f ? glm::vec4(glm::vec3(col) / col.w, col.w) : glm::vec4(paintColor, 0.0f); // (un-premultiplied)
}

void registerStencilCommands(CommandRegistry& registry) {
    using T = ParamSpec::Type;
    registry.add({"stencil_load",
                  "Lay an image over the view as the Stencil: the brushes work through it (sculpt brushes take its "
                  "brightness, Vertex Paint its colours) wherever it covers the model.",
                  {{"path", T::String, "Image file (png, jpg, ...).", false},
                   {"alpha", T::Bool, "Use the brush's current alpha instead of a file.", false},
                   {"invert", T::Bool, "Use the image's negative.", false},
                   {"reset", T::Bool, "Centre it again, upright, 80% of the view's height (default: keep where it is).", false}},
                  [](Document& doc, const Args& a) -> CommandResult {
                      Stencil& st = doc.stencil;
                      const std::string path = argString(a, "path");
                      if (!path.empty()) {
                          Image img;
                          std::string err;
                          if (!Image::load(path, img, err)) return {false, err};
                          st.set(std::move(img), fileStem(path), path);
                      } else if (argBool(a, "alpha", false)) {
                          const BrushAlpha& al = doc.brushAlpha;
                          if (al.empty()) return {false, "The brush has no alpha (Round): pick one, or load an image"};
                          Image img;
                          std::string err;
                          // An image alpha at its own resolution (the alpha keeps 512 px); a built-in pattern made
                          // again at 1024 px (at 128 its pixels showed as stair-steps along every edge it sculpted).
                          BrushAlpha big;
                          if (!al.path.empty() && Image::load(al.path, img, err)) {
                              if (al.inverted)
                                  for (size_t i = 0; i < img.rgba.size(); i += 4)
                                      for (int k = 0; k < 3; ++k) img.rgba[i + size_t(k)] = uint8_t(255 - img.rgba[i + size_t(k)]);
                          } else if (al.path.empty() && BrushAlpha::builtinAt(al.name, kBuiltinStencilSize, big) && !big.empty()) {
                              img = alphaImage(big);
                          } else {
                              img = alphaImage(al);
                          }
                          st.set(std::move(img), al.name, al.path);
                      } else {
                          return {false, "stencil_load needs path=<image> or alpha=true"};
                      }
                      if (a.contains("invert")) st.invert = argBool(a, "invert", false);
                      if (argBool(a, "reset", false)) st.resetPlacement();
                      st.visible = true;
                      return {true, "Stencil '" + st.name + "' (" + std::to_string(st.image->width) + "x" +
                                        std::to_string(st.image->height) + ")"};
                  }});
    registry.add({"stencil_set",
                  "Place and set up the Stencil: where it sits on the view, its size and turn, how it shows and how "
                  "the brushes take it.",
                  {{"x", T::Number, "Centre across the view, 0 (left) to 1 (right).", false},
                   {"y", T::Number, "Centre down the view, 0 (top) to 1 (bottom).", false},
                   {"size", T::Number, "Height as a fraction of the view's height (0.02-20).", false},
                   {"angle", T::Number, "Turn in degrees, counter-clockwise.", false},
                   {"opacity", T::Number, "How strongly the overlay shows, 0-1 (the brushes take it whole).", false},
                   {"invert", T::Bool, "Use the image's negative.", false},
                   {"tile", T::Bool, "Repeat the image across the whole view.", false},
                   {"visible", T::Bool, "Show it (hidden: the brushes ignore it).", false},
                   {"colors", T::Bool, "Vertex Paint lays the image's colours (off: its brightness masks the paint colour).", false},
                   {"reset", T::Bool, "Centre it, upright, 80% of the view's height (before the values above).", false}},
                  [](Document& doc, const Args& a) -> CommandResult {
                      Stencil& st = doc.stencil;
                      if (argBool(a, "reset", false)) st.resetPlacement();
                      auto num = [&](const char* k, float& v, float lo, float hi) {
                          if (!a.contains(k)) return true;
                          const double d = argNumber(a, k, v);
                          if (!std::isfinite(d)) return false;
                          v = std::clamp(float(d), lo, hi);
                          return true;
                      };
                      float angleDeg = glm::degrees(st.angle);
                      if (!num("x", st.center.x, -10.0f, 11.0f) || !num("y", st.center.y, -10.0f, 11.0f) ||
                          !num("size", st.size, 0.02f, 20.0f) || !num("angle", angleDeg, -36000.0f, 36000.0f) ||
                          !num("opacity", st.opacity, 0.0f, 1.0f))
                          return {false, "stencil_set: numbers only"};
                      angleDeg = std::remainder(angleDeg, 360.0f);
                      st.angle = glm::radians(angleDeg);
                      if (a.contains("invert")) st.invert = argBool(a, "invert", st.invert);
                      if (a.contains("tile")) st.tile = argBool(a, "tile", st.tile);
                      if (a.contains("visible")) st.visible = argBool(a, "visible", st.visible);
                      if (a.contains("colors")) st.colors = argBool(a, "colors", st.colors);
                      return {true, st.image ? "Stencil '" + st.name + "'" : std::string("Stencil set (no image loaded)")};
                  }});
    registry.add({"stencil_clear", "Take the Stencil off the view.", {},
                  [](Document& doc, const Args&) -> CommandResult {
                      doc.stencil.set({}, {}, {});
                      return {true, "Stencil off"};
                  }});
}

} // namespace sf
