// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Built-in MatCap ("material capture") materials. Each is a lit-sphere image
// generated procedurally here (no third-party images): the viewport looks up
// the colour by the view-space normal, so a whole material costs one texture
// fetch. Polypaint multiplies the matcap; cavity shading is applied on top.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sf {

int matcapCount();
const char* matcapName(int index);
// Case-insensitive; also accepts names without spaces ("redwax"). -1 if unknown.
int findMatcap(const std::string& name);
// The material new documents (and thumbnails) start with: Soft Clay, a matte warm clay.
int defaultMatcap();
// The order the pickers list the MatCaps in: k = 0 is the default, then the others in index order.
int matcapInListOrder(int k);

// How strongly the viewport darkens creases and lifts ridges on top of this matcap (the
// curvature term in the mesh shader); the Sculpt material leans on it more than the others.
struct MatcapCavity {
    float crease = 0.55f, ridge = 0.2f;
};
MatcapCavity matcapCavity(int index);

// `size` x `size` RGBA8 pixels, sRGB-encoded, rows bottom-up (OpenGL order):
// pixel (i, j) shows the sphere normal x = (i + 0.5) / size * 2 - 1,
// y = (j + 0.5) / size * 2 - 1. Pixels outside the disk repeat the rim.
// For a custom MatCap: its own pixels, resampled to `size`.
std::vector<uint8_t> generateMatcap(int index, int size = 256);

// --- Custom MatCaps (#15) ---
// Sphere images the artist loads (Material > Load MatCap...) or drops into the library's MatCaps folder. They come
// after the built-ins in index order (matcapCount() includes them), for the session; the registry is process-wide
// and only touched from one thread (the app's main thread, or a CLI / test).
struct CustomMatcap {
    std::string name;          // the file name without its extension
    std::string path;          // the image it was read from (empty: embedded in a project)
    int size = 0;              // size x size pixels
    std::vector<uint8_t> rgba; // sRGB RGBA8, rows bottom-up, as generateMatcap
    uint64_t hash = 0;         // of the pixels (thumbnail keys, finding a project's MatCap again)
    bool removed = false;      // removeCustomMatcap: kept so indices stay put, but listed and found no more
};
constexpr int kMaxMatcapSide = 1024; // bigger images are scaled down

int builtinMatcapCount();
bool isCustomMatcap(int index); // also true for a removed one (its index stays taken)
// Shown in the material lists: a built-in, or a custom MatCap that wasn't removed.
bool isMatcapListed(int index);
const CustomMatcap* customMatcap(int index); // null for built-ins and bad indices
// Changes whenever a custom MatCap is added or its pixels replaced (renderers upload again).
uint64_t matcapRevision();
// Name plus, for custom MatCaps, the pixel hash: a cache key that changes with the image.
std::string matcapKey(int index);

// .png .jpg .jpeg .exr (also .bmp .tga .hdr, which stb_image reads).
bool isMatcapFile(const std::string& path);
// Reads a sphere image: 8-bit formats through stb_image, OpenEXR as linear light (clamped, then sRGB). A non-square
// image is cropped to its centred square; the name is the file name without its extension.
bool readMatcapImage(const std::string& path, CustomMatcap& out, std::string& error);
bool decodeMatcapImage(const unsigned char* bytes, size_t size, CustomMatcap& out, std::string& error);
// PNG of the MatCap's pixels (top row first), e.g. to embed it in a project; false for built-ins.
bool encodeMatcapPng(int index, std::vector<uint8_t>& png);
// Adds `m`, or replaces the pixels of the custom MatCap of the same name (case-insensitive; a removed one comes
// back at its old index); a built-in's name gets " 2" (" 3" ...). Returns its index.
int addCustomMatcap(CustomMatcap m);
// Takes a custom MatCap out of the lists and name lookups for the rest of the session (its file is the caller's
// business). False for built-ins and bad indices.
bool removeCustomMatcap(int index);
// The listed custom MatCap read from `path` (exact path), or -1.
int findMatcapByPath(const std::string& path);
// The index of a custom MatCap with exactly these pixels, or -1.
int findMatcapByHash(uint64_t hash);
// Every MatCap image directly in each folder (sorted by name; later folders replace earlier ones by name).
// Returns a message per file that couldn't be read.
std::vector<std::string> loadMatcapFolders(const std::vector<std::string>& dirs);
// Studio MatCap folders from SCULPTFORGE_MATCAP_PATH (':' separated, ';' on Windows), read-only.
std::vector<std::string> studioMatcapDirs();

} // namespace sf
