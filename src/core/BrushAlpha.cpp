// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/BrushAlpha.h"

#include "core/BakeImage.h"
#include "core/Commands.h"
#include "core/CreatureSkin.h"
#include "core/Sculpt.h"
#include "core/ThreadPool.h"

#include "tinyexr.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <thread>

namespace sf {

namespace {

constexpr int kBuiltinSize = 128;
// The size generate() fills: kBuiltinSize, or larger while BrushAlpha::builtinAt makes one (a Stencil spans the view).
thread_local int gGenerateSize = kBuiltinSize;

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
    const int n = gGenerateSize;
    a.width = a.height = n;
    a.values.resize(size_t(n) * size_t(n));
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const float u = (float(x) + 0.5f) / float(n) * 2.0f - 1.0f;
            const float v = 1.0f - (float(y) + 0.5f) / float(n) * 2.0f;
            a.values[size_t(y) * size_t(n) + size_t(x)] = std::clamp(f(u, v), 0.0f, 1.0f);
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
    {"Cobblestone", "Rounded stones packed together with grooves between, for paths, walls and rough ground.",
     [] {
         return generate("Cobblestone", [](float u, float v) {
             float f1, f2;
             uint32_t id = 0;
             worley(u * 3.2f + 40.0f, v * 3.2f + 40.0f, 91, f1, f2, &id);
             const float edge = smooth01((f2 - f1) / 0.32f); // 0 in the grooves, 1 inside a stone
             const float dome = std::sqrt(std::max(0.0f, edge)) * (0.8f + 0.2f * float(id & 0xffu) / 255.0f);
             const float rough = fbm(u * 10.0f + 3.0f, v * 10.0f, 93) * 0.15f;
             return std::clamp(dome * 0.85f + rough * edge, 0.0f, 1.0f) * squareEdge(u, v);
         });
     }},
    {"Leather Grain", "Fine irregular creases between small pebbled bumps, for leather, hide and elephant skin.",
     [] {
         return generate("Leather Grain", [](float u, float v) {
             float f1, f2;
             worley(u * 11.0f + 7.0f, v * 11.0f + 7.0f, 95, f1, f2);
             const float pebble = smooth01((f2 - f1) / 0.22f);
             float g1, g2;
             worley(u * 4.0f + 13.0f, v * 4.0f + 13.0f, 97, g1, g2);
             const float crease = smooth01((g2 - g1) / 0.06f); // a sparse web of deeper creases
             return std::clamp(pebble * 0.7f * crease + 0.15f * fbm(u * 20.0f, v * 20.0f, 99), 0.0f, 1.0f) *
                    squareEdge(u, v);
         });
     }},
    {"Hex Plates", "A honeycomb of flat hexagonal plates with bevelled gaps, for armour, sci-fi panels and shells.",
     [] {
         return generate("Hex Plates", [](float u, float v) {
             const float scale = 3.0f; // plates across
             float x = (u + 1.0f) * scale, y = (v + 1.0f) * scale;
             // Nearest centre on a hex lattice (two offset rectangular grids).
             const float w = 1.0f, h = 1.7320508f;
             auto near = [&](float ox, float oy) {
                 const float cx = (std::floor((x - ox) / w) + 0.5f) * w + ox;
                 const float cy = (std::floor((y - oy) / h) + 0.5f) * h + oy;
                 return glm::vec2(x - cx, y - cy);
             };
             glm::vec2 a = near(0.0f, 0.0f), b = near(0.5f, h * 0.5f);
             const glm::vec2 d = glm::dot(a, a) < glm::dot(b, b) ? a : b;
             // Hexagon distance (flat sides left and right).
             const glm::vec2 q = glm::abs(d);
             const float hex = std::max(q.x, q.x * 0.5f + q.y * 0.8660254f);
             return smooth01((0.5f - hex) / 0.09f) * squareEdge(u, v);
         });
     }},
    {"Knit", "Rows of interlocking V-shaped loops, for knitted wool and chunky fabric.",
     [] {
         return generate("Knit", [](float u, float v) {
             const float cols = 5.0f, rows = 7.0f;
             const float x = (u + 1.0f) * cols * 0.5f, y = (v + 1.0f) * rows * 0.5f;
             const float fx = x - std::floor(x) - 0.5f, fy = y - std::floor(y);
             // Each stitch is two leaning loops meeting at the bottom like a V.
             const float lx = std::abs(fx) - 0.25f - 0.15f * (fy - 0.5f);
             const float loop = std::sqrt(std::max(0.0f, 1.0f - (lx * lx) / 0.04f - (fy - 0.5f) * (fy - 0.5f) / 0.36f));
             return loop * squareEdge(u, v);
         });
     }},
    {"Bolt", "One hexagonal bolt head on a round washer, for machines and hard-surface props (stamp it).",
     [] {
         return generate("Bolt", [](float u, float v) {
             const float r = std::hypot(u, v);
             const float washer = 0.45f * (1.0f - smooth01((r - 0.82f) / 0.06f));
             const glm::vec2 q = glm::abs(glm::vec2(u, v));
             const float hex = std::max(q.x * 0.8660254f + q.y * 0.5f, q.y);
             const float head = (1.0f - smooth01((hex - 0.55f) / 0.04f)) * (0.85f + 0.15f * (1.0f - r));
             return std::max(washer, head);
         });
     }},
    {"Chips", "Broken, angular chips knocked out of an edge, for worn stone, wood and painted metal.",
     [] {
         return generate("Chips", [](float u, float v) {
             float f1, f2;
             uint32_t id = 0;
             worley(u * 4.5f + 60.0f, v * 4.5f + 60.0f, 101, f1, f2, &id);
             if ((id & 3u) == 0u) return 0.0f; // a quarter of the cells stay whole
             const float facet = 1.0f - smooth01(f1 / (0.35f + 0.2f * float((id >> 2) & 0xffu) / 255.0f));
             return std::clamp(facet * 1.6f, 0.0f, 1.0f) * roundEdge(u, v);
         });
     }},
    {"Weathered Grain", "Long, uneven grain lines with worn-down soft wood between, for old boards and driftwood.",
     [] {
         return generate("Weathered Grain", [](float u, float v) {
             const float warp = (fbm(u * 1.5f + 9.0f, v * 0.5f, 111, 3) - 0.5f) * 0.9f;
             const float line = 1.0f - std::abs(std::sin((u * 5.0f + warp) * kPi));
             const float hard = std::pow(line, 2.5f);                        // thin hard late-wood lines
             const float wear = fbm(u * 3.0f, v * 7.0f + 5.0f, 113, 3);      // soft wood wears away in patches
             return std::clamp(hard * (0.45f + 0.7f * wear) + 0.2f * wear, 0.0f, 1.0f) * squareEdge(u, v);
         });
     }},
    {"Splinters", "A fan of thin, sharp splinters and torn fibres, for broken planks and snapped wood.",
     [] {
         return generate("Splinters", [](float u, float v) {
             float best = 0.0f;
             for (int k = 0; k < 9; ++k) {
                 const float h = hash01(k, 3, 117);
                 const float x0 = (float(k) - 4.0f) * 0.17f + (h - 0.5f) * 0.1f;
                 const float len = 0.45f + 0.5f * hash01(k, 5, 119);
                 const float lean = (hash01(k, 7, 121) - 0.5f) * 0.35f;
                 const float y0 = -0.9f, y1 = -0.9f + len * 1.8f;
                 const float d = segmentDistance(u, v, x0, y0, x0 + lean * (y1 - y0), y1);
                 const float along = smooth01((v - y0) / std::max(y1 - y0, 0.01f));
                 const float width = 0.075f * (1.0f - along * 0.85f) + 0.008f; // tapers to a point
                 best = std::max(best, 1.0f - smooth01(d / width));
             }
             return best * squareEdge(u, v);
         });
     }},
    {"Grain Cracks", "Narrow splits that run along the grain and fork, for dried and checked timber.",
     [] {
         return generate("Grain Cracks", [](float u, float v) {
             float best = 0.0f;
             for (int k = 0; k < 4; ++k) {
                 const float cx = (float(k) - 1.5f) * 0.45f + (hash01(k, 2, 123) - 0.5f) * 0.2f;
                 const float len = 0.6f + 0.35f * hash01(k, 4, 125);
                 const float off = (hash01(k, 6, 127) - 0.5f) * 0.5f;
                 const float wob = (fbm(v * 3.0f + float(k) * 5.0f, 2.0f, 129, 2) - 0.5f) * 0.18f;
                 const float d = std::abs(u - cx - wob - 0.12f * (v + off) * (hash01(k, 8, 131) - 0.5f));
                 const float taper = smooth01((len - std::abs(v - off)) / 0.3f);
                 best = std::max(best, (1.0f - smooth01(d / (0.045f * taper + 0.006f))) * taper);
             }
             return best * squareEdge(u, v);
         });
     }},
    {"Knot", "A round knot where the grain bends around a dark core, for boards, beams and trunks.",
     [] {
         return generate("Knot", [](float u, float v) {
             const float r = std::hypot(u * 1.15f, v * 0.85f);
             const float rings = 0.5f + 0.5f * std::cos(r * 9.0f * kPi + (fbm(u * 3.0f + 2.0f, v * 3.0f, 133) - 0.5f));
             const float core = 1.0f - smooth01(r / 0.28f);
             const float body = (1.0f - smooth01((r - 0.45f) / 0.35f)) * (0.55f + 0.3f * rings);
             const float lines = (1.0f - smooth01(std::abs(v) / 0.9f)) * 0.15f * (0.5f + 0.5f * std::sin(u * 14.0f));
             return std::clamp(std::max(body, core) + lines, 0.0f, 1.0f) * roundEdge(u, v);
         });
     }},
    {"End Rings", "Concentric growth rings with radial checks, as on a sawn log end.",
     [] {
         return generate("End Rings", [](float u, float v) {
             const float cx = 0.08f, cy = -0.05f; // the pith sits a little off centre
             const float r = std::hypot(u - cx, v - cy) * (1.0f + 0.04f * std::sin(std::atan2(v - cy, u - cx) * 3.0f));
             const float wob = (fbm(u * 2.5f + 1.0f, v * 2.5f, 135) - 0.5f) * 0.12f;
             const float ring = std::pow(0.5f + 0.5f * std::cos((r + wob) * 13.0f * kPi * 0.5f), 1.6f);
             const float ang = std::atan2(v - cy, u - cx);
             const float check = smooth01(std::abs(std::sin(ang * 2.5f + 0.7f)) * 6.0f - 5.0f) * smooth01(r / 0.2f);
             return std::clamp(ring * (1.0f - 0.8f * check), 0.0f, 1.0f) * roundEdge(u, v);
         });
     }},
    {"Plank Seams", "Two boards meeting at a dark seam, with grain and nail dimples, for floors, decks and doors.",
     [] {
         return generate("Plank Seams", [](float u, float v) {
             const float seam = smooth01((std::abs(u) - 0.035f) / 0.09f);   // 0 in the seam, 1 on the boards
             const float side = u < 0.0f ? 0.0f : 1.0f;
             const float level = 0.72f + 0.14f * side;                       // each board sits at its own height
             const float grain = 0.1f * std::sin((u * 26.0f + 3.0f * fbm(u * 2.0f, v * 0.7f, 139)) * kPi);
             const float nail = 1.0f - smooth01(std::hypot(std::abs(u) - 0.55f, std::fmod(std::abs(v) + 0.5f, 1.0f) - 0.5f) / 0.09f);
             return std::clamp(seam * (level + grain) - 0.3f * nail * seam, 0.0f, 1.0f) * squareEdge(u, v);
         });
     }},
    // Procedural stand-ins for scanned boards (the alpha pool, core/AlphaOptions.h, takes real scans from a folder).
    {"Procedural Wood Straight", "Made in code, not a scan: fine straight grain lines with pores, for planed boards.",
     [] {
         return generate("Procedural Wood Straight", [](float u, float v) {
             const float warp = (fbm(u * 1.2f + 3.0f, v * 0.35f, 141, 3) - 0.5f) * 0.35f;
             const float ring = 0.5f + 0.5f * std::sin((u * 11.0f + warp) * kPi);
             const float late = std::pow(ring, 3.0f);                       // thin hard late-wood lines
             const float pores = fbm(u * 14.0f, v * 1.6f + 7.0f, 143, 2);   // long, thin pores along the grain
             const float fine = std::pow(std::max(pores - 0.48f, 0.0f) * 3.0f, 1.5f);
             return std::clamp(0.25f + 0.55f * late - 0.45f * fine, 0.0f, 1.0f) * squareEdge(u, v);
         });
     }},
    {"Procedural Wood Cathedral", "Made in code, not a scan: nested pointed arches of flat-sawn grain, for boards and veneers.",
     [] {
         return generate("Procedural Wood Cathedral", [](float u, float v) {
             const float wob = (fbm(u * 2.0f + 5.0f, v * 1.0f, 145, 3) - 0.5f) * 0.25f;
             const float d = std::abs(u) * 1.6f + (1.0f - (v * 0.5f + 0.5f)) * 0.55f * (1.0f - std::abs(u)) + wob; // arches opening downward
             const float rings = 0.5f + 0.5f * std::cos(d * 9.0f * kPi * 0.5f);
             const float late = std::pow(rings, 2.2f);
             const float fibre = fbm(u * 18.0f, v * 2.0f + 11.0f, 147, 2);
             return std::clamp(0.2f + 0.65f * late + 0.15f * fibre, 0.0f, 1.0f) * squareEdge(u, v);
         });
     }},
    {"Procedural Wood Weathered", "Made in code, not a scan: sun-bleached board with raised grain, worn pits and a few checks.",
     [] {
         return generate("Procedural Wood Weathered", [](float u, float v) {
             const float warp = (fbm(u * 1.4f + 2.0f, v * 0.4f, 149, 3) - 0.5f) * 0.6f;
             const float line = std::pow(1.0f - std::abs(std::sin((u * 7.0f + warp) * kPi)), 3.0f);
             const float wear = fbm(u * 4.0f, v * 2.5f + 3.0f, 151, 3);
             const float pit = smooth01((fbm(u * 22.0f, v * 22.0f, 153, 2) - 0.62f) / 0.12f);
             const float check = (1.0f - smooth01(std::abs(u - 0.35f - 0.1f * std::sin(v * 5.0f)) / 0.015f)) * smooth01((0.7f - std::abs(v)) / 0.2f);
             return std::clamp(line * (0.4f + 0.8f * wear) + 0.15f * wear - 0.35f * pit - 0.5f * check, 0.0f, 1.0f) * squareEdge(u, v);
         });
     }},
};

// Creature Skin (core/CreatureSkin.h): rows in parallel, then stretched to 0..1 between the 0.05% and 99.95% levels
// (each generator shapes its relief; this sets the range, so a few stray pixels can't flatten the rest).
// `ownThreads`: rows on threads of its own instead of the shared pool (a prefetch in the background must not take
// the pool from a stroke, which would then run on one thread).
BrushAlpha makeCreature(const CreatureAlpha& c, int n, int ownThreads = 0) {
    BrushAlpha a;
    a.name = c.name;
    a.width = a.height = n;
    a.values.resize(size_t(n) * size_t(n));
    auto rows = [&](size_t y0, size_t y1, size_t step) {
        for (size_t y = y0; y < y1; y += step)
            for (int x = 0; x < n; ++x)
                a.values[y * size_t(n) + size_t(x)] = c.height((float(x) + 0.5f) / float(n), (float(y) + 0.5f) / float(n));
    };
    if (ownThreads > 0) {
        std::vector<std::thread> workers;
        for (int t = 0; t < ownThreads; ++t) workers.emplace_back([&rows, t, ownThreads, n] { rows(size_t(t), size_t(n), size_t(ownThreads)); });
        for (std::thread& w : workers) w.join();
    } else {
        poolFor(size_t(n), [&](size_t y0, size_t y1) { rows(y0, y1, 1); }, 8);
    }
    const auto [mn, mx] = std::minmax_element(a.values.begin(), a.values.end());
    const float lo0 = *mn, span0 = std::max(*mx - *mn, 1e-6f);
    constexpr int kBins = 4096;
    std::vector<size_t> hist(kBins, 0);
    for (float v : a.values) ++hist[size_t(std::min(int((v - lo0) / span0 * float(kBins)), kBins - 1))];
    const size_t cut = a.values.size() / 2000;
    int b0 = 0, b1 = kBins - 1;
    for (size_t sum = 0; b0 < kBins - 1 && (sum += hist[size_t(b0)]) <= cut;) ++b0;
    for (size_t sum = 0; b1 > b0 && (sum += hist[size_t(b1)]) <= cut;) --b1;
    const float lo = lo0 + span0 * float(b0) / float(kBins), hi = lo0 + span0 * float(b1 + 1) / float(kBins);
    for (float& v : a.values) v = std::clamp((v - lo) / std::max(hi - lo, 1e-6f), 0.0f, 1.0f);
    return a;
}

int creatureIndex(const std::string& key) {
    size_t n = 0;
    const CreatureAlpha* all = creatureAlphas(n);
    for (size_t i = 0; i < n; ++i)
        if (lookupKey(all[i].name) == key) return int(i);
    return -1;
}

// The full-size one, made once (the first brush that needs it pays about a tenth of a second on the pool).
std::atomic<bool> gDeferCreature{false};
std::atomic<uint64_t> gCreatureReady{0}; // one bit per Creature Skin alpha made at full size

struct CreatureStore {
    size_t count = 0;
    const CreatureAlpha* all = creatureAlphas(count);
    std::unique_ptr<std::once_flag[]> once{new std::once_flag[count]};
    std::unique_ptr<std::shared_ptr<const BrushAlpha>[]> made{new std::shared_ptr<const BrushAlpha>[count]};
};
CreatureStore& creatureStore() {
    static CreatureStore store;
    return store;
}

const BrushAlpha& creatureFull(int i, int ownThreads = 0) {
    CreatureStore& s = creatureStore();
    std::call_once(s.once[size_t(i)], [&] {
        s.made[size_t(i)] = std::make_shared<const BrushAlpha>(makeCreature(s.all[i], kCreatureSize, ownThreads));
        gCreatureReady.fetch_or(uint64_t(1) << i);
    });
    return *s.made[size_t(i)];
}

// Background prefetches, joined at exit before the store goes (made after it, so destroyed first).
struct Prefetches {
    std::mutex mutex;
    std::vector<std::thread> threads;
    uint64_t started = 0; // one bit per Creature Skin alpha
    ~Prefetches() {
        for (std::thread& t : threads)
            if (t.joinable()) t.join();
    }
};


// Grey (luminance x alpha) planes from a file, at full precision: 16-bit PNG and OpenEXR (half or float, any
// compression tinyexr reads) without an 8-bit step; other formats through Image. False with `error` when unreadable.
bool loadGrey(const std::string& path, int& w, int& h, std::vector<float>& grey, std::string& error) {
    std::ifstream f(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
    if (!f) {
        error = "Cannot open " + path;
        return false;
    }
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto lum = [](float r, float g, float b) { return 0.2126f * r + 0.7152f * g + 0.0722f * b; };
    if (bytes.size() >= 4 && IsEXRFromMemory(bytes.data(), bytes.size()) == TINYEXR_SUCCESS) {
        EXRVersion version;
        EXRHeader header;
        InitEXRHeader(&header);
        bool hasAlpha = false, single = false;
        if (ParseEXRVersionFromMemory(&version, bytes.data(), bytes.size()) == TINYEXR_SUCCESS &&
            ParseEXRHeaderFromMemory(&header, &version, bytes.data(), bytes.size(), nullptr) == TINYEXR_SUCCESS) {
            single = header.num_channels == 1;
            for (int c = 0; c < header.num_channels; ++c) hasAlpha |= std::string(header.channels[c].name) == "A";
            FreeEXRHeader(&header);
        }
        float* rgba = nullptr;
        const char* err = nullptr;
        if (LoadEXRFromMemory(&rgba, &w, &h, bytes.data(), bytes.size(), &err) != TINYEXR_SUCCESS) {
            error = std::string("Can't read the EXR: ") + (err ? err : "unknown error");
            if (err) FreeEXRErrorMessage(err);
            return false;
        }
        if (w <= 0 || h <= 0 || size_t(w) * size_t(h) > size_t(16384) * 16384) {
            std::free(rgba);
            error = "The EXR is empty or too large";
            return false;
        }
        grey.resize(size_t(w) * size_t(h));
        for (size_t i = 0; i < grey.size(); ++i) {
            const float* p = &rgba[i * 4];
            const float a = (hasAlpha && !single) ? std::clamp(p[3], 0.0f, 1.0f) : 1.0f; // (one channel fills all four)
            grey[i] = (single ? p[0] : lum(p[0], p[1], p[2])) * a;
            if (!std::isfinite(grey[i])) grey[i] = 0.0f;
        }
        std::free(rgba);
        // A height map in its own units (negative, or above 1): its lowest to 0 and highest to 1.
        const auto [mn, mx] = std::minmax_element(grey.begin(), grey.end());
        if (*mn < 0.0f || *mx > 1.0f) {
            const float lo = *mn, span = std::max(*mx - *mn, 1e-12f);
            for (float& v : grey) v = (v - lo) / span;
        }
        return true;
    }
    if (bytes.size() >= 8 && bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G') {
        std::vector<uint16_t> px;
        int channels = 0;
        if (!decodePng16(bytes.data(), bytes.size(), w, h, channels, px, error, 4)) return false;
        grey.resize(size_t(w) * size_t(h));
        for (size_t i = 0; i < grey.size(); ++i) {
            const uint16_t* p = &px[i * 4];
            grey[i] = lum(p[0], p[1], p[2]) / 65535.0f * (float(p[3]) / 65535.0f);
        }
        return true;
    }
    Image img;
    if (!Image::decode(bytes.data(), bytes.size(), img, error)) return false;
    w = img.width;
    h = img.height;
    grey.resize(size_t(w) * size_t(h));
    for (size_t i = 0; i < grey.size(); ++i) {
        const uint8_t* p = &img.rgba[i * 4];
        grey[i] = lum(p[0], p[1], p[2]) / 255.0f * (float(p[3]) / 255.0f);
    }
    return true;
}

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
        size_t n = 0;
        const CreatureAlpha* creature = creatureAlphas(n);
        for (size_t i = 0; i < n; ++i) v.push_back(makeCreature(creature[i], kCreaturePreview)); // (tiles only)
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
    size_t n = 0;
    const CreatureAlpha* creature = creatureAlphas(n);
    if (const int i = creatureIndex(key); i >= 0) return creature[i].help;
    return {};
}

void BrushAlpha::prefetch(const std::string& name) {
    const int i = creatureIndex(lookupKey(name));
    if (i < 0) return;
    creatureStore();
    static Prefetches pre;
    std::lock_guard lock(pre.mutex);
    if (pre.started & (uint64_t(1) << i)) return;
    pre.started |= uint64_t(1) << i;
    const int threads = std::max(2, int(std::thread::hardware_concurrency()) / 2);
    pre.threads.emplace_back([i, threads] { creatureFull(i, threads); });
}

void BrushAlpha::setDeferCreature(bool defer) { gDeferCreature = defer; }

std::shared_ptr<const BrushAlpha> BrushAlpha::shared(const BrushAlpha& a) {
    if (!a.path.empty() || a.width != kCreatureSize || a.tiled()) return nullptr;
    const int i = creatureIndex(lookupKey(a.name));
    if (i < 0 || !(gCreatureReady.load() & (uint64_t(1) << i))) return nullptr;
    const std::shared_ptr<const BrushAlpha>& made = creatureStore().made[size_t(i)];
    if (made->values.size() != a.values.size()) return nullptr;
    for (size_t k = 0; k < 64; ++k) // (a copy of it, not an edited one: a few samples are enough)
        if (made->values[k * a.values.size() / 64] != a.values[k * a.values.size() / 64]) return nullptr;
    return made;
}

bool BrushAlpha::finishCreature(BrushAlpha& a, bool wait) {
    if (!a.pending) return false;
    const int i = creatureIndex(lookupKey(a.name));
    if (i < 0 || (!wait && !(gCreatureReady.load() & (uint64_t(1) << i)))) return false;
    const BrushAlpha& full = creatureFull(i);
    a.width = full.width;
    a.height = full.height;
    a.values = full.values; // (Tile and the rest stay as they are)
    a.pending = false;
    return true;
}

std::string BrushAlpha::builtinGroup(const std::string& name) {
    return creatureIndex(lookupKey(name)) >= 0 ? "Creature Skin" : std::string();
}

std::string BrushAlpha::builtinName(const std::string& name) {
    const std::string key = lookupKey(name);
    if (key == "none" || key.empty()) return "Round";
    for (const BrushAlpha& a : builtins())
        if (lookupKey(a.name) == key) return a.name;
    return {};
}

bool BrushAlpha::builtinPreview(const std::string& name, BrushAlpha& out) {
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
    if (const int i = creatureIndex(key); i >= 0) {
        if (gDeferCreature && !(gCreatureReady.load() & (uint64_t(1) << i))) { // the tile now, the full one soon
            prefetch(name);
            const bool ok = builtinPreview(name, out);
            out.pending = ok;
            return ok;
        }
        out = creatureFull(i);
        return true;
    }
    return builtinPreview(name, out);
}

bool BrushAlpha::builtinAt(const std::string& name, int size, BrushAlpha& out) {
    const std::string key = lookupKey(name);
    for (const Generated& g : kGenerated)
        if (lookupKey(g.name) == key) {
            gGenerateSize = std::clamp(size, 8, 4096);
            out = g.make();
            gGenerateSize = kBuiltinSize;
            return true;
        }
    if (const int i = creatureIndex(key); i >= 0) {
        size_t n = 0;
        out = makeCreature(creatureAlphas(n)[i], std::clamp(size, 8, 4096));
        return true;
    }
    return builtin(name, out); // (Round, Square, Noise: as made)
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

bool BrushAlpha::fromFile(const std::string& path, std::string name, bool invert, BrushAlpha& out, std::string& error,
                          int maxSide) {
    int w = 0, h = 0;
    std::vector<float> grey;
    if (!loadGrey(path, w, h, grey, error)) return false;
    // Box-filtered down to maxSide, in floats (no 8-bit step).
    const int side = std::max(w, h), cap = std::clamp(maxSide, 8, 8192);
    const int k = side > cap ? (side + cap - 1) / cap : 1;
    BrushAlpha a;
    a.name = std::move(name);
    a.width = std::max(1, w / k);
    a.height = std::max(1, h / k);
    a.values.resize(size_t(a.width) * size_t(a.height));
    for (int y = 0; y < a.height; ++y)
        for (int x = 0; x < a.width; ++x) {
            float sum = 0.0f;
            int cnt = 0;
            for (int yy = y * k; yy < std::min(h, (y + 1) * k); ++yy)
                for (int xx = x * k; xx < std::min(w, (x + 1) * k); ++xx, ++cnt) sum += grey[size_t(yy) * size_t(w) + size_t(xx)];
            const float l = std::clamp(sum / float(std::max(cnt, 1)), 0.0f, 1.0f);
            a.values[size_t(y) * size_t(a.width) + size_t(x)] = invert ? 1.0f - l : l;
        }
    a.path = path;
    a.inverted = invert;
    out = std::move(a);
    return true;
}

void registerBrushCommands(CommandRegistry& registry) {
    using T = ParamSpec::Type;
    registry.add({"brush_set_alpha",
                  "Set the brush alpha (grayscale stamp shaping the falloff of every brush): a built-in "
                  "(round = none, square, noise, pores, cracks, scales, rivets, stitches, fabric weave, bark, rock "
                  "strata, wrinkles, skin folds, hair strands, dot grid, the Creature Skin set: elephant hide, snake scales, "
                  "pores fine, nose pores open, skin crosshatch...) or a grayscale image file.",
                  {{"name", T::String, "Built-in alpha: round, square, noise, pores, cracks, scales, rivets, stitches, "
                                       "fabric_weave, bark, rock_strata, wrinkles, skin_folds, hair_strands or dot_grid.", false},
                   {"path", T::String, "Image file (png, jpg, exr, ...; 16-bit PNG and EXR keep their precision) used as the alpha; overrides name.", false},
                   {"invert", T::Bool, "Use the image's negative (white = no effect).", false}},
                  [](Document& doc, const Args& a) -> CommandResult {
                      const std::string path = argString(a, "path");
                      if (!path.empty()) {
                          std::string err;
                          auto stem = std::filesystem::path(std::u8string(path.begin(), path.end())).stem().u8string();
                          BrushAlpha loaded;
                          if (!BrushAlpha::fromFile(path, std::string(stem.begin(), stem.end()), argBool(a, "invert", false),
                                                    loaded, err))
                              return {false, err};
                          doc.brushAlpha = std::move(loaded);
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
