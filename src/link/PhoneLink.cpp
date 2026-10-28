// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "link/PhoneLink.h"

#include "ai/Http.h"
#include "core/Json.h"
#include "link/PhoneRtc.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace sf::phone_assets {
// The pages the server hands out (cmake: embed_files, assets/phone/capture.html and website/link/index.html).
extern const unsigned char capturePage[];
extern const unsigned long long capturePage_size;
extern const unsigned char facePage[];
extern const unsigned long long facePage_size;
} // namespace sf::phone_assets

namespace sf::link {

namespace fs = std::filesystem;

namespace {

constexpr auto kUnusedTokenLifetime = std::chrono::minutes(10);
constexpr int kMaxPairFailures = 5;
constexpr int kMaxOpenCaptures = 8;

fs::path u8path(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string pathString(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::string hex(const std::string& bytes) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (unsigned char c : bytes) {
        out += digits[c >> 4];
        out += digits[c & 15];
    }
    return out;
}

std::string newId() { return hex(randomBytes(8)); }

std::string newCode() {
    const std::string r = randomBytes(4);
    const uint32_t v = uint32_t(uint8_t(r[0])) | uint32_t(uint8_t(r[1])) << 8 | uint32_t(uint8_t(r[2])) << 16 |
                       uint32_t(uint8_t(r[3])) << 24;
    char buf[8];
    std::snprintf(buf, sizeof buf, "%06u", v % 1000000u);
    return buf;
}

// Compares without stopping at the first difference (the pairing code and token).
bool sameSecret(const std::string& a, const std::string& b) {
    if (a.size() != b.size() || a.empty()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i) diff |= uint8_t(a[i]) ^ uint8_t(b[i]);
    return diff == 0;
}

HttpResponse jsonError(int status, const std::string& message, const std::string& code = {}) {
    Json j = Json::object();
    j.set("error", message);
    if (!code.empty()) j.set("code", code);
    return HttpResponse::json(status, j.dump());
}

HttpResponse jsonOk(const Json& j) { return HttpResponse::json(200, j.dump()); }

std::string lowerExtension(const std::string& name) {
    const size_t dot = name.rfind('.');
    if (dot == std::string::npos) return {};
    std::string e = name.substr(dot);
    for (char& c : e) c = char(std::tolower(static_cast<unsigned char>(c)));
    return e;
}

bool parseUnsigned(const std::string& text, uint64_t& out) {
    if (text.empty() || text.size() > 19) return false;
    uint64_t v = 0;
    for (char c : text) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        v = v * 10 + uint64_t(c - '0');
    }
    out = v;
    return true;
}

std::string isoTime(std::chrono::system_clock::time_point t) {
    const std::time_t tt = std::chrono::system_clock::to_time_t(t);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%S", &tm);
    return buf;
}

std::string envOr(const char* name, const std::string& fallback) {
    const char* v = std::getenv(name);
    return v && *v ? std::string(v) : fallback;
}

// The path after `prefix` up to the next '/', and what follows: "/api/capture/<id>/finish".
bool splitId(const std::string& path, const std::string& prefix, std::string& id, std::string& rest) {
    if (path.rfind(prefix, 0) != 0) return false;
    const std::string tail = path.substr(prefix.size());
    const size_t slash = tail.find('/');
    id = tail.substr(0, slash);
    rest = slash == std::string::npos ? std::string() : tail.substr(slash);
    return !id.empty() && std::all_of(id.begin(), id.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); });
}

} // namespace

// --- Free helpers ---

const char* captureKindName(CaptureKind kind) {
    switch (kind) {
    case CaptureKind::Splat: return "splat";
    case CaptureKind::Photo: return "photo";
    case CaptureKind::Ply: return "ply";
    case CaptureKind::Face: return "face";
    }
    return "splat";
}

bool parseCaptureKind(const std::string& text, CaptureKind& kind) {
    if (text == "splat") kind = CaptureKind::Splat;
    else if (text == "photo") kind = CaptureKind::Photo;
    else if (text == "ply") kind = CaptureKind::Ply;
    else return false; // (face takes are recorded on the desktop, not uploaded)
    return true;
}

std::string sanitizeUploadName(const std::string& name) {
    // Only the last path component (browsers send bare names, but be sure).
    std::string base = name;
    const size_t slash = base.find_last_of("/\\");
    if (slash != std::string::npos) base = base.substr(slash + 1);
    std::string out;
    for (unsigned char c : base) {
        if (std::isalnum(c) || c == '.' || c == '-' || c == '_') out += char(c);
        else if (c >= 0x80) continue; // UTF-8 bytes: dropped rather than doubled into underscores
        else out += '_';
    }
    while (!out.empty() && (out[0] == '.' || out[0] == '_' || out[0] == '-')) out.erase(0, 1);
    while (!out.empty() && (out.back() == '.' || out.back() == ' ')) out.pop_back();
    if (out.size() > 80) {
        const std::string ext = lowerExtension(out);
        out = out.substr(0, 80 - std::min<size_t>(ext.size(), 16)) + ext.substr(0, 16);
    }
    if (out.empty() || out.find_first_not_of('.') == std::string::npos) return {};
    // Names Windows reserves, with or without an extension.
    std::string stem = out.substr(0, out.find('.'));
    for (char& c : stem) c = char(std::toupper(static_cast<unsigned char>(c)));
    for (const char* reserved : {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7",
                                 "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"})
        if (stem == reserved) return "file_" + out;
    if (out == "capture.json") return "capture_file.json"; // ours
    return out;
}

bool captureExtensionAllowed(CaptureKind kind, const std::string& extension) {
    static const char* video[] = {".mp4", ".mov", ".m4v", ".webm", ".3gp"};
    static const char* image[] = {".jpg", ".jpeg", ".png", ".heic", ".heif", ".webp"};
    static const char* splat[] = {".ply", ".splat", ".spz"};
    auto in = [&](const auto& list) {
        return std::any_of(std::begin(list), std::end(list), [&](const char* e) { return extension == e; });
    };
    switch (kind) {
    case CaptureKind::Splat: return in(video) || in(image);
    case CaptureKind::Photo: return in(image);
    case CaptureKind::Ply: return in(splat);
    case CaptureKind::Face: return extension == ".jsonl";
    }
    return false;
}

