// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Matcap.h"

#include "core/BrushLibrary.h"
#include "core/Image.h"
#include "core/Mesh.h"

#include "tinyexr.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace sf {

namespace {

// A small lighting model evaluated once per matcap pixel: wrapped diffuse with
// a subsurface tint at the terminator, Blinn specular, rim, and for metals a
// studio environment (sky, horizon, ground, softbox) seen by reflection.
struct MatcapStyle {
    const char* name;
    glm::vec3 albedo;     // linear
    float wrap;           // diffuse wrap (soft, waxy materials)
    glm::vec3 subsurface; // colour bleeding into the shadow side
    float specular, shininess;
    glm::vec3 rim;
    float metal; // 0 = dielectric, 1 = pure environment reflection
    float roughness; // blurs the environment for metals
    // The shared model (shade), or a light of its own: shadeSculpt (readability first) or shadeSoftClay.
    enum class Light { Shared, Sculpt, SoftClay } light = Light::Shared;
    MatcapCavity cavity{};
};

const MatcapStyle kStyles[] = {
    {"Sculpt", {0.54f, 0.46f, 0.39f}, 0.0f, {0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, {0.0f, 0.0f, 0.0f}, 0.0f, 0.0f,
     MatcapStyle::Light::Sculpt, {0.8f, 0.35f}},
    {"Red Wax", {0.50f, 0.045f, 0.03f}, 0.55f, {0.55f, 0.06f, 0.03f}, 0.55f, 70.0f, {0.25f, 0.08f, 0.06f}, 0.0f, 0.0f},
    {"Clay", {0.50f, 0.27f, 0.17f}, 0.25f, {0.05f, 0.03f, 0.02f}, 0.06f, 10.0f, {0.10f, 0.10f, 0.11f}, 0.0f, 0.0f},
    {"Skin", {0.68f, 0.38f, 0.28f}, 0.6f, {0.6f, 0.16f, 0.08f}, 0.16f, 28.0f, {0.22f, 0.16f, 0.15f}, 0.0f, 0.0f},
    {"Chrome", {0.95f, 0.96f, 1.0f}, 0.0f, {0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, {0.0f, 0.0f, 0.0f}, 1.0f, 0.05f},
    {"White", {0.78f, 0.78f, 0.76f}, 0.3f, {0.04f, 0.04f, 0.05f}, 0.3f, 45.0f, {0.12f, 0.13f, 0.15f}, 0.0f, 0.0f},
    {"Bronze", {0.80f, 0.50f, 0.25f}, 0.0f, {0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, {0.0f, 0.0f, 0.0f}, 1.0f, 0.35f},
    // The default (defaultMatcap()). Last, so the indices of the others stay put.
    {"Soft Clay", {0.50f, 0.455f, 0.41f}, 0.0f, {0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, {0.0f, 0.0f, 0.0f}, 0.0f, 0.0f,
     MatcapStyle::Light::SoftClay, {0.3f, 0.08f}},
};
constexpr int kStyleCount = int(sizeof kStyles / sizeof kStyles[0]);

const glm::vec3 kKey = glm::normalize(glm::vec3(-0.45f, 0.6f, 0.65f));
const glm::vec3 kFill = glm::normalize(glm::vec3(0.75f, -0.15f, 0.45f));

// Studio environment radiance in direction `r` (y up, z towards the viewer).
glm::vec3 environment(const glm::vec3& r, float roughness) {
    const float soft = 0.04f + roughness * 0.5f;
    const float up = r.y;
    const glm::vec3 sky = glm::mix(glm::vec3(0.75f, 0.8f, 0.85f), glm::vec3(0.22f, 0.26f, 0.32f), std::sqrt(std::max(up, 0.0f)));
    const glm::vec3 ground = glm::mix(glm::vec3(0.12f, 0.11f, 0.1f), glm::vec3(0.03f, 0.03f, 0.03f), std::sqrt(std::max(-up, 0.0f)));
    const float t = std::clamp(up / soft * 0.5f + 0.5f, 0.0f, 1.0f); // horizon line
    glm::vec3 c = glm::mix(ground, sky, t * t * (3.0f - 2.0f * t));
    // Soft box behind the camera's upper left, and a dimmer strip on the right.
    const float box = std::pow(std::max(glm::dot(r, kKey), 0.0f), 30.0f / (1.0f + 20.0f * roughness));
    const float strip = std::pow(std::max(glm::dot(r, kFill), 0.0f), 60.0f / (1.0f + 20.0f * roughness));
    c += glm::vec3(3.0f) * box + glm::vec3(0.6f, 0.65f, 0.7f) * strip;
    return c;
}

// Built for reading the surface while sculpting, after ZBrush's default material and Blender's sculpt
// studio light: a strong key from the upper left, little ambient so forms don't flatten, a crisp
// highlight that catches every undulation, only a hint of rim and no fresnel glow. Creases get the
// strongest cavity darkening of the materials (kStyles' cavity, applied by the mesh shader).
glm::vec3 shadeSculpt(const MatcapStyle& m, const glm::vec3& n) {
    const glm::vec3 v(0.0f, 0.0f, 1.0f);
    const glm::vec3 key = glm::normalize(glm::vec3(-0.4f, 0.65f, 0.65f));
    const glm::vec3 fill = glm::normalize(glm::vec3(0.75f, -0.3f, 0.6f));
    const float diff = std::max((glm::dot(n, key) + 0.1f) / 1.1f, 0.0f);
    const float fd = std::max(glm::dot(n, fill), 0.0f);
    const float hemi = 0.04f + 0.04f * (0.5f + 0.5f * n.y);
    // A steep falloff on both lights: brightness changes quickly with the slope everywhere in view.
    const glm::vec3 light = 1.6f * std::pow(diff, 2.5f) + 0.5f * fd * fd * glm::vec3(0.85f, 0.9f, 1.0f) + glm::vec3(hemi);
    // A tight highlight over a broad sheen, plus a faint one from the fill, so strokes catch light
    // on the shadow side too.
    const float nh = std::max(glm::dot(n, glm::normalize(key + v)), 0.0f);
    const float spec = (0.3f * std::pow(nh, 80.0f) + 0.25f * std::pow(nh, 12.0f)) * std::min(1.0f, diff * 4.0f);
    const float fillSpec = 0.07f * std::pow(std::max(glm::dot(n, glm::normalize(fill + v)), 0.0f), 40.0f);
    const float rim = std::pow(1.0f - std::max(n.z, 0.0f), 4.0f) * 0.1f;
    return m.albedo * light + glm::vec3(spec) + glm::vec3(0.8f, 0.85f, 0.95f) * (fillSpec + rim);
}

// The default: matte studio clay, a warm light grey, built so a sculpted stroke reads at once. One key from the
// upper left with little wrap and a steep falloff (brightness turns quickly with the slope, so the two sides of a
// ridge part clearly), a form shadow on the lower right that goes well down without going black, a cool fill and
// sky/ground ambient, a broad low sheen that runs along ridges (no hot spot), and a cool rim strongest on the
// lower right so the silhouette holds against the charcoal background where the key doesn't reach.
glm::vec3 shadeSoftClay(const MatcapStyle& m, const glm::vec3& n) {
    const glm::vec3 v(0.0f, 0.0f, 1.0f);
    const glm::vec3 key = glm::normalize(glm::vec3(-0.55f, 0.68f, 0.55f));
    const glm::vec3 fill = glm::normalize(glm::vec3(0.85f, -0.2f, 0.35f));
    const float k = std::max(glm::dot(n, key) * 0.92f + 0.08f, 0.0f);
    const float diff = std::pow(k, 2.4f);
    // A real fill from the right, about a third of the key: the shadow side stays a mid value, and slopes there
    // turn towards or away from it, so strokes read on that side too.
    const float fd = std::max((glm::dot(n, fill) + 0.25f) / 1.25f, 0.0f);
    // Sky above, a warm bounce from the ground below: the lower left, where neither light reaches, stays clay.
    const float up = 0.5f + 0.5f * n.y;
    const glm::vec3 ambient = glm::mix(glm::vec3(0.13f, 0.115f, 0.1f), glm::vec3(0.07f, 0.075f, 0.085f), std::sqrt(up));
    const glm::vec3 light = 1.3f * diff * glm::vec3(1.0f, 0.97f, 0.93f) + 0.3f * fd * fd * glm::vec3(0.9f, 0.93f, 1.0f) + ambient;
    const float nh = std::max(glm::dot(n, glm::normalize(key + v)), 0.0f);
    const float sheen = (0.03f * std::pow(nh, 8.0f) + 0.09f * std::pow(nh, 30.0f)) * std::min(1.0f, k * 3.0f);
    // Rim: grazing normals, weighted towards the lower right (the side the key leaves dark).
    const float grazing = std::pow(1.0f - std::max(n.z, 0.0f), 3.0f);
    const float len = std::sqrt(n.x * n.x + n.y * n.y);
    const float side = len > 1e-4f ? std::clamp((n.x * 0.8f - n.y * 0.6f) / len, 0.0f, 1.0f) : 0.0f;
    const glm::vec3 rim = (0.05f + 0.2f * side) * grazing * glm::vec3(0.82f, 0.88f, 1.0f);
    return m.albedo * light + glm::vec3(sheen) + rim;
}

glm::vec3 shade(const MatcapStyle& m, const glm::vec3& n) {
    if (m.light == MatcapStyle::Light::Sculpt) return shadeSculpt(m, n);
    if (m.light == MatcapStyle::Light::SoftClay) return shadeSoftClay(m, n);
    const glm::vec3 v(0.0f, 0.0f, 1.0f);
    const float nv = std::max(n.z, 0.0f);
    if (m.metal > 0.0f) {
        const glm::vec3 r = 2.0f * n.z * n - v; // reflect(-v, n)
        // Schlick-style brightening towards grazing angles.
        const float fresnel = std::pow(1.0f - nv, 5.0f);
        const glm::vec3 f0 = m.albedo;
        return environment(r, m.roughness) * (f0 + (glm::vec3(1.0f) - f0) * fresnel);
    }
    const float ndl = glm::dot(n, kKey);
    const float diff = std::max((ndl + m.wrap) / (1.0f + m.wrap), 0.0f);
    const float terminator = std::max(0.0f, 1.0f - std::abs(ndl - 0.05f) * 2.5f); // band around the shadow edge
    const float fill = std::max(glm::dot(n, kFill), 0.0f) * 0.2f;
    const float hemi = 0.06f + 0.06f * (0.5f + 0.5f * n.y);
    const glm::vec3 h = glm::normalize(kKey + v);
    const float spec = m.specular * std::pow(std::max(glm::dot(n, h), 0.0f), m.shininess);
    const float rim = std::pow(1.0f - nv, 3.0f);
    glm::vec3 c = m.albedo * (diff * diff * 1.05f + fill + hemi);
    c += m.subsurface * terminator * 0.35f;
    c += glm::vec3(spec) + m.rim * rim;
    return c;
}

std::string squash(const std::string& s) {
    std::string out;
    for (unsigned char c : s)
        if (!std::isspace(c)) out += char(std::tolower(c));
    return out;
}

} // namespace

namespace {

// Custom MatCaps, after the built-ins. A deque: names handed out by matcapName stay valid as more are added.
std::deque<CustomMatcap>& customs() {
    static std::deque<CustomMatcap> list;
    return list;
}
uint64_t g_revision = 1;

uint64_t hashPixels(const std::vector<uint8_t>& rgba, int size) {
    uint64_t h = 0xcbf29ce484222325ull ^ uint64_t(size);
    for (uint8_t b : rgba) h = (h ^ b) * 0x100000001b3ull;
    return h;
}

namespace fs = std::filesystem;
fs::path utf8Path(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string pathString(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}
std::string extensionOf(const std::string& path) {
    std::string e = pathString(utf8Path(path).extension());
    for (char& c : e) c = char(std::tolower(static_cast<unsigned char>(c)));
    return e;
}

// Top-down RGBA8 (sRGB) of any size -> the centred square, scaled down to kMaxMatcapSide, rows bottom-up.
bool fromTopDown(const std::vector<uint8_t>& src, int w, int h, CustomMatcap& out, std::string& error) {
    const int side = std::min(w, h);
    if (side < 16) {
        error = "The Lit Sphere image is too small (at least 16 x 16 pixels)";
        return false;
    }
    const int x0 = (w - side) / 2, y0 = (h - side) / 2;
    const int size = std::min(side, kMaxMatcapSide);
    out.size = size;
    out.rgba.assign(size_t(size) * size_t(size) * 4, 255);
    const float scale = float(side) / float(size); // box filter when scaling down
    for (int j = 0; j < size; ++j)
        for (int i = 0; i < size; ++i) {
            const int sx0 = x0 + int(float(i) * scale), sx1 = std::max(sx0 + 1, x0 + int(float(i + 1) * scale));
            const int sy0 = y0 + int(float(j) * scale), sy1 = std::max(sy0 + 1, y0 + int(float(j + 1) * scale));
            uint32_t sum[3] = {0, 0, 0}, n = 0;
            for (int y = sy0; y < sy1; ++y)
                for (int x = sx0; x < sx1; ++x) {
                    const uint8_t* p = &src[(size_t(y) * size_t(w) + size_t(x)) * 4];
                    for (int k = 0; k < 3; ++k) sum[k] += p[k];
                    ++n;
                }
            uint8_t* d = &out.rgba[(size_t(size - 1 - j) * size_t(size) + size_t(i)) * 4]; // bottom-up
            for (int k = 0; k < 3; ++k) d[k] = uint8_t((sum[k] + n / 2) / n);
        }
    out.hash = hashPixels(out.rgba, size);
    return true;
}

} // namespace

int matcapCount() { return kStyleCount + int(customs().size()); }
int builtinMatcapCount() { return kStyleCount; }
bool isCustomMatcap(int index) { return index >= kStyleCount && index < matcapCount(); }
bool isMatcapListed(int index) {
    return (index >= 0 && index < kStyleCount) || (isCustomMatcap(index) && !customs()[size_t(index - kStyleCount)].removed);
}
const CustomMatcap* customMatcap(int index) { return isCustomMatcap(index) ? &customs()[size_t(index - kStyleCount)] : nullptr; }
uint64_t matcapRevision() { return g_revision; }

std::string matcapKey(int index) {
    if (const CustomMatcap* c = customMatcap(index)) {
        char hex[17];
        std::snprintf(hex, sizeof hex, "%016llx", static_cast<unsigned long long>(c->hash));
        return c->name + "#" + hex;
    }
    return matcapName(index);
}

int defaultMatcap() {
    static const int index = findMatcap("Soft Clay");
    return index;
}

int matcapInListOrder(int k) {
    if (k < 0 || k >= matcapCount()) return -1;
    if (k >= kStyleCount) return k; // custom MatCaps after the built-ins, in the order they were added
    const int d = defaultMatcap();
    return k == 0 ? d : k <= d ? k - 1 : k;
}

const char* matcapName(int index) {
    if (const CustomMatcap* c = customMatcap(index)) return c->name.c_str();
    return index >= 0 && index < kStyleCount ? kStyles[index].name : "";
}

MatcapCavity matcapCavity(int index) { return index >= 0 && index < kStyleCount ? kStyles[index].cavity : MatcapCavity{}; }

int findMatcap(const std::string& name) {
    const std::string key = squash(name);
    for (int i = 0; i < matcapCount(); ++i)
        if (isMatcapListed(i) && squash(matcapName(i)) == key) return i;
    return -1;
}

int findMatcapByPath(const std::string& path) {
    for (size_t i = 0; i < customs().size(); ++i)
        if (!customs()[i].removed && !path.empty() && customs()[i].path == path) return kStyleCount + int(i);
    return -1;
}

bool removeCustomMatcap(int index) {
    if (!isCustomMatcap(index) || customs()[size_t(index - kStyleCount)].removed) return false;
    customs()[size_t(index - kStyleCount)].removed = true;
    ++g_revision;
    return true;
}

int findMatcapByHash(uint64_t hash) {
    for (size_t i = 0; i < customs().size(); ++i)
        if (!customs()[i].removed && customs()[i].hash == hash) return kStyleCount + int(i);
    return -1;
}

bool isMatcapFile(const std::string& path) {
    const std::string e = extensionOf(path);
    return e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".exr" || e == ".bmp" || e == ".tga" || e == ".hdr";
}

bool decodeMatcapImage(const unsigned char* bytes, size_t size, CustomMatcap& out, std::string& error) {
    if (bytes && size > 0 && IsEXRFromMemory(bytes, size) == TINYEXR_SUCCESS) {
        float* rgba = nullptr;
        int w = 0, h = 0;
        const char* err = nullptr;
        if (LoadEXRFromMemory(&rgba, &w, &h, bytes, size, &err) != TINYEXR_SUCCESS) {
            error = std::string("Can't read the EXR: ") + (err ? err : "unknown error");
            if (err) FreeEXRErrorMessage(err);
            return false;
        }
        if (w <= 0 || h <= 0 || w > 16384 || h > 16384) {
            std::free(rgba);
            error = "The EXR is too big";
            return false;
        }
        // Linear light: clamp to 0..1 and encode as sRGB like the built-ins.
        std::vector<uint8_t> src(size_t(w) * size_t(h) * 4, 255);
        for (size_t i = 0; i < size_t(w) * size_t(h); ++i) {
            const glm::vec3 c = glm::clamp(linearToSrgb(glm::max(glm::vec3(rgba[i * 4], rgba[i * 4 + 1], rgba[i * 4 + 2]),
                                                                 glm::vec3(0.0f))),
                                           0.0f, 1.0f);
            for (int k = 0; k < 3; ++k) src[i * 4 + size_t(k)] = uint8_t(std::lround(c[k] * 255.0f));
        }
        std::free(rgba);
        return fromTopDown(src, w, h, out, error);
    }
    Image img;
    if (!Image::decode(bytes, size, img, error)) return false;
    return fromTopDown(img.rgba, img.width, img.height, out, error);
}

bool readMatcapImage(const std::string& path, CustomMatcap& out, std::string& error) {
    std::ifstream f(utf8Path(path), std::ios::binary);
    if (!f) {
        error = "Can't open " + path;
        return false;
    }
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (bytes.size() > (size_t(256) << 20)) {
        error = path + " is too big for a Lit Sphere";
        return false;
    }
    if (!decodeMatcapImage(bytes.data(), bytes.size(), out, error)) {
        error = path + ": " + error;
        return false;
    }
    out.name = pathString(utf8Path(path).stem());
    if (out.name.empty()) out.name = "Lit Sphere";
    out.path = path;
    return true;
}

bool encodeMatcapPng(int index, std::vector<uint8_t>& png) {
    const CustomMatcap* c = customMatcap(index);
    if (!c) return false;
    Image img;
    img.width = img.height = c->size;
    img.rgba.resize(c->rgba.size());
    const size_t row = size_t(c->size) * 4;
    for (int y = 0; y < c->size; ++y) std::memcpy(&img.rgba[size_t(c->size - 1 - y) * row], &c->rgba[size_t(y) * row], row);
    return img.encodePng(png);
}

int addCustomMatcap(CustomMatcap m) {
    if (m.name.empty()) m.name = "Lit Sphere";
    if (m.hash == 0) m.hash = hashPixels(m.rgba, m.size);
    // A built-in's name stays the built-in's: the custom one becomes "Chrome 2".
    const std::string base = m.name;
    for (int n = 2; findMatcap(m.name) >= 0 && findMatcap(m.name) < kStyleCount; ++n) m.name = base + " " + std::to_string(n);
    ++g_revision;
    m.removed = false;
    for (size_t i = 0; i < customs().size(); ++i) // the same name, listed or removed: replace it in place
        if (squash(customs()[i].name) == squash(m.name)) {
            customs()[i] = std::move(m);
            return kStyleCount + int(i);
        }
    customs().push_back(std::move(m));
    return matcapCount() - 1;
}

std::vector<std::string> loadMatcapFolders(const std::vector<std::string>& dirs) {
    std::vector<std::string> messages;
    for (const std::string& dir : dirs) {
        std::vector<std::string> files;
        std::error_code ec;
        for (fs::directory_iterator it(utf8Path(dir), ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code fileEc;
            if (isMatcapFile(pathString(it->path())) && it->is_regular_file(fileEc)) files.push_back(pathString(it->path()));
        }
        std::sort(files.begin(), files.end());
        for (const std::string& f : files) {
            CustomMatcap m;
            std::string err;
            if (readMatcapImage(f, m, err)) addCustomMatcap(std::move(m));
            else messages.push_back("Skipped " + err);
        }
    }
    return messages;
}

std::vector<std::string> studioMatcapDirs() {
    const char* v = std::getenv("SCULPTFORGE_MATCAP_PATH");
    return splitSearchPath(v ? v : "");
}

std::vector<uint8_t> generateMatcap(int index, int size) {
    if (size <= 0) return {};
    if (const CustomMatcap* c = customMatcap(index)) { // nearest sample of its own pixels
        std::vector<uint8_t> rgba(size_t(size) * size_t(size) * 4);
        for (int j = 0; j < size; ++j)
            for (int i = 0; i < size; ++i) {
                const int si = std::min(c->size - 1, i * c->size / size), sj = std::min(c->size - 1, j * c->size / size);
                std::memcpy(&rgba[(size_t(j) * size_t(size) + size_t(i)) * 4], &c->rgba[(size_t(sj) * size_t(c->size) + size_t(si)) * 4], 4);
            }
        return rgba;
    }
    if (index < 0 || index >= kStyleCount) return {};
    const MatcapStyle& m = kStyles[index];
    std::vector<uint8_t> rgba(size_t(size) * size_t(size) * 4);
    for (int j = 0; j < size; ++j)
        for (int i = 0; i < size; ++i) {
            float x = (float(i) + 0.5f) / float(size) * 2.0f - 1.0f;
            float y = (float(j) + 0.5f) / float(size) * 2.0f - 1.0f;
            float r2 = x * x + y * y;
            if (r2 > 0.998f) { // outside the disk: repeat the rim so filtering at the edge stays clean
                const float k = std::sqrt(0.998f / r2);
                x *= k;
                y *= k;
                r2 = 0.998f;
            }
            const glm::vec3 n(x, y, std::sqrt(1.0f - r2));
            const glm::vec3 c = glm::clamp(linearToSrgb(glm::max(shade(m, n), glm::vec3(0.0f))), 0.0f, 1.0f);
            uint8_t* px = rgba.data() + (size_t(j) * size_t(size) + size_t(i)) * 4;
            for (int k = 0; k < 3; ++k) px[k] = uint8_t(std::lround(c[k] * 255.0f));
            px[3] = 255;
        }
    return rgba;
}

} // namespace sf
