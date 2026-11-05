// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/Http.h"

#include <atomic>
#include <chrono>
#include <random>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#elif defined(SF_HAVE_CURL)
#include <curl/curl.h>
#include <mutex>
#endif

namespace sf::ai {

namespace {
std::atomic<uint64_t> gRequests{0};
}

uint64_t httpRequestCount() { return gRequests; }
void resetHttpRequestCount() { gRequests = 0; }

bool httpSupported() {
#if defined(_WIN32) || defined(SF_HAVE_CURL)
    return true;
#else
    return false;
#endif
}

HttpResponse httpRequest(const std::string& method, const std::string& url, const std::vector<std::string>& headers,
                         const std::string& body, int timeoutSeconds) {
    std::string received;
    HttpResponse r = httpRequestStream(method, url, headers, body, timeoutSeconds, [&](const char* d, size_t n) {
        received.append(d, n);
        return true;
    });
    r.body = std::move(received);
    return r;
}

std::string multipartBody(const std::vector<MultipartField>& fields, std::string& contentTypeHeader) {
    std::random_device rd;
    std::mt19937_64 rng((uint64_t(rd()) << 32) ^ rd() ^ uint64_t(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::string boundary = "----sculptforge";
    for (int i = 0; i < 16; ++i) boundary += "0123456789abcdef"[rng() % 16];
    contentTypeHeader = "Content-Type: multipart/form-data; boundary=" + boundary;
    std::string body;
    for (const auto& f : fields) {
        body += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"" + f.name + "\"";
        if (!f.fileName.empty()) body += "; filename=\"" + f.fileName + "\"\r\nContent-Type: " + f.contentType;
        body += "\r\n\r\n" + f.value + "\r\n";
    }
    return body + "--" + boundary + "--\r\n";
}


namespace {

// One request of httpGetRestricted, no redirects followed: a 3xx answer leaves its target in `location`.
HttpResponse getOnce(const std::string& method, const std::string& url, const std::vector<std::string>& headers,
                     const std::string& body, int stallSeconds, const std::function<bool(const char*, size_t)>& onData,
                     const std::function<bool(uint64_t, uint64_t)>& onProgress, std::string& location);

} // namespace

HttpResponse followRedirects(const std::string& url, const std::function<bool(const std::string&)>& allowed,
                             const std::function<HttpResponse(const std::string&, std::string&)>& once) {
    std::string current = url;
    for (int hop = 0; hop < 6; ++hop) {
        if (current.rfind("https://", 0) != 0 || !allowed(current)) {
            HttpResponse r;
            r.error = "refused to download from " + current.substr(0, 200);
            return r;
        }
        std::string location;
        HttpResponse r = once(current, location);
        if (r.status < 300 || r.status >= 400 || !r.error.empty()) return r;
        if (location.empty()) {
            r.error = "the server answered " + std::to_string(r.status) + " without a new address";
            return r;
        }
        current = location;
    }
    HttpResponse r;
    r.error = "too many redirects";
    return r;
}

HttpResponse httpGetRestricted(const std::string& url, const std::function<bool(const std::string&)>& allowed,
                               int stallSeconds, const std::function<bool(const char*, size_t)>& onData,
                               const std::function<bool(uint64_t, uint64_t)>& onProgress) {
    return httpGetRestricted(url, allowed, {}, stallSeconds, onData, onProgress);
}

HttpResponse httpGetRestricted(const std::string& url, const std::function<bool(const std::string&)>& allowed,
                               const std::function<std::vector<std::string>(const std::string&)>& headersFor,
                               int stallSeconds, const std::function<bool(const char*, size_t)>& onData,
                               const std::function<bool(uint64_t, uint64_t)>& onProgress) {
    return followRedirects(url, allowed, [&](const std::string& u, std::string& location) {
        return getOnce("GET", u, headersFor ? headersFor(u) : std::vector<std::string>{}, {}, stallSeconds, onData,
                       onProgress, location);
    });
}

HttpResponse httpPostNoRedirect(const std::string& url, const std::vector<std::string>& headers, const std::string& body,
                                int timeoutSeconds) {
    std::string received, location;
    HttpResponse r;
    if (url.rfind("https://", 0) != 0) {
        r.error = "refused to send to " + url.substr(0, 200);
        return r;
    }
    r = getOnce("POST", url, headers, body, timeoutSeconds, [&](const char* d, size_t n) {
        if (received.size() + n > (1u << 20)) return false; // (a sign-in answer is a few hundred bytes)
        received.append(d, n);
        return true;
    }, {}, location);
    if (r.status >= 300 && r.status < 400 && r.error.empty()) r.error = "unexpected redirect";
    r.body = std::move(received);
    return r;
}

#if defined(_WIN32)

namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

std::string lastError(const char* what) { return std::string(what) + " failed (error " + std::to_string(GetLastError()) + ")"; }

struct Handle {
    HINTERNET h = nullptr;
    ~Handle() {
        if (h) WinHttpCloseHandle(h);
    }
};

} // namespace

HttpResponse httpRequestStream(const std::string& method, const std::string& url, const std::vector<std::string>& headers,
                               const std::string& body, int timeoutSeconds,
                               const std::function<bool(const char*, size_t)>& onData) {
    ++gRequests;
    HttpResponse r;
    std::wstring wurl = widen(url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof uc;
    wchar_t host[256] = {}, path[8192] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 8192;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        r.error = "Invalid URL: " + url;
        return r;
    }
    // WinHttpCrackUrl splits the query off the path; put it back.
    std::wstring fullPath = path;
    if (uc.lpszExtraInfo && uc.dwExtraInfoLength) fullPath.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);

    Handle session{WinHttpOpen(L"SculptForge/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.h) {
        r.error = lastError("WinHttpOpen");
        return r;
    }
    const int ms = timeoutSeconds * 1000;
    WinHttpSetTimeouts(session.h, ms, ms, ms, ms);
    Handle connection{WinHttpConnect(session.h, host, uc.nPort, 0)};
    if (!connection.h) {
        r.error = lastError("WinHttpConnect");
        return r;
    }
    const DWORD flags = uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    Handle request{WinHttpOpenRequest(connection.h, widen(method).c_str(), fullPath.c_str(), nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, flags)};
    if (!request.h) {
        r.error = lastError("WinHttpOpenRequest");
        return r;
    }
    std::wstring allHeaders;
    for (const auto& h : headers) allHeaders += widen(h) + L"\r\n";
    if (!WinHttpSendRequest(request.h, allHeaders.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : allHeaders.c_str(), DWORD(-1L),
                            const_cast<char*>(body.data()), DWORD(body.size()), DWORD(body.size()), 0) ||
        !WinHttpReceiveResponse(request.h, nullptr)) {
        r.error = lastError("HTTP request");
        return r;
    }
    DWORD status = 0, size = sizeof status;
    WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &size, WINHTTP_NO_HEADER_INDEX);
    r.status = long(status);
    while (true) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.h, &available) || available == 0) break;
        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request.h, chunk.data(), available, &read) || read == 0) break;
        if (!onData(chunk.data(), read)) break;
    }
    return r;
}