std::string describeDevice(const std::string& ua) {
    auto has = [&](const char* s) { return ua.find(s) != std::string::npos; };
    std::string device = has("iPhone")                            ? "iPhone"
                         : has("iPad")                            ? "iPad"
                         : has("Android")                         ? "Android"
                         : has("Macintosh") && has("Mobile")      ? "iPad"
                         : has("Macintosh")                       ? "Mac"
                         : has("Windows")                         ? "Windows PC"
                         : has("Linux")                           ? "Linux PC"
                                                                  : "Device";
    std::string browser = has("CriOS") || (has("Chrome") && !has("Edg") && !has("SamsungBrowser") && !has("OPR"))
                              ? "Chrome"
                          : has("FxiOS") || has("Firefox")             ? "Firefox"
                          : has("EdgiOS") || has("EdgA") || has("Edg/") ? "Edge"
                          : has("SamsungBrowser")                      ? "Samsung Internet"
                          : has("OPR") || has("Opera")                 ? "Opera"
                          : has("Safari")                              ? "Safari"
                                                                       : "";
    return browser.empty() ? device : device + " \xC2\xB7 " + browser;
}

std::string captureTimestamp(std::chrono::system_clock::time_point t) {
    const std::time_t tt = std::chrono::system_clock::to_time_t(t);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d_%H%M%S", &tm);
    return buf;
}

// --- Session state ---

struct PhoneLink::Upload {
    std::string id, captureId, name, path;
    CaptureKind kind = CaptureKind::Splat;
    uint64_t size = 0, received = 0;
    Clock::time_point started{}, lastChunk{};
    double bytesPerSecond = 0.0;
    bool finished = false;
    std::mutex write; // one chunk at a time
};

struct PhoneLink::Capture {
    std::string id, device, dir; // dir: <inbox>/.incoming/<id> until finished
    CaptureKind kind = CaptureKind::Splat;
    std::vector<std::shared_ptr<Upload>> files;
    uint64_t bytes = 0;
    std::chrono::system_clock::time_point started{};
    bool finished = false;
    std::string folder; // final folder once finished
};

PhoneLink::PhoneLink() = default;

PhoneLink::~PhoneLink() { stop(); }

void PhoneLink::newToken() {
    token_ = hex(randomBytes(16));
    code_ = newCode();
    tokenIssued_ = Clock::now();
    paired_ = false;
    failedPairs_ = 0;
}

bool PhoneLink::start(const PhoneLinkOptions& options, std::string& error) {
    stop();
    options_ = options;
    if (options_.siteUrl.empty()) options_.siteUrl = envOr("SCULPTFORGE_LINK_URL", kPhoneLinkSiteUrl);
    if (options_.relayUrl.empty()) options_.relayUrl = envOr("SCULPTFORGE_LINK_RELAY", kPhoneLinkRelayUrl);
    if (options_.inboxRoot.empty()) {
        error = "No Captures folder (the user library is unavailable)";
        return false;
    }
    std::error_code ec;
    fs::create_directories(u8path(options_.inboxRoot) / ".incoming", ec);
    if (ec) {
        error = "Cannot create " + options_.inboxRoot + ": " + ec.message();
        return false;
    }
    {
        std::lock_guard lock(mutex_);
        newToken();
        addresses_ = localNetworkAddresses();
        address_ = addresses_.empty() ? "127.0.0.1" : addresses_.front();
        lastError_.clear();
    }
    if (options_.startServer) {
        HttpServer::Options so;
        so.port = options_.port;
        so.localNetworkOnly = options_.localNetworkOnly;
        so.limits.maxBodyBytes = kMaxUploadChunkBytes + 64 * 1024;
        if (!server_.start(so, [this](const HttpRequest& r) { return handle(r); }, error)) {
            error = "Cannot open the phone link's network port: " + error;
            return false;
        }
    }
    running_ = true;
    return true;
}

void PhoneLink::stop() {
    if (!running_ && !server_.running()) return;
    server_.stop();
    stopRelay();
    {
        std::lock_guard lock(rtcMutex_);
        if (rtc_) rtc_->close();
        rtc_.reset();
    }
    stopTake();
    std::lock_guard lock(mutex_);
    // Unfinished uploads can't resume in another session (the token changes): remove their files.
    std::error_code ec;
    for (const auto& [id, capture] : captures_)
        if (!capture->finished) fs::remove_all(u8path(capture->dir), ec);
    captures_.clear();
    uploads_.clear();
    faceConnected_ = false;
    seen_ = false;
    paired_ = false;
    arGlb_.clear();
    arUsdz_.clear();
    arBusy_ = false;
    running_ = false;
}

std::vector<std::string> PhoneLink::addresses() const {
    std::lock_guard lock(mutex_);
    return addresses_;
}

void PhoneLink::setAddress(const std::string& address) {
    std::lock_guard lock(mutex_);
    address_ = address;
}

uint16_t PhoneLink::port() const { return options_.startServer ? server_.port() : options_.port; }

std::string PhoneLink::url() const {
    std::lock_guard lock(mutex_);
    return "http://" + address_ + ":" + std::to_string(port()) + "/?k=" + token_;
}

std::string PhoneLink::code() const {
    std::lock_guard lock(mutex_);
    return code_;
}

void PhoneLink::pushEvent(PhoneEvent event) {
    // (mutex_ held by the caller)
    if (event.type == PhoneEvent::Type::Error) lastError_ = event.text;
    events_.push_back(std::move(event));
    while (events_.size() > 256) events_.pop_front();
}

std::vector<PhoneEvent> PhoneLink::takeEvents() {
    std::lock_guard lock(mutex_);
    std::vector<PhoneEvent> out(events_.begin(), events_.end());
    events_.clear();
    return out;
}

