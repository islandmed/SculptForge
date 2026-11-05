// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Studio builds (core/StudioProfile.h) and studio mode's network gate (core/NetworkPolicy.h): the profile is read
// strictly, and in studio mode nothing leaves the machine except the studio's own channel.
#include "TestHarness.h"

#include "ai/AiPolicy.h"
#include "ai/Http.h"
#include "core/NetworkPolicy.h"
#include "core/StudioProfile.h"
#include "core/UpdateCheck.h"
#include "link/Socket.h"

#include <string>

using namespace sf;

namespace {

const char* kProfile = R"({
  "format": "sculptforge-studio", "version": 1,
  "id": "acme", "name": "Acme Animation",
  "updates": {"feed": "https://sw.acme.internal/sculptforge/latest.json", "mirror": "mirror/latest.json"},
  "allow": ["https://comfy.acme.internal:8188/"],
  "packs": ["packs/acme.sfpack"],
  "brushPaths": ["/studio/brushes"],
  "preferences": "ui.json",
  "branding": {"logo": "logo.png", "title": "Acme"}
})";

// Restores the default (online) policy when a test ends, whatever it checked.
struct PolicyScope {
    ~PolicyScope() {
        net::setNetworkPolicy({});
        net::resetBlockedCount();
    }
};

std::string with(const std::string& key, const std::string& value) {
    return std::string(R"({"format": "sculptforge-studio", "version": 1, "id": "acme", "name": "Acme", )") + "\"" + key +
           "\": " + value + "}";
}

} // namespace

TEST(studio_profile_reads_every_field) {
    StudioProfile p;
    std::string error;
    CHECK(parseStudioProfile(kProfile, "/opt/acme", p, error));
    CHECK(p.id == "acme" && p.name == "Acme Animation");
    CHECK(p.offline && !p.ai && !p.localNetwork); // the defaults: offline, no AI, no LAN
    CHECK(p.updateFeed == "https://sw.acme.internal/sculptforge/latest.json");
    CHECK(p.updateMirror == "/opt/acme/mirror/latest.json");
    CHECK(p.packs.size() == 1 && p.packs[0] == "/opt/acme/packs/acme.sfpack");
    CHECK(p.brushPaths.size() == 1 && p.brushPaths[0] == "/studio/brushes");
    CHECK(p.preferences == "/opt/acme/ui.json" && p.logo == "/opt/acme/logo.png" && p.title == "Acme");
    const net::NetworkPolicy n = studioNetworkPolicy(p);
    CHECK(n.offline && !n.localNetwork);
    CHECK(n.allow.size() == 2); // the feed (the mirror is a file) and the allow entry
}

