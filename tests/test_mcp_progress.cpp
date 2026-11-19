// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Progress and cancel for long MCP calls (cli/McpServer.h, core/OpControl.h): a request with _meta.progressToken gets
// notifications/progress (at most 10 a second), notifications/cancelled stops the request in flight at its next cancel
// point and leaves the scene as it was, the reader keeps answering ping meanwhile, and no line is ever interleaved.
#include "TestHarness.h"

#include "cli/McpServer.h"
#include "core/Commands.h"
#include "core/Json.h"
#include "core/OpControl.h"
#include "core/UndoBatch.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <istream>
#include <mutex>
#include <streambuf>
#include <string>
#include <thread>
#include <vector>

using namespace sf;
using namespace std::chrono_literals;

namespace {

// stdin of the server: lines the test feeds one by one; reading blocks until a line or close().
class PipeBuf : public std::streambuf {
public:
    void feed(const std::string& line) {
        {
            std::lock_guard lock(m_);
            data_ += line + "\n";
        }
        cv_.notify_all();
    }
    void close() {
        {
            std::lock_guard lock(m_);
            closed_ = true;
        }
        cv_.notify_all();
    }

protected:
    int_type underflow() override {
        std::unique_lock lock(m_);
        cv_.wait(lock, [&] { return !data_.empty() || closed_; });
        if (data_.empty()) return traits_type::eof();
        buf_ = data_;
        data_.clear();
        setg(buf_.data(), buf_.data(), buf_.data() + buf_.size());
        return traits_type::to_int_type(buf_[0]);
    }

private:
    std::mutex m_;
    std::condition_variable cv_;
    std::string data_, buf_;
    bool closed_ = false;
};

// stdout of the server: complete lines, with the time each arrived.
class LineSink : public std::streambuf {
public:
    struct Line {
        std::string text;
        std::chrono::steady_clock::time_point at;
    };
    std::vector<Line> lines() {
        std::lock_guard lock(m_);
        return lines_;
    }
    bool waitFor(const std::function<bool(const std::vector<Line>&)>& done, std::chrono::milliseconds limit = 20s) {
        std::unique_lock lock(m_);
        return cv_.wait_for(lock, limit, [&] { return done(lines_); });
    }

protected:
    int_type overflow(int_type c) override {
        if (c == traits_type::eof()) return c;
        std::lock_guard lock(m_);
        if (c == '\n') {
            lines_.push_back({partial_, std::chrono::steady_clock::now()});
            partial_.clear();
            cv_.notify_all();
        } else {
            partial_ += char(c);
        }
        return c;
    }
    std::streamsize xsputn(const char* s, std::streamsize n) override {
        for (std::streamsize i = 0; i < n; ++i) overflow(traits_type::to_int_type(s[i]));
        return n;
    }

private:
    std::mutex m_;
    std::condition_variable cv_;
    std::vector<Line> lines_;
    std::string partial_;
};

Json parse(const std::string& text) {
    Json j;
    std::string err;
    CHECK(Json::parse(text, j, err)); // (a garbled or interleaved line fails here)
    return j;
}

bool isProgress(const LineSink::Line& l) { return parse(l.text)["method"].asString() == "notifications/progress"; }
bool isReplyTo(const LineSink::Line& l, int id) {
    const Json j = parse(l.text);
    return j.has("id") && j["id"].isNumber() && int(j["id"].asNumber()) == id;
}
bool hasReplyTo(const std::vector<LineSink::Line>& ls, int id) {
    for (const auto& l : ls)
        if (isReplyTo(l, id)) return true;
    return false;
}
size_t progressCount(const std::vector<LineSink::Line>& ls) {
    size_t n = 0;
    for (const auto& l : ls) n += isProgress(l);
    return n;
}

std::atomic<int> g_started{0}, g_finished{0}, g_cancelled{0};

// A long operation with a cancel point every step. `steps` x `ms`; changes the scene only at the end.
CommandRegistry testRegistry() {
    CommandRegistry reg;
    reg.add({"fake_long",
             "A test operation: takes steps x ms, reports progress, can be cancelled.",
             {{"steps", ParamSpec::Type::Number, "Steps.", false}, {"ms", ParamSpec::Type::Number, "Milliseconds per step.", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 const int steps = int(argNumber(a, "steps", 50));
                 const int ms = int(argNumber(a, "ms", 10));
                 ++g_started;
                 for (int i = 0; i < steps; ++i) {
                     opProgress(float(i) / float(steps), "step " + std::to_string(i));
                     if (opCancelled()) {
                         ++g_cancelled;
                         return {false, "fake_long cancelled"};
                     }
                     std::this_thread::sleep_for(std::chrono::milliseconds(ms));
                 }
                 doc.setMesh(Mesh()); // (the scene changes only once it has finished)
                 ++g_finished;
                 return {true, "fake_long done"};
             }});
    reg.add({"fake_self_cancel", "A test operation that is cancelled while it runs.", {},
             [](Document&, const Args&) -> CommandResult {
                 if (OpControl* c = currentOpControl()) c->cancel = true;
                 return opCancelled() ? CommandResult{false, "cancelled"} : CommandResult{true, "not cancelled"};
             }});
    return reg;
}

std::string toolCall(int id, const char* tool, const std::string& arguments, const char* token = nullptr) {
    return R"({"jsonrpc":"2.0","id":)" + std::to_string(id) + R"(,"method":"tools/call","params":{)" +
           (token ? std::string(R"("_meta":{"progressToken":)") + token + "}," : std::string()) + R"("name":")" + tool +
           R"(","arguments":)" + arguments + "}}";
}
std::string cancelNote(int id) {
    return R"({"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":)" + std::to_string(id) + "}}";
}

