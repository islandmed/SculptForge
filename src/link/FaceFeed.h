// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The live face feed from the phone (link/PhoneLink.h): the phone runs MediaPipe Face Landmarker on its front
// camera and sends one JSON message per frame,
//   {"type":"face","t":<ms>,"weights":{"jawOpen":0.31,...},"head":[qx,qy,qz,qw]}
// with the blend shape names of Apple ARKit (which MediaPipe's blendshape categories use too; MediaPipe has no
// tongueOut and adds "_neutral", which is ignored). The desktop hands the weights to the `face_weights` command
// (facial shapes on the active Part) and can record a take: one such message per line (JSON Lines).
#pragma once

#include <array>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include <glm/gtc/quaternion.hpp>

namespace sf::link {

constexpr int kArkitShapeCount = 52;
// The 52 ARKit blend shape names, in Apple's documentation order.
const std::array<const char*, kArkitShapeCount>& arkitShapeNames();
// Index of an ARKit name, or -1.
int arkitShapeIndex(const std::string& name);
// The same shape on the other side ("eyeBlinkLeft" <-> "eyeBlinkRight"); itself for centre shapes.
int arkitMirrorIndex(int index);

struct FaceFrame {
    double t = 0.0; // the phone's clock, milliseconds
    std::array<float, kArkitShapeCount> weights{};   // 0..1
    std::array<bool, kArkitShapeCount> present{};    // sent in this message
    glm::quat head{1.0f, 0.0f, 0.0f, 0.0f};          // w, x, y, z (glm order); the message sends x, y, z, w
    bool hasHead = false;
    int unknownNames = 0; // weights whose names aren't ARKit's (ignored)

    float weight(const char* name) const;
    int presentCount() const;
    // Left and right swapped (shapes and the head's yaw and roll), for a feed that comes mirrored.
    FaceFrame mirrored() const;
    // {"jawOpen":0.31,...}: the `json` argument of face_weights (present shapes only, 4 decimals).
    std::string weightsJson() const;
    // The whole message again (as a take stores it).
    std::string messageJson() const;
};

constexpr size_t kMaxFaceMessageBytes = 16 * 1024;

// Parses one face message. False with `error` for anything else: not JSON, another type, a weight that isn't a
// number, a head that isn't four numbers. Weights are clamped to 0..1, the head quaternion is normalised.
bool parseFaceMessage(const std::string& text, FaceFrame& frame, std::string& error);

// Records a take: one message per line, written as frames arrive (thread-safe use is the caller's).
class FaceTakeWriter {
public:
    bool open(const std::string& path, std::string& error);
    void write(const FaceFrame& frame);
    void close();
    bool isOpen() const { return out_.is_open(); }
    int frames() const { return frames_; }
    // Milliseconds between the first and the last frame (phone clock).
    double durationMs() const { return frames_ > 0 ? lastT_ - firstT_ : 0.0; }

private:
    std::ofstream out_;
    int frames_ = 0;
    double firstT_ = 0.0, lastT_ = 0.0;
};

// Reads a take back (for tests and playback); false if a line doesn't parse.
bool readFaceTake(const std::string& path, std::vector<FaceFrame>& frames, std::string& error);

} // namespace sf::link
