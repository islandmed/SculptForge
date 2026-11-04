// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/BrushAlpha.h"

#include "core/Commands.h"
#include "core/Sculpt.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>

namespace sf {

namespace {

constexpr int kBuiltinSize = 128;

float smooth01(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

uint32_t hash2(int x, int y, uint32_t seed) {
    uint32_t h = uint32_t(x) * 0x8da6b343u ^ uint32_t(y) * 0xd8163841u ^ seed * 0xcb1ab31fu;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}

float valueNoise2(float x, float y, uint32_t seed) {
    const float fx = std::floor(x), fy = std::floor(y);
    const int ix = int(fx), iy = int(fy);
    const float tx = smooth01(x - fx), ty = smooth01(y - fy);
    auto at = [&](int dx, int dy) { return float(hash2(ix + dx, iy + dy, seed) & 0xffffu) / 65535.0f; };
    const float a = at(0, 0) + (at(1, 0) - at(0, 0)) * tx;
    const float b = at(0, 1) + (at(1, 1) - at(0, 1)) * tx;
    return a + (b - a) * ty;
}

// Fills a square alpha from f(u, v) with u, v in -1..1.
template <class F>
BrushAlpha generate(std::string name, F&& f) {
    BrushAlpha a;
    a.name = std::move(name);
    a.width = a.height = kBuiltinSize;
    a.values.resize(size_t(kBuiltinSize) * kBuiltinSize);
    for (int y = 0; y < kBuiltinSize; ++y)
        for (int x = 0; x < kBuiltinSize; ++x) {
            const float u = (float(x) + 0.5f) / kBuiltinSize * 2.0f - 1.0f;
            const float v = 1.0f - (float(y) + 0.5f) / kBuiltinSize * 2.0f;
            a.values[size_t(y) * kBuiltinSize + size_t(x)] = std::clamp(f(u, v), 0.0f, 1.0f);
        }
    return a;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

// A name to look up by: lower case without spaces, '_' or '-' ("Fabric Weave", "fabric_weave").
std::string lookupKey(const std::string& s) {
    std::string out;
    for (unsigned char c : s)
        if (c != ' ' && c != '_' && c != '-') out += char(std::tolower(c));
    return out;
}

float hash01(int x, int y, uint32_t seed) { return float(hash2(x, y, seed) & 0xffffu) / 65535.0f; }

float fbm(float x, float y, uint32_t seed, int octaves = 3) {
    float sum = 0.0f, amp = 0.5f, norm = 0.0f;
    for (int o = 0; o < octaves; ++o) {
        sum += valueNoise2(x, y, seed + uint32_t(o)) * amp;
        norm += amp;
        x = x * 2.03f + 1.7f;
        y = y * 2.03f + 3.1f;
        amp *= 0.5f;
    }
    return sum / norm;
}

// Distances to the nearest and second nearest feature point of a jittered grid (Worley noise); `id` gets the
// nearest point's cell hash.
void worley(float x, float y, uint32_t seed, float& f1, float& f2, uint32_t* id = nullptr) {
    const int cx = int(std::floor(x)), cy = int(std::floor(y));
    f1 = f2 = 1e9f;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            const int gx = cx + dx, gy = cy + dy;
            const float px = float(gx) + hash01(gx, gy, seed), py = float(gy) + hash01(gx, gy, seed + 7);
            const float d = std::hypot(x - px, y - py);
            if (d < f1) {
                f2 = f1;
                f1 = d;
                if (id) *id = hash2(gx, gy, seed + 13);
            } else if (d < f2) {
                f2 = d;
            }
        }
}

// Fades a pattern that fills the square out over its outer edge, so the stamp has no hard border.
float squareEdge(float u, float v) { return smooth01((1.0f - std::max(std::abs(u), std::abs(v))) / 0.18f); }
float roundEdge(float u, float v) { return brushFalloff(std::sqrt(u * u + v * v), 1.0f); }

// Distance from p to the segment a-b.
float segmentDistance(float px, float py, float ax, float ay, float bx, float by) {
    const float vx = bx - ax, vy = by - ay;
    const float t = std::clamp(((px - ax) * vx + (py - ay) * vy) / std::max(vx * vx + vy * vy, 1e-12f), 0.0f, 1.0f);
    return std::hypot(px - (ax + vx * t), py - (ay + vy * t));
}

constexpr float kPi = 3.14159265f;

// The generated built-ins (#43): made here in code from noise and simple shapes, no image files.
struct Generated {
    const char* name;
    const char* help;
    BrushAlpha (*make)();
};

const Generated kGenerated[] = {
    {"Pores", "Scattered skin pores of mixed sizes (use with Sub to press them in).",
     [] {
         return generate("Pores", [](float u, float v) {
             float f1, f2;
             uint32_t id = 0;
             worley(u * 9.0f + 20.0f, v * 9.0f + 20.0f, 41, f1, f2, &id);
             const float r = 0.16f + 0.14f * float(id & 0xffu) / 255.0f;
             const float pore = 1.0f - smooth01(f1 / r);
             const float grain = fbm(u * 14.0f + 5.0f, v * 14.0f + 5.0f, 43) * 0.25f;
             return (pore * 0.85f + grain) * squareEdge(u, v);
         });
     }},
    {"Cracks", "A web of thin cracks, as in dry earth, old paint or stone.",
     [] {
         return generate("Cracks", [](float u, float v) {
             const float wx = u * 3.2f + (fbm(u * 3.0f + 9.0f, v * 3.0f, 51) - 0.5f) * 0.9f;
             const float wy = v * 3.2f + (fbm(u * 3.0f, v * 3.0f + 9.0f, 53) - 0.5f) * 0.9f;
             float f1, f2;
             worley(wx + 30.0f, wy + 30.0f, 55, f1, f2);
             const float width = 0.035f + 0.04f * fbm(u * 5.0f + 2.0f, v * 5.0f, 57);
             return (1.0f - smooth01((f2 - f1) / width)) * squareEdge(u, v);
         });
     }},
    {"Scales", "Overlapping rounded scales in offset rows, for reptiles and fish.",
     [] {
         return generate("Scales", [](float u, float v) {
             const float rows = 5.0f, size = 2.0f / rows;
             float best = 0.0f;
             const float y = (v + 1.0f) / size;
             for (int r = int(std::floor(y)) - 1; r <= int(std::floor(y)) + 1; ++r) {
                 const float shift = (r & 1) ? 0.5f : 0.0f;
                 const float x = (u + 1.0f) / size - shift;
                 for (int c = int(std::floor(x)) - 1; c <= int(std::floor(x)) + 1; ++c) {
                     const float dx = x - (float(c) + 0.5f), dy = y - float(r); // each scale hangs from its row line
                     const float d = std::hypot(dx, dy * 1.15f);
                     if (d > 0.62f || dy > 0.1f) continue;
                     const float h = (1.0f - smooth01(d / 0.62f)) * (0.55f + 0.45f * smooth01(-dy / 0.6f));
                     best = std::max(best, h * (0.85f + 0.15f * float(r & 1))); // lower rows lie over upper ones
                 }
             }
             return best * squareEdge(u, v);
         });
     }},
    {"Rivets", "A three by three grid of round rivet heads, for armour and machines.",
     [] {
         return generate("Rivets", [](float u, float v) {
             const float cell = 2.0f / 3.0f;
             const float cx = (std::floor((u + 1.0f) / cell) + 0.5f) * cell - 1.0f;
             const float cy = (std::floor((v + 1.0f) / cell) + 0.5f) * cell - 1.0f;
             const float d = std::hypot(u - cx, v - cy) / (cell * 0.3f);
             return d < 1.0f ? std::sqrt(1.0f - d * d) : 0.0f;
         });
     }},
    {"Stitches", "A seam with cross stitches along it (drag along the seam with Drag Stamp or a stroke).",
     [] {
         return generate("Stitches", [](float u, float v) {
             const float seam = 0.35f * (1.0f - smooth01(std::abs(u) / 0.05f)); // the sewn-together edge
             float stitch = 0.0f;
             for (int k = -4; k <= 4; ++k) {
                 const float y = float(k) * 0.22f;
                 const float d = segmentDistance(u, v, -0.32f, y - 0.06f, 0.32f, y + 0.06f);
                 stitch = std::max(stitch, 1.0f - smooth01(d / 0.055f));
             }
             return std::max(seam, stitch) * smooth01((1.0f - std::abs(v)) / 0.15f) * smooth01((1.0f - std::abs(u)) / 0.3f);
         });
     }},
    {"Fabric Weave", "Threads woven over and under, for cloth and canvas.",
     [] {
         return generate("Fabric Weave", [](float u, float v) {
             const float f = 6.0f; // threads across
             const float x = (u + 1.0f) * f, y = (v + 1.0f) * f;
             const int ix = int(std::floor(x)), iy = int(std::floor(y));
             const float warp = std::sin(kPi * (x - float(ix))); // across a vertical thread
             const float weft = std::sin(kPi * (y - float(iy))); // across a horizontal thread
             const bool warpOnTop = ((ix + iy) & 1) == 0;
             // The thread on top bulges in the middle of its crossing, the one below dips.
             const float along = warpOnTop ? std::sin(kPi * (y - float(iy))) : std::sin(kPi * (x - float(ix)));
             const float top = (warpOnTop ? warp : weft) * (0.6f + 0.4f * along);
             const float under = (warpOnTop ? weft : warp) * 0.45f;
             return std::max(top, under) * squareEdge(u, v);
         });
     }},
    {"Bark", "Rough vertical ridges and furrows, as on tree bark.",
     [] {
         return generate("Bark", [](float u, float v) {
             const float warp = (fbm(u * 2.0f + 4.0f, v * 0.6f, 61, 4) - 0.5f) * 1.2f;
             const float ridge = 1.0f - std::abs(std::sin((u * 3.5f + warp) * kPi));
             const float breaks = fbm(u * 6.0f, v * 2.0f + 7.0f, 63);
             return std::clamp(std::pow(ridge, 0.6f) * (0.55f + 0.45f * breaks), 0.0f, 1.0f) * squareEdge(u, v);
         });
     }},
    {"Rock Strata", "Stepped layers of rock, wavy and broken, for cliffs and stone.",
     [] {
         return generate("Rock Strata", [](float u, float v) {
             const float y = v * 4.0f + (fbm(u * 1.5f + 3.0f, v * 0.8f, 71) - 0.5f) * 1.6f;
             const float layer = y - std::floor(y);
             const float step = smooth01(layer / 0.75f); // each layer slopes up, then drops off
             const float rough = fbm(u * 8.0f, v * 8.0f + 11.0f, 73) * 0.3f;
             return std::clamp(step * 0.75f + rough, 0.0f, 1.0f) * squareEdge(u, v);
         });
     }},
    {"Wrinkles", "Fine parallel wrinkle lines, for skin around eyes, knuckles and necks.",
     [] {
         return generate("Wrinkles", [](float u, float v) {
             float lines = 0.0f;
             for (int k = -3; k <= 3; ++k) {
                 const float y = float(k) * 0.2f + 0.05f * std::sin(u * 2.5f + float(k)) + 0.12f * u * u;
                 const float len = 0.75f - 0.06f * float(std::abs(k));
                 const float d = std::abs(v - y);
                 const float along = smooth01((len - std::abs(u)) / 0.25f); // tapered ends
                 lines = std::max(lines, (1.0f - smooth01(d / 0.035f)) * along);
             }
             return lines * roundEdge(u, v * 0.8f);
         });
     }},
    {"Skin Folds", "Soft, broad folds of skin, for bellies, elbows and heavy brows.",
     [] {
         return generate("Skin Folds", [](float u, float v) {
             float folds = 0.0f;
             for (int k = -1; k <= 1; ++k) {
                 const float y = float(k) * 0.42f + 0.18f * u * u;
                 const float d = v - y;
                 // A rounded roll: steep below the crease line, a long soft slope above it.
                 const float roll = d < 0.0f ? 1.0f - smooth01(-d / 0.08f) : 1.0f - smooth01(d / 0.3f);
                 folds = std::max(folds, roll * smooth01((0.85f - std::abs(u)) / 0.35f));
             }
             return folds * roundEdge(u * 0.9f, v * 0.9f);
         });
     }},
    {"Hair Strands", "Long flowing strands, for hair, fur and feathers (drag along the flow).",
     [] {
         return generate("Hair Strands", [](float u, float v) {
             const float x = u + 0.12f * std::sin(v * 2.2f) + 0.05f * std::sin(v * 5.0f + u * 3.0f);
             const float strands = 22.0f;
             const float s = (x + 1.0f) * strands * 0.5f;
             const int is = int(std::floor(s));
             const float across = s - float(is) - 0.5f;
             const float bright = 0.55f + 0.45f * hash01(is, 0, 81);
             const float width = 0.22f + 0.15f * hash01(is, 1, 81);
             const float strand = 1.0f - smooth01(std::abs(across) / width);
             const float ends = smooth01((0.85f - std::abs(v + 0.2f * (hash01(is, 2, 81) - 0.5f))) / 0.3f);
             return strand * bright * ends * smooth01((1.0f - std::abs(u)) / 0.2f);
         });
     }},
    {"Dot Grid", "An even grid of soft dots, for studs, textures and patterns.",
     [] {
         return generate("Dot Grid", [](float u, float v) {
             const float cell = 2.0f / 7.0f;
             const float cx = (std::floor((u + 1.0f) / cell) + 0.5f) * cell - 1.0f;
             const float cy = (std::floor((v + 1.0f) / cell) + 0.5f) * cell - 1.0f;
             return (1.0f - smooth01(std::hypot(u - cx, v - cy) / (cell * 0.32f))) * squareEdge(u, v);
         });
     }},
};

} // namespace

float BrushAlpha::sample(float u, float v) const {
    if (u < -1.0f || u > 1.0f || v < -1.0f || v > 1.0f) return 0.0f;
    if (tiled()) { // each tile shows the whole alpha: its own -1..1 square
        auto wrap = [](float t, int n) {
            const float s = (t * 0.5f + 0.5f) * float(n);
            const float f = std::min(s - std::floor(s), 1.0f);
            return f * 2.0f - 1.0f;
        };
        u = wrap(u, tileX);
        v = wrap(v, tileY);
    }
    if (empty()) return brushFalloff(std::sqrt(u * u + v * v), 1.0f);
    // Bilinear lookup; pixel centres span the -1..1 square.
    const float px = std::clamp((u * 0.5f + 0.5f) * float(width) - 0.5f, 0.0f, float(width - 1));
    const float py = std::clamp((0.5f - v * 0.5f) * float(height) - 0.5f, 0.0f, float(height - 1));
    const int x0 = int(px), y0 = int(py);
    const int x1 = std::min(x0 + 1, width - 1), y1 = std::min(y0 + 1, height - 1);
    const float tx = px - float(x0), ty = py - float(y0);
    auto at = [&](int x, int y) { return values[size_t(y) * size_t(width) + size_t(x)]; };
    const float a = at(x0, y0) + (at(x1, y0) - at(x0, y0)) * tx;
    const float b = at(x0, y1) + (at(x1, y1) - at(x0, y1)) * tx;
    return a + (b - a) * ty;
}

float BrushAlpha::stamp(float u, float v, float focal) const {
    const float d = std::sqrt(u * u + v * v);
    if (empty() && !tiled()) return d < 1.0f ? brushFalloff(d, 1.0f, focal) : 0.0f;
    const float a = sample(u, v);
    return a > 0.0f ? a * alphaFalloff(d, 1.0f, focal) : 0.0f;
}

BrushAlpha BrushAlpha::round() { return BrushAlpha{}; }

BrushAlpha BrushAlpha::square() {
    // Flat plateau with soft edges over the outer fifth.
    return generate("Square", [](float u, float v) {
        return smooth01((1.0f - std::max(std::abs(u), std::abs(v))) / 0.2f);
    });
}

BrushAlpha BrushAlpha::noise(uint32_t seed) {
    // Three octaves of value noise inside the round falloff.
    return generate("Noise", [seed](float u, float v) {
        const float x = u * 4.0f + 8.0f, y = v * 4.0f + 8.0f;
        float n = valueNoise2(x, y, seed) * 0.57f + valueNoise2(x * 2.03f, y * 2.03f, seed + 1) * 0.29f +
                  valueNoise2(x * 4.01f, y * 4.01f, seed + 2) * 0.14f;
        n = std::clamp(n * 1.6f - 0.3f, 0.0f, 1.0f);
        return n * brushFalloff(std::sqrt(u * u + v * v), 1.0f);
    });
}

const std::vector<BrushAlpha>& BrushAlpha::builtins() {
    static const std::vector<BrushAlpha> all = [] { // made once, on first use
        std::vector<BrushAlpha> v{round(), square(), noise()};
        v.front().name = "Round";
        for (const Generated& g : kGenerated) v.push_back(g.make());
        return v;
    }();
    return all;
}

std::string BrushAlpha::builtinHelp(const std::string& name) {
    const std::string key = lookupKey(name);
    if (key == "round" || key == "none" || key.empty()) return "The plain round falloff (no alpha).";
    if (key == "square") return "A square stamp with soft edges.";
    if (key == "noise") return "A noisy stamp for rough, organic texture.";
    for (const Generated& g : kGenerated)
        if (lookupKey(g.name) == key) return g.help;
    return {};
}

std::string BrushAlpha::builtinNames() {
    std::string out;
    for (const BrushAlpha& a : builtins()) out += (out.empty() ? "" : ", ") + lower(a.name);
    return out;
}

bool BrushAlpha::builtin(const std::string& name, BrushAlpha& out) {
    const std::string key = lookupKey(name);
    if (key == "none" || key.empty()) {
        out = round();
        return true;
    }
    for (const BrushAlpha& a : builtins())
        if (lookupKey(a.name) == key) {
            out = a;
            return true;
        }
    return false;
}

BrushAlpha BrushAlpha::fromImage(const Image& image, std::string name, bool invert) {
    BrushAlpha a;
    a.name = std::move(name);
    if (image.empty()) return a;
    const Image img = image.downscaled(512);
    a.width = img.width;
    a.height = img.height;
    a.values.resize(size_t(img.width) * size_t(img.height));
    for (size_t i = 0; i < a.values.size(); ++i) {
        const uint8_t* p = &img.rgba[i * 4];
        float l = (0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2]) / 255.0f;
        if (invert) l = 1.0f - l;
        a.values[i] = l * (float(p[3]) / 255.0f);
    }
    return a;
}

void registerBrushCommands(CommandRegistry& registry) {
    using T = ParamSpec::Type;
    registry.add({"brush_set_alpha",
                  "Set the brush alpha (grayscale stamp shaping the falloff of every brush): a built-in "
                  "(round = none, square, noise, pores, cracks, scales, rivets, stitches, fabric weave, bark, rock "
                  "strata, wrinkles, skin folds, hair strands, dot grid) or a grayscale image file.",
                  {{"name", T::String, "Built-in alpha: round, square, noise, pores, cracks, scales, rivets, stitches, "
                                       "fabric_weave, bark, rock_strata, wrinkles, skin_folds, hair_strands or dot_grid.", false},
                   {"path", T::String, "Image file (png, jpg, ...) used as the alpha; overrides name.", false},
                   {"invert", T::Bool, "Use the image's negative (white = no effect).", false}},
                  [](Document& doc, const Args& a) -> CommandResult {
                      const std::string path = argString(a, "path");
                      if (!path.empty()) {
                          Image img;
                          std::string err;
                          if (!Image::load(path, img, err)) return {false, err};
                          auto stem = std::filesystem::path(std::u8string(path.begin(), path.end())).stem().u8string();
                          doc.brushAlpha = BrushAlpha::fromImage(img, std::string(stem.begin(), stem.end()),
                                                                 argBool(a, "invert", false));
                          doc.brushAlpha.path = path;
                          doc.brushAlpha.inverted = argBool(a, "invert", false);
                          return {true, "Alpha '" + doc.brushAlpha.name + "' (" + std::to_string(doc.brushAlpha.width) +
                                            "x" + std::to_string(doc.brushAlpha.height) + ")"};
                      }
                      const std::string name = argString(a, "name", "round");
                      BrushAlpha alpha;
                      if (!BrushAlpha::builtin(name, alpha))
                          return {false, "Unknown alpha: " + name + " (" + BrushAlpha::builtinNames() + ")"};
                      doc.brushAlpha = std::move(alpha);
                      return {true, "Alpha '" + doc.brushAlpha.name + "'"};
                  }});
}

} // namespace sf
