// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The phone link (File > Capture from phone; docs/phone-link.md): while its window is open, a small HTTP server
// on the local network serves the capture page to a phone that scanned the window's QR code, and takes:
//  - captures: a scan video or photo burst (for splat training), one photo (for image to 3D) or a splat .ply from
//    a scanning app, uploaded in resumable chunks into the Captures inbox,
//      <user library>/Captures/<yyyy-mm-dd_hhmmss>-<kind>/   (kind = splat | photo | ply)
//    with a capture.json (kind, device, files, times); the app then runs the command for that kind;
//  - AR requests: the active SubTool (or the scene), decimated and coloured, as USDZ (iPhone AR Quick Look) and
//    GLB (Android: sent over the WebRTC link to the website's WebXR page), re-exported after strokes while the
//    phone is looking;
//  - the live face feed: link/FaceFeed.h messages over WebRTC (link/PhoneRtc.h) or HTTP, and face takes.
// Security: everything but the page itself needs the session token (in the QR code's URL; the page also pairs
// with a 6-digit code typed on the phone). The token changes every time the window opens and expires if no
// phone uses it within 10 minutes. Only local-network clients are answered, uploads have size limits, file names
// are reduced to safe characters and written only inside the session's own capture folders. Nothing listens
// while the window is closed.
#pragma once

#include "link/FaceFeed.h"
#include "link/HttpServer.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sf::link {

class FaceRtc;

// Where the face page lives (HTTPS, needed for the camera): our website. One constant; SCULPTFORGE_LINK_URL
// overrides it (e.g. a local HTTPS test server).
inline constexpr const char* kPhoneLinkSiteUrl = "https://sculptforge.app/link/";
// The relays that carry the phone's (encrypted) WebRTC answer, since the HTTPS page can't reach the desktop yet:
// space-separated, tried in order. Ours first (services/link-worker, a Cloudflare Worker), ntfy.sh as the
// fallback (same protocol). SCULPTFORGE_LINK_RELAY overrides the list.
inline constexpr const char* kPhoneLinkRelayUrl = "https://link.sculptforge.app/relay/ https://ntfy.sh/";
inline constexpr uint16_t kPhoneLinkPort = 8765;

inline constexpr uint64_t kMaxCaptureFileBytes = 4ull << 30;  // one video or .ply
inline constexpr uint64_t kMaxCaptureBytes = 8ull << 30;      // all files of one capture
inline constexpr size_t kMaxCaptureFiles = 500;              // a photo burst
inline constexpr size_t kUploadChunkBytes = 4u << 20;        // what the page sends per request
inline constexpr size_t kMaxUploadChunkBytes = 16u << 20;
inline constexpr size_t kMaxArModelBytes = 64u << 20;

struct PhoneLinkOptions {
    std::string inboxRoot;        // the Captures folder (created on first use)
    uint16_t port = kPhoneLinkPort;
    bool localNetworkOnly = true;
    std::string desktopName;      // shown on the phone ("Tony's PC")
    std::string version;          // SculptForge version, shown on the phone
    std::string siteUrl;          // face page; empty = kPhoneLinkSiteUrl (or SCULPTFORGE_LINK_URL)
    std::string relayUrl;         // empty = kPhoneLinkRelayUrl (or SCULPTFORGE_LINK_RELAY); "off" = none;
                                  // space-separated URLs, each followed by the topic
    bool startServer = true;      // false: handle() only (unit tests)
};

enum class CaptureKind { Splat, Photo, Ply, Face };
const char* captureKindName(CaptureKind kind);
bool parseCaptureKind(const std::string& text, CaptureKind& kind);

// A safe file name for an upload: letters, digits, '.', '-', '_' (others become '_'), no leading dot, at most
// 80 characters, keeping the extension. Empty when nothing usable is left.
std::string sanitizeUploadName(const std::string& name);
// Whether an upload of `kind` may have this (lower-case) extension.
bool captureExtensionAllowed(CaptureKind kind, const std::string& extension);
// "iPhone · Safari" from a User-Agent.
std::string describeDevice(const std::string& userAgent);
// "2026-10-26_195803" for local time `t`.
std::string captureTimestamp(std::chrono::system_clock::time_point t);

