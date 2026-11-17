// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Console.h"

#include "app/App.h"
#include "app/Python.h"
#include "app/Ui.h"
#include "core/Script.h"

#include <IconsLucide.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace sf {

namespace {

constexpr size_t kMaxLines = 4000;
constexpr size_t kMaxHistory = 200;

std::filesystem::path fsPath(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string firstWord(const std::string& s) {
    const std::string t = trim(s);
    const size_t sp = t.find_first_of(" \t");
    return sp == std::string::npos ? t : t.substr(0, sp);
}

std::string commonPrefix(const std::vector<std::string>& v) {
    if (v.empty()) return {};
    std::string p = v[0];
    for (const auto& s : v) {
        size_t n = 0;
        while (n < p.size() && n < s.size() && p[n] == s[n]) ++n;
        p.resize(n);
    }
    return p;
}

const char* typeName(ParamSpec::Type t) {
    switch (t) {
    case ParamSpec::Type::Bool: return "true/false";
    case ParamSpec::Type::Number: return "number";
    case ParamSpec::Type::String: return "text";
    }
    return "";
}

// Names laid out in columns, like a shell listing.
std::vector<std::string> columns(std::vector<std::string> names, size_t width = 96) {
    std::sort(names.begin(), names.end());
    size_t col = 0;
    for (const auto& n : names) col = std::max(col, n.size() + 2);
    const size_t perLine = std::max<size_t>(1, width / std::max<size_t>(col, 1));
    std::vector<std::string> lines;
    std::string line;
    for (size_t i = 0; i < names.size(); ++i) {
        std::string cell = names[i];
        if ((i + 1) % perLine != 0 && i + 1 < names.size()) cell.resize(col, ' ');
        line += cell;
        if ((i + 1) % perLine == 0) {
            lines.push_back("  " + line);
            line.clear();
        }
    }
    if (!line.empty()) lines.push_back("  " + line);
    return lines;
}

} // namespace

void Console::print(Kind kind, const std::string& text) {
    // Multi-line messages become several lines (the log draws one line per entry).
    size_t start = 0;
    while (start <= text.size()) {
        const size_t nl = text.find('\n', start);
        lines_.push_back({kind, text.substr(start, nl == std::string::npos ? std::string::npos : nl - start), {}});
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    if (lines_.size() > kMaxLines) lines_.erase(lines_.begin(), lines_.begin() + std::ptrdiff_t(lines_.size() - kMaxLines));
    scrollToBottom_ = true;
}

void Console::echo(const std::string& statement, const CommandResult& result, const std::string& source) {
    const double now = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
    // A slider runs its command every frame while dragged: keep one line for it, with the latest values.
    if (result.ok && firstWord(statement) == firstWord(lastEcho_) && now - lastEchoTime_ < 0.5 &&
        lastEchoLine_ + 2 == lines_.size() && lines_[lastEchoLine_].kind == Kind::Command) {
        lines_[lastEchoLine_].text = statement;
        lines_[lastEchoLine_ + 1].text = result.message;
        if (!session_.empty()) session_.back() = statement;
        lastEcho_ = statement;
        lastEchoTime_ = now;
        return;
    }
    if (result.ok) {
        lines_.push_back({Kind::Command, statement, source});
        lastEchoLine_ = lines_.size() - 1;
        lines_.push_back({Kind::Result, result.message, {}});
        session_.push_back(statement);
    } else {
        lines_.push_back({Kind::Error, statement + "   -> " + result.message, source});
        lastEchoLine_ = 0;
    }
    lastEcho_ = statement;
    lastEchoTime_ = now;
    if (lines_.size() > kMaxLines) {
        const size_t drop = lines_.size() - kMaxLines;
        lines_.erase(lines_.begin(), lines_.begin() + std::ptrdiff_t(drop));
        lastEchoLine_ = lastEchoLine_ >= drop ? lastEchoLine_ - drop : 0;
    }
    scrollToBottom_ = true;
}

void Console::watchStatus(const std::string& status) {
    if (status == lastStatus_) return;
    lastStatus_ = status;
    if (status.empty()) return;
    // A job's progress ("Auto UV: 42%") updates its line instead of adding one per percent.
    const auto progressPrefix = [](const std::string& s) {
        const size_t sp = s.find_last_of(' ');
        return !s.empty() && s.back() == '%' && sp != std::string::npos ? s.substr(0, sp) : std::string();
    };
    if (const std::string prefix = progressPrefix(status);
        !prefix.empty() && !lines_.empty() && lines_.back().kind == Kind::Status && progressPrefix(lines_.back().text) == prefix) {
        lines_.back().text = status;
        return;
    }
    print(Kind::Status, status);
}

void Console::clear() {
    lines_.clear();
    lastEchoLine_ = 0;
    lastEcho_.clear();
}

std::string Console::sessionScript() const {
    std::string out;
    for (const auto& s : session_) out += s + "\n";
    return out;
}

void Console::focusInput() {
    focusInput_ = bringToFront_ = true;
    focusFrame_ = ImGui::GetCurrentContext() ? ImGui::GetFrameCount() : -100;
}

void Console::prefill(const std::string& text) {
    pendingText_ = text;
    hasPendingText_ = true;
    focusInput();
}

void Console::loadHistory(const std::string& path) {
    historyPath_ = path;
    history_.clear();
    std::ifstream f(fsPath(path));
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) history_.push_back(line);
    }
    if (history_.size() > kMaxHistory) history_.erase(history_.begin(), history_.end() - kMaxHistory);
}

