// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Android's one network request: the update check's latest.json (the same file the desktop app fetches). The NDK has
// no libcurl here, so the request goes through the activity's Java (SculptForgeActivity.updateFeedGet: Android's own
// TLS and certificate store). Only https GETs of the website's feed and its GitHub copy are made, without redirects.
#pragma once

#include "ai/Http.h"

#include <string>

namespace sf::android {

// True for the two feed addresses (https://sculptforge.app/..., https://islandmed.github.io/...; no port, query or login).
bool isUpdateFeedUrl(const std::string& url);

// Splits the activity's "<status>\n<error>\n<body>" answer into a response (a malformed answer is an error).
ai::HttpResponse parseFeedAnswer(const std::string& answer);

#ifdef __ANDROID__
// Blocking GET of an update feed address; call from a worker thread. Refused (error set) for any other address.
ai::HttpResponse updateFeedGet(const std::string& url, int timeoutSeconds);
#endif

} // namespace sf::android