struct PhoneEvent {
    enum class Type;
    static PhoneEvent make(Type type, std::string text = {}) {
        PhoneEvent e;
        e.type = type;
        e.text = std::move(text);
        return e;
    }
    enum class Type {
        DeviceConnected, // a phone opened the page (text: device)
        CaptureReady,    // an upload finished: kind, folder, files
        ArRequested,     // the phone asked for an AR model (scope, sizeCm)
        FaceConnected,   // the face feed started (text: how)
        FaceDisconnected,
        Error,           // something went wrong (text, in plain words)
    };
    Type type = Type::Error;
    std::string text;
    CaptureKind kind = CaptureKind::Splat;
    std::string folder;
    std::vector<std::string> files; // full paths
    std::string device;
    bool wholeScene = false;
    float sizeCm = 20.0f;
};

struct PhoneUploadInfo {
    std::string captureId, name;
    CaptureKind kind = CaptureKind::Splat;
    uint64_t size = 0, received = 0;
    double bytesPerSecond = 0.0;
    bool finished = false;
};

struct PhoneLinkStatus {
    bool running = false;
    std::string url;     // what the QR code holds
    std::string address; // ip:port
    std::string code;    // 6-digit pairing code
    double tokenExpiresIn = 0; // seconds until an unused token expires (0 once a phone used it)
    bool paired = false;       // a phone used the token
    std::string device;        // the last phone seen ("iPhone · Safari")
    std::string deviceAddress;
    double secondsSinceSeen = -1; // -1 = never
    std::vector<PhoneUploadInfo> uploads; // in progress and recently finished
    int capturesReceived = 0;
    // AR
    uint64_t arVersion = 0; // models published so far
    bool arBusy = false;
    std::string arSummary, arError;
    double secondsSinceArViewed = -1;
    // Face
    bool faceConnected = false;
    std::string faceTransport; // "WebRTC", "Wi-Fi (HTTP)"
    std::string faceState;
    double faceFps = 0;
    uint64_t faceFrames = 0;
    double secondsSinceFace = -1;
    bool recording = false;
    int takeFrames = 0;
    double takeSeconds = 0;
    std::string lastError;
};

class PhoneLink {
public:
    PhoneLink();
    ~PhoneLink();
    PhoneLink(const PhoneLink&) = delete;
    PhoneLink& operator=(const PhoneLink&) = delete;

    // Starts a session: new token and code, and (unless options.startServer is false) the server.
    bool start(const PhoneLinkOptions& options, std::string& error);
    // Stops listening, closes the face link, drops unfinished uploads.
    void stop();
    bool running() const { return running_; }

    // Local addresses the QR code can use (default route first) and the one it uses.
    std::vector<std::string> addresses() const;
    void setAddress(const std::string& address);
    std::string url() const;
    PhoneLinkStatus status() const;
    const std::string& token() const { return token_; }
    std::string code() const;
    uint16_t port() const;
    std::string inboxRoot() const { return options_.inboxRoot; }

    // Events for the main thread, oldest first.
    std::vector<PhoneEvent> takeEvents();

    // The newest face frame (false when there is none newer than `serial`; updates `serial`).
    bool latestFace(FaceFrame& frame, uint64_t& serial) const;
    // Records every face frame into <inbox>/<stamp>-face/take.jsonl (with a capture.json at stop).
    bool startTake(std::string& folder, std::string& error);
    // Stops recording; the take's folder, or "" when nothing was recording.
    std::string stopTake(std::string* summary = nullptr);

    // AR: the app publishes models (GLB for Android, USDZ for iOS) for the phone to fetch.
    void setArBusy(bool busy);
    void publishArModel(std::string glb, std::string usdz, std::string summary);
    void setArError(std::string error);
    // Seconds since the phone last fetched or asked for an AR model (-1 = never): while it is looking, edits are
    // re-exported.
    double secondsSinceArViewed() const;