void Console::addHistory(const std::string& line) {
    historyPos_ = -1;
    if (line.empty()) return;
    history_.erase(std::remove(history_.begin(), history_.end(), line), history_.end());
    history_.push_back(line);
    if (history_.size() > kMaxHistory) history_.erase(history_.begin(), history_.end() - kMaxHistory);
    if (historyPath_.empty()) return;
    std::ofstream f(fsPath(historyPath_), std::ios::binary | std::ios::trunc);
    for (const auto& h : history_) f << h << "\n";
}

void Console::printHelp(App& app, const std::string& topic) {
    const std::string t = trim(topic);
    if (t.empty()) {
        print(Kind::Info, "Type a command and press Enter, e.g.  remesh resolution=200   or   subdivide; decimate ratio=0.5");
        print(Kind::Info, "Tab completes names and parameters, Up/Down walks the history, help <command> explains one.");
        std::vector<std::string> names;
        for (const auto& c : app.commands_.all()) names.push_back(c.name);
        for (const auto& l : columns(names)) print(Kind::Info, l);
        return;
    }
    const Command* c = app.commands_.find(t);
    if (!c) {
        print(Kind::Error, "help " + t + "   -> No command named '" + t + "' (type help for the list)");
        return;
    }
    print(Kind::Info, c->name + "  -  " + c->description);
    if (c->params.empty()) print(Kind::Info, "  (no parameters)");
    for (const auto& p : c->params)
        print(Kind::Info, "  " + p.name + "=<" + typeName(p.type) + ">" + (p.required ? " (required)" : "") + "  " + p.description);
}

void Console::setPythonMode(App& app, bool on) {
    if (on == pythonMode_) return;
    if (pythonMore_) app.python().cancelLine();
    pythonMore_ = false;
    pythonMode_ = on;
    if (on) {
        print(Kind::Info, "Python: sf is the sculptforge module, doc the open scene. sf.run('remesh', resolution=200) runs a "
                          "command; each line you enter is one undo step. help(sf) lists the API.");
        if (!app.python().enabled() || !app.python().allowed())
            print(Kind::Error, "Python scripts are off: turn them on in Preferences > Scripts");
    }
    focusInput();
}

// One line of Python, as Python's own prompt takes it: a line that opens a block (if, for, def...) waits for the
// rest, ended by an empty line. Leading spaces are kept (they are the block's indentation).
void Console::runPythonLine(App& app, const std::string& raw) {
    std::string line = raw;
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    if (!trim(line).empty()) addHistory(line);
    else historyPos_ = -1;
    print(Kind::Info, std::string(pythonMore_ ? "... " : ">>> ") + line);
    session_.push_back("# python: " + line); // (the session script keeps it, as a comment: it isn't a statement)
    const PythonScripting::Result r = app.python().runLine(line);
    pythonMore_ = r == PythonScripting::Result::More;
}