PhoneLinkStatus PhoneLink::status() const {
    PhoneLinkStatus s;
    const auto now = Clock::now();
    const auto seconds = [now](Clock::time_point t) { return std::chrono::duration<double>(now - t).count(); };
    std::lock_guard lock(mutex_);
    s.running = running_;
    s.address = address_ + ":" + std::to_string(port());
    s.url = "http://" + s.address + "/?k=" + token_;
    s.code = code_;
    s.paired = paired_;
    s.tokenExpiresIn = paired_ ? 0.0 : std::max(0.0, std::chrono::duration<double>(kUnusedTokenLifetime).count() - seconds(tokenIssued_));
    s.device = device_;
    s.deviceAddress = deviceAddress_;
    s.secondsSinceSeen = seen_ ? seconds(lastSeen_) : -1.0;
    for (const auto& [id, u] : uploads_) {
        PhoneUploadInfo info;
        info.captureId = u->captureId;
        info.name = u->name;
        info.kind = u->kind;
        info.size = u->size;
        info.received = u->received;
        info.bytesPerSecond = u->bytesPerSecond;
        info.finished = u->finished;
        s.uploads.push_back(info);
    }
    s.capturesReceived = capturesReceived_;
    s.arVersion = arVersion_;
    s.arBusy = arBusy_;
    s.arSummary = arSummary_;
    s.arError = arError_;
    s.secondsSinceArViewed = arViewedEver_ ? seconds(arViewed_) : -1.0;
    s.faceConnected = faceConnected_;
    s.faceTransport = faceTransport_;
    s.faceFrames = faceSerial_;
    s.secondsSinceFace = faceSerial_ ? seconds(lastFace_) : -1.0;
    size_t recent = 0;
    for (const auto& t : faceTimes_)
        if (now - t <= std::chrono::seconds(1)) ++recent;
    s.faceFps = double(recent);
    s.recording = take_.isOpen();
    s.takeFrames = take_.frames();
    s.takeSeconds = take_.durationMs() / 1000.0;
    s.lastError = lastError_;
    return s;
}

// --- Requests ---

HttpResponse PhoneLink::handle(const HttpRequest& request) {
    HttpResponse r = route(request);
    return r;
}

bool PhoneLink::authorised(const HttpRequest& request) {
    std::string token = request.param("k");
    if (token.empty()) token = request.header("x-sf-token");
    std::lock_guard lock(mutex_);
    if (!paired_ && Clock::now() - tokenIssued_ > kUnusedTokenLifetime) {
        newToken(); // nobody used it in time: the QR code shows a fresh one
        return false;
    }
    if (!sameSecret(token, token_)) return false;
    paired_ = true;
    return true;
}

void PhoneLink::noteDevice(const HttpRequest& request) {
    const std::string device = describeDevice(request.header("user-agent"));
    std::lock_guard lock(mutex_);
    const bool fresh = !seen_ || device != device_ || request.peer != deviceAddress_ || Clock::now() - lastSeen_ > std::chrono::seconds(30);
    device_ = device;
    deviceAddress_ = request.peer;
    lastSeen_ = Clock::now();
    seen_ = true;
    if (fresh) pushEvent(PhoneEvent::make(PhoneEvent::Type::DeviceConnected, device));
}

HttpResponse PhoneLink::route(const HttpRequest& request) {
    const std::string& path = request.path;
    const std::string& method = request.method;
    const bool get = method == "GET" || method == "HEAD";

    if (get && (path == "/" || path == "/index.html")) {
        HttpResponse r = HttpResponse::html(std::string(reinterpret_cast<const char*>(phone_assets::capturePage),
                                                        size_t(phone_assets::capturePage_size)));
        r.headers.emplace_back("Content-Security-Policy",
                               "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; "
                               "img-src 'self' data: blob:; media-src 'self' blob:; connect-src 'self'; frame-ancestors 'none'");
        return r;
    }
    if (get && path == "/face") // the face page from this computer (works where its address counts as secure: localhost)
        return HttpResponse::html(std::string(reinterpret_cast<const char*>(phone_assets::facePage), size_t(phone_assets::facePage_size)));
    if (get && (path == "/favicon.ico" || path == "/apple-touch-icon.png" || path == "/apple-touch-icon-precomposed.png")) {
        HttpResponse r = HttpResponse::text(404, "");
        return r;
    }
    if (method == "POST" && path == "/api/pair") return pair(request);

    const bool api = path.rfind("/api/", 0) == 0 || path.rfind("/ar/", 0) == 0 || path == "/rtc-answer";
    if (!api) return HttpResponse::text(404, "Not found\n");
    if (!authorised(request))
        return jsonError(403, "This link has expired or is not for this session. Scan the QR code on your computer again.",
                         "token");
    noteDevice(request);

    if (get && path == "/api/status") return HttpResponse::json(200, statusJson());
    if (method == "POST" && path == "/api/capture") return startCapture(request);
    std::string id, rest;
    if (splitId(path, "/api/capture/", id, rest)) {
        if (method == "POST" && rest == "/file") return addFile(id, request);
        if (method == "POST" && rest == "/finish") return finishCapture(id);
        if (method == "POST" && rest == "/cancel") return cancelCapture(id);
        return jsonError(404, "Unknown capture request");
    }
    if (splitId(path, "/api/file/", id, rest) && rest.empty()) {
        if (method == "PUT" || method == "POST") return putChunk(id, request);
        if (get) return fileStatus(id);
        return jsonError(405, "Use PUT to send data");
    }
    if (method == "POST" && path == "/api/ar") return requestAr(request);
    if (get && path == "/ar/model.glb") return serveArModel(false);
    if (get && path == "/ar/model.usdz") return serveArModel(true);
    if (method == "POST" && path == "/api/face") {
        // One message, or several (one per line) from a phone that batches.
        size_t pos = 0, accepted = 0;
        std::string error;
        while (pos < request.body.size()) {
            size_t end = request.body.find('\n', pos);
            if (end == std::string::npos) end = request.body.size();
            const std::string line = request.body.substr(pos, end - pos);
            pos = end + 1;
            if (line.find_first_not_of(" \r\t") == std::string::npos) continue;
            if (receiveFaceMessage(line, "Wi-Fi (HTTP)", error)) ++accepted;
            else return jsonError(400, error);
        }
        Json j = Json::object();
        j.set("accepted", double(accepted));
        return jsonOk(j);
    }
    if (get && path == "/api/face-link") return faceLink(request);
    if (get && path == "/rtc-answer") return directAnswer(request);
    return jsonError(404, "Unknown request");
}

