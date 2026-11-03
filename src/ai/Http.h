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

inline HttpResponse httpPostJson(const std::string& url, const std::vector<std::string>& headers, const std::string& body,
                                 int timeoutSeconds = 120) {
    return httpRequest("POST", url, headers, body, timeoutSeconds);
}

} // namespace sf::ai
