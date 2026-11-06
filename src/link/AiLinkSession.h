// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Live mode's rules, one connection at a time (link/AiLink.h is the channel). Pure logic, no window or socket, so
// the tests drive every path:
//   1. Pairing. The client says hello; the app shows an Approve dialog with a one-time 6-digit code. The user
//      clicks Approve, or reads the code to the AI tool, which sends it back (3 tries, then the connection closes).
//      Approval lasts for this connection only. Before it, every request is refused.
//   2. Requests are JSON-RPC (MCP tools/list, tools/call). They are data: a tool name is looked up, its arguments
//      become typed values; nothing is evaluated. A token bucket limits the rate; a flood closes the connection.
//   3. The policy (AiLinkPolicy) sorts every call: allowed, asked first in the app (files outside the project
//      folder and what the user picked, overwrites, saving over the project, deleting library files, scripts and
//      macros, anything that reaches the network), or refused (packs, the AI switch).
#pragma once

#include "core/Commands.h"
#include "core/Json.h"

#include <string>
#include <vector>

namespace sf::link {

enum class AiVerdict { Allow, Confirm, Deny };

struct AiDecision {
    AiVerdict verdict = AiVerdict::Allow;
    std::string reason; // Confirm: what the dialog says ("Overwrites C:/x.glb"); Deny: the error. English, also for the tool
    // Confirm: the reason's English template with one %s for `subject` ("Overwrites %s"), so the app shows it through
    // tr() (shownReason); the templates are listed in tools/i18n/coverage.py (DYNAMIC).
    const char* format = nullptr;
    std::string subject;
};
// A Confirm decision whose reason is `format` with `subject` in place of its %s.
AiDecision confirmBecause(const char* format, const std::string& subject = {});

struct AiLinkPolicy {
    std::string projectDir;          // the saved project's folder ("" when none)
    std::vector<std::string> picked; // files and folders the user chose in a file dialog this session

    void addPicked(const std::string& path);
    // `cmd` null: a live-window tool (viewport_image, view_set...), always allowed.
    AiDecision classify(const Command* cmd, const std::string& name, const Args& args) const;
    // Never offered to an AI tool: installing / switching packs and the AI switch stay the user's.
    static bool hidden(const std::string& name);
    // True when `path` is inside the project folder, is a picked file, or is inside a picked folder.
    bool allowedPath(const std::string& path) const;
};

class RateLimiter {
public:
    RateLimiter(double perSecond, double burst) : rate_(perSecond), burst_(burst), tokens_(burst) {}
    bool take(double now);

private:
    double rate_, burst_, tokens_, last_ = -1.0;
};

struct AiLinkCall {
    Json id;
    std::string name;
    Args args;
};

class AiLinkSession {
public:
    enum class State { Hello, Pairing, Paired, Closed };

    struct Step {
        enum class Kind {
            None,         // nothing to do
            Reply,        // send `reply`
            Close,        // send `reply` (if any), then close the connection
            ShowApproval, // show the Approve dialog (code(), client())
            ListTools,    // answer tools/list for `call.id` (listReply)
            Execute,      // run `call`, then send resultReply / contentReply
            Confirm,      // ask the user (`reason`), then Execute or send declinedReply
        } kind = Kind::None;
        std::string reply;
        AiLinkCall call;
        std::string reason;
    };

    explicit AiLinkSession(std::string code);

    Step handle(const std::string& line, double now);
    std::string approve(); // the user clicked Approve: the line to send
    std::string deny();    // the user clicked Deny (or the dialog timed out): the line to send, then close

    State state() const { return state_; }
    const std::string& code() const { return code_; }
    const std::string& client() const { return client_; }
    int attemptsLeft() const { return attemptsLeft_; }

    AiLinkPolicy policy;

    static std::string listReply(const Json& id, Json tools);
    static std::string resultReply(const Json& id, bool ok, const std::string& text);
    static std::string contentReply(const Json& id, Json content, bool isError);
    static std::string errorReply(const Json& id, int code, const std::string& message);
    static std::string declinedReply(const Json& id, const std::string& name);

    static constexpr int kCodeTries = 3;
    static constexpr int kMaxRejected = 50; // refused or malformed messages before the connection closes

private:
    Step reject(Step s);

    State state_ = State::Hello;
    std::string code_, client_;
    int attemptsLeft_ = kCodeTries;
    int rejected_ = 0;
    RateLimiter limiter_{10.0, 30.0};
};

// A fresh 6-digit code from the system's random source.
std::string makePairingCode();
// Compares without stopping at the first difference.
bool sameCode(const std::string& a, const std::string& b);
// What the client calls itself, shown in the dialog as data: printable characters only, at most 40.
std::string sanitizeClientName(const std::string& name);
// JSON arguments as command arguments (objects and arrays as their JSON text). False: not an object, or too many.
bool jsonToArgs(const Json& arguments, Args& out);
// "brush=Clay, radius=0.1": a call's arguments for the confirm dialog, each value cut to `maxValue` characters.
std::string describeArgs(const Args& args, size_t maxValue = 80);

} // namespace sf::link