HttpResponse PhoneLink::pair(const HttpRequest& request) {
    Json body;
    std::string err;
    std::string code;
    if (Json::parse(request.body, body, err)) code = body["code"].asString();
    code.erase(std::remove_if(code.begin(), code.end(), [](char c) { return !std::isdigit(static_cast<unsigned char>(c)); }),
               code.end());
    std::lock_guard lock(mutex_);
    const auto now = Clock::now();
    if (now - lastPairAttempt_ < std::chrono::milliseconds(800)) return jsonError(429, "Too fast - wait a second and try again.");
    lastPairAttempt_ = now;
    if (!paired_ && now - tokenIssued_ > kUnusedTokenLifetime) newToken();
    if (!sameSecret(code, code_)) {
        if (++failedPairs_ >= kMaxPairFailures) {
            code_ = newCode(); // too many guesses: the code on the computer changes
            failedPairs_ = 0;
            pushEvent(PhoneEvent::make(PhoneEvent::Type::Error, "Several wrong pairing codes were typed on a phone; the code changed."));
            return jsonError(403, "Too many wrong codes. Use the new code shown on your computer.", "code");
        }
        return jsonError(403, "That's not the code shown on your computer.", "code");
    }
    paired_ = true;
    Json j = Json::object();
    j.set("token", token_);
    return jsonOk(j);
}

std::string PhoneLink::statusJson() {
    Json j = Json::object();
    std::lock_guard lock(mutex_);
    j.set("desktop", options_.desktopName.empty() ? std::string("SculptForge") : options_.desktopName);
    j.set("version", options_.version);
    j.set("device", device_);
    Json limits = Json::object();
    limits.set("chunk", double(kUploadChunkBytes));
    limits.set("file", double(kMaxCaptureFileBytes));
    limits.set("capture", double(kMaxCaptureBytes));
    limits.set("files", double(kMaxCaptureFiles));
    j.set("limits", limits);
    Json ar = Json::object();
    ar.set("version", double(arVersion_));
    ar.set("busy", arBusy_);
    ar.set("summary", arSummary_);
    ar.set("error", arError_);
    j.set("ar", ar);
    Json face = Json::object();
    face.set("webrtc", webrtcAvailable());
    face.set("connected", faceConnected_);
    face.set("recording", take_.isOpen());
    j.set("face", face);
    j.set("captures", double(capturesReceived_));
    return j.dump();
}

HttpResponse PhoneLink::startCapture(const HttpRequest& request) {
    Json body;
    std::string err;
    if (!Json::parse(request.body, body, err) || !body.isObject()) return jsonError(400, "Expected JSON: {\"kind\": ...}");
    CaptureKind kind;
    if (!parseCaptureKind(body["kind"].asString(), kind)) return jsonError(400, "Unknown capture kind");
    auto capture = std::make_shared<Capture>();
    capture->id = newId();
    capture->kind = kind;
    capture->started = std::chrono::system_clock::now();
    std::lock_guard lock(mutex_);
    capture->device = device_;
    const auto open = std::count_if(captures_.begin(), captures_.end(), [](const auto& c) { return !c.second->finished; });
    if (open >= kMaxOpenCaptures) return jsonError(429, "Too many uploads at once; finish or cancel one first.");
    capture->dir = pathString(u8path(options_.inboxRoot) / ".incoming" / capture->id);
    std::error_code ec;
    fs::create_directories(u8path(capture->dir), ec);
    if (ec) {
        pushEvent(PhoneEvent::make(PhoneEvent::Type::Error, "Cannot write to the Captures folder: " + ec.message()));
        return jsonError(500, "Your computer can't save the capture (" + ec.message() + ").");
    }
    captures_[capture->id] = capture;
    Json j = Json::object();
    j.set("id", capture->id);
    j.set("chunk", double(kUploadChunkBytes));
    return jsonOk(j);
}

HttpResponse PhoneLink::addFile(const std::string& captureId, const HttpRequest& request) {
    Json body;
    std::string err;
    if (!Json::parse(request.body, body, err) || !body.isObject()) return jsonError(400, "Expected JSON: {\"name\": ..., \"size\": ...}");
    const double sizeNumber = body["size"].asNumber(-1);
    if (!(sizeNumber >= 1) || sizeNumber > double(kMaxCaptureFileBytes))
        return jsonError(413, "That file is too big (the limit is 4 GB per file).");
    const uint64_t size = uint64_t(sizeNumber);
    std::lock_guard lock(mutex_);
    const auto it = captures_.find(captureId);
    if (it == captures_.end() || it->second->finished) return jsonError(404, "Unknown or finished capture");
    Capture& capture = *it->second;
    if (capture.files.size() >= kMaxCaptureFiles) return jsonError(413, "Too many files in one capture (500 at most).");
    if (capture.bytes + size > kMaxCaptureBytes) return jsonError(413, "This capture is too big (8 GB at most).");
    std::string name = sanitizeUploadName(body["name"].asString());
    const std::string ext = lowerExtension(name);
    if (name.empty() || !captureExtensionAllowed(capture.kind, ext))
        return jsonError(400, "SculptForge can't use this kind of file here (" + (ext.empty() ? std::string("no extension") : ext) + ").");
    // Unique within the capture ("IMG_0001.jpg", "IMG_0001-2.jpg").
    const std::string stem = name.substr(0, name.size() - ext.size());
    for (int n = 2; std::any_of(capture.files.begin(), capture.files.end(), [&](const auto& f) { return f->name == name; }); ++n)
        name = stem + "-" + std::to_string(n) + ext;
    auto upload = std::make_shared<Upload>();
    upload->id = newId();
    upload->captureId = captureId;
    upload->name = name;
    upload->kind = capture.kind;
    upload->size = size;
    upload->path = pathString(u8path(capture.dir) / (name + ".part"));
    upload->started = upload->lastChunk = Clock::now();
    {
        std::ofstream create(u8path(upload->path), std::ios::binary | std::ios::trunc);
        if (!create) return jsonError(500, "Your computer can't save the file.");
    }
    capture.files.push_back(upload);
    capture.bytes += size;
    uploads_[upload->id] = upload;
    Json j = Json::object();
    j.set("id", upload->id);
    j.set("name", name);
    j.set("received", 0.0);
    j.set("chunk", double(kUploadChunkBytes));
    return jsonOk(j);
}

