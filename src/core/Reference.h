// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reference images (View > Reference images; idea by Oslen on Discord): pictures that stay in the view while you
// sculpt, to match proportions against. Each is either Behind the model (a backdrop fixed to the view, under the
// Parts) or In the scene (a plane at the front, side or top of the model that turns with the view and fades out
// when seen edge-on). They never take strokes or picks (the brushes ray-cast the mesh only) and show in renders,
// exports and the timelapse only when "Show in renders" is on. Projects keep them (the image embedded, or only its
// path); reference_add / reference_set / reference_remove script them.
#pragma once

#include "core/Image.h"
#include "core/Mesh.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sf {

class CommandRegistry;
struct Document;
class Json;

enum class ReferenceMode : uint8_t { Behind, Scene };
enum class ReferencePlane : uint8_t { Front, Side, Top };

constexpr int kMaxReferences = 8;                          // per project
constexpr int kReferenceMaxSide = 4096;                    // longer side; bigger images are scaled down on load
constexpr size_t kReferenceMaxPixels = size_t(64) << 20;    // a header claiming more is refused before decoding,
constexpr int kReferenceMaxHeaderSide = 8192;               // and so is one with a longer side than this
constexpr size_t kReferenceMaxFileBytes = size_t(64) << 20; // files bigger than this are refused
constexpr size_t kReferenceMaxEmbedBytes = size_t(32) << 20; // the original file embeds as is up to this size

struct ReferenceImage {
    std::shared_ptr<const Image> image; // null: the file is missing (an unembedded reference whose path moved)
    // The file's own bytes when they can be embedded unchanged (not scaled down, small enough); else null and an
    // embedded reference is saved as a PNG of `image`.
    std::shared_ptr<const std::vector<uint8_t>> fileBytes;
    std::string name, path;
    ReferenceMode mode = ReferenceMode::Behind;
    ReferencePlane plane = ReferencePlane::Front;
    glm::vec2 center{0.5f, 0.5f}; // Behind: the centre in canvas fractions (0..1 across, 0..1 down)
    glm::vec3 origin{0.0f};       // In the scene: the plane's centre, world space
    float height = 0.8f;          // Behind: fraction of the canvas's height
    float sceneHeight = 0.0f;     // In the scene: world units (0: not placed yet, placed from the scene's bounds)
    float stretch = 1.0f;         // width / (height x the image's aspect): 1 keeps the image's proportions
    float opacity = 0.5f;
    bool flipX = false, flipY = false;
    bool locked = false;    // Adjust leaves it alone
    bool visible = true;
    bool inRenders = false; // also in renders, exports and the timelapse
    bool embed = true;      // saved inside the project (off: only its path)
    bool invalid = false;   // its data isn't an image within the caps (not saved: the list says "(invalid image)")

    bool shown() const { return visible && image && !image->empty(); }
    float aspect() const { return image && !image->empty() ? image->aspect() : 1.0f; }
    // In the scene: the plane's right (u) and down (v) directions on the image, and its normal toward the view it
    // is for (Front: +Z, Side: +X, Top: +Y, as View > Front / Right / Top look at it).
    void axes(glm::vec3& u, glm::vec3& v, glm::vec3& n) const;
    // Its corners at image coordinates (0,0) (1,0) (1,1) (0,1): Behind, canvas fractions (z = 0) on a canvas
    // `canvasAspect` (width / height) wide; In the scene, world positions.
    std::array<glm::vec3, 4> corners(float canvasAspect) const;
    // The image coordinates drawn at corner i (the flips applied).
    glm::vec2 cornerUv(int i) const;
    // In the scene: placed just behind `bounds` (as seen from its view), as tall as them plus a margin.
    void placeInScene(const AABB& bounds);
    // Behind: centred, 80% of the canvas's height; In the scene: placeInScene. Proportions back to the image's.
    void resetPlacement(const AABB& bounds);
    // How much of it shows from a view: 1 face on, fading to 0 as it turns edge-on (Behind: always 1).
    float facing(const glm::mat4& view, bool ortho) const;
};

// A path that names a file on this computer. Not: \\host\share\x.png or //host/x (network shares: opening one makes the app
// connect out, and on Windows hand over credentials), \\?\UNC\..., \\.\..., anything with "://" or a URL scheme (file:x,
// http:x). A drive letter (C:\x.png) is local. A project is untrusted: these are never opened.
bool isLocalReferencePath(const std::string& path);
// Reads an image file into `out` (name, path, image, fileBytes), refusing files over kReferenceMaxFileBytes and
// scaling images down to kReferenceMaxSide. Placement is left as it is.
bool loadReferenceFile(const std::string& path, ReferenceImage& out, std::string& error);
// The handle at window point `p` of a reference drawn with window corners `c` (as corners()): 1-4 a corner (within
// `grab` px), 0 inside, -1 neither.
int referenceHandleAt(const std::array<glm::vec2, 4>& c, glm::vec2 p, float grab);
// The GPU memory the shown references' textures take (RGBA8 with mip levels), in bytes.
size_t referenceGpuBytes(const std::vector<ReferenceImage>& refs);

// Project files: the manifest's "references" list. `embedded` is the image saved inside the project for this
// reference (empty: saved by path only). fromJson loads the embedded bytes, else the path (missing: no image).
Json referenceToJson(const ReferenceImage& r);
bool referenceFromJson(const Json& j, const std::vector<uint8_t>* embedded, ReferenceImage& out);
// What an embedded reference saves: its file as read, or a PNG of the image.
bool referenceEmbedBytes(const ReferenceImage& r, std::vector<uint8_t>& out);

// reference_add, reference_set, reference_remove, reference_pair (called by the CommandRegistry constructor).
void registerReferenceCommands(CommandRegistry& registry);

} // namespace sf