namespace {

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

HttpResponse getOnce(const std::string& method, const std::string& url, const std::vector<std::string>& headers,
                     const std::string& body, int stallSeconds, const std::function<bool(const char*, size_t)>& onData,
                     const std::function<bool(uint64_t, uint64_t)>& onProgress, std::string& location) {
    ++gRequests;
    HttpResponse r;
    std::wstring wurl = widen(url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof uc;
    wchar_t host[256] = {}, path[8192] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 8192;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS) {
        r.error = "Invalid URL: " + url;
        return r;
    }
    std::wstring fullPath = path;
    if (uc.lpszExtraInfo && uc.dwExtraInfoLength) fullPath.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    Handle session{WinHttpOpen(L"SculptForge/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.h) {
        r.error = lastError("WinHttpOpen");
        return r;
    }
    const int ms = stallSeconds * 1000;
    WinHttpSetTimeouts(session.h, ms, ms, ms, ms); // (each wait, not the whole download)
    Handle connection{WinHttpConnect(session.h, host, uc.nPort, 0)};
    if (!connection.h) {
        r.error = lastError("WinHttpConnect");
        return r;
    }
    Handle request{WinHttpOpenRequest(connection.h, widen(method).c_str(), fullPath.c_str(), nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
    if (!request.h) {
        r.error = lastError("WinHttpOpenRequest");
        return r;
    }
    DWORD never = WINHTTP_OPTION_REDIRECT_POLICY_NEVER; // each hop is checked by the caller
    WinHttpSetOption(request.h, WINHTTP_OPTION_REDIRECT_POLICY, &never, sizeof never);
    // (Certificate checks stay on: no WINHTTP_OPTION_SECURITY_FLAGS that would ignore errors.)
    std::wstring allHeaders;
    for (const auto& h : headers) allHeaders += widen(h) + L"\r\n";
    if (!WinHttpSendRequest(request.h, allHeaders.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : allHeaders.c_str(), DWORD(-1L),
                            body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()), DWORD(body.size()),
                            DWORD(body.size()), 0) ||
        !WinHttpReceiveResponse(request.h, nullptr)) {
        r.error = lastError("HTTP request");
        return r;
    }
    DWORD status = 0, size = sizeof status;
    WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &size, WINHTTP_NO_HEADER_INDEX);
    r.status = long(status);
    if (status >= 300 && status < 400) {
        wchar_t loc[4096] = {};
        DWORD locSize = sizeof loc;
        if (WinHttpQueryHeaders(request.h, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, loc, &locSize,
                                WINHTTP_NO_HEADER_INDEX))
            location = narrow(loc);
        return r;
    }
    if (status != 200 && method == "GET") return r; // (a POST's error answer is read: it says why)
    uint64_t total = 0, received = 0;
    wchar_t len[32] = {};
    DWORD lenSize = sizeof len;
    if (WinHttpQueryHeaders(request.h, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, len, &lenSize,
                            WINHTTP_NO_HEADER_INDEX))
        total = _wcstoui64(len, nullptr, 10);
    if (onProgress && !onProgress(0, total)) {
        r.error = "cancelled";
        return r;
    }
    std::string chunk;
    while (true) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.h, &available)) {
            r.error = lastError("download");
            break;
        }
        if (available == 0) break;
        chunk.resize(available);
        DWORD read = 0;
        if (!WinHttpReadData(request.h, chunk.data(), available, &read)) {
            r.error = lastError("download");
            break;
        }
        if (read == 0) break;
        received += read;
        if (!onData(chunk.data(), read) || (onProgress && !onProgress(received, total))) {
            r.error = "cancelled";
            break;
        }
    }
    return r;
}

} // namespace