HttpResponse PhoneLink::putChunk(const std::string& fileId, const HttpRequest& request) {
    std::shared_ptr<Upload> upload;
    {
        std::lock_guard lock(mutex_);
        const auto it = uploads_.find(fileId);
        if (it == uploads_.end()) return jsonError(404, "Unknown upload (the computer may have closed the link)");
        upload = it->second;
    }
    uint64_t offset = 0;
    if (!parseUnsigned(request.param("offset"), offset)) return jsonError(400, "Missing offset");
    if (request.body.size() > kMaxUploadChunkBytes) return jsonError(413, "Chunk too large");
    std::lock_guard write(upload->write);
    Json j = Json::object();
    if (upload->finished) {
        j.set("received", double(upload->received));
        return jsonOk(j);
    }
    if (offset > upload->received) { // a chunk went missing: the phone resumes from what arrived
        j.set("error", "Out of order");
        j.set("received", double(upload->received));
        return HttpResponse::json(409, j.dump());
    }
    if (offset + request.body.size() > upload->size) return jsonError(400, "More data than the file's size");
    const uint64_t skip = upload->received - offset; // a resent chunk overlapping what we have
    if (skip < request.body.size()) {
        std::ofstream out(u8path(upload->path), std::ios::binary | std::ios::app);
        out.write(request.body.data() + skip, std::streamsize(request.body.size() - skip));
        out.close();
        if (!out) {
            std::lock_guard lock(mutex_);
            pushEvent(PhoneEvent::make(PhoneEvent::Type::Error, "Saving " + upload->name + " failed: is the disk full?"));
            return jsonError(500, "Your computer couldn't save the data (disk full?).");
        }
        const uint64_t added = request.body.size() - skip;
        const auto now = Clock::now();
        const double dt = std::max(1e-3, std::chrono::duration<double>(now - upload->lastChunk).count());
        const double rate = double(added) / dt;
        {
            std::lock_guard lock(mutex_);
            upload->received += added;
            upload->bytesPerSecond = upload->bytesPerSecond > 0 ? upload->bytesPerSecond * 0.7 + rate * 0.3 : rate;
            upload->lastChunk = now;
        }
    }
    j.set("received", double(upload->received));
    return jsonOk(j);
}

HttpResponse PhoneLink::fileStatus(const std::string& fileId) {
    std::lock_guard lock(mutex_);
    const auto it = uploads_.find(fileId);
    if (it == uploads_.end()) return jsonError(404, "Unknown upload");
    Json j = Json::object();
    j.set("received", double(it->second->received));
    j.set("size", double(it->second->size));
    return jsonOk(j);
}

HttpResponse PhoneLink::finishCapture(const std::string& captureId) {
    std::shared_ptr<Capture> capture;
    {
        std::lock_guard lock(mutex_);
        const auto it = captures_.find(captureId);
        if (it == captures_.end()) return jsonError(404, "Unknown capture");
        capture = it->second;
        if (capture->finished) {
            Json j = Json::object();
            j.set("folder", capture->folder);
            return jsonOk(j);
        }
        if (capture->files.empty()) return jsonError(400, "Nothing was uploaded");
        Json missing = Json::array();
        for (const auto& f : capture->files)
            if (f->received != f->size) missing.push(f->name);
        if (missing.size()) {
            Json j = Json::object();
            j.set("error", "Some files are incomplete");
            j.set("missing", missing);
            return HttpResponse::json(409, j.dump());
        }
        capture->finished = true; // (no more chunks are accepted from here on)
        for (const auto& f : capture->files) f->finished = true;
    }
    // Move the files into their final folder.
    std::error_code ec;
    const fs::path root = u8path(options_.inboxRoot);
    const std::string base = captureTimestamp(capture->started) + "-" + captureKindName(capture->kind);
    fs::path folder = root / base;
    for (int n = 2; fs::exists(folder, ec); ++n) folder = root / (base + "-" + std::to_string(n));
    fs::create_directories(folder, ec);
    std::vector<std::string> paths;
    Json files = Json::array();
    bool ok = !ec;
    for (const auto& f : capture->files) {
        const fs::path to = folder / u8path(f->name);
        if (ok) fs::rename(u8path(f->path), to, ec);
        if (ec) ok = false;
        paths.push_back(pathString(to));
        Json entry = Json::object();
        entry.set("name", f->name);
        entry.set("size", double(f->size));
        files.push(entry);
    }
    if (!ok) {
        std::lock_guard lock(mutex_);
        capture->finished = false;
        for (const auto& f : capture->files) f->finished = false;
        pushEvent(PhoneEvent::make(PhoneEvent::Type::Error, "Couldn't move the capture into " + pathString(folder) + ": " + ec.message()));
        return jsonError(500, "Your computer couldn't store the capture (" + ec.message() + ").");
    }
    fs::remove_all(u8path(capture->dir), ec);
    Json info = Json::object();
    info.set("kind", captureKindName(capture->kind));
    info.set("device", capture->device);
    info.set("files", files);
    info.set("started", isoTime(capture->started));
    info.set("finished", isoTime(std::chrono::system_clock::now()));
    info.set("source", "SculptForge phone link");
    info.set("app", "SculptForge " + options_.version);
    {
        std::ofstream out(folder / "capture.json", std::ios::binary);
        out << info.dump(true) << '\n';
    }
    const std::string folderText = pathString(folder);
    {
        std::lock_guard lock(mutex_);
        capture->folder = folderText;
        ++capturesReceived_;
        PhoneEvent e = PhoneEvent::make(PhoneEvent::Type::CaptureReady);
        e.kind = capture->kind;
        e.folder = folderText;
        e.files = paths;
        e.device = capture->device;
        pushEvent(std::move(e));
    }
    Json j = Json::object();
    j.set("folder", folderText);
    j.set("name", pathString(folder.filename()));
    return jsonOk(j);
}