void Console::runLine(App& app, const std::string& raw) {
    if (pythonMode_) {
        runPythonLine(app, raw);
        return;
    }
    const std::string line = trim(raw);
    addHistory(line);
    if (line.empty()) return;
    const std::string word = firstWord(line);
    if (word == "help" || word == "?") {
        print(Kind::Command, line);
        printHelp(app, line.substr(word.size()));
        return;
    }
    if (line == "clear") {
        clear();
        return;
    }
    std::vector<ScriptStatement> statements;
    std::string err;
    if (!parseScript(line, statements, err)) {
        print(Kind::Error, line + "   -> " + err);
        return;
    }
    runStatements(app, std::move(statements), 0, false);
}

// Runs statements[from...]; a command that works on a worker (uv_auto) holds the rest until its result is in
// (update()), as later statements usually depend on it.
void Console::runStatements(App& app, std::vector<ScriptStatement> statements, size_t from, bool file) {
    for (size_t i = from; i < statements.size(); ++i) {
        const ScriptStatement& st = statements[i];
        if (!app.commands_.find(st.command)) {
            if (file) print(Kind::Error, "Line " + std::to_string(st.line) + ": unknown command '" + st.command + "' - stopped");
            else
                print(Kind::Error, formatStatement(st.command, st.args) + "   -> Unknown command '" + st.command +
                                       "' (Tab completes, help lists them)");
            return;
        }
        app.runCommand(st.command, st.args); // echoes itself
        if (!app.lastCommandOk_) {            // later statements usually depend on this one
            if (file) print(Kind::Error, "Line " + std::to_string(st.line) + " failed - stopped");
            return;
        }
        if (app.commandRunning()) {
            if (i + 1 < statements.size()) resume_ = Resume{std::move(statements), i + 1, file};
            return;
        }
    }
}

void Console::update(App& app) {
    if (app.python_ && app.python_->loaded()) app.python_->drainOutput(); // what a script's threads printed
    if (!resume_ || app.commandRunning()) return;
    Resume r = std::move(*resume_);
    resume_.reset();
    if (CommandResult done; app.takeCommandResult(done) && !done.ok) {
        if (r.file) print(Kind::Error, "Line " + std::to_string(r.statements[r.next - 1].line) + " failed - stopped");
        return;
    }
    runStatements(app, std::move(r.statements), r.next, r.file);
}

void Console::runFile(App& app, const std::string& path) {
    {
        std::string ext = fsPath(path).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (ext == ".py") {
            app.runPythonFile(path);
            return;
        }
    }
    std::ifstream f(fsPath(path), std::ios::binary);
    if (!f) {
        print(Kind::Error, "Run script   -> Cannot read " + path);
        return;
    }
    const std::string text((std::istreambuf_iterator<char>(f)), {});
    std::vector<ScriptStatement> statements;
    std::string err;
    if (!parseScript(text, statements, err)) {
        print(Kind::Error, "Run script " + path + "   -> " + err);
        return;
    }
    print(Kind::Info, "Running " + fsPath(path).filename().string() + " (" + std::to_string(statements.size()) + " statements)");
    runStatements(app, std::move(statements), 0, true);
}

void Console::queueScriptFile(std::string path) {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    pendingFiles_.push_back(std::move(path));
}

// Tab in Python mode: the name before the cursor (with its dots), from the console's names.
void Console::completePython(App& app, ImGuiInputTextCallbackData* data) {
    const std::string text(data->Buf, size_t(data->CursorPos));
    size_t start = text.size();
    while (start > 0 && (std::isalnum(static_cast<unsigned char>(text[start - 1])) || text[start - 1] == '_' || text[start - 1] == '.')) --start;
    const std::string prefix = text.substr(start);
    if (prefix.empty()) { // Tab at the start of a line indents
        data->InsertChars(data->CursorPos, "    ");
        return;
    }
    std::vector<std::string> candidates = app.python().complete(prefix);
    if (candidates.empty()) return;
    const std::string common = candidates.size() == 1 ? candidates[0] : commonPrefix(candidates);
    if (common.size() > prefix.size()) {
        data->DeleteChars(int(start), int(prefix.size()));
        data->InsertChars(int(start), common.c_str());
        lastTab_.clear();
        return;
    }
    const std::string now(data->Buf, size_t(data->BufTextLen));
    if (lastTab_ == now) {
        for (auto& c : candidates) c = c.substr(c.find_last_of('.') == std::string::npos ? 0 : c.find_last_of('.') + 1);
        for (const auto& l : columns(candidates)) print(Kind::Info, l);
        lastTab_.clear();
    } else {
        lastTab_ = now;
    }
}

