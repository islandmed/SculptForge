// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Managed configuration: a policy file a studio's IT deploys (docs/guide/studios.md "IT deployment"). It locks what an
// artist may change; the app shows "Managed by your studio" on each locked setting. It is data only: nothing in it runs.
//
// Where it comes from, first found wins:
//   1. the system-wide file, writable only by administrators:
//        Linux   /etc/sculptforge/policy.json
//        macOS   /Library/Application Support/SculptForge/policy.json
//        Windows %ProgramData%\SculptForge\policy.json   (C:\ProgramData\SculptForge\policy.json)
//   2. SF_POLICY=FILE, only when there is no system-wide file (IT testing a policy, a launcher, a Rez package):
//      a user can't loosen a deployed policy by pointing SF_POLICY elsewhere.
// No file: nothing is managed and everything works as before.
//
// {
//   "format": "sculptforge-policy", "version": 1,
//   "organization": "Acme Animation",
//   "ai": {"enabled": true, "providers": ["litellm", "comfyui"], "endpoints": ["https://llm.acme.internal/"],
//          "requireSignIn": true},
//   "network": {"enabled": false, "allow": ["https://sw.acme.internal/"], "localNetwork": false},
//   "mcp": {"enabled": true, "live": "never", "categories": ["info", "edit", "files"]},
//   "audit": {"path": "//fs01/audit/sculptforge/{user}@{host}.jsonl", "maxMB": 20, "keep": 10, "required": true},
//   "signIn": {"issuer": "https://acme.okta.com/oauth2/default", "clientId": "0oa1...", "scopes": ["openid", "email",
//              "offline_access"], "audience": "", "useFor": ["https://llm.acme.internal/"]},
//   "telemetry": "none",
//   "updates": {"check": true, "channel": "stable", "feed": "https://sw.acme.internal/sculptforge/latest.json"}
// }
// Every key is optional: what the policy leaves out stays the artist's choice. Unknown keys are errors, so a typo never
// quietly drops a lock. A broken policy fails closed: AI, network features, MCP and live mode off (policy().broken).
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace sf::policy {

constexpr const char* kPolicyFormat = "sculptforge-policy";
constexpr int kPolicyVersion = 1;
// Shown next to every setting the policy locks (translated with tr()).
constexpr const char* kManagedLabel = "Managed by your studio";

// A setting the policy may lock: Unset = the artist decides.
enum class Lock { Unset, Off, On };

// What an MCP tool does, for mcp.categories. A tool needs every category it is in.
//   info    reads the scene (mesh_info, part_list, ...)
//   edit    changes the scene in memory (sculpt, remesh, masks, undo, ...)
//   files   reads or writes files (open, export_*, save, anything with a path), and the bridges to other apps
//   scripts runs macros or scripts
//   network reaches beyond this computer (brush gallery, Unreal link)
//   ai      the AI features (brush_ai, image_to_3d, text_to_3d, comfyui_check)
const std::vector<std::string>& mcpCategoryNames();

struct Policy {
    bool present = false;          // a policy file was found
    bool broken = false;           // found but unusable: fails closed (everything below off)
    std::string source;            // the file it came from, for messages
    std::string error;             // why it is broken
    std::string organization;      // shown in "Managed by ..." tooltips and Help > About

    Lock ai = Lock::Unset;                  // ai.enabled
    std::vector<std::string> aiProviders;   // allowed provider ids (aiProviderIds()); empty = any
    std::vector<std::string> aiEndpoints;   // allowed URL prefixes for AI requests; empty = any
    bool requireSignIn = false;             // AI only while signed in (signIn below)

    Lock network = Lock::Unset;             // network.enabled: Off = studio mode (only `allow`)
    std::vector<std::string> networkAllow;  // more URL prefixes reachable while network is off
    bool localNetwork = false;

    Lock mcp = Lock::Unset;                 // mcp.enabled: Off = no MCP server (sculptforge-cli --mcp refuses)
    bool mcpLiveAllowed = true;             // mcp.live: "never" | "allowed"
    bool mayaLinkAllowed = true;            // maya.link: "never" | "allowed" (the Maya Live link, link/MayaLink.h)
    std::vector<std::string> mcpCategories; // allowed categories; empty = all

    std::string auditPath;                  // with {user}, {host}, {date} filled in; "" = the default
    double auditMaxMB = 10;
    int auditKeep = 5;
    bool auditRequired = false;             // refuse an AI/MCP action whose audit record can't be written

    std::string issuer, clientId, audience;
    std::string clientSecret;               // only where the provider requires one for installed apps (Google)
    std::vector<std::string> scopes;        // default openid profile email offline_access
    std::vector<std::string> signInUseFor;  // URL prefixes that get the bearer token; empty = aiEndpoints

    Lock updateCheck = Lock::Unset;         // updates.check
    std::string updateChannel;              // "stable" | "preview" | "" (artist's choice)
    std::string updateFeed, updateMirror;   // the studio's own feed (a URL) and mirror (a URL or a file)

    bool signInConfigured() const { return !issuer.empty() && !clientId.empty(); }
};

// Every provider id ai.providers accepts: the Brush Lab presets (ai/Providers.h) and the Generate 3D services.
const std::vector<std::string>& aiProviderIds();

// Reads and checks a policy (SOURCE names it in messages). False: ERROR says what is wrong.
bool parsePolicy(const std::string& text, const std::string& source, Policy& out, std::string& error);

// Reads the policy at PATH (ORIGIN names it in messages). A file that can't be read or parsed fails closed (broken).
// MESSAGES gets warnings (a file other users can write).
Policy loadPolicyFile(const std::string& path, const std::string& origin, std::vector<std::string>* messages = nullptr);

// The system-wide path for this OS (see above).
std::string systemPolicyPath();

// Finds and loads the policy (system file, else SF_POLICY), once per process; later calls return it. A file that
// can't be read or parsed fails closed. MESSAGES gets what to log.
const Policy& startPolicy(std::vector<std::string>* messages = nullptr);
// The process's policy (an empty one before startPolicy).
const Policy& current();
// Tests: replace the process's policy ("" text: none). Not thread-safe: only before workers start.
void setPolicyForTests(const Policy& p);
void resetPolicyForTests();

// The checks the rest of the app makes. WHY gets a sentence an artist can read.
bool aiProviderAllowed(std::string_view providerId, std::string* why = nullptr);
bool aiEndpointAllowed(const std::string& url, std::string* why = nullptr);
bool mcpAllowed(std::string* why = nullptr);
bool mcpLiveAllowed(std::string* why = nullptr);
bool mayaLinkAllowed(std::string* why = nullptr);
// The categories of command NAME (needs the registry's parameter names: a "path", "folder" or "file" parameter is files).
std::vector<std::string> mcpCategories(std::string_view name, const std::vector<std::string>& paramNames);
bool mcpToolAllowed(std::string_view name, const std::vector<std::string>& paramNames, std::string* why = nullptr);

// "Managed by your studio" or "Managed by Acme Animation", for tooltips.
std::string managedBy();

// A readable summary of what the policy locks (sculptforge-cli --policy, Help > About, the log).
std::string describe(const Policy& p);

// {user}, {host} and {date} (YYYY-MM-DD, UTC) in a path.
std::string expandPathTemplate(const std::string& path);

} // namespace sf::policy