HttpResponse PhoneLink::cancelCapture(const std::string& captureId) {
    std::lock_guard lock(mutex_);
    const auto it = captures_.find(captureId);
    if (it == captures_.end()) return jsonError(404, "Unknown capture");
    if (!it->second->finished) {
        std::error_code ec;
        fs::remove_all(u8path(it->second->dir), ec);
        for (const auto& f : it->second->files) uploads_.erase(f->id);
    }
    captures_.erase(it);
    return jsonOk(Json::object());
}

// --- AR ---

HttpResponse PhoneLink::requestAr(const HttpRequest& request) {
    Json body;
    std::string err;
    Json::parse(request.body.empty() ? std::string("{}") : request.body, body, err);
    PhoneEvent e = PhoneEvent::make(PhoneEvent::Type::ArRequested);
    e.wholeScene = body["scope"].asString() == "scene";
    e.sizeCm = std::clamp(float(body["size"].asNumber(20.0)), 2.0f, 200.0f);
    std::lock_guard lock(mutex_);
    arViewed_ = Clock::now();
    arViewedEver_ = true;
    pushEvent(e);
    Json j = Json::object();
    j.set("version", double(arVersion_));
    return jsonOk(j);
}

HttpResponse PhoneLink::serveArModel(bool usdz) {
    std::lock_guard lock(mutex_);
    arViewed_ = Clock::now();
    arViewedEver_ = true;
    const std::string& data = usdz ? arUsdz_ : arGlb_;
    if (data.empty()) return jsonError(503, arBusy_ ? "The model is still being prepared" : "No model yet");
    HttpResponse r = HttpResponse::bytes(usdz ? "model/vnd.usdz+zip" : "model/gltf-binary", data);
    r.headers.emplace_back("Content-Disposition", usdz ? "inline; filename=\"SculptForge.usdz\"" : "inline; filename=\"SculptForge.glb\"");
    r.headers.emplace_back("Access-Control-Allow-Origin", "*"); // Scene Viewer and model viewers fetch it themselves
    return r;
}

void PhoneLink::setArBusy(bool busy) {
    std::lock_guard lock(mutex_);
    arBusy_ = busy;
}

void PhoneLink::publishArModel(std::string glb, std::string usdz, std::string summary) {
    {
        std::lock_guard lock(mutex_);
        if (glb.size() > kMaxArModelBytes || usdz.size() > kMaxArModelBytes) {
            arError_ = "The model is too big for a phone";
            arBusy_ = false;
        } else {
            arGlb_ = std::move(glb);
            arUsdz_ = std::move(usdz);
            arSummary_ = std::move(summary);
            arError_.clear();
            arBusy_ = false;
            ++arVersion_;
        }
    }
    sendArModelToPage();
}

void PhoneLink::setArError(std::string error) {
    {
        std::lock_guard lock(mutex_);
        arError_ = std::move(error);
        arBusy_ = false;
    }
    sendArModelToPage();
}

// The AR page's protocol, on its reliable channel: {"type":"model","version","bytes","summary"}, the GLB in
// binary chunks, {"type":"end","version"}; or {"type":"error","text"}.
void PhoneLink::sendArModelToPage() {
    std::lock_guard rtcLock(rtcMutex_);
    if (!rtc_ || rtc_->mode() != FaceRtc::Mode::Ar || rtc_->state() != FaceRtc::State::Connected) return;
    std::string glb, summary, error;
    uint64_t version;
    {
        std::lock_guard lock(mutex_);
        version = arVersion_;
        error = arError_;
        if (error.empty() && version != arSentVersion_) {
            glb = arGlb_;
            summary = arSummary_;
        }
    }
    Json msg = Json::object();
    if (!error.empty()) {
        msg.set("type", "error");
        msg.set("text", error);
        rtc_->send(msg.dump());
        return;
    }
    if (glb.empty()) return;
    msg.set("type", "model");
    msg.set("version", double(version));
    msg.set("bytes", double(glb.size()));
    msg.set("summary", summary);
    if (!rtc_->send(msg.dump()) && rtc_->state() != FaceRtc::State::Connected) return;
    constexpr size_t kChunk = 64 * 1024; // well under every browser's message limit (256 KB)
    for (size_t pos = 0; pos < glb.size(); pos += kChunk)
        if (!rtc_->sendBinary(glb.substr(pos, kChunk))) return;
    Json end = Json::object();
    end.set("type", "end");
    end.set("version", double(version));
    rtc_->send(end.dump());
    arSentVersion_ = version;
}

bool PhoneLink::receiveArMessage(const std::string& text, std::string& error) {
    Json msg;
    if (!Json::parse(text, msg, error)) return false;
    const std::string type = msg["type"].asString();
    if (type == "ar-ping") {
        std::lock_guard lock(mutex_);
        arViewed_ = Clock::now();
        arViewedEver_ = true;
        return true;
    }
    if (type != "ar") {
        error = "Unknown AR message";
        return false;
    }
    PhoneEvent e = PhoneEvent::make(PhoneEvent::Type::ArRequested);
    e.wholeScene = msg["scope"].asString() == "scene";
    e.sizeCm = std::clamp(float(msg["size"].asNumber(20.0)), 2.0f, 200.0f);
    std::lock_guard lock(mutex_);
    arViewed_ = Clock::now();
    arViewedEver_ = true;
    pushEvent(e);
    return true;
}

double PhoneLink::secondsSinceArViewed() const {
    std::lock_guard lock(mutex_);
    return arViewedEver_ ? std::chrono::duration<double>(Clock::now() - arViewed_).count() : -1.0;
}

