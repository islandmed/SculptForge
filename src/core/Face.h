// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Facial shapes: Generate Facial Shapes gives a sculpted head the 52 Apple ARKit blend shapes (eyeBlinkLeft,
// jawOpen, mouthSmileRight, ...) as sculpt layers, and face_weights drives them live (the phone's face feed).
//
// How the shapes are made:
//  1. A template head with the 52 shapes (assets/face/ict-arkit.sfface, built from the MIT-licensed ICT-FaceKit
//     by tools/face/build_ict_template.py; embedded in the binary).
//  2. Fit: a similarity transform (from landmarks the artist placed, else from the nose tip and the head's
//     height, refined by ICP over the face) and then a non-rigid registration (non-rigid ICP: closest points on
//     the sculpt pull the template, a membrane energy on the displacement keeps it smooth, the stiffness relaxed
//     over the iterations) wrap the template around the sculpt.
//  3. Transfer: each template shape is carried onto the fitted template by deformation transfer (Sumner and
//     Popovic 2004, solved per shape as a Poisson system over the vertices the shape moves), so the shapes follow
//     the sculpt's proportions instead of the template's.
//  4. Bind: every sculpt vertex near the face takes the offsets of its closest point on the fitted template (with
//     a falloff), so the sculpt's own detail rides along. Symmetric sculpts get mirrored Left / Right pairs.
//  5. Parts: eyes (named "eye...") rotate for the eyeLook shapes, teeth and tongue ("teeth", "tooth", "gum",
//     "jaw", "tongue") follow the lower jaw's motion below the bite, and other Parts on the face (brows,
//     lashes, beard) follow the skin.
// Multires: the shapes are made at the lowest level with enough vertices for a face (kFaceMinVertices) and the
// subdivision levels carry them up, so higher-level detail is kept.
#pragma once

#include "core/Json.h"
#include "core/Mesh.h"
#include "core/MorphAnimation.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sf {

class CommandRegistry;
struct Document;

// The 52 ARKit blend shape names, exactly as Apple ARKit and MediaPipe's Face Landmarker spell them, in
// alphabetical order (MediaPipe's order without _neutral, then tongueOut). Left / Right are the character's own.
const std::vector<std::string>& arkitBlendShapeNames();
int arkitBlendShapeIndex(std::string_view name); // -1 if not an ARKit name
// The other side's shape (eyeBlinkLeft <-> eyeBlinkRight, jawLeft <-> jawRight); centre shapes give themselves.
std::string arkitMirrorName(std::string_view name);

// --- The template head (centimetres, Y up, looking down +Z; the character's left is +X) ---
struct FaceTemplate {
    std::vector<glm::vec3> neutral;
    std::vector<glm::uvec3> triangles;
    // Vertex ranges: [0, faceEnd) face, [faceEnd, skinEnd) head and neck, [skinEnd, mouthEnd) mouth socket,
    // [mouthEnd, neutral.size()) eye sockets.
    uint32_t faceEnd = 0, skinEnd = 0, mouthEnd = 0;
    glm::vec3 lowerTeethCentroid{0.0f};
    float upperTeethY = 0.0f;
    struct Shape {
        std::string name;
        std::vector<uint32_t> index; // the vertices the shape moves
        std::vector<glm::vec3> offset;
        glm::quat jawRotation{1.0f, 0.0f, 0.0f, 0.0f}; // the lower teeth: x' = R x + t
        glm::vec3 jawTranslation{0.0f};
    };
    std::vector<Shape> shapes; // in arkitBlendShapeNames() order
};
// The embedded template, parsed once (thread-safe).
const FaceTemplate& faceTemplate();
bool parseFaceTemplate(const uint8_t* data, size_t size, FaceTemplate& out, std::string& error);

// --- Landmarks: points the artist can place on the sculpt to guide the fit ---
enum FaceLandmark : int {
    kEyeOuterRight, kEyeInnerRight, kEyeInnerLeft, kEyeOuterLeft, kNoseTip, kMouthRight, kMouthLeft, kChin,
    kFaceLandmarkCount
};
const char* faceLandmarkKey(int i);   // "eye_outer_right", ... (JSON keys)
const char* faceLandmarkLabel(int i); // "Right eye, outer corner", ...
uint32_t faceLandmarkVertex(int i);   // the template vertex

struct FaceFitLandmarks {
    std::array<glm::vec3, kFaceLandmarkCount> position{};
    std::array<bool, kFaceLandmarkCount> set{};
    int count() const;
    Json toJson() const;
    // {"nose_tip":[x,y,z], ...}; unknown keys are an error.
    static bool fromJson(const Json& j, FaceFitLandmarks& out, std::string& error);
};