#elif defined(SF_HAVE_CURL)

namespace {

struct StreamSink {
    const std::function<bool(const char*, size_t)>* onData;
    bool stopped = false;
};

size_t writeCallback(char* data, size_t size, size_t count, void* user) {
    auto* sink = static_cast<StreamSink*>(user);
    if (!(*sink->onData)(data, size * count)) {
        sink->stopped = true;
        return 0; // abort the transfer
    }
    return size * count;
}

} // namespace

HttpResponse httpRequestStream(const std::string& method, const std::string& url, const std::vector<std::string>& headers,
                               const std::string& body, int timeoutSeconds,
                               const std::function<bool(const char*, size_t)>& onData) {
    ++gRequests;
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });

    HttpResponse r;
    CURL* curl = curl_easy_init();
    if (!curl) {
        r.error = "curl_easy_init failed";
        return r;
    }
    StreamSink sink{&onData};
    curl_slist* list = nullptr;
    for (const auto& h : headers) list = curl_slist_append(list, h.c_str());
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, long(body.size()));
    } else {
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    }
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, long(timeoutSeconds));
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "SculptForge/0.1");
    CURLcode rc = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &r.status);
    if (rc != CURLE_OK && !sink.stopped) r.error = curl_easy_strerror(rc);
    curl_slist_free_all(list);
    curl_easy_cleanup(curl);
    return r;
}