// --- Face ---

bool PhoneLink::receiveFaceMessage(const std::string& text, const std::string& transport, std::string& error) {
    if (text.find("\"hello\"") != std::string::npos || text.find("\"bye\"") != std::string::npos) {
        Json msg;
        std::string err;
        if (Json::parse(text, msg, err) && (msg["type"].asString() == "hello" || msg["type"].asString() == "bye")) {
            std::lock_guard lock(mutex_);
            if (msg["type"].asString() == "bye") {
                if (faceConnected_) pushEvent(PhoneEvent::make(PhoneEvent::Type::FaceDisconnected, "The phone stopped the face feed"));
                faceConnected_ = false;
            }
            return true;
        }
    }
    FaceFrame frame;
    if (!parseFaceMessage(text, frame, error)) return false;
    const auto now = Clock::now();
    std::lock_guard lock(mutex_);
    face_ = frame;
    ++faceSerial_;
    lastFace_ = now;
    faceTimes_.push_back(now);
    while (!faceTimes_.empty() && now - faceTimes_.front() > std::chrono::seconds(1)) faceTimes_.pop_front();
    if (!faceConnected_ || faceTransport_ != transport) {
        faceConnected_ = true;
        faceTransport_ = transport;
        pushEvent(PhoneEvent::make(PhoneEvent::Type::FaceConnected, transport));
    }
    if (take_.isOpen()) take_.write(frame);
    return true;
}

bool PhoneLink::latestFace(FaceFrame& frame, uint64_t& serial) const {
    std::lock_guard lock(mutex_);
    if (faceSerial_ == 0 || faceSerial_ == serial) return false;
    frame = face_;
    serial = faceSerial_;
    return true;
}

bool PhoneLink::startTake(std::string& folder, std::string& error) {
    std::lock_guard lock(mutex_);
    if (take_.isOpen()) {
        folder = takeFolder_;
        return true;
    }
    takeStarted_ = std::chrono::system_clock::now();
    const fs::path root = u8path(options_.inboxRoot);
    const std::string base = captureTimestamp(takeStarted_) + "-face";
    std::error_code ec;
    fs::path dir = root / base;
    for (int n = 2; fs::exists(dir, ec); ++n) dir = root / (base + "-" + std::to_string(n));
    fs::create_directories(dir, ec);
    if (ec) {
        error = "Cannot create " + pathString(dir) + ": " + ec.message();
        return false;
    }
    if (!take_.open(pathString(dir / "take.jsonl"), error)) return false;
    takeFolder_ = folder = pathString(dir);
    return true;
}

std::string PhoneLink::stopTake(std::string* summary) {
    std::lock_guard lock(mutex_);
    if (!take_.isOpen()) return {};
    const int frames = take_.frames();
    const double seconds = take_.durationMs() / 1000.0;
    take_.close();
    Json info = Json::object();
    info.set("kind", "face");
    info.set("device", device_);
    Json files = Json::array();
    Json entry = Json::object();
    entry.set("name", "take.jsonl");
    entry.set("frames", double(frames));
    files.push(entry);
    info.set("files", files);
    info.set("frames", double(frames));
    info.set("seconds", seconds);
    info.set("format", "JSON Lines: one face message per line, {\"type\":\"face\",\"t\":ms,\"weights\":{ARKit name: 0..1},\"head\":[qx,qy,qz,qw]}");
    info.set("started", isoTime(takeStarted_));
    info.set("finished", isoTime(std::chrono::system_clock::now()));
    info.set("source", "SculptForge phone link");
    info.set("app", "SculptForge " + options_.version);
    {
        std::ofstream out(u8path(takeFolder_) / "capture.json", std::ios::binary);
        out << info.dump(true) << '\n';
    }
    if (summary) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%d frames, %.1f s", frames, seconds);
        *summary = buf;
    }
    std::string folder = takeFolder_;
    takeFolder_.clear();
    return folder;
}

void PhoneLink::onFaceState(int state) {
    const auto s = FaceRtc::State(state);
    std::lock_guard lock(mutex_);
    if (s == FaceRtc::State::Connected) {
        if (!faceConnected_ || faceTransport_ != "WebRTC") pushEvent(PhoneEvent::make(PhoneEvent::Type::FaceConnected, "WebRTC"));
        faceConnected_ = true;
        faceTransport_ = "WebRTC";
    } else if ((s == FaceRtc::State::Closed || s == FaceRtc::State::Failed) && faceTransport_ == "WebRTC") {
        if (faceConnected_)
            pushEvent(PhoneEvent::make(PhoneEvent::Type::FaceDisconnected, s == FaceRtc::State::Failed ? "The face link failed (network)" : "The phone closed the face link"));
        faceConnected_ = false;
    }
}

