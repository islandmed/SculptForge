// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/GlbFetch.h"

#include "ai/Hyper3D.h"
#include "ai/ImageTo3DService.h"
#include "core/NetworkPolicy.h"

#include <filesystem>
#include <fstream>

namespace sf::ai::glbfile {

namespace {
constexpr int kStallSeconds = 60;
}

bool hostUnder(const std::string& host, const char* domain) {
    const std::string d = domain;
    return host == d || (host.size() > d.size() && host.compare(host.size() - d.size(), d.size(), d) == 0 &&
                         host[host.size() - d.size() - 1] == '.');
}

bool urlOnDomains(const std::string& url, const std::vector<const char*>& domains) {
    net::UrlParts u;
    if (!net::parseUrl(url, u) || u.scheme != "https" || u.port != 443) return false;
    for (const char* d : domains)
        if (hostUnder(u.host, d)) return true;
    return false;
}

bool fetch(const std::string& url, const std::function<bool(const std::string&)>& allowed, GenerationProgress* progress,
           const Transport& transport, std::string& bytes, std::string& error, std::string& refusedHost) {
    bytes.clear();
    refusedHost.clear();
    net::UrlParts parts;
    if (!allowed(url)) {
        refusedHost = net::parseUrl(url, parts) ? parts.host : std::string("not an https address");
        error = "refused";
        return false;
    }
    bool tooBig = false;
    HttpResponse got;
    if (transport) {
        got = transport("GET", url, {}, {});
        if (got.body.size() > kMaxBytes) tooBig = true;
        else bytes = std::move(got.body);
        got.body.clear();
    } else {
        got = httpGetRestricted(
            url, allowed, kStallSeconds,
            [&](const char* d, size_t n) {
                if (bytes.size() + n > kMaxBytes) return (tooBig = true, false);
                bytes.append(d, n);
                return true;
            },
            [&](uint64_t received, uint64_t total) {
                if (total > kMaxBytes) return (tooBig = true, false);
                if (progress && total > 0) progress->percent = int(received * 100 / total);
                return !(progress && progress->cancel);
            });
    }
    if (tooBig) return (error = "The model file is larger than 200 MB - refused", false);
    if (progress && progress->cancel) return (error = "Cancelled", false);
    if (got.error.rfind("refused to download from ", 0) == 0) { // (a redirect left the provider's servers)
        const std::string to = got.error.substr(25);
        refusedHost = net::parseUrl(to, parts) ? parts.host : std::string("not an https address");
        error = "refused";
        return false;
    }
    if (!got.error.empty() || got.status != 200)
        return (error = "Download failed: " + (got.error.empty() ? "HTTP " + std::to_string(got.status) : got.error), false);
    return true;
}

GeneratorResult save(const std::string& bytes) {
    GeneratorResult r;
    if (std::string err; !hyper3d::checkGlb(bytes, err)) {
        r.error = err;
        return r;
    }
    std::string err;
    const std::string dir = hyper3d::makePrivateDir(err);
    if (dir.empty()) {
        r.error = err;
        return r;
    }
    const std::string path = dir + "/model.glb";
    {
        std::ofstream f(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
        if (!f || !f.write(bytes.data(), std::streamsize(bytes.size()))) {
            f.close();
            std::error_code ec;
            std::filesystem::remove_all(std::filesystem::path(std::u8string(dir.begin(), dir.end())), ec);
            r.error = "Cannot write the model file";
            return r;
        }
    }
    r.ok = true;
    r.outputPath = path;
    r.privateDir = dir;
    return r;
}

} // namespace sf::ai::glbfile