void Console::complete(App& app, ImGuiInputTextCallbackData* data) {
    if (pythonMode_) {
        if (app.python().loaded()) completePython(app, data);
        return;
    }
    const std::string text(data->Buf, size_t(data->CursorPos));
    const size_t stmtStart = text.find_last_of(';') == std::string::npos ? 0 : text.find_last_of(';') + 1;
    const std::string stmt = text.substr(stmtStart);
    const size_t tokStart = stmt.find_last_of(" \t") == std::string::npos ? 0 : stmt.find_last_of(" \t") + 1;
    const std::string prefix = stmt.substr(tokStart);
    const bool commandName = trim(stmt.substr(0, tokStart)).empty();
    std::vector<std::string> candidates;
    std::string suffix;
    if (commandName) {
        for (const auto& c : app.commands_.all())
            if (c.name.rfind(prefix, 0) == 0) candidates.push_back(c.name);
        if (std::string("help").rfind(prefix, 0) == 0) candidates.push_back("help");
        suffix = " ";
    } else {
        if (prefix.find('=') != std::string::npos) return; // a value: nothing to complete
        const Command* c = app.commands_.find(firstWord(stmt));
        if (!c) return;
        for (const auto& p : c->params)
            if (p.name.rfind(prefix, 0) == 0 && stmt.find(" " + p.name + "=") == std::string::npos) candidates.push_back(p.name);
        suffix = "=";
    }
    if (candidates.empty()) return;
    std::sort(candidates.begin(), candidates.end());
    const std::string common = candidates.size() == 1 ? candidates[0] + suffix : commonPrefix(candidates);
    if (common.size() > prefix.size()) {
        const int at = data->CursorPos - int(prefix.size());
        data->DeleteChars(at, int(prefix.size()));
        data->InsertChars(at, common.c_str());
        lastTab_.clear();
        return;
    }
    // Nothing more to add: a second Tab lists what could follow.
    const std::string now(data->Buf, size_t(data->BufTextLen));
    if (lastTab_ == now) {
        print(Kind::Info, commandName ? "Commands:" : "Parameters of " + firstWord(stmt) + ":");
        for (const auto& l : columns(candidates)) print(Kind::Info, l);
        lastTab_.clear();
    } else {
        lastTab_ = now;
    }
}

int Console::inputCallback(ImGuiInputTextCallbackData* data) {
    auto* self = static_cast<Console*>(data->UserData);
    switch (data->EventFlag) {
    case ImGuiInputTextFlags_CallbackCharFilter:
        if (data->EventChar == '`') { // the console key closes the console, even while typing
            if (ImGui::GetFrameCount() - self->focusFrame_ > 2) self->closeRequested_ = true;
            return 1;
        }
        return 0;
    case ImGuiInputTextFlags_CallbackCompletion:
        if (self->app_) self->complete(*self->app_, data);
        return 0;
    case ImGuiInputTextFlags_CallbackHistory: {
        const auto& h = self->history_;
        if (h.empty()) return 0;
        int& pos = self->historyPos_;
        if (data->EventKey == ImGuiKey_UpArrow) pos = pos < 0 ? int(h.size()) - 1 : std::max(0, pos - 1);
        else if (data->EventKey == ImGuiKey_DownArrow && pos >= 0) pos = pos + 1 >= int(h.size()) ? -1 : pos + 1;
        data->DeleteChars(0, data->BufTextLen);
        if (pos >= 0) data->InsertChars(0, h[size_t(pos)].c_str());
        return 0;
    }
    case ImGuiInputTextFlags_CallbackAlways:
        if (self->hasPendingText_) {
            data->DeleteChars(0, data->BufTextLen);
            data->InsertChars(0, self->pendingText_.c_str());
            self->hasPendingText_ = false;
            self->cursorToEnd_ = true;
        }
        if (self->cursorToEnd_) {
            data->CursorPos = data->SelectionStart = data->SelectionEnd = data->BufTextLen;
            self->cursorToEnd_ = false;
        }
        return 0;
    default:
        return 0;
    }
}