bool PhoneLink::createFaceLink(std::string& faceUrl, std::string& error, bool ar, const std::string& arScope, float arSizeCm) {
    if (!webrtcAvailable()) {
        error = ar ? "This SculptForge was built without WebRTC, so AR on Android is not available (use Download)."
                   : "This SculptForge was built without WebRTC, so the live face feed is not available.";
        return false;
    }
    std::lock_guard linkLock(faceLinkMutex_); // one new face link at a time
    stopRelay();                               // (outside rtcMutex_: the relay thread takes it to answer)
    std::lock_guard rtcLock(rtcMutex_);
    if (rtc_) rtc_->close();
    rtc_ = std::make_unique<FaceRtc>(ar ? FaceRtc::Mode::Ar : FaceRtc::Mode::Face, uint16_t(port()));
    arSentVersion_ = 0;
    if (ar) {
        rtc_->onMessage = [this](const std::string& text) {
            std::string err;
            receiveArMessage(text, err);
        };
    } else {
        rtc_->onMessage = [this](const std::string& text) {
            std::string err;
            receiveFaceMessage(text, "WebRTC", err);
        };
        rtc_->onState = [this](FaceRtc::State s) { onFaceState(int(s)); };
    }
    std::string sdp;
    if (!rtc_->createOffer(sdp, error)) {
        rtc_.reset();
        return false;
    }
    const std::string topic = "sculptforge-" + hex(randomBytes(12));
    const std::string key = randomBytes(32);
    std::string desktop, token;
    {
        std::lock_guard lock(mutex_);
        desktop = "http://" + address_ + ":" + std::to_string(port());
        token = token_;
        relayTopic_ = topic;
        relayKey_ = key;
    }
    const std::vector<std::string> relays = relayUrls();
    // Everything after '#' stays on the phone: the website never sees the offer, the token or the key.
    std::string page = options_.siteUrl;
    if (ar) page = page.substr(0, page.rfind('/') + 1) + "ar.html";
    faceUrl = page + "#v=1&k=" + token + "&d=" + percentEncode(desktop) + "&o=" + base64UrlEncode(sdp) +
              "&n=" + percentEncode(options_.desktopName) + "&x=" + base64UrlEncode(key);
    if (ar) {
        char size[32];
        std::snprintf(size, sizeof size, "%g", double(arSizeCm));
        faceUrl += std::string("&m=ar&s=") + (arScope == "scene" ? "scene" : "subtool") + "&z=" + size;
    }
    for (const std::string& relay : relays) faceUrl += "&r=" + percentEncode(relay + topic); // the page tries them in order
    if (!relays.empty()) {
        const int64_t since = int64_t(std::chrono::duration_cast<std::chrono::seconds>(
                                          std::chrono::system_clock::now().time_since_epoch()).count()) - 5;
        relayStop_ = false;
        relayThread_ = std::thread([this, topic, key, since] { relayLoop(topic, key, since); });
    }
    return true;
}

HttpResponse PhoneLink::faceLink(const HttpRequest& request) {
    std::string url, error;
    const bool ar = request.param("mode") == "ar";
    float size = 20.0f;
    try { size = std::clamp(std::stof(request.param("size", "20")), 2.0f, 200.0f); } catch (...) {}
    if (!createFaceLink(url, error, ar, request.param("scope", "subtool"), size)) return jsonError(webrtcAvailable() ? 500 : 501, error);
    Json j = Json::object();
    j.set("url", url);
    return jsonOk(j);
}

bool PhoneLink::acceptFaceAnswer(const std::string& sdp, std::string& error) {
    std::lock_guard rtcLock(rtcMutex_);
    if (!rtc_) {
        error = "No face link is waiting (open Face on the phone again)";
        return false;
    }
    if (sdp.find("v=0") == std::string::npos || sdp.size() > 16 * 1024) {
        error = "Not a WebRTC answer";
        return false;
    }
    return rtc_->setAnswer(sdp, error);
}

bool PhoneLink::acceptRelayMessage(const std::string& packed, std::string& error) {
    std::string key, token;
    {
        std::lock_guard lock(mutex_);
        key = relayKey_;
        token = token_;
    }
    std::string plain;
    if (!aesGcmDecrypt(key, packed, plain, error)) return false;
    Json msg;
    if (!Json::parse(plain, msg, error)) return false;
    if (!sameSecret(msg["k"].asString(), token)) {
        error = "The answer is for another session";
        return false;
    }
    return acceptFaceAnswer(msg["sdp"].asString(), error);
}

HttpResponse PhoneLink::directAnswer(const HttpRequest& request) {
    std::string sdp, error;
    const bool ok = base64UrlDecode(request.param("a"), sdp) && acceptFaceAnswer(sdp, error);
    std::string page =
        "<!doctype html><meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\">"
        "<title>SculptForge</title><body style=\"background:#14161a;color:#e8e8ea;font:17px -apple-system,system-ui,sans-serif;"
        "display:flex;align-items:center;justify-content:center;height:90vh;text-align:center\"><div>";
    page += ok ? "<h2>Connected</h2><p>Go back to the previous tab: your face drives SculptForge now.</p>"
               : "<h2>Not connected</h2><p>" + error + "</p>";
    page += "</div><script>setTimeout(function(){window.close()},700)</script></body>";
    return HttpResponse::html(page);
}

std::vector<std::string> PhoneLink::relayUrls() const {
    std::vector<std::string> urls;
    if (options_.relayUrl == "off") return urls;
    size_t pos = 0;
    while (pos < options_.relayUrl.size()) {
        size_t end = options_.relayUrl.find_first_of(" \t,", pos);
        if (end == std::string::npos) end = options_.relayUrl.size();
        if (end > pos) urls.push_back(options_.relayUrl.substr(pos, end - pos));
        pos = end + 1;
    }
    return urls;
}

void PhoneLink::relayLoop(std::string topic, std::string key, int64_t sinceUnix) {
    const auto started = Clock::now();
    const std::vector<std::string> relays = relayUrls();
    std::string seen;
    while (!relayStop_ && Clock::now() - started < std::chrono::minutes(5)) {
        // Every relay in turn: the page posts to the first one that takes it (an unreachable one fails fast).
        for (const std::string& relay : relays) {
            const ai::HttpResponse r =
                ai::httpRequest("GET", relay + topic + "/json?poll=1&since=" + std::to_string(sinceUnix), {}, {}, 5);
            if (relayStop_) return;
            if (r.status != 200) continue;
            size_t pos = 0;
            while (pos < r.body.size()) {
                size_t end = r.body.find('\n', pos);
                if (end == std::string::npos) end = r.body.size();
                Json event;
                std::string err;
                if (Json::parse(r.body.substr(pos, end - pos), event, err) && event["event"].asString() == "message") {
                    const std::string id = "|" + relay + " " + event["id"].asString() + "|";
                    if (seen.find(id) == std::string::npos) {
                        seen += id;
                        if (acceptRelayMessage(event["message"].asString(), err)) return;
                    }
                }
                pos = end + 1;
            }
        }
        for (int i = 0; i < 15 && !relayStop_; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void PhoneLink::stopRelay() {
    relayStop_ = true;
    if (relayThread_.joinable()) relayThread_.join();
    relayStop_ = false;
}

} // namespace sf::link
