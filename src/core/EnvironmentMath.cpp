// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/EnvironmentMath.h"

#include "core/BakeImage.h"
#include "core/Parallel.h"

#define STBI_NO_STDIO
#include <stb_image.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>

namespace sf {

namespace assets {
extern const unsigned char hdriPhotoStudio[];
extern const unsigned long long hdriPhotoStudio_size;
extern const unsigned char hdriSmallStudio[];
extern const unsigned long long hdriSmallStudio_size;
extern const unsigned char hdriOutdoorSky[];
extern const unsigned long long hdriOutdoorSky_size;
} // namespace assets

namespace {

constexpr float kPi = 3.14159265358979f;

float radicalInverse(uint32_t b) {
    b = (b << 16u) | (b >> 16u);
    b = ((b & 0x55555555u) << 1u) | ((b & 0xAAAAAAAAu) >> 1u);
    b = ((b & 0x33333333u) << 2u) | ((b & 0xCCCCCCCCu) >> 2u);
    b = ((b & 0x0F0F0F0Fu) << 4u) | ((b & 0xF0F0F0F0u) >> 4u);
    b = ((b & 0x00FF00FFu) << 8u) | ((b & 0xFF00FF00u) >> 8u);
    return float(b) * 2.3283064365386963e-10f;
}

// A GGX half vector around +Z for roughness alpha (= roughness^2).
glm::vec3 sampleGgx(float u1, float u2, float alpha) {
    const float phi = 2.0f * kPi * u1;
    const float a2 = alpha * alpha;
    const float cosT = std::sqrt((1.0f - u2) / (1.0f + (a2 - 1.0f) * u2));
    const float sinT = std::sqrt(std::max(0.0f, 1.0f - cosT * cosT));
    return {sinT * std::cos(phi), sinT * std::sin(phi), cosT};
}

void basis(const glm::vec3& n, glm::vec3& t, glm::vec3& b) {
    const float s = n.z >= 0.0f ? 1.0f : -1.0f;
    const float a = -1.0f / (s + n.z), c = n.x * n.y * a;
    t = glm::vec3(1.0f + s * n.x * n.x * a, s * c, -s * n.x);
    b = glm::vec3(c, s + n.y * n.y * a, -n.y);
}

float ggxD(float NdotH, float alpha) {
    const float a2 = alpha * alpha;
    const float d = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    return a2 / (kPi * d * d);
}

std::filesystem::path fsPath(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }

bool decodeHdr(const unsigned char* data, size_t size, EnvImage& out, std::string& error) {
    int w = 0, h = 0, n = 0;
    float* px = stbi_loadf_from_memory(data, int(size), &w, &h, &n, 3);
    if (!px) {
        error = std::string("Can't read the environment image: ") + (stbi_failure_reason() ? stbi_failure_reason() : "unknown format");
        return false;
    }
    out.width = w;
    out.height = h;
    out.texels.resize(size_t(w) * size_t(h));
    for (size_t i = 0; i < out.texels.size(); ++i)
        out.texels[i] = glm::max(glm::vec3(px[i * 3], px[i * 3 + 1], px[i * 3 + 2]), glm::vec3(0.0f));
    stbi_image_free(px);
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Images and directions
// ---------------------------------------------------------------------------

glm::vec3 equirectDirection(float u, float v) {
    const float phi = (u - 0.5f) * 2.0f * kPi, theta = v * kPi;
    const float st = std::sin(theta);
    return {st * std::sin(phi), std::cos(theta), -st * std::cos(phi)};
}

glm::vec2 equirectCoords(const glm::vec3& d) {
    const float phi = std::atan2(d.x, -d.z);
    return {phi / (2.0f * kPi) + 0.5f, std::acos(std::clamp(d.y, -1.0f, 1.0f)) / kPi};
}

glm::vec3 EnvImage::sample(float u, float v) const {
    if (!valid()) return glm::vec3(0.0f);
    const float fx = u * float(width) - 0.5f, fy = std::clamp(v * float(height) - 0.5f, 0.0f, float(height - 1));
    const float x0f = std::floor(fx), y0f = std::floor(fy);
    const float ax = fx - x0f, ay = fy - y0f;
    auto wrap = [&](int x) { return ((x % width) + width) % width; };
    const int x0 = wrap(int(x0f)), x1 = wrap(int(x0f) + 1);
    const int y0 = int(y0f), y1 = std::min(y0 + 1, height - 1);
    auto at = [&](int x, int y) { return texels[size_t(y) * size_t(width) + size_t(x)]; };
    return glm::mix(glm::mix(at(x0, y0), at(x1, y0), ax), glm::mix(at(x0, y1), at(x1, y1), ax), ay);
}

glm::vec3 EnvImage::sample(const glm::vec3& dir) const {
    const glm::vec2 uv = equirectCoords(dir);
    return sample(uv.x, uv.y);
}

EnvImage downsample(const EnvImage& img) {
    EnvImage out;
    out.width = std::max(1, img.width / 2);
    out.height = std::max(1, img.height / 2);
    out.texels.resize(size_t(out.width) * size_t(out.height));
    for (int y = 0; y < out.height; ++y)
        for (int x = 0; x < out.width; ++x) {
            glm::vec3 s(0.0f);
            int n = 0;
            for (int dy = 0; dy < 2; ++dy)
                for (int dx = 0; dx < 2; ++dx) {
                    const int sx = std::min(x * 2 + dx, img.width - 1), sy = std::min(y * 2 + dy, img.height - 1);
                    s += img.texels[size_t(sy) * size_t(img.width) + size_t(sx)];
                    ++n;
                }
            out.texels[size_t(y) * size_t(out.width) + size_t(x)] = s / float(n);
        }
    return out;
}

// ---------------------------------------------------------------------------
// Diffuse: SH9
// ---------------------------------------------------------------------------

std::array<float, 9> shBasis(const glm::vec3& n) {
    return {0.282095f,
            0.488603f * n.y,
            0.488603f * n.z,
            0.488603f * n.x,
            1.092548f * n.x * n.y,
            1.092548f * n.y * n.z,
            0.315392f * (3.0f * n.z * n.z - 1.0f),
            1.092548f * n.x * n.z,
            0.546274f * (n.x * n.x - n.y * n.y)};
}

glm::vec3 SH9::eval(const glm::vec3& n) const {
    const std::array<float, 9> b = shBasis(n);
    glm::vec3 s(0.0f);
    for (int i = 0; i < 9; ++i) s += c[size_t(i)] * b[size_t(i)];
    return glm::max(s, glm::vec3(0.0f));
}

SH9 irradianceSH9(const EnvImage& env) {
    SH9 sh;
    if (!env.valid()) return sh;
    const float dPhi = 2.0f * kPi / float(env.width), dTheta = kPi / float(env.height);
    for (int y = 0; y < env.height; ++y) {
        const float v = (float(y) + 0.5f) / float(env.height);
        const float dOmega = dPhi * dTheta * std::sin(v * kPi);
        for (int x = 0; x < env.width; ++x) {
            const glm::vec3 d = equirectDirection((float(x) + 0.5f) / float(env.width), v);
            const std::array<float, 9> b = shBasis(d);
            const glm::vec3 L = env.texels[size_t(y) * size_t(env.width) + size_t(x)] * dOmega;
            for (int i = 0; i < 9; ++i) sh.c[size_t(i)] += L * b[size_t(i)];
        }
    }
    // Cosine-lobe convolution (Ramamoorthi & Hanrahan 2001): A0 = pi, A1 = 2 pi / 3, A2 = pi / 4; then / pi.
    const float band[9] = {1.0f, 2.0f / 3.0f, 2.0f / 3.0f, 2.0f / 3.0f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f};
    for (int i = 0; i < 9; ++i) sh.c[size_t(i)] *= band[i];
    return sh;
}

glm::vec3 irradianceBruteForce(const EnvImage& env, const glm::vec3& n) {
    glm::vec3 s(0.0f);
    const float dPhi = 2.0f * kPi / float(env.width), dTheta = kPi / float(env.height);
    for (int y = 0; y < env.height; ++y) {
        const float v = (float(y) + 0.5f) / float(env.height);
        const float dOmega = dPhi * dTheta * std::sin(v * kPi);
        for (int x = 0; x < env.width; ++x) {
            const glm::vec3 d = equirectDirection((float(x) + 0.5f) / float(env.width), v);
            const float c = glm::dot(d, n);
            if (c > 0.0f) s += env.texels[size_t(y) * size_t(env.width) + size_t(x)] * (c * dOmega);
        }
    }
    return s / kPi;
}

// ---------------------------------------------------------------------------
// Specular: BRDF table and prefiltered levels
// ---------------------------------------------------------------------------

glm::vec2 integrateBrdf(float NdotV, float roughness, int samples) {
    NdotV = std::clamp(NdotV, 1e-4f, 1.0f);
    const glm::vec3 V(std::sqrt(1.0f - NdotV * NdotV), 0.0f, NdotV);
    const float alpha = std::max(roughness * roughness, 1e-4f);
    const float k = alpha / 2.0f; // Smith-Schlick for image-based lighting (Karis 2013)
    float A = 0.0f, B = 0.0f;
    for (int i = 0; i < samples; ++i) {
        const glm::vec3 H = sampleGgx((float(i) + 0.5f) / float(samples), radicalInverse(uint32_t(i)), alpha);
        const glm::vec3 L = 2.0f * glm::dot(V, H) * H - V;
        const float NdotL = L.z, NdotH = std::max(H.z, 0.0f), VdotH = std::max(glm::dot(V, H), 0.0f);
        if (NdotL <= 0.0f) continue;
        const float gv = NdotV / (NdotV * (1.0f - k) + k), gl = NdotL / (NdotL * (1.0f - k) + k);
        const float gVis = gv * gl * VdotH / std::max(NdotH * NdotV, 1e-6f);
        const float fc = std::pow(1.0f - VdotH, 5.0f);
        A += (1.0f - fc) * gVis;
        B += fc * gVis;
    }
    return glm::vec2(A, B) / float(samples);
}

std::vector<glm::vec2> brdfLut(int size) {
    std::vector<glm::vec2> lut(size_t(size) * size_t(size));
    parallelFor(size_t(size), [&](size_t b, size_t e) {
        for (size_t y = b; y < e; ++y)
            for (int x = 0; x < size; ++x)
                lut[y * size_t(size) + size_t(x)] =
                    integrateBrdf((float(x) + 0.5f) / float(size), (float(y) + 0.5f) / float(size), 256);
    }, 1);
    return lut;
}

std::vector<EnvImage> prefilterEnvironment(const EnvImage& envIn, int samples) {
    std::vector<EnvImage> levels;
    if (!envIn.valid()) return levels;
    // Source pyramid from at least the base size, for filtered importance sampling (Krivanek & Colbert 2008:
    // each sample reads the mip whose texels cover its share of the lobe, so few samples come out smooth).
    std::vector<EnvImage> pyramid{envIn};
    while (pyramid.back().width > kEnvBaseWidth * 2) pyramid.push_back(downsample(pyramid.back()));
    while (pyramid.back().width > 4) pyramid.push_back(downsample(pyramid.back()));
    auto trilinear = [&](const glm::vec3& d, float mip) {
        mip = std::clamp(mip, 0.0f, float(pyramid.size() - 1));
        const int m0 = int(mip), m1 = std::min(m0 + 1, int(pyramid.size()) - 1);
        const glm::vec2 uv = equirectCoords(d);
        return glm::mix(pyramid[size_t(m0)].sample(uv.x, uv.y), pyramid[size_t(m1)].sample(uv.x, uv.y), mip - float(m0));
    };
    const float texelSolid = 4.0f * kPi / (float(pyramid[0].width) * float(pyramid[0].height));
    for (int k = 0; k < kEnvLevels; ++k) {
        EnvImage out;
        out.width = std::max(4, kEnvBaseWidth >> k);
        out.height = std::max(2, out.width / 2);
        out.texels.resize(size_t(out.width) * size_t(out.height));
        const float roughness = float(k) / float(kEnvLevels - 1);
        const float alpha = std::max(roughness * roughness, 1e-3f);
        // The level-0 image: the source at this size (the pyramid level that is at least as big, resampled).
        parallelFor(size_t(out.height), [&](size_t b, size_t e) {
            for (size_t y = b; y < e; ++y)
                for (int x = 0; x < out.width; ++x) {
                    const glm::vec3 N = equirectDirection((float(x) + 0.5f) / float(out.width), (float(y) + 0.5f) / float(out.height));
                    glm::vec3& o = out.texels[y * size_t(out.width) + size_t(x)];
                    if (k == 0) {
                        const float mip = std::log2(float(pyramid[0].width) / float(out.width));
                        o = trilinear(N, mip);
                        continue;
                    }
                    glm::vec3 t, bt;
                    basis(N, t, bt);
                    glm::vec3 sum(0.0f);
                    float weight = 0.0f;
                    for (int i = 0; i < samples; ++i) {
                        const glm::vec3 h = sampleGgx((float(i) + 0.5f) / float(samples), radicalInverse(uint32_t(i)), alpha);
                        const glm::vec3 H = t * h.x + bt * h.y + N * h.z;
                        const glm::vec3 L = 2.0f * glm::dot(N, H) * H - N; // V = N
                        const float NdotL = glm::dot(N, L);
                        if (NdotL <= 0.0f) continue;
                        const float pdf = ggxD(h.z, alpha) / 4.0f; // D * NdotH / (4 VdotH) with V = N
                        const float sampleSolid = 1.0f / (float(samples) * std::max(pdf, 1e-6f));
                        const float mip = 0.5f * std::log2(sampleSolid / texelSolid) + 1.0f;
                        sum += trilinear(L, mip) * NdotL;
                        weight += NdotL;
                    }
                    o = weight > 0.0f ? sum / weight : glm::vec3(0.0f);
                }
        }, 1);
        levels.push_back(std::move(out));
    }
    return levels;
}

// ---------------------------------------------------------------------------
// Tone mapping
// ---------------------------------------------------------------------------

const char* toneMapId(ToneMap t) {
    switch (t) {
    case ToneMap::Aces: return "aces";
    case ToneMap::AgX: return "agx";
    case ToneMap::Reinhard: return "reinhard";
    case ToneMap::None: return "none";
    }
    return "aces";
}

const char* toneMapLabel(ToneMap t) {
    switch (t) {
    case ToneMap::Aces: return "ACES";
    case ToneMap::AgX: return "AgX";
    case ToneMap::Reinhard: return "Reinhard";
    case ToneMap::None: return "None";
    }
    return "ACES";
}

bool parseToneMap(const std::string& text, ToneMap& out) {
    std::string s;
    for (char c : text) s += char(std::tolower(static_cast<unsigned char>(c)));
    for (int i = 0; i < kToneMapCount; ++i)
        if (s == toneMapId(ToneMap(i))) {
            out = ToneMap(i);
            return true;
        }
    if (s == "linear" || s == "off") {
        out = ToneMap::None;
        return true;
    }
    return false;
}

glm::vec3 toneMap(ToneMap t, const glm::vec3& cIn) {
    const glm::vec3 c = glm::max(cIn, glm::vec3(0.0f));
    switch (t) {
    case ToneMap::Aces: { // Narkowicz's fit of the ACES film curve
        const glm::vec3 r = (c * (2.51f * c + 0.03f)) / (c * (2.43f * c + 0.59f) + 0.14f);
        return glm::clamp(r, 0.0f, 1.0f);
    }
    case ToneMap::AgX: {
        // AgX-style: into a log2 space of 16.5 stops around mid grey, through a smooth S curve, then back to
        // display-linear. The inset matrix (rows) desaturates bright colours toward white as film does.
        const glm::mat3 inset(glm::vec3(0.842479f, 0.0423282f, 0.0423756f), glm::vec3(0.0784336f, 0.878468f, 0.0784336f),
                              glm::vec3(0.0792237f, 0.0791661f, 0.879142f));
        const glm::mat3 outset(glm::vec3(1.19688f, -0.0528968f, -0.0529716f), glm::vec3(-0.0980209f, 1.15190f, -0.0980435f),
                               glm::vec3(-0.0990297f, -0.0989612f, 1.15107f));
        glm::vec3 x = inset * c;
        const float minEv = -12.47393f, maxEv = 4.026069f;
        for (int i = 0; i < 3; ++i) {
            float v = std::log2(std::max(x[i], 1e-10f));
            v = std::clamp((v - minEv) / (maxEv - minEv), 0.0f, 1.0f);
            // A smooth S curve through (0, 0), (0.606, 0.5), (1, 1): mid grey lands at about 0.18 display-linear.
            const float p = 0.6060606f;
            const float s = v < p ? 0.5f * std::pow(v / p, 2.2f) : 1.0f - 0.5f * std::pow((1.0f - v) / (1.0f - p), 1.6f);
            x[i] = s;
        }
        x = outset * x;
        return glm::clamp(glm::pow(glm::max(x, glm::vec3(0.0f)), glm::vec3(2.2f)), 0.0f, 1.0f);
    }
    case ToneMap::Reinhard: { // extended Reinhard on luminance, white at 6
        const float l = glm::dot(c, glm::vec3(0.2126f, 0.7152f, 0.0722f));
        if (l <= 0.0f) return glm::vec3(0.0f);
        const float w2 = 36.0f;
        const float lo = l * (1.0f + l / w2) / (1.0f + l);
        return glm::clamp(c * (lo / l), 0.0f, 1.0f);
    }
    case ToneMap::None: return glm::clamp(c, 0.0f, 1.0f);
    }
    return glm::clamp(c, 0.0f, 1.0f);
}

// ---------------------------------------------------------------------------
// Environments
// ---------------------------------------------------------------------------

EnvImage studioEnvironment(int width) {
    EnvImage img;
    img.width = std::max(8, width);
    img.height = img.width / 2;
    img.texels.resize(size_t(img.width) * size_t(img.height));
    struct Box {
        float azimuth, elevation; // degrees: azimuth 0 = -Z (in front of the model), 90 = +X
        float halfW, halfH;       // half size on the box's plane at distance 1 (tangent of the half angle)
        glm::vec3 color;
    };
    const Box boxes[] = {
        {55.0f, 38.0f, 0.45f, 0.32f, glm::vec3(9.0f, 8.7f, 8.2f)},    // key: big, warm-white, high to the front right
        {-110.0f, 10.0f, 0.55f, 0.35f, glm::vec3(0.8f, 0.9f, 1.1f)},  // fill: broad and cool, to the left
        {170.0f, 22.0f, 0.10f, 0.75f, glm::vec3(6.0f, 6.0f, 6.3f)},   // rim: a tall strip behind
        {0.0f, 85.0f, 0.5f, 0.5f, glm::vec3(0.9f)},                   // overhead
    };
    for (int y = 0; y < img.height; ++y)
        for (int x = 0; x < img.width; ++x) {
            const glm::vec3 d = equirectDirection((float(x) + 0.5f) / float(img.width), (float(y) + 0.5f) / float(img.height));
            // Dome: a soft grey that darkens toward the horizon, then a warm dim floor.
            glm::vec3 c = d.y >= 0.0f ? glm::mix(glm::vec3(0.10f, 0.105f, 0.11f), glm::vec3(0.22f, 0.225f, 0.24f), std::sqrt(d.y))
                                      : glm::mix(glm::vec3(0.12f, 0.11f, 0.10f), glm::vec3(0.05f, 0.045f, 0.04f), std::sqrt(-d.y));
            for (const Box& b : boxes) {
                const float az = glm::radians(b.azimuth), el = glm::radians(b.elevation);
                const glm::vec3 dir(std::cos(el) * std::sin(az), std::sin(el), -std::cos(el) * std::cos(az));
                const float facing = glm::dot(d, dir);
                if (facing <= 0.05f) continue;
                glm::vec3 right = glm::normalize(glm::cross(dir, glm::vec3(0, 1, 0)) + glm::vec3(1e-6f, 0, 0));
                const glm::vec3 up = glm::cross(right, dir);
                const float px = glm::dot(d, right) / facing, py = glm::dot(d, up) / facing;
                const float soft = 0.06f;
                const float wx = 1.0f - glm::smoothstep(b.halfW - soft, b.halfW + soft, std::abs(px));
                const float wy = 1.0f - glm::smoothstep(b.halfH - soft, b.halfH + soft, std::abs(py));
                c += b.color * (wx * wy);
            }
            img.texels[size_t(y) * size_t(img.width) + size_t(x)] = c;
        }
    return img;
}

const std::vector<EnvironmentInfo>& environments() {
    static std::vector<EnvironmentInfo> list;
    static std::once_flag once;
    std::call_once(once, [] {
        list.push_back({"studio", "Studio", "", "Generated by SculptForge"});
        list.push_back({"photo_studio", "Photo Studio", "", "Brown Photostudio 02 by Sergej Majboroda, Poly Haven, CC0"});
        list.push_back({"small_studio", "Small Studio", "", "Studio Small 09 by Sergej Majboroda, Poly Haven, CC0"});
        list.push_back({"outdoor_sky", "Outdoor Sky", "",
                        "Kloofendal 48d Partly Cloudy (Pure Sky) by Greg Zaal and Jarod Guest, Poly Haven, CC0"});
        const char* env = std::getenv(kHdriPathVariable);
        if (!env) return;
#ifdef _WIN32
        const char sep = ';';
#else
        const char sep = ':';
#endif
        std::string all = env;
        size_t start = 0;
        while (start <= all.size()) {
            const size_t end = std::min(all.find(sep, start), all.size());
            const std::string dir = all.substr(start, end - start);
            start = end + 1;
            if (dir.empty()) continue;
            std::error_code ec;
            std::vector<std::filesystem::path> files;
            for (auto it = std::filesystem::directory_iterator(fsPath(dir), ec); !ec && it != std::filesystem::directory_iterator();
                 it.increment(ec)) {
                std::string ext = it->path().extension().string();
                for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
                if (ext == ".hdr" || ext == ".exr") files.push_back(it->path());
            }
            std::sort(files.begin(), files.end());
            for (const auto& f : files) {
                const std::u8string stem = f.stem().u8string(), full = f.u8string();
                const std::string s(stem.begin(), stem.end());
                list.push_back({s, s, std::string(full.begin(), full.end()), "From " + std::string(kHdriPathVariable)});
            }
        }
    });
    return list;
}

int environmentCount() { return int(environments().size()); }

int findEnvironment(const std::string& text) {
    std::string s;
    for (char c : text) s += char(std::tolower(static_cast<unsigned char>(c)));
    const auto& list = environments();
    for (size_t i = 0; i < list.size(); ++i) {
        std::string id = list[i].id, label = list[i].label;
        for (char& c : id) c = char(std::tolower(static_cast<unsigned char>(c)));
        for (char& c : label) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (s == id || s == label) return int(i);
    }
    if (!s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        const int i = std::atoi(s.c_str());
        if (i >= 0 && i < int(list.size())) return i;
    }
    return -1;
}

bool loadEnvironment(int index, EnvImage& out, std::string& error) {
    const auto& list = environments();
    if (index < 0 || index >= int(list.size())) {
        error = "No such environment";
        return false;
    }
    switch (index) {
    case 0: out = studioEnvironment(512); return true;
    case 1: return decodeHdr(assets::hdriPhotoStudio, size_t(assets::hdriPhotoStudio_size), out, error);
    case 2: return decodeHdr(assets::hdriSmallStudio, size_t(assets::hdriSmallStudio_size), out, error);
    case 3: return decodeHdr(assets::hdriOutdoorSky, size_t(assets::hdriOutdoorSky_size), out, error);
    default: break;
    }
    const std::string& path = list[size_t(index)].path;
    std::string ext = fsPath(path).extension().string();
    for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
    if (ext == ".exr") {
        int w = 0, h = 0, ch = 0;
        std::vector<float> px;
        if (!loadExr(path, w, h, ch, px, error)) return false;
        out.width = w;
        out.height = h;
        out.texels.resize(size_t(w) * size_t(h));
        for (size_t i = 0; i < out.texels.size(); ++i)
            for (int c = 0; c < 3; ++c) out.texels[i][c] = std::max(0.0f, px[i * size_t(ch) + size_t(std::min(c, ch - 1))]);
        return true;
    }
    std::ifstream f(fsPath(path), std::ios::binary);
    if (!f) {
        error = "Can't open " + path;
        return false;
    }
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (bytes.size() > (size_t(512) << 20)) {
        error = "The environment file is too large";
        return false;
    }
    return decodeHdr(bytes.data(), bytes.size(), out, error);
}

bool buildEnvironmentLighting(int index, EnvironmentLighting& out, std::string& error) {
    const auto start = std::chrono::steady_clock::now();
    EnvImage env;
    if (!loadEnvironment(index, env, error)) return false;
    if (!env.valid() || env.width < 8 || env.height < 4) {
        error = "The environment image is too small";
        return false;
    }
    out.index = index;
    out.levels = prefilterEnvironment(env);
    // SH from a small copy: the low bands don't need the detail.
    EnvImage small = env;
    while (small.width > 128) small = downsample(small);
    out.sh = irradianceSH9(small);
    out.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return true;
}

} // namespace sf