// The server on its own thread, the way a client sees it.
struct Server {
    CommandRegistry reg = testRegistry();
    PipeBuf pipe;
    LineSink sink;
    std::istream in{&pipe};
    std::ostream out{&sink};
    std::thread thread;
    int exitCode = -1;
    Server() {
        thread = std::thread([this] { exitCode = runMcpServer(reg, in, out); });
        pipe.feed(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18"}})");
    }
    void finish() {
        pipe.close();
        if (thread.joinable()) thread.join();
    }
    ~Server() { finish(); }
};

} // namespace

TEST(mcp_progress_notifications_for_a_long_call) {
    Server s;
    const auto start = std::chrono::steady_clock::now();
    s.pipe.feed(toolCall(2, "fake_long", R"({"steps":60,"ms":20})", "\"tok-1\""));
    CHECK(s.sink.waitFor([](const auto& ls) { return hasReplyTo(ls, 2); }));
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    s.finish();
    const auto lines = s.sink.lines();
    size_t responseAt = lines.size();
    for (size_t i = 0; i < lines.size(); ++i)
        if (isReplyTo(lines[i], 2)) responseAt = i;
    CHECK(responseAt < lines.size());
    double last = -1;
    size_t count = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        const Json j = parse(lines[i].text); // every line is one whole JSON object
        CHECK(j.isObject() && j["jsonrpc"].asString() == "2.0");
        if (j["method"].asString() != "notifications/progress") continue;
        ++count;
        CHECK(i < responseAt); // no progress after the answer
        CHECK(j["params"]["progressToken"].asString() == "tok-1");
        CHECK(j["params"]["total"].asNumber() == 100.0);
        CHECK(j["params"]["progress"].asNumber() > last); // only ever increases
        last = j["params"]["progress"].asNumber();
        CHECK(!j["params"]["message"].asString().empty());
        CHECK(!j.has("id")); // a notification
    }
    CHECK(count >= 3);
    CHECK(double(count) <= seconds * 10.0 + 1.0); // at most 10 a second (plus the first)
    CHECK(!parse(lines[responseAt].text)["result"]["isError"].asBool(true));
}

TEST(mcp_no_progress_without_a_token) {
    Server s;
    s.pipe.feed(toolCall(2, "fake_long", R"({"steps":10,"ms":20})"));
    CHECK(s.sink.waitFor([](const auto& ls) { return hasReplyTo(ls, 2); }));
    s.finish();
    CHECK(progressCount(s.sink.lines()) == 0);
}

TEST(mcp_numeric_progress_token_is_echoed) {
    Server s;
    s.pipe.feed(toolCall(2, "fake_long", R"({"steps":10,"ms":20})", "7"));
    CHECK(s.sink.waitFor([](const auto& ls) { return hasReplyTo(ls, 2); }));
    s.finish();
    bool seen = false;
    for (const auto& l : s.sink.lines())
        if (isProgress(l)) {
            seen = true;
            CHECK(parse(l.text)["params"]["progressToken"].isNumber());
        }
    CHECK(seen);
}

TEST(mcp_ping_is_answered_during_a_long_call) {
    Server s;
    const int finishedBefore = g_finished;
    s.pipe.feed(toolCall(2, "fake_long", R"({"steps":100,"ms":20})", "\"t\"")); // about 2 s
    CHECK(s.sink.waitFor([](const auto& ls) { return progressCount(ls) >= 1; }));
    s.pipe.feed(R"({"jsonrpc":"2.0","id":3,"method":"ping"})");
    CHECK(s.sink.waitFor([](const auto& ls) { return hasReplyTo(ls, 3); }, 1500ms)); // well before the 2 s are up
    CHECK(!hasReplyTo(s.sink.lines(), 2));
    CHECK(s.sink.waitFor([](const auto& ls) { return hasReplyTo(ls, 2); }));
    CHECK(g_finished == finishedBefore + 1);
    s.finish();
}

