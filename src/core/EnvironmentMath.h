// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Environment maths for the PBR viewport (render/Renderer.cpp, RenderSettings::shading = Pbr): the CPU side of
// image-based lighting, kept free of GL so it is tested on its own.
//
//   - Environments are equirectangular linear-RGB images (EnvImage, row 0 at the top, +Y up, u = 0.5 looking
//     down -Z). The built-in list is "Studio" (generated in code) and three CC0 HDRIs bundled in assets/hdri
//     (THIRD_PARTY_NOTICES.md); .hdr / .exr files in the folders of SCULPTFORGE_HDRI_PATH are added after them, then
//     the files the user added with "Add HDRI..." (referenced by path, never copied; a missing file stays listed).
//     Every file goes through the one decoder (stb_image's Radiance reader, BakeImage's EXR reader) behind the same
//     caps (kMaxEnvironmentWidth x Height, kMaxEnvironmentFileBytes), checked from the header before any pixel is read.
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
    std::string path;  // a file from SCULPTFORGE_HDRI_PATH or one the user added; empty for built-ins
    std::string credit; // who made it and its licence, for the UI's tooltip
    int builtin = -1;   // 0..3 for the generated Studio and the three bundled files, else -1
    bool user = false;  // added with addEnvironmentFile (can be removed again)
};
// Largest image an environment may be, and its file: a 8192 x 4096 image is 400 MB decoded as floats. (The view
// uses at most kEnvBaseWidth texels across, so bigger files add only load time.)
constexpr int kMaxEnvironmentWidth = 8192, kMaxEnvironmentHeight = 4096;
constexpr size_t kMaxEnvironmentFileBytes = size_t(512) << 20;
constexpr int kMaxUserEnvironments = 64;
// The built-ins, then the files found in SCULPTFORGE_HDRI_PATH (searched once, at the first call), then the user's.
// The list changes only from the main thread (addEnvironmentFile and friends): a worker that loads an environment
// is handed its EnvironmentInfo by value.
const std::vector<EnvironmentInfo>& environments();
int environmentCount();
// Accepts the id, the label (any case) or an index.
int findEnvironment(const std::string& text);
// Loads environment `index` (generated, decoded from the embedded file, or read from disk).
bool loadEnvironment(int index, EnvImage& out, std::string& error);
bool loadEnvironment(const EnvironmentInfo& info, EnvImage& out, std::string& error);
// A path on another computer: UNC (\\server\share, //server/share, \\?\UNC\...). Never opened, not even to look at it: on
// Windows the system would send the user's credentials to that host. Mapped drive letters are not this.
bool isNetworkPath(const std::string& path);
// Reads only the header of a .hdr / .exr file: its size, and an error when it is not one, is too big or is unreadable
// (the message says what to do). addEnvironmentFile and loadEnvironment both start with this.
bool probeEnvironmentFile(const std::string& path, int& width, int& height, std::string& error);

// --- The user's own environments (Material > Viewport Shading > Add HDRI...) ---
// Adds the file (checked with probeEnvironmentFile) after the others and returns its index; a file already listed
// returns its index. -1 and `error` otherwise. The list's index of everything after it is unchanged.
int addEnvironmentFile(const std::string& path, std::string& error);
// Removes a user-added entry; the entries after it move up by one. False for a built-in or a folder file.
bool removeEnvironment(int index);
// The user's entries as paths, in order: what is saved in the preferences (hdri.json).
std::vector<std::string> userEnvironmentPaths();
// Replaces the user's entries with these paths (the preferences at start). Files are not read: a missing one stays
// listed, greyed out. At most kMaxUserEnvironments.
void setUserEnvironmentPaths(const std::vector<std::string>& paths);
// A file-backed entry whose file can't be opened now (built-ins never are).
bool environmentFileMissing(int index);
// Whether the file of a file-backed entry can be used now: its header is read (probeEnvironmentFile), not its pixels.
// False with `why` for a missing file, an unsupported EXR compression, a size over the caps or a file that is not an HDR
// image; true for built-ins.
bool environmentUsable(int index, std::string& why);
// Moves when entries are added or removed: the renderer drops what it kept by index.
uint32_t environmentsRevision();
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
bool buildEnvironmentLighting(const EnvironmentInfo& info, int index, EnvironmentLighting& out, std::string& error);

} // namespace sf