    // One request, as the server hands it over (public for tests: no sockets needed).
    HttpResponse handle(const HttpRequest& request);
    // A face message from any transport (public for tests).
    bool receiveFaceMessage(const std::string& text, const std::string& transport, std::string& error);
    // Applies a WebRTC answer for the pending face link (the relay and the direct /rtc-answer path use it).
    bool acceptFaceAnswer(const std::string& sdp, std::string& error);
    // The face page URL with the pending offer (creates one); for tests too. With `ar`, the link is for the AR page
    // (website/link/ar.html) instead: the phone asks for the sculpt over it, and each model published while it is
    // open goes down it (`arScope` "subtool"/"scene" and `arSizeCm` are passed to the page, which asks with them).
    bool createFaceLink(std::string& faceUrl, std::string& error, bool ar = false, const std::string& arScope = "subtool",
                        float arSizeCm = 20.0f);
    // An AR page message ({"type":"ar","scope","size"} asks for a model, {"type":"ar-ping"} says it's still
    // looking); public for tests.
    bool receiveArMessage(const std::string& text, std::string& error);
    // Relay message handling, exposed for tests: decrypts and applies an answer posted to the relay.
    bool acceptRelayMessage(const std::string& packed, std::string& error);

private:
    struct Upload;
    struct Capture;
    using Clock = std::chrono::steady_clock;

    HttpResponse route(const HttpRequest& request);
    bool authorised(const HttpRequest& request);
    void noteDevice(const HttpRequest& request);
    void pushEvent(PhoneEvent event);
    void newToken();
    std::string statusJson();
    HttpResponse startCapture(const HttpRequest& request);
    HttpResponse addFile(const std::string& captureId, const HttpRequest& request);
    HttpResponse putChunk(const std::string& fileId, const HttpRequest& request);
    HttpResponse fileStatus(const std::string& fileId);
    HttpResponse finishCapture(const std::string& captureId);
    HttpResponse cancelCapture(const std::string& captureId);
    HttpResponse pair(const HttpRequest& request);
    HttpResponse requestAr(const HttpRequest& request);
    HttpResponse serveArModel(bool usdz);
    HttpResponse faceLink(const HttpRequest& request);
    void sendArModelToPage(); // the latest model (or error) to the AR page, if one is connected
    void uploadArModel();     // the latest model, encrypted, to the relay for the AR page (Android Chrome)
    HttpResponse directAnswer(const HttpRequest& request);
    void relayLoop(std::string topic, std::string key, int64_t sinceUnix);
    std::vector<std::string> relayUrls() const;
    void stopRelay();
    void onFaceState(int state);

    PhoneLinkOptions options_;
    std::atomic<bool> running_{false};
    HttpServer server_;
    mutable std::mutex mutex_;
    std::string token_, code_, address_;
    std::vector<std::string> addresses_;
    Clock::time_point tokenIssued_;
    bool paired_ = false;
    int failedPairs_ = 0;
    Clock::time_point lastPairAttempt_{};
    std::string device_, deviceAddress_;
    Clock::time_point lastSeen_{};
    bool seen_ = false;
    std::map<std::string, std::shared_ptr<Capture>> captures_;
    std::map<std::string, std::shared_ptr<Upload>> uploads_;
    int capturesReceived_ = 0;
    std::deque<PhoneEvent> events_;
    std::string lastError_;
    // AR
    std::string arGlb_, arUsdz_, arSummary_, arError_;
    uint64_t arVersion_ = 0;
    bool arBusy_ = false;
    Clock::time_point arViewed_{};
    bool arViewedEver_ = false;
    // Face
    std::unique_ptr<FaceRtc> rtc_;
    std::mutex faceLinkMutex_; // createFaceLink
    std::mutex rtcMutex_;      // rtc_: creating / answering the peer connection
    uint64_t arSentVersion_ = 0; // the model version the AR page has (rtcMutex_)
    // The AR page's copy on the relay (mutex_): Android Chrome can't reach this computer at all, so the model goes
    // through the internet, encrypted with a key only the page has.
    std::string arBlobUrl_, arBlobKey_;
    uint64_t arBlobVersion_ = 0;
    Clock::time_point arBlobUntil_{};
    std::string faceTransport_;
    bool faceConnected_ = false;
    FaceFrame face_;
    uint64_t faceSerial_ = 0;
    Clock::time_point lastFace_{};
    std::deque<Clock::time_point> faceTimes_; // the last second's frames, for the rate
    FaceTakeWriter take_;
    std::string takeFolder_;
    std::chrono::system_clock::time_point takeStarted_{};
    std::string relayTopic_, relayKey_;
    std::thread relayThread_;
    std::atomic<bool> relayStop_{false};
};

} // namespace sf::link