TEST(mcp_cancel_stops_the_request_in_flight) {
    Server s;
    const int cancelledBefore = g_cancelled, finishedBefore = g_finished;
    s.pipe.feed(toolCall(2, "fake_long", R"({"steps":1000,"ms":10})", "\"t\"")); // 10 s unless cancelled
    CHECK(s.sink.waitFor([](const auto& ls) { return progressCount(ls) >= 1; }));
    const auto sent = std::chrono::steady_clock::now();
    s.pipe.feed(cancelNote(2));
    // The next request is answered once the cancelled one has stopped, and quickly.
    s.pipe.feed(toolCall(4, "fake_long", R"({"steps":1,"ms":1})"));
    CHECK(s.sink.waitFor([](const auto& ls) { return hasReplyTo(ls, 4); }, 3s));
    CHECK(std::chrono::steady_clock::now() - sent < 2s);
    CHECK(g_cancelled == cancelledBefore + 1);
    CHECK(g_finished == finishedBefore + 1); // only the second ran to the end
    s.finish();
    CHECK(!hasReplyTo(s.sink.lines(), 2)); // a cancelled request gets no reply
    size_t afterCancel = 0; // and no progress for it once it was cancelled
    for (const auto& l : s.sink.lines())
        if (isProgress(l) && l.at > sent + 200ms) ++afterCancel;
    CHECK(afterCancel == 0);
}

TEST(mcp_cancel_of_a_queued_request_drops_it) {
    Server s;
    const int startedBefore = g_started;
    s.pipe.feed(toolCall(2, "fake_long", R"({"steps":1000,"ms":10})", "\"t\""));
    CHECK(s.sink.waitFor([](const auto& ls) { return progressCount(ls) >= 1; }));
    s.pipe.feed(toolCall(3, "fake_long", R"({"steps":1,"ms":1})")); // queued behind it
    s.pipe.feed(cancelNote(3));
    s.pipe.feed(cancelNote(2));
    s.pipe.feed(toolCall(5, "fake_long", R"({"steps":1,"ms":1})"));
    CHECK(s.sink.waitFor([](const auto& ls) { return hasReplyTo(ls, 5); }, 3s));
    s.finish();
    CHECK(!hasReplyTo(s.sink.lines(), 2) && !hasReplyTo(s.sink.lines(), 3));
    CHECK(g_started == startedBefore + 2); // the first, and the fifth: the third never started
}

TEST(mcp_cancel_after_the_answer_or_for_an_unknown_id_is_harmless) {
    Server s;
    s.pipe.feed(toolCall(2, "fake_long", R"({"steps":2,"ms":1})"));
    CHECK(s.sink.waitFor([](const auto& ls) { return hasReplyTo(ls, 2); }));
    s.pipe.feed(cancelNote(2));
    s.pipe.feed(cancelNote(99));
    s.pipe.feed(toolCall(3, "fake_long", R"({"steps":2,"ms":1})"));
    CHECK(s.sink.waitFor([](const auto& ls) { return hasReplyTo(ls, 3); }));
    s.finish();
    CHECK(s.exitCode == 0);
}

TEST(mcp_requests_stay_in_order_and_lines_never_interleave) {
    Server s;
    for (int i = 0; i < 20; ++i) s.pipe.feed(toolCall(10 + i, "fake_long", R"({"steps":3,"ms":5})", "\"p\""));
    CHECK(s.sink.waitFor([](const auto& ls) { return hasReplyTo(ls, 29); }));
    s.finish();
    int expected = 10;
    for (const auto& l : s.sink.lines()) {
        const Json j = parse(l.text);
        if (j.has("id") && j["id"].asNumber() >= 10) CHECK(int(j["id"].asNumber()) == expected++);
    }
    CHECK(expected == 30);
}

TEST(mcp_closing_input_answers_what_was_sent) {
    Server s;
    s.pipe.feed(toolCall(2, "fake_long", R"({"steps":5,"ms":10})"));
    s.pipe.feed(toolCall(3, "fake_long", R"({"steps":5,"ms":10})"));
    s.finish(); // EOF straight away: both are still answered, then the server exits
    CHECK(s.exitCode == 0);
    CHECK(hasReplyTo(s.sink.lines(), 2) && hasReplyTo(s.sink.lines(), 3));
}

// ---- the operations themselves (no server: an OpControl on this thread) ----

namespace {
struct Recorder {
    OpControl control;
    std::vector<float> fractions;
    Recorder() {
        control.onProgress = [this](float f, const std::string&) { fractions.push_back(f); };
    }
    bool monotone() const {
        for (size_t i = 1; i < fractions.size(); ++i)
            if (fractions[i] < fractions[i - 1]) return false;
        return !fractions.empty() && fractions.front() >= 0.0f && fractions.back() <= 1.0f;
    }
};

Document sphereDoc(CommandRegistry& reg, int subdivisions) {
    Document d;
    reg.execute(d, "new_sphere", {{"subdivisions", double(subdivisions)}});
    return d;
}
} // namespace