TEST(studio_profile_refuses_what_it_should) {
    StudioProfile p;
    std::string error;
    CHECK(!parseStudioProfile("[]", "", p, error));
    CHECK(!parseStudioProfile(R"({"format": "other", "version": 1, "id": "a", "name": "A"})", "", p, error));
    CHECK(!parseStudioProfile(R"({"format": "sculptforge-studio", "version": 2, "id": "a", "name": "A"})", "", p, error));
    CHECK(!parseStudioProfile(R"({"format": "sculptforge-studio", "version": 1, "id": "Acme!", "name": "A"})", "", p, error));
    CHECK(!parseStudioProfile(with("ofline", "true"), "", p, error)); // a typo never quietly drops a setting
    CHECK(error.find("ofline") != std::string::npos);
    CHECK(!parseStudioProfile(with("packs", R"(["../../etc/x.sfpack"])"), "/opt/acme", p, error));
    CHECK(!parseStudioProfile(with("packs", R"(["/abs/x.sfpack"])"), "/opt/acme", p, error));
    CHECK(!parseStudioProfile(with("branding", R"({"logo": "../logo.png"})"), "/opt/acme", p, error));
    CHECK(!parseStudioProfile(with("allow", R"(["https://ok.internal@evil.example/"])"), "", p, error));
    CHECK(!parseStudioProfile(with("allow", R"(["ftp://files.internal/"])"), "", p, error));
    CHECK(!parseStudioProfile(with("updates", R"({"feed": "https://user@sw.internal/latest.json"})"), "", p, error));
    CHECK(!parseStudioProfile(R"({"format": "sculptforge-studio", "version": 1, "id": "a", "id": "b", "name": "A"})", "", p, error));
}

TEST(studio_url_prefixes_match_whole_parts) {
    CHECK(net::urlUnderPrefix("https://sw.acme.internal/sf/latest.json", "https://sw.acme.internal/sf/"));
    CHECK(net::urlUnderPrefix("https://SW.Acme.Internal/sf/latest.json", "https://sw.acme.internal/sf"));
    CHECK(net::urlUnderPrefix("https://sw.acme.internal:443/sf/x", "https://sw.acme.internal/sf/"));
    CHECK(!net::urlUnderPrefix("https://sw.acme.internal/sfx/latest.json", "https://sw.acme.internal/sf"));
    CHECK(!net::urlUnderPrefix("http://sw.acme.internal/sf/x", "https://sw.acme.internal/sf/"));      // scheme
    CHECK(!net::urlUnderPrefix("https://sw.acme.internal:8443/sf/x", "https://sw.acme.internal/sf/")); // port
    CHECK(!net::urlUnderPrefix("https://sw.acme.internal.evil.example/sf/x", "https://sw.acme.internal/sf/"));
    CHECK(!net::urlUnderPrefix("https://sw.acme.internal@evil.example/sf/x", "https://sw.acme.internal/sf/"));
    CHECK(!net::urlUnderPrefix("https://sw.acme.internal/sf/../other", "https://sw.acme.internal/sf/"));
    CHECK(!net::urlUnderPrefix("https://sw.acme.internal/sf/%2e%2e/other", "https://sw.acme.internal/sf/"));
}

TEST(studio_mode_sends_nothing_outside) {
    PolicyScope scope;
    net::NetworkPolicy n;
    n.offline = true;
    n.allow = {"https://127.0.0.1:9/channel/"}; // the studio's channel (a closed local port: nothing answers)
    net::setNetworkPolicy(n);
    net::resetBlockedCount();
    const uint64_t before = ai::httpRequestCount();
    // The public feed, the community gallery, an AI service, the phone relay: refused before anything is sent.
    for (const char* url : {"https://sculptforge.app/latest.json", "https://islandmed.github.io/latest.json",
                            "https://api.anthropic.com/v1/messages", "https://fal.run/x", "https://ntfy.sh/topic"}) {
        const ai::HttpResponse r = ai::httpRequest("GET", url, {}, {}, 2);
        CHECK(r.status == 0);
        CHECK(r.error.find("Studio mode") != std::string::npos);
    }
    CHECK(ai::httpRequestCount() == before); // no request was started
    CHECK(net::blockedCount() == 5);
    // The studio's own channel is let through (it gets as far as the transport).
    ai::httpRequest("GET", "https://127.0.0.1:9/channel/latest.json", {}, {}, 2);
    CHECK(ai::httpRequestCount() == before + 1);
}

TEST(studio_mode_opens_no_socket_beyond_loopback) {
    PolicyScope scope;
    net::NetworkPolicy n;
    n.offline = true;
    net::setNetworkPolicy(n);
    net::resetBlockedCount();
    std::string error;
    link::Socket s;
    uint16_t bound = 0;
    CHECK(!s.connect("93.184.216.34", 443, error));
    CHECK(error.find("Studio mode") != std::string::npos);
    CHECK(!s.listen("0.0.0.0", 0, bound, error)); // the phone capture server on the LAN
    CHECK(!s.openMulticast("239.0.0.1", 6766, "0.0.0.0", 0, error)); // Unreal discovery
    CHECK(net::blockedCount() == 3);
    CHECK(s.listen("127.0.0.1", 0, bound, error)); // loopback stays (bridges on this computer)
    s.close();
    // The profile can allow the studio's own network.
    n.localNetwork = true;
    net::setNetworkPolicy(n);
    CHECK(net::socketAllowed("10.0.0.5") && net::multicastAllowed());
    CHECK(!net::localIpcAllowed()); // Discord presence stays off in studio mode
}

TEST(studio_mode_locks_ai_off) {
    PolicyScope scope;
    net::NetworkPolicy n;
    n.offline = true;
    net::setNetworkPolicy(n);
    ai::setStudioAiAllowed(false);
    CHECK(!ai::aiFeaturesAllowed(true) && ai::aiPolicyLocked());
    ai::setStudioAiAllowed(true); // "ai": true in the profile
    CHECK(ai::aiFeaturesAllowed(true));
    ai::setStudioAiAllowed(false);
    net::setNetworkPolicy({});
    CHECK(ai::aiFeaturesAllowed(true) == (ai::aiEnvOverride() != ai::AiOverride::Off));
}

TEST(studio_update_feed_is_the_studio_channel) {
    StudioStart s;
    std::string fallback;
    CHECK(updateFeedSource(s, nullptr, fallback) == kUpdateFeedUrl && fallback == kUpdateFeedMirrorUrl);
    CHECK(updateFeedSource(s, "/tmp/feed.json", fallback) == "/tmp/feed.json" && fallback.empty());
    s.active = true;
    CHECK(updateFeedSource(s, "/tmp/feed.json", fallback).empty()); // no channel: no check (and no public feed)
    s.profile.updateMirror = "/mnt/sw/latest.json";
    CHECK(updateFeedSource(s, nullptr, fallback) == "/mnt/sw/latest.json");
    s.profile.updateFeed = "https://sw.acme.internal/latest.json";
    CHECK(updateFeedSource(s, nullptr, fallback) == "https://sw.acme.internal/latest.json" && fallback == "/mnt/sw/latest.json");
    CHECK(!isStudioBuild()); // public builds compile no profile in
}
