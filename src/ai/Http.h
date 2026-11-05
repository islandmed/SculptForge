// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Minimal blocking HTTPS client. libcurl on Linux/macOS, WinHTTP on Windows,
// so no extra dependency needs to be shipped. Call from a worker thread.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace sf::ai {

struct HttpResponse {
    long status = 0;   // 0 if the request never reached the server
    std::string body;
    std::string error; // transport error description
};

// `method` is "GET" or "POST"; `headers` are "Name: value" strings. Redirects
// are followed (result files are usually served from a CDN).
HttpResponse httpRequest(const std::string& method, const std::string& url, const std::vector<std::string>& headers,
                         const std::string& body = {}, int timeoutSeconds = 120);

// Like httpRequest, but hands the body to `onData` as it arrives (server-sent
// events, progress). Return false from `onData` to stop reading early.
HttpResponse httpRequestStream(const std::string& method, const std::string& url, const std::vector<std::string>& headers,
                               const std::string& body, int timeoutSeconds,
                               const std::function<bool(const char* data, size_t size)>& onData);

// A GET for files that must come from known places (the updater, core/SelfUpdate.h). Redirects are followed one hop
// at a time and every URL, the first included, must pass `allowed`; https only, certificates checked. The body reaches
// `onData` only from the final 200 answer. `onProgress(received, total)` (total 0 while unknown) returns false to
// cancel. `stallSeconds`: give up when nothing arrives for that long (no limit on the whole download).
HttpResponse httpGetRestricted(const std::string& url, const std::function<bool(const std::string& url)>& allowed,
                               int stallSeconds, const std::function<bool(const char* data, size_t size)>& onData,
                               const std::function<bool(uint64_t received, uint64_t total)>& onProgress = {});
// The same with headers chosen per hop: `headersFor(url)` (credentials only to the host they belong to, never to
// the place a redirect points at).
HttpResponse httpGetRestricted(const std::string& url, const std::function<bool(const std::string& url)>& allowed,
                               const std::function<std::vector<std::string>(const std::string& url)>& headersFor,
                               int stallSeconds, const std::function<bool(const char* data, size_t size)>& onData,
                               const std::function<bool(uint64_t received, uint64_t total)>& onProgress = {});
// A POST to exactly `url` (https, certificates checked, no redirects followed): sign-in endpoints.
HttpResponse httpPostNoRedirect(const std::string& url, const std::vector<std::string>& headers, const std::string& body,
                                int timeoutSeconds);
// httpGetRestricted's redirect rule on its own (tests): `once` makes one request, a 3xx leaving its target in `location`.
HttpResponse followRedirects(const std::string& url, const std::function<bool(const std::string& url)>& allowed,
                             const std::function<HttpResponse(const std::string& url, std::string& location)>& once);

// One part of a multipart/form-data body: a plain field, or a file when `fileName` is set.
struct MultipartField {
    std::string name;
    std::string value;
    std::string fileName;
    std::string contentType = "application/octet-stream";
};
// Builds the body; `contentTypeHeader` receives the matching "Content-Type: multipart/form-data; boundary=..." header.
std::string multipartBody(const std::vector<MultipartField>& fields, std::string& contentTypeHeader);

// Every request this process started (tests: the AI switch sends nothing while it is off).
uint64_t httpRequestCount();
void resetHttpRequestCount(); // (the warm UI test server, between scripts: app/UiServe.h)
// False in a build without libcurl (non-Windows): every request fails at once. Tests that talk to a local server skip.
bool httpSupported();

inline HttpResponse httpPostJson(const std::string& url, const std::vector<std::string>& headers, const std::string& body,
                                 int timeoutSeconds = 120) {
    return httpRequest("POST", url, headers, body, timeoutSeconds);
}

} // namespace sf::ai
