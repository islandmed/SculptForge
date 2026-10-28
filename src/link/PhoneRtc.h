// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The live face feed's transport (docs/phone-link.md): a WebRTC data channel between the phone's face page,
// which must be served over HTTPS for camera access (it lives on our website, website/link/), and this desktop.
// No server of ours is involved:
//  - desktop -> phone: the desktop's offer (SDP) travels in the face page URL's fragment, which the browser never
//    sends to the website;
//  - phone -> desktop: the answer goes through ntfy.sh, a free public message relay, encrypted with AES-256-GCM
//    under a key that is also only in the fragment (or, without internet, in a tab the phone opens on this
//    desktop's own page);
//  - then the data channel runs directly over the local network (unordered, no retransmits: a late face frame is
//    useless).
// The same link carries a sculpt to the phone's AR page (website/link/ar.html, Mode::Ar): then the channel is
// reliable and ordered, and the model goes down it as binary chunks (Android's Scene Viewer can't load a model from
// this computer's plain-HTTP address, so the page shows it in AR itself, with WebXR).
// Built with libdatachannel and Mbed TLS (cmake/WebRTC.cmake, -DSCULPTFORGE_WEBRTC=ON); without them
// webrtcAvailable() is false and FaceRtc refuses to start.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace sf::link {

bool webrtcAvailable();

class FaceRtc {
public:
    enum class State { Idle, Offered, Connecting, Connected, Closed, Failed };
    enum class Mode { Face, Ar }; // Face: the "face" channel, unordered and lossy; Ar: the "ar" channel, reliable

    explicit FaceRtc(Mode mode = Mode::Face);
    ~FaceRtc();
    FaceRtc(const FaceRtc&) = delete;
    FaceRtc& operator=(const FaceRtc&) = delete;

    // Called on WebRTC threads: each text message, and each state change.
    std::function<void(const std::string&)> onMessage;
    std::function<void(State)> onState;

    // Creates the peer connection and its "face" data channel (negotiated, id 0) and waits (up to `timeoutMs`)
    // for the host candidates. The offer SDP.
    bool createOffer(std::string& sdp, std::string& error, int timeoutMs = 3000);
    bool setAnswer(const std::string& sdp, std::string& error);
    // Sends a text message on the data channel (when connected).
    bool send(const std::string& text);
    // Sends binary data (Mode::Ar: queued, delivered in order). False when the channel isn't open.
    bool sendBinary(const std::string& bytes);
    Mode mode() const { return mode_; }
    State state() const;
    void close();

    // A second peer for tests: answers `offerSdp` like the phone's page does.
    bool answerForTest(const std::string& offerSdp, std::string& answerSdp, std::string& error, int timeoutMs = 3000);
    bool sendFromTestPeer(const std::string& text); // once its channel is open
    // What the test peer receives (set before answerForTest): the message and whether it was binary.
    std::function<void(const std::string&, bool)> onTestMessage;

private:
    struct Impl;
    Mode mode_;
    std::unique_ptr<Impl> impl_;
};

const char* faceRtcStateName(FaceRtc::State state);

// base64url without padding (RFC 4648 section 5); decoding also accepts '+', '/' and padding.
std::string base64UrlEncode(const std::string& bytes);
bool base64UrlDecode(const std::string& text, std::string& bytes);

// AES-256-GCM as the face page's WebCrypto does it: `packed` is base64url(iv[12] | ciphertext | tag[16]).
// Needs the WebRTC build (Mbed TLS); false otherwise.
bool aesGcmEncrypt(const std::string& key, const std::string& plain, std::string& packed, std::string& error);
bool aesGcmDecrypt(const std::string& key, const std::string& packed, std::string& plain, std::string& error);
// `n` cryptographically random bytes (the system's generator).
std::string randomBytes(size_t n);

} // namespace sf::link