void Console::draw(App& app, const CanvasRect& rect) {
    app_ = &app;
    {
        std::vector<std::string> files;
        {
            std::lock_guard<std::mutex> lock(pendingMutex_);
            files.swap(pendingFiles_);
        }
        for (const auto& f : files) runFile(app, f);
    }
    const float s = app.uiScale_;
    ImGui::SetNextWindowPos(ImVec2(rect.x, rect.y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(rect.w, rect.h), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f * s, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(IM_COL32(0x16, 0x16, 0x18, 0xff)));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse |
                                   ImGuiWindowFlags_NoFocusOnAppearing;
    if (bringToFront_) ImGui::SetNextWindowFocus();
    bringToFront_ = false;
    const bool visible = ImGui::Begin(tr("Console"), nullptr, flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
    if (!visible) {
        ImGui::End();
        return;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetWindowPos();
    const float w = rect.w;

    // Header bar (h 24) with a resize grip on its top edge.
    const float headerH = 24.0f * s;
    dl->AddRectFilled(origin, ImVec2(origin.x + w, origin.y + headerH), ui::color::header);
    dl->AddLine(origin, ImVec2(origin.x + w, origin.y), ui::color::border);
    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton("##console_resize", ImVec2(w, 5.0f * s));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    if (ImGui::IsItemActive() && ImGui::GetIO().MouseDelta.y != 0.0f)
        app.layout_.consoleHeight = std::clamp(app.layout_.consoleHeight - ImGui::GetIO().MouseDelta.y / s, 100.0f, 400.0f);
    {
        const ImVec2 grip(origin.x + w * 0.5f, origin.y + 3.0f * s);
        dl->AddLine(ImVec2(grip.x - 14.0f * s, grip.y), ImVec2(grip.x + 14.0f * s, grip.y), IM_COL32(0x5a, 0x5a, 0x60, 0xff), 2.0f * s);
    }
    const float textY = origin.y + (headerH - ImGui::GetTextLineHeight()) * 0.5f;
    dl->AddText(ImVec2(origin.x + 10.0f * s, textY), ui::color::accent, ICON_LC_SQUARE_TERMINAL);
    dl->AddText(ImVec2(origin.x + 32.0f * s, textY), ui::color::text, "Console");
    dl->AddText(ImVec2(origin.x + 32.0f * s + ImGui::CalcTextSize("Console").x + 12.0f * s, textY), ui::color::textDim,
                "Every command you run appears here as a script line");
    {
        const float bs = 22.0f * s, gap = 2.0f * s;
        float x = origin.x + w - 8.0f * s - 4 * bs - 3 * gap;
        const float y = origin.y + (headerH - bs) * 0.5f;
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f * s);
        // Commands | Python: what the input line runs.
        {
            const char* labels[2] = {tr("Commands"), "Python"};
            float segW[2];
            for (int i = 0; i < 2; ++i) segW[i] = ImGui::CalcTextSize(labels[i]).x + 14.0f * s;
            float sx = x - 10.0f * s - segW[0] - segW[1];
            for (int i = 0; i < 2; ++i) {
                const bool on = (i == 1) == pythonMode_;
                ImGui::SetCursorScreenPos(ImVec2(sx, y));
                ImGui::PushStyleColor(ImGuiCol_Button, on ? ImGui::ColorConvertU32ToFloat4(ui::color::accent) : ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_Text, on ? ImVec4(0.07f, 0.07f, 0.08f, 1.0f) : ImGui::ColorConvertU32ToFloat4(ui::color::textDim));
                if (ImGui::Button((std::string(labels[i]) + "##console_mode" + std::to_string(i)).c_str(), ImVec2(segW[i], bs)))
                    setPythonMode(app, i == 1);
                ImGui::PopStyleColor(2);
                ui::HelpText(i ? "Python" : "Commands", nullptr,
                             i ? "The input line runs Python: sf is the sculptforge module, doc the open scene. Turn Python "
                                 "scripts on in Preferences > Scripts. Only run code you trust."
                               : "The input line runs commands, one per line or separated by ';'.",
                             "Script");
                sx += segW[i];
            }
        }
        for (const char* id : {"script.run_file", "script.copy_session", "script.clear_console"}) {
            ImGui::SetCursorScreenPos(ImVec2(x, y));
            ui::ActionButton(app, id, ImVec2(bs, bs), ui::Look::Icon);
            x += bs + gap;
        }
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        if (ui::GlyphButton("###console_close", ICON_LC_X, ImVec2(bs, bs))) closeRequested_ = true;
        const std::string key = app.actions_.shortcutText("ui.console");
        ui::HelpText("Close the console", key.c_str(), "Hide the console. The log and the session script are kept.", "Script");
        ui::DelayedTooltip(("Close   " + key).c_str());
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    // Log.
    ImGui::PushFont(app.consoleFont());
    const float inputH = ImGui::GetFrameHeight() + 8.0f * s;
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + headerH));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * s, 4.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 1.0f * s));
    ImGui::BeginChild("##console_log", ImVec2(w, rect.h - headerH - inputH), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_HorizontalScrollbar);
    const bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.0f;
    ImGuiListClipper clipper;
    clipper.Begin(int(lines_.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const Line& l = lines_[size_t(i)];
            const std::string src = l.source.empty() ? std::string() : "[" + l.source + "] ";
            switch (l.kind) {
            case Kind::Command:
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(IM_COL32(0xee, 0xee, 0xee, 0xff)), "%s> %s", src.c_str(), l.text.c_str());
                break;
            case Kind::Result:
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(IM_COL32(0xa4, 0xa4, 0xa8, 0xff)), "  %s", l.text.c_str());
                break;
            case Kind::Error:
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::color::danger), "%s! %s", src.c_str(), l.text.c_str());
                break;
            case Kind::Status:
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(IM_COL32(0x6c, 0x6c, 0x72, 0xff)), "  %s", l.text.c_str());
                break;
            case Kind::Info:
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(IM_COL32(0xc8, 0xc8, 0xc8, 0xff)), "%s", l.text.c_str());
                break;
            }
        }
    }
    if (lines_.empty())
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::color::textDim), "%s", tr("Type help and press Enter to list the commands."));
    if (scrollToBottom_ && (atBottom || focusInput_)) ImGui::SetScrollHereY(1.0f);
    scrollToBottom_ = false;
    ImGui::EndChild();
    ImGui::PopStyleVar(2);

    // Input line.
    const ImVec2 inputPos(origin.x, origin.y + rect.h - inputH);
    dl->AddLine(inputPos, ImVec2(inputPos.x + w, inputPos.y), IM_COL32(0x2a, 0x2a, 0x2e, 0xff));
    const float fy = inputPos.y + (inputH - ImGui::GetFrameHeight()) * 0.5f;
    const char* prompt = !pythonMode_ ? ">" : pythonMore_ ? "..." : ">>>";
    dl->AddText(ImVec2(origin.x + 10.0f * s, fy + ImGui::GetStyle().FramePadding.y), ui::color::accent, prompt);
    const float promptW = std::max(12.0f * s, ImGui::CalcTextSize(prompt).x + 6.0f * s);
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 10.0f * s + promptW, fy));
    ImGui::SetNextItemWidth(w - 20.0f * s - promptW);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(1, 1, 1, 0.03f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(1, 1, 1, 0.04f));
    if (focusInput_) {
        ImGui::SetKeyboardFocusHere();
        focusInput_ = false;
    }
    const ImGuiInputTextFlags inputFlags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCompletion |
                                           ImGuiInputTextFlags_CallbackHistory | ImGuiInputTextFlags_CallbackCharFilter |
                                           ImGuiInputTextFlags_CallbackAlways;
    const char* inputHint = pythonMode_ ? tr("Python, such as  sf.run('remesh', resolution=200)   or   print(doc.parts)")
                                        : tr("help, or a command such as  remesh resolution=200");
    const bool entered = ImGui::InputTextWithHint("##console", inputHint, input_,
                                                  sizeof input_, inputFlags, &Console::inputCallback, this);
    ImGui::PopStyleColor(3);
    ImGui::PopFont();
    ui::HelpText("Console", app.actions_.shortcutText("ui.console").c_str(),
                 "Runs SculptForge script: one command per line or separated by ';', parameters as name=value "
                 "(quote text with spaces). Tab completes, Up/Down recalls, help lists every command.",
                 "Script");
    if (entered) {
        const std::string line = input_;
        input_[0] = 0;
        runLine(app, line);
        focusInput_ = true; // keep typing, like a terminal
        scrollToBottom_ = true;
    }
    ImGui::End();
    if (closeRequested_) {
        closeRequested_ = false;
        app.setConsoleOpen(false);
    }
}

} // namespace sf