// Uniform scale, rotation and translation (template -> sculpt).
struct Similarity {
    float scale = 1.0f;
    glm::mat3 rotation{1.0f};
    glm::vec3 translation{0.0f};
    glm::vec3 apply(const glm::vec3& p) const { return scale * (rotation * p) + translation; }
    glm::vec3 applyVector(const glm::vec3& v) const { return scale * (rotation * v); }
    glm::vec3 inverse(const glm::vec3& p) const { return glm::transpose(rotation) * (p - translation) / scale; }
};

struct FaceFit {
    Similarity align;                 // the rigid part of the fit
    std::vector<glm::vec3> positions; // the template's vertices wrapped onto the sculpt (sculpt space)
    FaceFitLandmarks landmarks;          // the template's landmarks after the fit (sculpt space)
    float rmsError = 0.0f;            // face vertices that found the sculpt: RMS distance to it (sculpt units)
    float coverage = 0.0f;            // the fraction of face vertices that found the sculpt
};

// Fits the template to the visible triangles of `positions` / `triangles` (the sculpt with its layers at zero).
// `landmarks` (optional, at least 3 set) start the fit and pull their template vertices all the way.
bool fitFaceTemplate(const std::vector<glm::vec3>& positions, const std::vector<glm::uvec3>& triangles,
                     const FaceFitLandmarks* landmarks, FaceFit& out, std::string& error);

// Deformation transfer: the offsets (sculpt space, per template vertex) of every template shape on the fitted
// template, in arkitBlendShapeNames() order. On the template itself this gives back its own shapes.
void transferFaceShapes(const FaceFit& fit, std::vector<std::vector<glm::vec3>>& offsets);

constexpr size_t kFaceMinVertices = 20000;

struct FaceShapesOptions {
    FaceFitLandmarks landmarks;  // none set = automatic
    int symmetry = -1;        // -1 = when the sculpt is symmetric about X = 0, 0 = off, 1 = on
    bool parts = true;     // eyes, teeth, tongue and Parts on the face get the shapes too
    int level = -1;           // subdivision level to make them on (-1 = the lowest with kFaceMinVertices)
};
struct FaceShapesReport {
    FaceFit fit;
    int level = 0;          // the subdivision level the shapes were made on
    bool symmetric = false; // Left / Right were mirrored
    size_t movedVertices = 0;
    std::vector<std::string> parts; // "Eyes: eye rotations", ...
};
// Gives the active Part (and, with options.parts, the others) the 52 shapes as sculpt layers at weight 0,
// replacing layers with those names; one undo step per Part (active one: "Generate Facial Shapes").
bool generateFaceShapes(Document& doc, const FaceShapesOptions& options, FaceShapesReport& report, std::string& error);

// --- Live weights (face_weights) ---
// {"jawOpen":0.3, ...}: names and weights (clamped to -1..2). Error on bad JSON or non-number weights.
bool parseFaceWeights(const std::string& json, std::vector<std::pair<std::string, float>>& out, std::string& error);
struct FaceWeightsStats {
    int layers = 0;         // layers whose weight changed
    size_t vertices = 0;    // vertices re-evaluated (all Parts)
    int matched = 0;        // names that matched a layer on some Part
    std::vector<std::string> unknown; // names no Part has
};
// Sets the weights of the layers with these names on every Part. Only the vertices those layers move are
// re-evaluated (positions, normals, dirty range for the GPU); no undo step, the document isn't marked modified.
FaceWeightsStats applyFaceWeights(Document& doc, const std::vector<std::pair<std::string, float>>& weights);

// --- Takes: recorded face feeds ---
// A JSON array of face messages ({"type":"face","t":<ms>,"weights":{...},"head":[...]}), an object with such an
// array in "frames", or JSON Lines of them. Channels: the 52 ARKit names; times in seconds from the first frame.
bool parseFaceTake(const std::string& text, MorphAnimation& out, std::string& error);
bool loadFaceTake(const std::string& path, MorphAnimation& out, std::string& error);
// The weights at `time` (linear between keys, clamped at the ends).
void sampleMorphAnimation(const MorphAnimation& a, float time, std::vector<std::pair<std::string, float>>& out);
// Keys every 1/fps seconds.
MorphAnimation resampleMorphAnimation(const MorphAnimation& a, float fps);
// The baked take the exporters write (kept in the project, Document::projectSettings["face_animation"]).
void setBakedFaceAnimation(Document& doc, const MorphAnimation* a);
bool bakedFaceAnimation(const Document& doc, MorphAnimation& out);

// --- Expression presets (Face test) ---
struct FacePreset {
    const char* name;
    const char* help;
    std::vector<std::pair<const char*, float>> weights; // the rest are 0
};
const std::vector<FacePreset>& facePresets();
const FacePreset* findFacePreset(std::string_view name);

// face_shapes_generate, face_landmarks_detect, face_weights, face_preset, face_take_bake, face_take_clear.
void registerFaceCommands(CommandRegistry& registry);

} // namespace sf