TEST(long_ops_report_progress_and_leave_the_scene_when_cancelled) {
    CommandRegistry reg;
    struct Case {
        const char* tool;
        Args args;
        int subdivisions;
    };
    const std::string lods = tempPath("sf_progress_lods.glb");
    const std::vector<Case> cases = {{"decimate", {{"ratio", 0.1}}, 6},
                                     {"remesh", {{"resolution", 96.0}}, 3},
                                     {"subdivide", {{"levels", 2.0}}, 3},
                                     {"export_lods", {{"path", lods}, {"levels", 3.0}}, 5}};
    for (const Case& c : cases) {
        // Running with a control: progress is reported, only ever forward, and the command still works.
        {
            Document d = sphereDoc(reg, c.subdivisions);
            Recorder rec;
            OpControlScope scope(&rec.control);
            const CommandResult r = reg.execute(d, c.tool, c.args);
            CHECK(r.ok);
            CHECK(rec.monotone());
            CHECK(!rec.control.honoured);
        }
        // Cancelled before it starts: it stops at the first cancel point, says so, and the scene is untouched.
        {
            Document d = sphereDoc(reg, c.subdivisions);
            const uint64_t before = sceneHash(d);
            const size_t depth = d.undo.undoCount();
            Recorder rec;
            rec.control.cancel = true;
            OpControlScope scope(&rec.control);
            const CommandResult r = reg.execute(d, c.tool, c.args);
            CHECK(!r.ok);
            CHECK(r.message.find("cancel") != std::string::npos);
            CHECK(rec.control.honoured);
            CHECK(sceneHash(d) == before);
            CHECK(d.undo.undoCount() == depth); // no undo step
        }
    }
    std::error_code ec;
    for (int i = 0; i < 3; ++i) std::filesystem::remove(std::filesystem::path(lods.substr(0, lods.size() - 4) + "_LOD" + std::to_string(i) + ".glb"), ec);
}

TEST(cancelled_export_lods_writes_nothing) {
    CommandRegistry reg;
    Document d = sphereDoc(reg, 4);
    const std::string base = tempPath("sf_progress_nolod.glb");
    const std::string first = tempPath("sf_progress_nolod_LOD0.glb");
    std::filesystem::remove(first);
    Recorder rec;
    rec.control.cancel = true;
    OpControlScope scope(&rec.control);
    CHECK(!reg.execute(d, "export_lods", {{"path", base}}).ok);
    CHECK(!std::filesystem::exists(first));
}

TEST(a_cancelled_batch_rolls_back_whole) {
    CommandRegistry reg = testRegistry();
    Document d = sphereDoc(reg, 3);
    const uint64_t before = sceneHash(d);
    const size_t depth = d.undo.undoCount();
    Recorder rec;
    OpControlScope scope(&rec.control);
    Json args;
    std::string err;
    CHECK(Json::parse(R"({"commands":[{"name":"subdivide","arguments":{}},{"name":"fake_self_cancel","arguments":{}},{"name":"subdivide","arguments":{}}],"stop_on_error":false})", args, err));
    const std::string reply = handleMcpMessage(reg, d, R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"batch","arguments":)" + args.dump() + "}}");
    const Json j = parse(reply);
    CHECK(j["result"]["isError"].asBool());
    CHECK(j["result"]["structuredContent"]["rolled_back"].asBool()); // (even with stop_on_error off)
    CHECK(j["result"]["structuredContent"]["executed"].asNumber() == 2);
    CHECK(rec.control.honoured);
    CHECK(sceneHash(d) == before);
    CHECK(d.undo.undoCount() == depth);
}

TEST(batch_progress_covers_each_command_in_turn) {
    CommandRegistry reg = testRegistry();
    Document d = sphereDoc(reg, 2);
    Recorder rec;
    OpControlScope scope(&rec.control);
    const std::string reply = handleMcpMessage(
        reg, d, R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"batch","arguments":{"commands":[{"name":"fake_long","arguments":{"steps":4,"ms":1}},{"name":"fake_long","arguments":{"steps":4,"ms":1}}]}}})");
    CHECK(!parse(reply)["result"]["isError"].asBool(true));
    CHECK(rec.monotone());
    CHECK(rec.fractions.size() >= 8);
    CHECK(rec.fractions.front() < 0.1f && rec.fractions.back() > 0.7f && rec.fractions.back() < 1.0f);
}

TEST(op_control_without_a_control_does_nothing) {
    CHECK(currentOpControl() == nullptr);
    opProgress(0.5f, "nobody listens");
    CHECK(!opCancelled());
}
