// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tripo, direct: Generate 3D's "tripo" service (image or text to 3D on Tripo's own API, v3, billed to the user's Tripo
// account). Written in house from Tripo's public developer documentation (developers.tripo3d.ai, the global site; its China-region twin on tripo3d.com is a
// different platform with its own keys and is not used). A shape only
// (texture and PBR off): the model is what gets sculpted, and it is the cheapest tier (about $0.20 an image, $0.10 a text).
//
// Kept as narrow as the other direct providers (ai/Hyper3D.h):
//  - Only with the user's own key, from the OS keychain (ai/Secrets.h "ai/tripo") or typed in this session. No
//    environment variable, no shared or trial key: no key, no request.
//  - One host for the API, pinned here (kApiHost); every call is to exactly that host under /v3/, https, certificates
//    checked, no redirects followed. The key goes only there, never to the file host.
//  - The model file: https only, from Tripo's own domains (downloadUrlAllowed, every redirect hop too), at most 200 MB,
//    checked to be a well-formed GLB before the glTF importer reads it (ai/GlbFetch.h); a private temporary folder.
//  - Every call has a timeout, the wait for the model is capped (kMaxMinutes), polling backs off, transient errors
//    are retried a few times, and a failure says what happened and whether another provider may take over.
//  - The studio policy decides (provider id "tripo", the endpoint allow-list, sign-in); one audit log line per
//    generation (model, input kind, prompt length; never the key or the image).
#pragma once

#include "ai/BrushAssistant.h"
#include "ai/Generator3D.h"
#include "ai/GlbFetch.h"

#include <string>
#include <vector>

namespace sf::ai {
struct GenerationProgress;
}

namespace sf::ai::tripo {

inline constexpr const char* kApiHost = "openapi.tripo3d.ai";
inline constexpr const char* kApiBase = "https://openapi.tripo3d.ai/v3";
inline constexpr const char* kDefaultModel = "v3.1-20260211"; // H3.1
inline constexpr const char* kKeyUrl = "https://developers.tripo3d.ai/en/keys";

struct Model {
    const char* id;
    const char* label;
};
const std::vector<Model>& models();
// A known model id; anything else runs kDefaultModel (a retired id would only be refused by Tripo).
std::string resolvedModel(const std::string& model);

// Pieces exposed for tests.
// True for exactly https://openapi.tripo3d.ai/v3/<path> (default port, no user info).
bool apiUrl(const std::string& url);
// True for an https URL (default port, no user info) on tripo3d.com or tripo3d.ai or a subdomain.
bool downloadUrlAllowed(const std::string& url);
std::string imageBody(const std::string& model, const std::string& fileToken);
std::string textBody(const std::string& model, const std::string& prompt);
std::string uploadBody(const std::string& png, std::string& contentTypeHeader);
// The answers: {"code":0,"data":{...}} on success, a flat {"code":N,"message":...} on an error.
bool parseUpload(const std::string& body, std::string& fileToken, std::string& error);
bool parseCreate(const std::string& body, std::string& taskId, std::string& error);
enum class TaskState { Queued, Running, Done, Failed };
struct TaskStatus {
    TaskState state = TaskState::Queued;
    int percent = -1;
    std::string modelUrl; // Done: output.model_url
    std::string error;    // Failed: Tripo's message
};
bool parseTask(const std::string& body, TaskStatus& out, std::string& error);
// The message for an HTTP status + body, so "no credits", "bad key", "rate limited" read plainly. `transient` is set
// for what is worth another provider (or another try): unreachable, 5xx, busy.
std::string errorMessage(long status, const std::string& transportError, const std::string& body, bool& transient);
// The wait before status check `attempt` (0 = the first): 2 s, growing to 5 s.
int pollDelaySeconds(int attempt);

// Blocking (a worker thread). Exactly one of `imagePath` / `prompt` is used. The checks of generate3D (the AI switch,
// the studio policy) come first there; this repeats the gate before it sends anything.
GeneratorResult run(const AiSettings& settings, const std::string& imagePath, const std::string& prompt,
                    GenerationProgress* progress);

// Tests: answer the requests run() makes ("POST" calls, the "GET" of the status and of the file) instead of the
// network, after the same URL checks; waits are recorded in `waits` instead of slept. {} = the network again.
void setTransportForTests(glbfile::Transport transport, std::vector<int>* waits = nullptr);
// Tests: the longest wait for a model, in seconds (-1 = already over; 0 = kMaxMinutes again).
void setTimeoutSecondsForTests(int seconds);

} // namespace sf::ai::tripo
