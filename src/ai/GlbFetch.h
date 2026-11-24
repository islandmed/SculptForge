// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the paid direct providers (Tripo) share after the model is made: fetching the file from the
// provider's own hosts only (every redirect hop too, https, no key sent, capped), checking it is a well-formed GLB
// (ai/Hyper3D.h checkGlb) and writing it into a private temporary folder the caller deletes after import
// (GeneratorResult::privateDir). The same rules as Hyper3D's, in one place for the providers added after it.
#pragma once

#include "ai/Generator3D.h"
#include "ai/Http.h"

#include <functional>
#include <string>
#include <vector>

namespace sf::ai {
struct GenerationProgress;
}

namespace sf::ai::glbfile {

inline constexpr size_t kMaxBytes = size_t(200) << 20;

// A fake network for tests: answers the requests a provider makes in place of libcurl / WinHTTP.
using Transport = std::function<HttpResponse(const std::string& method, const std::string& url,
                                             const std::vector<std::string>& headers, const std::string& body)>;

// True for an https URL (default port, no user info) on DOMAIN or a subdomain of it.
bool hostUnder(const std::string& host, const char* domain);
// True for an https URL (default port, no user info) whose host is under one of DOMAINS.
bool urlOnDomains(const std::string& url, const std::vector<const char*>& domains);

// GETs URL (no key sent) into BYTES. `allowed` decides each hop. On failure ERROR says why; REFUSED_HOST is the host
// a refused hop pointed at ("" otherwise) so the caller can say the paid result is still in the user's account.
// `transport` set (tests): it answers the one GET.
bool fetch(const std::string& url, const std::function<bool(const std::string&)>& allowed, GenerationProgress* progress,
           const Transport& transport, std::string& bytes, std::string& error, std::string& refusedHost);

// Checks BYTES is a GLB and writes it as model.glb in a new private folder: ok with outputPath + privateDir set.
GeneratorResult save(const std::string& bytes);

} // namespace sf::ai::glbfile
