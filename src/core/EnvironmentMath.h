// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Environment maths for the PBR viewport (render/Renderer.cpp, RenderSettings::shading = Pbr): the CPU side of
// image-based lighting, kept free of GL so it is tested on its own.
//
//   - Environments are equirectangular linear-RGB images (EnvImage, row 0 at the top, +Y up, u = 0.5 looking
//     down -Z). The built-in list is "Studio" (generated in code) and three CC0 HDRIs bundled in assets/hdri
//     (THIRD_PARTY_NOTICES.md); .hdr / .exr files in the folders of SCULPTFORGE_HDRI_PATH are added after them.
//   - Diffuse light is nine spherical-harmonic coefficients (SH9) of the environment, already convolved with the
//     cosine lobe and divided by pi: evaluating them at a normal gives the colour a white diffuse surface shows.
//   - Specular light uses the split-sum approximation: the environment prefiltered with the GGX lobe at a few
//     roughness levels (one image per level, half the size each, uploaded as the mip levels of one texture), and
//     a lookup table of the BRDF's scale and bias for F0 per (n.v, roughness).
//   - Tone maps (ACES fit, AgX-style, Reinhard) take the exposed linear colour to display range; the shader has
//     the same formulas, these are their CPU reference.
#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace sf {

constexpr int kBrdfLutSize = 64;
constexpr const char* kHdriPathVariable = "SCULPTFORGE_HDRI_PATH";
// Prefiltered levels: roughness 0, 0.2, ..., 1 (level k at roughness k / (kEnvLevels - 1)).
constexpr int kEnvLevels = 6;
constexpr int kEnvBaseWidth = 256; // level 0 is 256 x 128

struct EnvImage {
    int width = 0, height = 0;
    std::vector<glm::vec3> texels; // linear RGB, rows top-down
    bool valid() const { return width > 0 && height > 0 && texels.size() == size_t(width) * size_t(height); }
    // Bilinear lookup at equirect coordinates (u wraps, v clamps).
    glm::vec3 sample(float u, float v) const;
    glm::vec3 sample(const glm::vec3& dir) const;
};

// Equirect coordinates <-> unit direction.
glm::vec3 equirectDirection(float u, float v);
glm::vec2 equirectCoords(const glm::vec3& dir);
// Half the size (box filter), at least 1 x 1.
EnvImage downsample(const EnvImage& img);

// --- Diffuse ---
struct SH9 {
    std::array<glm::vec3, 9> c{}; // multiply by the basis below at a normal and add up
    glm::vec3 eval(const glm::vec3& n) const;
};
// Real SH basis, band 0..2, at a unit direction.
std::array<float, 9> shBasis(const glm::vec3& n);
// The environment's irradiance divided by pi, as SH9 (cosine lobe applied).
SH9 irradianceSH9(const EnvImage& env);
// The same by brute force (tests): the cosine-weighted mean radiance over the hemisphere around n.
glm::vec3 irradianceBruteForce(const EnvImage& env, const glm::vec3& n);

// --- Specular ---
// Split-sum BRDF integral for GGX with Smith-Schlick visibility: F0 * x + y is the directional albedo.
glm::vec2 integrateBrdf(float NdotV, float roughness, int samples = 512);
// kBrdfLutSize^2 entries, row = roughness (0..1), column = n.v (0..1), texel centres.
std::vector<glm::vec2> brdfLut(int size = kBrdfLutSize);
// GGX-prefiltered levels of `env` (level 0 = the environment itself at kEnvBaseWidth), kEnvLevels of them.
std::vector<EnvImage> prefilterEnvironment(const EnvImage& env, int samples = 96);

// --- Tone mapping ---
enum class ToneMap : uint8_t { Aces, AgX, Reinhard, None };
constexpr int kToneMapCount = 4;
const char* toneMapId(ToneMap t);    // "aces", "agx", "reinhard", "none"
const char* toneMapLabel(ToneMap t); // "ACES", "AgX", "Reinhard", "None"
bool parseToneMap(const std::string& text, ToneMap& out);
// Exposed linear colour -> display-linear 0..1 (the caller sRGB-encodes).
glm::vec3 toneMap(ToneMap t, const glm::vec3& c);

// --- Environments ---
struct EnvironmentInfo {
    std::string id;    // "studio", "photo_studio", ... or the file's stem for SCULPTFORGE_HDRI_PATH files
    std::string label; // "Studio", ...
    std::string path;  // a file from SCULPTFORGE_HDRI_PATH; empty for built-ins
    std::string credit; // who made it and its licence, for the UI's tooltip
};
// The built-ins, then the files found in SCULPTFORGE_HDRI_PATH (searched once, at the first call).
const std::vector<EnvironmentInfo>& environments();
int environmentCount();
// Accepts the id, the label (any case) or an index.
int findEnvironment(const std::string& text);
// Loads environment `index` (generated, decoded from the embedded file, or read from disk).
bool loadEnvironment(int index, EnvImage& out, std::string& error);
// The generated "Studio": two soft boxes, a rim strip and a warm floor bounce over a dark grey dome.
EnvImage studioEnvironment(int width = 512);

// Everything the renderer uploads for one environment.
struct EnvironmentLighting {
    int index = -1;
    std::vector<EnvImage> levels; // prefiltered, kEnvLevels
    SH9 sh;
    double seconds = 0.0;
};
bool buildEnvironmentLighting(int index, EnvironmentLighting& out, std::string& error);

} // namespace sf
