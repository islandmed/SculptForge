// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Hyper3D Rodin, direct: Generate 3D's "hyper3d" service (image or text to 3D on Hyper3D's own API, billed to the
// user's Hyper3D account). Written in house from Hyper3D's public API documentation (docs.hyper3d.ai); the idea came
// from a contributor (ElgoogUdiab, PR #123).
//
// Kept narrow on purpose:
//  - Only with the user's own key, from the OS keychain (ai/Secrets.h "ai/hyper3d") or typed in this session. No
//    environment variable, no shared or trial key: no key, no request.
//  - One host for the API, pinned here (kApiHost); every API call is a POST to exactly that URL, https, certificates
//    checked, no redirects followed. The key goes only there, never to the file host.
//  - The model file: https only, from Hyper3D's own domains (downloadUrlAllowed, every redirect hop too), at most
//    kMaxDownloadBytes, checked to be a well-formed GLB (checkGlb) before the existing glTF importer reads it. It is
//    written into a private temporary folder (GeneratorResult::privateDir) that the caller deletes after import.
//  - Polling backs off from 5 s to 30 s (pollDelaySeconds); 20 minutes at most.
//  - The studio policy decides (provider id "hyper3d", the endpoint allow-list, sign-in); one audit log line per
//    generation (tier, input kind, prompt length; never the key or the image).
#pragma once

#include "ai/BrushAssistant.h"
#include "ai/Generator3D.h"
#include "ai/Http.h"

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace sf::ai {
struct GenerationProgress;
}

namespace sf::ai::hyper3d {

inline constexpr const char* kApiHost = "api.hyper3d.com";
inline constexpr const char* kApiBase = "https://api.hyper3d.com/api/v2";
inline constexpr size_t kMaxDownloadBytes = size_t(200) << 20;
inline constexpr const char* kDefaultTier = "Gen-2.5-Low"; // (Basic API access covers Extreme-Low and Low)

struct Tier {
    const char* id;
    const char* label;
};
// The Gen-2.5 tiers the API offers.
const std::vector<Tier>& tiers();
// A known tier id; anything else runs kDefaultTier.
std::string resolvedTier(const std::string& tier);

// Pieces exposed for tests.
// True for exactly https://api.hyper3d.com/api/v2/<path> (default port, no user info).
bool apiUrl(const std::string& url);
// True for an https URL (default port, no user info) on hyper3d.com, hyper3d.ai or deemos.com or a subdomain.
bool downloadUrlAllowed(const std::string& url);
// The multipart submit body: tier, GLB, raw mesh, no material; the PNG as `images` (image to 3D) or `prompt` (text).
std::string submitBody(const std::string& tier, const std::string& png, const std::string& prompt,
                       std::string& contentTypeHeader);
// Reads a /rodin answer: the task uuid (for /download) and the subscription key (for /status).
bool parseSubmit(const std::string& body, std::string& taskUuid, std::string& subscriptionKey, std::string& error);
enum class JobState { Waiting, Generating, Done, Failed };
// Reads a /status answer: Done only when every job is, Failed when any is. `queue` gets queue_length (-1 if none).
bool parseStatus(const std::string& body, JobState& state, int& queue, std::string& error);
// Reads a /download answer: the URL of the first .glb file.
bool parseDownload(const std::string& body, std::string& url, std::string& error);
// The wait before status check `attempt` (0 = the first): 5 s, growing to 30 s.
int pollDelaySeconds(int attempt);
// A well-formed binary glTF 2.0: header, length, a JSON chunk holding an object with "asset", an optional BIN chunk.
bool checkGlb(const std::string& bytes, std::string& error);
// A folder only this user can read, under the system temp folder ("" and ERROR when it can't be made).
std::string makePrivateDir(std::string& error);

// Blocking (a worker thread). Exactly one of `imagePath` / `prompt` is used. The checks of generate3D (the AI switch,
// the studio policy) come first there; this repeats the gate before it sends anything.
GeneratorResult run(const AiSettings& settings, const std::string& imagePath, const std::string& prompt,
                    GenerationProgress* progress);

// Tests: answer the requests run() makes ("POST" API calls, the "GET" of the file) instead of the network, after the
// same URL checks; waits are recorded in `waits` instead of slept. {} = the network again.
using Transport = std::function<HttpResponse(const std::string& method, const std::string& url,
                                             const std::vector<std::string>& headers, const std::string& body)>;
void setTransportForTests(Transport transport, std::vector<int>* waits = nullptr);
// Tests: a smaller size cap (0 = kMaxDownloadBytes).
void setMaxDownloadBytesForTests(size_t bytes);

} // namespace sf::ai::hyper3d
