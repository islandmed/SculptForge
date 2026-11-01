// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Vdm.h"
#include "core/Masking.h"

#include "core/BrushLibrary.h"
#include "core/Pack.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/PathStroke.h"
#include "core/Primitives.h"
#include "core/Sculpt.h"
#include "core/ThreadPool.h"

#include "tinyexr.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

namespace sf {

namespace fs = std::filesystem;

namespace {

constexpr int kMaxVdmSide = 2048;
constexpr char kSfvdmMagic[8] = {'S', 'F', 'V', 'D', 'M', '1', '\n', '\0'};

fs::path utf8Path(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string pathString(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}
std::string extensionOf(const std::string& path) { return lower(pathString(utf8Path(path).extension())); }
std::string stemOf(const std::string& path) { return pathString(utf8Path(path).stem()); }

bool readBytes(const std::string& path, std::vector<unsigned char>& out, size_t limit, std::string& error) {
    std::ifstream f(utf8Path(path), std::ios::binary);
    if (!f) {
        error = "Can't open " + path;
        return false;
    }
    f.seekg(0, std::ios::end);
    const std::streamoff size = f.tellg();
    if (size < 0 || size_t(size) > limit) {
        error = path + " is too large";
        return false;
    }
    f.seekg(0);
    out.resize(size_t(size));
    if (size > 0 && !f.read(reinterpret_cast<char*>(out.data()), size)) {
        error = "Can't read " + path;
        return false;
    }
    return true;
}

bool writeBytes(const std::string& path, const void* data, size_t size, std::string& error) {
    std::error_code ec;
    const fs::path p = utf8Path(path);
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f || !f.write(static_cast<const char*>(data), std::streamsize(size))) {
        error = "Can't write " + path;
        return false;
    }
    return true;
}

float smooth01(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

void applyImport(VectorDisplacement& vdm, const VdmImportOptions& opt) {
    for (glm::vec3& v : vdm.vectors) {
        if (opt.swapYZ) std::swap(v.y, v.z);
        if (opt.flipX) v.x = -v.x;
        if (opt.flipY) v.y = -v.y;
        if (opt.flipZ) v.z = -v.z;
        v *= opt.scale;
    }
}

// --- Procedural maps: f(x, y) is where the point (x, y, 0) of the flat square goes (x, y in -1..1). ---

template <class F>
VectorDisplacement generate(std::string name, int size, F&& f) {
    VectorDisplacement m;
    m.name = std::move(name);
    m.builtin = true;
    m.width = m.height = std::max(size, 8);
    m.vectors.resize(size_t(m.width) * size_t(m.height));
    poolFor(size_t(m.height), [&](size_t b, size_t e) {
        for (size_t row = b; row < e; ++row)
            for (int col = 0; col < m.width; ++col) {
                const float x = (float(col) + 0.5f) / float(m.width) * 2.0f - 1.0f;
                const float y = 1.0f - (float(row) + 0.5f) / float(m.height) * 2.0f;
                m.vectors[row * size_t(m.width) + size_t(col)] = f(x, y) - glm::vec3(x, y, 0.0f);
            }
    }, 16);
    return m;
}

// A tube grown out of the disc of radius `base`: the disc's rim stays put, its centre becomes the tip. The
// centre line bends toward +Y (`bend` radians over `length`; < 0 length digs in instead of rising); the cross
// section flattens to `flat` (along the bend) and tapers with `power`.
struct Tube {
    float base = 0.62f, length = 1.0f, bend = 1.1f, flat = 1.0f, power = 1.3f, rise = 1.6f;
};

glm::vec3 tubePoint(const Tube& h, float x, float y) {
    const float rho = std::sqrt(x * x + y * y);
    if (rho >= h.base) return {x, y, 0.0f};
    const float theta = std::atan2(y, x);
    const float s = 1.0f - rho / h.base;          // 0 at the rim, 1 at the tip
    const float len = std::abs(h.length);
    const float t = len * std::pow(s, h.rise);    // arc length: slow at first, a soft fillet into the surface
    const float depth = h.length < 0.0f ? -1.0f : 1.0f;
    const float kappa = h.bend / len;
    const float phi = kappa * t;
    glm::vec3 c(0.0f, 0.0f, t * depth);
    if (std::abs(kappa) > 1e-5f) c = glm::vec3(0.0f, (1.0f - std::cos(phi)) / kappa, depth * std::sin(phi) / kappa);
    const glm::vec3 e1(1.0f, 0.0f, 0.0f), e2(0.0f, std::cos(phi), -depth * std::sin(phi));
    const float radius = h.base * std::pow(1.0f - s, h.power);
    const float flat = 1.0f + (h.flat - 1.0f) * smooth01(s / 0.3f);
    return c + radius * (std::cos(theta) * e1 + flat * std::sin(theta) * e2);
}

} // namespace

// ---------------------------------------------------------------------------
// VectorDisplacement
// ---------------------------------------------------------------------------

glm::vec3 VectorDisplacement::sample(float u, float v) const {
    if (empty() || u < -1.0f || u > 1.0f || v < -1.0f || v > 1.0f) return glm::vec3(0.0f);
    // Pixel centres span the square, as BrushAlpha::sample.
    const float px = std::clamp((u * 0.5f + 0.5f) * float(width) - 0.5f, 0.0f, float(width - 1));
    const float py = std::clamp((0.5f - v * 0.5f) * float(height) - 0.5f, 0.0f, float(height - 1));
    const int x0 = int(px), y0 = int(py);
    const int x1 = std::min(x0 + 1, width - 1), y1 = std::min(y0 + 1, height - 1);
    const float tx = px - float(x0), ty = py - float(y0);
    auto at = [&](int x, int y) { return vectors[size_t(y) * size_t(width) + size_t(x)]; };
    const glm::vec3 a = glm::mix(at(x0, y0), at(x1, y0), tx);
    const glm::vec3 b = glm::mix(at(x0, y1), at(x1, y1), tx);
    return glm::mix(a, b, ty);
}

float VectorDisplacement::extent() const {
    float m = 0.0f;
    for (const glm::vec3& v : vectors) m = std::max(m, glm::length(v));
    return m;
}

uint64_t VectorDisplacement::hash() const {
    uint64_t h = 0xcbf29ce484222325ull;
    auto mix = [&h](const void* data, size_t n) {
        const auto* p = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 0x100000001b3ull;
    };
    mix(&width, sizeof width);
    mix(&height, sizeof height);
    mix(vectors.data(), vectors.size() * sizeof(glm::vec3));
    return h;
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------

bool decodeVdmExr(const unsigned char* bytes, size_t size, VectorDisplacement& out, std::string& error,
                  const VdmImportOptions& opt) {
    if (!bytes || size == 0 || IsEXRFromMemory(bytes, size) != TINYEXR_SUCCESS) {
        error = "Not an OpenEXR file";
        return false;
    }
    float* rgba = nullptr;
    int w = 0, h = 0;
    const char* err = nullptr;
    // tinyexr reads the R, G, B (and A) channels, or a single channel into all four.
    if (LoadEXRFromMemory(&rgba, &w, &h, bytes, size, &err) != TINYEXR_SUCCESS) {
        error = std::string("Can't read the EXR: ") + (err ? err : "unknown error");
        if (err) FreeEXRErrorMessage(err);
        return false;
    }
    if (w <= 0 || h <= 0 || w > kMaxVdmSide || h > kMaxVdmSide) {
        std::free(rgba);
        error = "The EXR must be 1 to " + std::to_string(kMaxVdmSide) + " pixels a side";
        return false;
    }
    // Grayscale (one channel, so R = G = B = A): a height map along the normal.
    EXRVersion version;
    EXRHeader header;
    InitEXRHeader(&header);
    bool gray = false;
    if (ParseEXRVersionFromMemory(&version, bytes, size) == TINYEXR_SUCCESS &&
        ParseEXRHeaderFromMemory(&header, &version, bytes, size, nullptr) == TINYEXR_SUCCESS) {
        gray = header.num_channels == 1;
        FreeEXRHeader(&header);
    }
    out.width = w;
    out.height = h;
    out.vectors.resize(size_t(w) * size_t(h));
    for (size_t i = 0; i < out.vectors.size(); ++i) {
        const float* p = rgba + i * 4;
        glm::vec3 v = gray ? glm::vec3(0.0f, 0.0f, p[0]) : glm::vec3(p[0], p[1], p[2]);
        for (int k = 0; k < 3; ++k)
            if (!std::isfinite(v[k])) v[k] = 0.0f;
        out.vectors[i] = v;
    }
    std::free(rgba);
    applyImport(out, opt);
    return true;
}

bool readVdmExr(const std::string& path, VectorDisplacement& out, std::string& error, const VdmImportOptions& opt) {
    std::vector<unsigned char> bytes;
    if (!readBytes(path, bytes, size_t(1) << 30, error)) return false;
    if (!decodeVdmExr(bytes.data(), bytes.size(), out, error, opt)) {
        error = path + ": " + error;
        return false;
    }
    out.name = stemOf(path);
    out.path = path;
    out.builtin = false;
    return true;
}

bool encodeVdmExr(const VectorDisplacement& vdm, std::vector<unsigned char>& out, std::string& error) {
    if (vdm.empty()) {
        error = "The VDM is empty";
        return false;
    }
    std::vector<float> rgb(vdm.vectors.size() * 3);
    for (size_t i = 0; i < vdm.vectors.size(); ++i) {
        rgb[i * 3] = vdm.vectors[i].x;
        rgb[i * 3 + 1] = vdm.vectors[i].y;
        rgb[i * 3 + 2] = vdm.vectors[i].z;
    }
    unsigned char* buffer = nullptr;
    const char* err = nullptr;
    const int n = SaveEXRToMemory(rgb.data(), vdm.width, vdm.height, 3, 0, &buffer, &err);
    if (n <= 0 || !buffer) {
        error = std::string("Can't encode the EXR: ") + (err ? err : "unknown error");
        if (err) FreeEXRErrorMessage(err);
        std::free(buffer);
        return false;
    }
    out.assign(buffer, buffer + n);
    std::free(buffer);
    return true;
}

bool writeVdmExr(const std::string& path, const VectorDisplacement& vdm, std::string& error) {
    std::vector<unsigned char> bytes;
    return encodeVdmExr(vdm, bytes, error) && writeBytes(path, bytes.data(), bytes.size(), error);
}

bool readVdmFile(const std::string& path, VectorDisplacement& out, std::string& error) {
    std::vector<unsigned char> b;
    if (!readBytes(path, b, size_t(kMaxVdmSide) * kMaxVdmSide * 12 + 4096, error)) return false;
    auto u32 = [&](size_t at) {
        return uint32_t(b[at]) | uint32_t(b[at + 1]) << 8 | uint32_t(b[at + 2]) << 16 | uint32_t(b[at + 3]) << 24;
    };
    if (b.size() < 20 || std::memcmp(b.data(), kSfvdmMagic, 8) != 0) {
        error = path + " is not a SculptForge VDM (.sfvdm)";
        return false;
    }
    const uint32_t w = u32(8), h = u32(12), nameLen = u32(16);
    if (w == 0 || h == 0 || w > uint32_t(kMaxVdmSide) || h > uint32_t(kMaxVdmSide) || nameLen > 1024 ||
        b.size() != 20 + size_t(nameLen) + size_t(w) * h * 12) {
        error = path + " is damaged (bad size)";
        return false;
    }
    out.name.assign(reinterpret_cast<const char*>(b.data() + 20), nameLen);
    if (out.name.empty()) out.name = stemOf(path);
    out.width = int(w);
    out.height = int(h);
    out.vectors.resize(size_t(w) * h);
    const unsigned char* p = b.data() + 20 + nameLen;
    for (size_t i = 0; i < out.vectors.size(); ++i)
        for (int k = 0; k < 3; ++k) {
            const uint32_t bits = uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
            p += 4;
            float f;
            std::memcpy(&f, &bits, 4);
            out.vectors[i][k] = std::isfinite(f) ? f : 0.0f;
        }
    out.path = path;
    out.builtin = false;
    return true;
}

bool writeVdmFile(const std::string& path, const VectorDisplacement& vdm, std::string& error) {
    if (vdm.empty()) {
        error = "The VDM is empty";
        return false;
    }
    std::vector<unsigned char> b(kSfvdmMagic, kSfvdmMagic + 8);
    auto put = [&b](uint32_t v) {
        for (int k = 0; k < 4; ++k) b.push_back(uint8_t(v >> (8 * k)));
    };
    const std::string name = vdm.name.substr(0, 1024);
    put(uint32_t(vdm.width));
    put(uint32_t(vdm.height));
    put(uint32_t(name.size()));
    b.insert(b.end(), name.begin(), name.end());
    b.reserve(b.size() + vdm.vectors.size() * 12);
    for (const glm::vec3& v : vdm.vectors)
        for (int k = 0; k < 3; ++k) {
            uint32_t bits;
            std::memcpy(&bits, &v[k], 4);
            put(bits);
        }
    return writeBytes(path, b.data(), b.size(), error);
}

bool isVdmFile(const std::string& path) {
    const std::string ext = extensionOf(path);
    return ext == ".sfvdm" || ext == ".exr";
}

bool loadVdm(const std::string& path, VectorDisplacement& out, std::string& error, const VdmImportOptions& opt) {
    if (extensionOf(path) == ".sfvdm") {
        if (!readVdmFile(path, out, error)) return false;
        applyImport(out, opt);
        return true;
    }
    return readVdmExr(path, out, error, opt);
}

// ---------------------------------------------------------------------------
// Built-ins
// ---------------------------------------------------------------------------

VectorDisplacement makeHornVdm(int size) {
    Tube h; // a horn that curls over toward the stamp's top
    h.base = 0.62f;
    h.length = 1.05f;
    h.bend = 1.15f;
    h.power = 1.25f;
    return generate("Horn", size, [&](float x, float y) { return tubePoint(h, x, y); });
}

VectorDisplacement makeEarCurlVdm(int size) {
    Tube h; // a flattened, blunt flap that curls right over: an ear rim, a fin, a leaf
    h.base = 0.58f;
    h.length = 1.2f;
    h.bend = 2.6f;
    h.flat = 0.42f;
    h.power = 0.75f;
    h.rise = 1.4f;
    return generate("Ear Curl", size, [&](float x, float y) {
        // The flap leans back toward the stamp's bottom first, so the curl sits over the middle.
        glm::vec3 p = tubePoint(h, x, y + 0.12f);
        p.y -= 0.12f;
        return p;
    });
}

VectorDisplacement makeScalesVdm(int size) {
    // Overlapping scales in staggered rows; each lip is pushed toward the stamp's bottom over the next row.
    struct Scale {
        glm::vec2 c;
        float r;
    };
    std::vector<Scale> scales;
    const float r = 0.25f;
    for (int row = 0; row < 3; ++row) {
        const float y = 0.42f - 0.38f * float(row);
        const int n = row == 1 ? 4 : 3;
        for (int i = 0; i < n; ++i) {
            const float x = (float(i) - float(n - 1) * 0.5f) * 0.38f;
            scales.push_back({glm::vec2(x, y), r});
        }
    }
    return generate("Scales", size, [&](float x, float y) {
        glm::vec3 best(x, y, 0.0f);
        float bestZ = 0.0f;
        for (const Scale& s : scales) {
            const glm::vec2 q = (glm::vec2(x, y) - s.c) / s.r;
            const float d = glm::length(q * glm::vec2(1.0f, 0.85f));
            if (d >= 1.0f) continue;
            const float plateau = smooth01((1.0f - d) / 0.28f);
            const float front = 0.55f + 0.45f * std::clamp((1.0f - q.y) * 0.5f, 0.0f, 1.0f); // higher at the lip
            const float z = 0.13f * plateau * front * (1.0f - 0.3f * d * d);
            if (z <= bestZ) continue;
            bestZ = z;
            best = glm::vec3(x, y - 0.075f * plateau, z); // the lip slides past its edge: an overhang
        }
        return best;
    });
}

VectorDisplacement makeNostrilVdm(int size) {
    Tube h; // a socket that dives in and bends under its top rim
    h.base = 0.6f;
    h.length = -0.75f;
    h.bend = 1.0f;
    h.power = 0.9f;
    h.rise = 1.35f;
    return generate("Nostril", size, [&](float x, float y) {
        glm::vec3 p = tubePoint(h, x, y);
        const float rho = std::sqrt(x * x + y * y);
        if (rho >= h.base) return p;
        // A raised rim that leans in over the opening.
        const float s = 1.0f - rho / h.base;
        const float bump = std::sin(3.14159265f * std::clamp(s / 0.42f, 0.0f, 1.0f));
        const glm::vec2 radial = rho > 1e-6f ? glm::vec2(x, y) / rho : glm::vec2(0.0f);
        p += glm::vec3(-radial * (0.09f * bump), 0.12f * bump);
        return p;
    });
}

namespace {

// 1 inside the stamp, 0 from 0.95 radii out: the generated maps below leave the square's rim where it was.
float rimFade(float x, float y) { return 1.0f - smooth01((std::sqrt(x * x + y * y) - 0.55f) / 0.4f); }

// Distance from p to the segment a-b, and the closest point on it.
float toSegment(glm::vec2 p, glm::vec2 a, glm::vec2 b, glm::vec2& closest) {
    const glm::vec2 v = b - a;
    const float t = std::clamp(glm::dot(p - a, v) / std::max(glm::dot(v, v), 1e-12f), 0.0f, 1.0f);
    closest = a + v * t;
    return glm::length(p - closest);
}

// A smooth, repeatable wiggle (sums of sines) for crack and wrinkle lines.
float wiggle(float t, float seed) {
    return 0.6f * std::sin(t * 3.1f + seed) + 0.3f * std::sin(t * 7.3f + seed * 2.1f) + 0.1f * std::sin(t * 17.0f + seed);
}

} // namespace

VectorDisplacement makeRivetVdm(int size) {
    // A domed head on a short neck, its rim bulging out over the plate a little.
    return generate("Rivet", size, [](float x, float y) {
        const float rho = std::sqrt(x * x + y * y), r = 0.42f;
        if (rho >= r + 0.2f) return glm::vec3(x, y, 0.0f);
        const glm::vec2 radial = rho > 1e-6f ? glm::vec2(x, y) / rho : glm::vec2(0.0f);
        if (rho < r) {
            const float s = rho / r;
            const float z = 0.06f + 0.24f * std::sqrt(std::max(0.0f, 1.0f - s * s)); // head on a 0.06 neck
            const float out = 0.07f * smooth01((s - 0.55f) / 0.45f);                    // the rim overhangs
            return glm::vec3(glm::vec2(x, y) + radial * out, z);
        }
        // The neck: from under the rim down to the plate.
        const float t = (rho - r) / 0.2f;
        const float z = 0.06f * (1.0f - smooth01(t));
        const float in = -0.04f * (1.0f - t) * (1.0f - smooth01(t));
        return glm::vec3(glm::vec2(x, y) + radial * in, z);
    });
}

VectorDisplacement makeStitchesVdm(int size) {
    // Three raised thread bars across a seam (drag along the seam: the stamp's Y); the threads draw the two sides
    // of the seam together, so the skin bunches toward it.
    return generate("Stitches", size, [](float x, float y) {
        const float fade = rimFade(x, y);
        float z = -0.06f * (1.0f - smooth01(std::abs(x) / 0.08f)); // the seam line itself
        for (int k = -1; k <= 1; ++k) {
            const float cy = float(k) * 0.33f;
            glm::vec2 c;
            const float d = toSegment(glm::vec2(x, y), glm::vec2(-0.32f, cy - 0.08f), glm::vec2(0.32f, cy + 0.08f), c);
            z = std::max(z, 0.11f * std::sqrt(std::max(0.0f, 1.0f - smooth01(d / 0.07f))));
        }
        const float gather = -0.1f * std::tanh(x / 0.12f) * std::exp(-x * x / 0.12f);
        return glm::vec3(x + gather * fade, y, z * fade);
    });
}

VectorDisplacement makeSkinFoldVdm(int size) {
    // A heavy roll of skin hanging over a crease (the stamp's bottom, -Y), as over an eyelid or a belly.
    return generate("Skin Fold", size, [](float x, float y) {
        const float crease = -0.15f + 0.25f * x * x;
        const float d = y - crease; // above the crease > 0
        const float fade = rimFade(x, y);
        const float roll = d > 0.0f ? std::exp(-d * d / 0.08f) : std::exp(-d * d / 0.004f);
        const float z = 0.22f * roll - 0.06f * std::exp(-d * d / 0.003f); // a deep line where it folds under
        const float hang = d > 0.0f ? -0.16f * std::exp(-d * d / 0.03f) : 0.0f; // slides down over the crease
        return glm::vec3(x, y + hang * fade, z * fade);
    });
}

VectorDisplacement makeWrinkleVdm(int size) {
    // A fine crease: skin pinched in from both sides and pressed down along a slightly wavy line (along X).
    return generate("Wrinkle Line", size, [](float x, float y) {
        const float line = 0.06f * wiggle(x * 1.2f, 0.7f);
        const float d = y - line;
        const float along = smooth01((0.8f - std::abs(x)) / 0.35f); // tapered ends
        const float fade = rimFade(x, y) * along;
        const float pinch = -0.12f * std::tanh(d / 0.06f) * std::exp(-d * d / 0.05f);
        const float z = -0.18f * std::exp(-d * d / 0.002f) + 0.04f * std::exp(-d * d / 0.03f);
        return glm::vec3(x, y + pinch * fade, z * fade);
    });
}

VectorDisplacement makeCrackVdm(int size) {
    // A jagged crack (along Y) whose sides split apart and break downward into it.
    return generate("Crack", size, [](float x, float y) {
        const float line = 0.12f * wiggle(y * 1.6f, 2.3f);
        const float d = x - line;
        const float along = smooth01((0.85f - std::abs(y)) / 0.3f);
        const float fade = rimFade(x, y) * along;
        const float width = 0.03f + 0.03f * (0.5f + 0.5f * std::sin(y * 9.0f));
        const float split = 0.09f * std::tanh(d / width) * std::exp(-d * d / 0.04f); // the sides move apart
        const float z = -0.28f * std::exp(-d * d / (width * width)) + 0.02f * std::exp(-d * d / 0.02f);
        return glm::vec3(x + split * fade, y, z * fade);
    });
}

VectorDisplacement makeRockLedgeVdm(int size) {
    // A stepped shelf of rock strata whose upper layer juts out over the one below (toward -Y): an overhang.
    return generate("Rock Ledge", size, [](float x, float y) {
        const float fade = rimFade(x, y);
        const float edge = -0.05f + 0.05f * wiggle(x * 2.0f, 4.1f);
        const float t = smooth01((y - edge) / 0.18f); // 0 below the ledge, 1 on top
        const float lip = std::exp(-(y - edge - 0.1f) * (y - edge - 0.1f) / 0.01f);
        const float z = 0.2f * t + 0.03f * wiggle(x * 4.0f + y * 3.0f, 1.3f) * t;
        const float jut = -0.14f * lip; // the lip pushed out over the lower layer
        return glm::vec3(x, y + jut * fade, z * fade);
    });
}

const std::vector<std::shared_ptr<const VectorDisplacement>>& builtinVdms() {
    static const std::vector<std::shared_ptr<const VectorDisplacement>> all = [] {
        std::vector<std::shared_ptr<const VectorDisplacement>> v;
        v.push_back(std::make_shared<const VectorDisplacement>(makeHornVdm()));
        v.push_back(std::make_shared<const VectorDisplacement>(makeEarCurlVdm()));
        v.push_back(std::make_shared<const VectorDisplacement>(makeScalesVdm()));
        v.push_back(std::make_shared<const VectorDisplacement>(makeNostrilVdm()));
        // #43: six more, made here in code like the four above (no files).
        v.push_back(std::make_shared<const VectorDisplacement>(makeRivetVdm()));
        v.push_back(std::make_shared<const VectorDisplacement>(makeStitchesVdm()));
        v.push_back(std::make_shared<const VectorDisplacement>(makeSkinFoldVdm()));
        v.push_back(std::make_shared<const VectorDisplacement>(makeWrinkleVdm()));
        v.push_back(std::make_shared<const VectorDisplacement>(makeCrackVdm()));
        v.push_back(std::make_shared<const VectorDisplacement>(makeRockLedgeVdm()));
        return v;
    }();
    return all;
}

std::shared_ptr<const VectorDisplacement> builtinVdm(const std::string& name) {
    for (const auto& v : builtinVdms())
        if (lower(v->name) == lower(name)) return v;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Create VDM from a plane
// ---------------------------------------------------------------------------

Mesh makeVdmPlane(int cells) {
    Mesh m = makePlane(std::clamp(cells, 2, 2000), 2.0f);
    // UVs from the flat start: u along +X, v up the top view (toward -Z).
    m.uvs.resize(m.triangles.size() * 3);
    for (size_t t = 0; t < m.triangles.size(); ++t)
        for (int k = 0; k < 3; ++k) {
            const glm::vec3 p = m.positions[m.triangles[t][k]];
            m.uvs[t * 3 + size_t(k)] = glm::vec2((p.x + 1.0f) * 0.5f, (1.0f - p.z) * 0.5f);
        }
    // The border stays where it is, so the stamp blends into any surface.
    auto& mask = m.ensureMask(0.0f);
    for (uint32_t v = 0; v < m.vertexCount(); ++v) {
        const glm::vec3 p = m.positions[v];
        if (std::max(std::abs(p.x), std::abs(p.z)) > 1.0f - 1e-4f) mask[v] = 1.0f;
    }
    return m;
}

bool captureVdm(const Mesh& mesh, int size, VectorDisplacement& out, std::string& error) {
    if (!mesh.hasUvs() || mesh.uvs.size() != mesh.triangles.size() * 3) {
        error = "The mesh has no UVs to say where each point started: start from Brush > Vector Displacement > New VDM plane";
        return false;
    }
    size = std::clamp(size, 16, kMaxVdmSide);
    out = VectorDisplacement{};
    out.width = out.height = size;
    out.vectors.assign(size_t(size) * size_t(size), glm::vec3(0.0f));
    const float fs = float(size);
    // Rows in parallel: each band rasterizes the triangles that reach it, so no texel is written twice at once.
    const size_t bands = size_t(std::min(size, 64));
    std::vector<std::vector<uint32_t>> inBand(bands);
    for (uint32_t t = 0; t < mesh.triangleCount(); ++t) {
        float lo = 1e9f, hi = -1e9f;
        for (int k = 0; k < 3; ++k) {
            const float py = (1.0f - mesh.uvs[size_t(t) * 3 + size_t(k)].y) * fs - 0.5f;
            lo = std::min(lo, py);
            hi = std::max(hi, py);
        }
        const int r0 = std::max(0, int(std::ceil(lo))), r1 = std::min(size - 1, int(std::floor(hi)));
        if (r0 > r1) continue;
        const size_t b0 = size_t(r0) * bands / size_t(size), b1 = size_t(r1) * bands / size_t(size);
        for (size_t b = b0; b <= b1; ++b) inBand[b].push_back(t);
    }
    poolFor(bands, [&](size_t bb, size_t be) {
        for (size_t band = bb; band < be; ++band) {
            const int rowLo = int(band * size_t(size) / bands), rowHi = int((band + 1) * size_t(size) / bands) - 1;
            for (uint32_t t : inBand[band]) {
                glm::vec2 q[3];
                glm::vec3 d[3];
                for (int k = 0; k < 3; ++k) {
                    const glm::vec2 uv = mesh.uvs[size_t(t) * 3 + size_t(k)];
                    q[k] = glm::vec2(uv.x * fs - 0.5f, (1.0f - uv.y) * fs - 0.5f);
                    const glm::vec3 p = mesh.positions[mesh.triangles[t][k]];
                    // Tangent axes of the plane (see makeVdmPlane) minus where the point started.
                    d[k] = glm::vec3(p.x, -p.z, p.y) - glm::vec3(uv.x * 2.0f - 1.0f, uv.y * 2.0f - 1.0f, 0.0f);
                }
                const float area = (q[1].x - q[0].x) * (q[2].y - q[0].y) - (q[2].x - q[0].x) * (q[1].y - q[0].y);
                if (std::abs(area) < 1e-12f) continue;
                const int x0 = std::max(0, int(std::ceil(std::min({q[0].x, q[1].x, q[2].x}) - 1e-4f)));
                const int x1 = std::min(size - 1, int(std::floor(std::max({q[0].x, q[1].x, q[2].x}) + 1e-4f)));
                const int y0 = std::max(rowLo, int(std::ceil(std::min({q[0].y, q[1].y, q[2].y}) - 1e-4f)));
                const int y1 = std::min(rowHi, int(std::floor(std::max({q[0].y, q[1].y, q[2].y}) + 1e-4f)));
                for (int y = y0; y <= y1; ++y)
                    for (int x = x0; x <= x1; ++x) {
                        const glm::vec2 c{float(x), float(y)};
                        auto edge = [&](int a, int b) {
                            return ((q[b].x - q[a].x) * (c.y - q[a].y) - (c.x - q[a].x) * (q[b].y - q[a].y)) / area;
                        };
                        const float w0 = edge(1, 2), w1 = edge(2, 0), w2 = edge(0, 1);
                        if (w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f) continue;
                        out.vectors[size_t(y) * size_t(size) + size_t(x)] = d[0] * w0 + d[1] * w1 + d[2] * w2;
                    }
            }
        }
    }, 1, 1);
    return true;
}

// ---------------------------------------------------------------------------
// The brush (Sculptor::dabVdmSingle)
// ---------------------------------------------------------------------------

void Sculptor::dabVdmSingle(Mesh& mesh, BVH& bvh, const DabParams& p, float handedness) {
    auto clock = std::chrono::steady_clock::now();
    const VectorDisplacement* vdm = p.vdm && !p.vdm->empty() ? p.vdm : builtinVdms().front().get();
    // The stamp is the brush's square, so the corners reach out to radius x sqrt(2).
    brushRegion(mesh, bvh, p.center, p.viewDir, p.radius * 1.4143f, p.connectedOnly, p.viewFalloff, verts_, connected_);
    if (verts_.empty()) return;
    const auto& pos = mesh.positions;
    const auto& nrm = mesh.normals;
    if (p.frontFacesOnly) {
        std::erase_if(verts_, [&](uint32_t v) { return glm::dot(nrm[v], p.viewDir) > 0.0f; });
        if (verts_.empty()) return;
    }
    // Area normal under the brush (inside its radius, smoothstep weighted): per-block partials summed in
    // block order, so it doesn't depend on the thread count.
    constexpr size_t kBlock = 4096;
    std::vector<glm::vec3> parts((verts_.size() + kBlock - 1) / kBlock, glm::vec3(0.0f));
    poolFor(verts_.size(), [&](size_t b, size_t e) {
        glm::vec3 acc(0.0f);
        for (size_t i = b; i < e; ++i)
            acc += nrm[verts_[i]] * brushFalloff(glm::distance(pos[verts_[i]], p.center), p.radius);
        parts[b / kBlock] = acc;
    }, kBlock);
    glm::vec3 n(0.0f);
    for (const glm::vec3& part : parts) n += part;
    n = glm::length(n) > 1e-12f ? glm::normalize(n) : -p.viewDir;

    // Tangent frame: Z = the surface, Y = the stamp's top (screen up or the drag), X = right. A mirrored pass
    // flips X too, so the mirrored stamp is the exact mirror image (a mirror swaps the handedness of a cross).
    glm::vec3 up = p.alphaUp - n * glm::dot(p.alphaUp, n);
    if (glm::length(up) < 1e-6f) {
        const glm::vec3 helper = std::abs(n.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        up = helper - n * glm::dot(helper, n);
    }
    up = glm::normalize(up);
    const glm::vec3 right = glm::cross(up, n) * handedness;

    const float r = p.radius;
    // Z Intensity 50 (the default) presses the map at its own size; 100 doubles it. Alt digs the shape in
    // (Blender's convention: X and Y stay, Z flips).
    const float amount = 2.0f * std::clamp(p.strength, 0.0f, 1.0f) * std::clamp(p.pressure, 0.0f, 1.0f) * r;
    const float zSign = p.invert ? -1.0f : 1.0f;
    scratch_.resize(verts_.size());
    poolFor(verts_.size(), [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            const glm::vec3 o = pos[verts_[i]] - p.center;
            const float u = glm::dot(o, right) / r, v = glm::dot(o, up) / r;
            const float edge = 1.0f - std::max(std::abs(u), std::abs(v));
            if (edge <= 0.0f) {
                scratch_[i] = glm::vec3(0.0f);
                continue;
            }
            const glm::vec3 d = vdm->sample(u, v) * (amount * smooth01(edge / 0.03f)); // maps whose border isn't 0 fade out
            scratch_[i] = right * d.x + up * d.y + n * (d.z * zSign);
        }
    }, kBlock);
    displaceAll(mesh, verts_, scratch_);
    finishDab(mesh, bvh, clock);
}

// ---------------------------------------------------------------------------
// Library
// ---------------------------------------------------------------------------

std::string vdmFileName(const std::string& name) {
    std::string f = brushFileName(name);
    return f.substr(0, f.size() - std::strlen(".sfbrush")) + ".sfvdm";
}

int findVdm(const Document& doc, const std::string& name) {
    for (size_t i = 0; i < doc.vdms.size(); ++i)
        if (lower(doc.vdms[i]->name) == lower(name)) return int(i);
    return -1;
}

std::shared_ptr<const VectorDisplacement> addVdm(Document& doc, VectorDisplacement vdm) {
    auto shared = std::make_shared<const VectorDisplacement>(std::move(vdm));
    const int i = findVdm(doc, shared->name);
    if (i >= 0) doc.vdms[size_t(i)] = shared;
    else doc.vdms.push_back(shared);
    doc.brushVdm = shared;
    return shared;
}

void loadVdmLibrary(Document& doc, const BrushLibraryPaths& paths, std::vector<std::string>& messages) {
    const std::string current = doc.brushVdm ? doc.brushVdm->name : std::string();
    doc.vdms = builtinVdms();
    auto scan = [&](const std::string& dir, bool user, const std::string& pack = {}) {
        std::vector<std::string> files;
        std::error_code ec;
        for (fs::directory_iterator it(utf8Path(dir), ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code fileEc;
            if (extensionOf(pathString(it->path())) == ".sfvdm" && it->is_regular_file(fileEc))
                files.push_back(pathString(it->path()));
        }
        std::sort(files.begin(), files.end());
        for (const std::string& f : files) {
            VectorDisplacement v;
            std::string err;
            if (!readVdmFile(f, v, err)) {
                messages.push_back("Skipped " + f + ": " + err);
                continue;
            }
            if (!pack.empty()) {
                // A pack's VDM never replaces another: it gets the pack's name after its own.
                if (findVdm(doc, v.name) >= 0) v.name += " (" + pack + ")";
                if (findVdm(doc, v.name) >= 0) continue;
            }
            const int i = findVdm(doc, v.name);
            auto shared = std::make_shared<const VectorDisplacement>(std::move(v));
            if (i < 0) doc.vdms.push_back(shared);
            else if (user || doc.vdms[size_t(i)]->builtin) doc.vdms[size_t(i)] = shared; // user > studio > built-in
        }
    };
    for (const std::string& dir : paths.studioDirs) scan(dir, false);
    for (const auto& p : paths.packs)
        if (p.vdm && !packsSafeMode()) scan(p.dir, false, p.pack);
    if (!paths.userDir.empty()) scan(paths.userDir, true);
    const int i = findVdm(doc, current);
    doc.brushVdm = i >= 0 ? doc.vdms[size_t(i)] : doc.vdms.front();
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

namespace {

VdmImportOptions importOptions(const Args& a) {
    VdmImportOptions o;
    o.swapYZ = argBool(a, "swap_yz", false);
    o.flipX = argBool(a, "flip_x", false);
    o.flipY = argBool(a, "flip_y", false);
    o.flipZ = argBool(a, "flip_z", false);
    o.scale = float(std::clamp(argNumber(a, "scale", 1.0), -100.0, 100.0));
    return o;
}

// Saves `vdm` into the user library (when there is one); returns the note for the status line.
std::string saveToLibrary(VectorDisplacement& vdm) {
    const std::string dir = brushLibraryPaths().userDir;
    if (dir.empty()) return " (not saved: no brush library)";
    const std::string file = pathString(utf8Path(dir) / utf8Path(vdmFileName(vdm.name)));
    std::string err;
    if (!writeVdmFile(file, vdm, err)) return " (not saved: " + err + ")";
    vdm.path = file;
    return "";
}

} // namespace

void registerVdmCommands(CommandRegistry& registry) {
    using T = ParamSpec::Type;
    const std::vector<ParamSpec> importParams = {
        {"swap_yz", T::Bool, "Swap the map's Y and Z (G and B) channels, for maps made with Y as the normal.", false},
        {"flip_x", T::Bool, "Negate X (R).", false},
        {"flip_y", T::Bool, "Negate Y (G).", false},
        {"flip_z", T::Bool, "Negate Z (B).", false},
        {"scale", T::Number, "Multiply every vector (default 1).", false},
    };
    std::vector<ParamSpec> setParams = {
        {"name", T::String, "A built-in (horn, ear curl, scales, nostril) or library VDM, by name.", false},
        {"path", T::String, "A vector displacement map: 32/16-bit OpenEXR (Blender's format) or .sfvdm. Overrides name.", false},
        {"save", T::Bool, "Keep a loaded file in the brush library (default true).", false},
    };
    setParams.insert(setParams.end(), importParams.begin(), importParams.end());
    registry.add({"vdm_set",
                  "Choose the vector displacement map the VDM brush stamps: a built-in, a library VDM, or an EXR file "
                  "(RGB = XYZ in the brush's tangent space: X right, Y up, Z out of the surface, in brush radii).",
                  setParams,
                  [](Document& doc, const Args& a) -> CommandResult {
                      const std::string path = argString(a, "path");
                      if (!path.empty()) {
                          VectorDisplacement v;
                          std::string err;
                          if (!loadVdm(path, v, err, importOptions(a))) return {false, err};
                          std::string note;
                          if (argBool(a, "save", true) && extensionOf(path) == ".exr") note = saveToLibrary(v);
                          const std::string msg = "VDM '" + v.name + "' (" + std::to_string(v.width) + "x" +
                                                  std::to_string(v.height) + ")" + note;
                          addVdm(doc, std::move(v));
                          return {true, msg};
                      }
                      const std::string name = argString(a, "name", "horn");
                      const int i = findVdm(doc, name);
                      if (i < 0) return {false, "No VDM called " + name + " (vdm_list shows them)"};
                      doc.brushVdm = doc.vdms[size_t(i)];
                      return {true, "VDM '" + doc.brushVdm->name + "'"};
                  }});
    registry.add({"vdm_list", "List the vector displacement maps (built-in and library); * marks the brush's.", {},
                  [](Document& doc, const Args&) -> CommandResult {
                      std::string s;
                      for (const auto& v : doc.vdms) {
                          if (!s.empty()) s += ", ";
                          s += (v == doc.brushVdm ? "*" : "") + v->name;
                      }
                      return {true, s};
                  }});
    registry.add({"vdm_export",
                  "Save the VDM brush's map as a 32-bit float OpenEXR (for Blender's VDM brushes) or .sfvdm.",
                  {{"path", T::String, "File to write (.exr or .sfvdm).", true},
                   {"name", T::String, "Which VDM (default: the brush's).", false}},
                  [](Document& doc, const Args& a) -> CommandResult {
                      const std::string path = argString(a, "path");
                      std::shared_ptr<const VectorDisplacement> v = doc.brushVdm;
                      if (const std::string name = argString(a, "name"); !name.empty()) {
                          const int i = findVdm(doc, name);
                          if (i < 0) return {false, "No VDM called " + name};
                          v = doc.vdms[size_t(i)];
                      }
                      if (!v) return {false, "No VDM"};
                      std::string err;
                      const bool ok = extensionOf(path) == ".sfvdm" ? writeVdmFile(path, *v, err) : writeVdmExr(path, *v, err);
                      if (!ok) return {false, err};
                      return {true, "Exported VDM '" + v->name + "' to " + path};
                  }});
    registry.add({"vdm_new_plane",
                  "Add a VDM plane to sculpt a new vector displacement brush on (ZBrush's VDM plane template): a flat "
                  "2 x 2 grid seen from the top, its border masked; the other Parts are hidden. Then vdm_capture.",
                  {{"resolution", T::Number, "Quads per side (16-1024). Default 256; divide it for finer detail.", false}},
                  [](Document& doc, const Args& a) -> CommandResult {
                      const int res = std::clamp(int(argNumber(a, "resolution", 256)), 16, 1024);
                      for (auto& s : doc.subtools) s.visible = false;
                      doc.addSubtool(makeVdmPlane(res), doc.uniqueSubtoolName("VDM Plane"));
                      doc.activeSubtoolInfo().visible = true;
                      doc.modified = true;
                      return {true, "VDM plane (" + std::to_string(res) + " x " + std::to_string(res) +
                                        "): sculpt it from the top, then Create VDM"};
                  }});
    registry.add({"vdm_capture",
                  "Create a VDM brush from the active VDM plane: how far each point moved from the flat grid becomes "
                  "a new vector displacement map in the brush library (ZBrush: create brush from the VDM plane).",
                  {{"name", T::String, "Name of the new VDM (default 'My VDM').", false},
                   {"resolution", T::Number, "Map size in pixels (16-2048, default 256; Blender suggests 512 or less).", false},
                   {"exr", T::String, "Also write it as a 32-bit OpenEXR here, for Blender.", false},
                   {"save", T::Bool, "Keep it in the brush library (default true).", false}},
                  [](Document& doc, const Args& a) -> CommandResult {
                      VectorDisplacement v;
                      std::string err;
                      if (!captureVdm(doc.mesh, int(argNumber(a, "resolution", 256)), v, err)) return {false, err};
                      v.name = argString(a, "name", "My VDM");
                      if (v.name.empty()) v.name = "My VDM";
                      if (builtinVdm(v.name)) v.name += " (mine)";
                      std::string note;
                      if (argBool(a, "save", true)) note = saveToLibrary(v);
                      if (const std::string exr = argString(a, "exr"); !exr.empty()) {
                          if (!writeVdmExr(exr, v, err)) return {false, err};
                          note += ", EXR " + exr;
                      }
                      const std::string msg = "Created VDM '" + v.name + "' (" + std::to_string(v.width) + " px)" + note;
                      addVdm(doc, std::move(v));
                      return {true, msg};
                  }});
    registry.add({"vdm_stamp",
                  "Press the VDM brush once (like a Drag Stamp click): at the surface point a ray along `view` through "
                  "`point` hits, `radius` in world units, its top turned by `angle` from the view's up. One undo step.",
                  {{"point", T::String, "x,y,z the ray goes through.", true},
                   {"view", T::String, "x,y,z view direction into the scene (default 0,0,-1).", false},
                   {"radius", T::Number, "Brush radius in world units (default 4% of the model's size).", false},
                   {"angle", T::Number, "Rotation in degrees, counter-clockwise on screen (default 0).", false},
                   {"strength", T::Number, "Intensity 0-1 (default 0.5 = the map's own size; 1 doubles it).", false},
                   {"invert", T::Bool, "Dig the shape in (Alt).", false},
                   {"symmetry", T::String, "Mirror axes, e.g. \"x\" or \"xz\" (default none).", false}},
                  [](Document& doc, const Args& a) -> CommandResult {
                      glm::vec3 point, view(0.0f, 0.0f, -1.0f);
                      if (!parseVec3(argString(a, "point"), point)) return {false, "Bad point (expected x,y,z)"};
                      if (a.count("view") && !parseVec3(argString(a, "view"), view)) return {false, "Bad view (expected x,y,z)"};
                      if (glm::length(view) < 1e-6f) return {false, "The view direction must not be zero"};
                      view = glm::normalize(view);
                      if (doc.mesh.triangleCount() == 0) return {false, "The active Part is empty"};
                      doc.syncBvh();
                      const AABB box = doc.mesh.bounds();
                      const float diag = std::max(glm::length(box.extent()), 1e-4f);
                      RayHit hit;
                      const glm::vec3 origin = point - view * (glm::dot(point - box.center(), view) + diag);
                      if (!doc.bvh.raycast(doc.mesh, origin, view, hit)) return {false, "The ray misses the surface"};
                      const glm::vec3 helper = std::abs(view.y) > 0.9f ? glm::vec3(0.0f, 0.0f, -1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
                      const glm::vec3 right = glm::normalize(glm::cross(view, helper));
                      const glm::vec3 up = glm::cross(right, view);
                      const float angle = float(argNumber(a, "angle", 0.0)) * 3.14159265f / 180.0f;
                      DabParams p;
                      p.center = hit.position;
                      p.viewDir = view;
                      p.radius = float(argNumber(a, "radius", 0.0));
                      if (!(p.radius > 0.0f)) p.radius = diag * 0.04f;
                      p.strength = float(std::clamp(argNumber(a, "strength", 0.5), 0.0, 1.0));
                      p.invert = argBool(a, "invert", false);
                      p.alphaUp = up * std::cos(angle) - right * std::sin(angle);
                      p.vdm = doc.brushVdm.get();
                      Symmetry sym{false, false, false};
                      std::string symError;
                      if (!symmetryArg(a, doc.mesh, sym, symError)) return {false, symError};
                      if (!doc.sculptor.beginStroke(doc.mesh, "VDM", doc.activeLayer))
                          return {false, "Active layer weight is 0 - raise it to sculpt into the layer"};
                      doc.sculptor.dab(doc.mesh, doc.bvh, BrushType::Vdm, p, sym);
                      UndoRecord r = doc.sculptor.endStroke();
                      if (!r.empty()) doc.undo.push(std::move(r));
                      return {true, "Stamped VDM '" + (doc.brushVdm ? doc.brushVdm->name : std::string("Horn")) + "'"};
                  }});
    registry.add({"vdm_delete", "Delete a VDM from your brush library (built-ins and studio VDMs stay).",
                  {{"name", T::String, "The VDM to delete.", true}},
                  [](Document& doc, const Args& a) -> CommandResult {
                      const int i = findVdm(doc, argString(a, "name"));
                      if (i < 0) return {false, "No VDM called " + argString(a, "name")};
                      const auto v = doc.vdms[size_t(i)];
                      const std::string dir = brushLibraryPaths().userDir;
                      std::error_code ec;
                      const bool inLibrary = !v->builtin && !v->path.empty() && !dir.empty() &&
                                             fs::equivalent(utf8Path(v->path).parent_path(), utf8Path(dir), ec);
                      if (v->builtin) return {false, v->name + " is built in"};
                      if (!v->path.empty() && !inLibrary) return {false, v->name + " isn't in your brush library"};
                      if (!v->path.empty() && !fs::remove(utf8Path(v->path), ec))
                          return {false, "Can't delete " + v->path};
                      doc.vdms.erase(doc.vdms.begin() + i);
                      if (doc.brushVdm == v) doc.brushVdm = doc.vdms.front();
                      // A studio or built-in VDM of the same name comes back on the next library load.
                      return {true, "Deleted VDM '" + v->name + "'"};
                  }});
}

} // namespace sf