namespace {

struct RestrictedSink {
    CURL* curl;
    const std::function<bool(const char*, size_t)>* onData;
    const std::function<bool(uint64_t, uint64_t)>* onProgress;
    bool anyAnswer = false; // a POST's error answer is read too (it says why)
    bool stopped = false;
};

bool finalAnswer(CURL* curl) {
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    return status == 200;
}

size_t restrictedWrite(char* data, size_t size, size_t count, void* user) {
    auto* sink = static_cast<RestrictedSink*>(user);
    if (!sink->anyAnswer && !finalAnswer(sink->curl)) return size * count; // a redirect's or an error's body
    if (!(*sink->onData)(data, size * count)) {
        sink->stopped = true;
        return 0;
    }
    return size * count;
}

int restrictedProgress(void* user, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t) {
    auto* sink = static_cast<RestrictedSink*>(user);
    if (!*sink->onProgress || !finalAnswer(sink->curl)) return 0;
    if ((*sink->onProgress)(uint64_t(now), uint64_t(total))) return 0;
    sink->stopped = true;
    return 1;
}

HttpResponse getOnce(const std::string& method, const std::string& url, const std::vector<std::string>& headers,
                     const std::string& body, int stallSeconds, const std::function<bool(const char*, size_t)>& onData,
                     const std::function<bool(uint64_t, uint64_t)>& onProgress, std::string& location) {
    ++gRequests;
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
    HttpResponse r;
    CURL* curl = curl_easy_init();
    if (!curl) {
        r.error = "curl_easy_init failed";
        return r;
    }
    RestrictedSink sink{curl, &onData, &onProgress, method == "POST"};
    curl_slist* list = nullptr;
    for (const auto& h : headers) list = curl_slist_append(list, h.c_str());
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, long(body.size()));
    } else {
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    }
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
#else
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, long(CURLPROTO_HTTPS));
#endif
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L); // each hop is checked by the caller
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, long(stallSeconds));
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L); // (a stall, not the whole download, times out)
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, long(stallSeconds));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, restrictedWrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, restrictedProgress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &sink);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "SculptForge/0.1");
    const CURLcode rc = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &r.status);
    if (sink.stopped) r.error = "cancelled";
    else if (rc != CURLE_OK) r.error = curl_easy_strerror(rc);
    char* target = nullptr;
    if (r.status >= 300 && r.status < 400 && curl_easy_getinfo(curl, CURLINFO_REDIRECT_URL, &target) == CURLE_OK && target)
        location = target;
    curl_slist_free_all(list);
    curl_easy_cleanup(curl);
    return r;
}

} // namespace

#else

HttpResponse httpRequestStream(const std::string&, const std::string&, const std::vector<std::string>&, const std::string&, int,
                               const std::function<bool(const char*, size_t)>&) {
    ++gRequests;
    HttpResponse r;
    r.error = "This build of SculptForge has no HTTP support (libcurl was not found at build time)";
    return r;
}

namespace {

HttpResponse getOnce(const std::string&, const std::string&, const std::vector<std::string>&, const std::string&, int,
                     const std::function<bool(const char*, size_t)>&, const std::function<bool(uint64_t, uint64_t)>&,
                     std::string&) {
    ++gRequests;
    HttpResponse r;
    r.error = "This build of SculptForge has no HTTP support (libcurl was not found at build time)";
    return r;
}

} // namespace

#endif

} // namespace sf::ai
