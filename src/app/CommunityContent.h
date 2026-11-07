// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The content lane (docs/design/release-lanes.md §1): credits, the Founding Members and testimonials change on the
// website without an app release. pages.yml publishes website/content/v1.json (tools/content/build_content.py) and
// its Ed25519 signature v1.json.sig, made with the content key (GitHub environment `content`), never the release key.
// The app fetches both with the daily update check (PanelSupport.cpp), keeps the last good copy in the preferences
// folder and falls back to the compiled-in files. Text only: every name and quote goes through the same sanitiser
// as the compiled-in lists, and nothing in it is ever a path, a URL, markup or code.
#pragma once

#include "app/Support.h"
#include "core/Ed25519.h"
#include "core/Json.h"
#include "core/SelfUpdate.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sf {

inline constexpr const char* kContentUrl = "https://sculptforge.app/content/v1.json";
// The website's GitHub copy, for networks that block sculptforge.app (like kUpdateFeedMirrorUrl).
inline constexpr const char* kContentMirrorUrl = "https://islandmed.github.io/content/v1.json";
inline constexpr size_t kContentMaxBytes = 64 * 1024; // refused while reading past this
inline constexpr int kContentSchema = 1;              // a newer schema is ignored (the cache stays)
// The public half of the content key (Ed25519, hex). Empty: no key yet, so the app never fetches and uses the
// compiled-in copy. Never the release key (kUpdatePublicKeyHex): the content key signs on every push to main.
inline constexpr const char* kContentPublicKeyHex = "";

struct CommunityContent {
    int64_t serial = 0; // rises with every publish (the commit time); an older one is never taken over a newer one
    bool hasSupporters = false, hasFounding = false, hasTestimonials = false;
    std::vector<std::string> supporters;
    FoundingMembers founding;
    std::vector<Testimonial> testimonials;
};

// Reads content/v1.json. False (with `error`) for anything that isn't one: not JSON, another schema, no serial.
// Sections that are missing keep has* false (the compiled-in copy is used for them).
inline bool parseCommunityContent(const std::string& json, CommunityContent& out, std::string& error) {
    out = {};
    Json j;
    if (!Json::parse(json, j, error) || !j.isObject()) {
        if (error.empty()) error = "not a JSON object";
        return false;
    }
    if (j["schema"].asNumber(0.0) != double(kContentSchema)) {
        error = "schema " + std::to_string(int(j["schema"].asNumber(0.0))) + " (this version reads 1)";
        return false;
    }
    const double serial = j["serial"].asNumber(-1.0);
    if (!(serial >= 0.0 && serial < 1e15)) {
        error = "no serial";
        return false;
    }
    out.serial = int64_t(serial);
    if (j["supporters"].isArray()) {
        out.hasSupporters = true;
        for (const Json& n : j["supporters"].items()) {
            if (out.supporters.size() >= kSupportersMax) break;
            if (!n.isString()) continue;
            std::string name = sanitizeSupporterName(n.asString());
            if (!name.empty()) out.supporters.push_back(std::move(name));
        }
    }
    if (j["founding"].isObject()) {
        out.hasFounding = true;
        out.founding = parseFoundingMembers(j["founding"].dump());
    }
    if (j["testimonials"].isArray()) { // the compiled-in file's format and rules (parseTestimonials: exact or skipped)
        out.hasTestimonials = true;
        out.testimonials = parseTestimonials(j["testimonials"].dump());
    }
    return true;
}

// The whole check: size, signature (64 raw bytes over the exact file) under `keyHex`, then the parser, then the
// serial (>= minSerial). False with `error` otherwise; the caller keeps what it had.
inline bool acceptCommunityContent(const std::string& json, const std::string& signature, const std::string& keyHex,
                                   int64_t minSerial, CommunityContent& out, std::string& error) {
    uint8_t key[32];
    if (!update::parseHexKey(keyHex, key) || !ed25519PublicKeyValid(key)) {
        error = "no content key";
        return false;
    }
    if (json.empty() || json.size() > kContentMaxBytes) {
        error = "size " + std::to_string(json.size());
        return false;
    }
    if (signature.size() != 64 ||
        !ed25519Verify(reinterpret_cast<const uint8_t*>(signature.data()), json.data(), json.size(), key)) {
        error = "bad signature";
        return false;
    }
    CommunityContent c;
    if (!parseCommunityContent(json, c, error)) return false;
    if (c.serial < minSerial) {
        error = "older than the copy we have (serial " + std::to_string(c.serial) + ")";
        return false;
    }
    out = std::move(c);
    return true;
}

} // namespace sf
