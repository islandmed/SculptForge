// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Stability AI, direct: Generate 3D's "stability" service, image to 3D with Stable Point Aware 3D (SPAR3D) on
// Stability's own API, billed to the user's Stability account (4 credits, about $0.04, nothing for a failed call).
// A "quick look": a few seconds, good for blocking a form out to sculpt over. Written in house from Stability's public
// API reference (docs.stability.ai/api-reference). Image only: SPAR3D has no text input.
//
// Kept as narrow as the other direct providers (ai/Hyper3D.h, ai/Tripo.h):
//  - Only with the user's own key, from the OS keychain (ai/Secrets.h "ai/stability") or typed in this session. No
//    environment variable, no shared or trial key: no key, no request.
//  - One POST to exactly kApiUrl (https, certificates checked, no redirects followed); the key goes only there.
//  - The answer is the GLB itself (synchronous): capped at 200 MB and checked to be a well-formed GLB before the
//    glTF importer reads it (ai/GlbFetch.h); written to a private temporary folder.
//  - A timeout on the call, a clear message for every status Stability documents, and the failures that another
//    provider may not share (unreachable, busy, server error) marked transient.
//  - The studio policy decides (provider id "stability", the endpoint allow-list, sign-in); one audit log line per
//    generation (never the key or the image). Licence: free below US $1M annual revenue ("Powered by Stability AI").
#pragma once

#include "ai/BrushAssistant.h"
#include "ai/Generator3D.h"
#include "ai/GlbFetch.h"

#include <string>

namespace sf::ai {
struct GenerationProgress;
}

namespace sf::ai::stability {

inline constexpr const char* kApiHost = "api.stability.ai";
inline constexpr const char* kApiUrl = "https://api.stability.ai/v2beta/3d/stable-point-aware-3d";
inline constexpr const char* kKeyUrl = "https://platform.stability.ai/account/keys";

// Pieces exposed for tests.
bool apiUrl(const std::string& url); // exactly kApiUrl
// The multipart body: the PNG as `image`, 1024 px textures, the default framing.
std::string submitBody(const std::string& png, std::string& contentTypeHeader);
// The message for an HTTP status + the JSON error body {"id","name","errors":[...]}. `transient` is set for what
// another provider may not share (unreachable, rate limited, 5xx).
std::string errorMessage(long status, const std::string& transportError, const std::string& body, bool& transient);

// Blocking (a worker thread): image to 3D only. The checks of generate3D (the AI switch, the studio policy) come
// first there; this repeats the gate before it sends anything.
GeneratorResult run(const AiSettings& settings, const std::string& imagePath, GenerationProgress* progress);

// Tests: answers the POST instead of the network, after the same URL check. {} = the network again.
void setTransportForTests(glbfile::Transport transport);

} // namespace sf::ai::stability
