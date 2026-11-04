// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The script console (the ZBrush "script window", backtick key): every command
// the app runs is echoed as a script line with its result, and the input line
// runs script statements (see core/Script.h) with Tab completion and history.
#pragma once

#include "app/UiLayout.h"
#include "core/Commands.h"

#include <imgui.h>

#include <mutex>
#include <string>
#include <vector>

namespace sf {

class App;

class Console {
public:
    enum class Kind { Command, Result, Error, Status, Info };

    // A command ran (from any source): "> name args" then its result.
    void echo(const std::string& statement, const CommandResult& result, const std::string& source = {});
    void print(Kind kind, const std::string& text);
    // Status messages that didn't come from a command appear dim.
    void watchStatus(const std::string& status);
    void seenStatus(const std::string& status) { lastStatus_ = status; } // already shown as a command result
    void clear();
    // The commands run this session, one per line, ready to paste back in (a macro recorder).
    std::string sessionScript() const;

    void focusInput();
    void prefill(const std::string& text);
    void runLine(App& app, const std::string& line);
    void runFile(App& app, const std::string& path);
    void queueScriptFile(std::string path); // from the file dialog thread

    void loadHistory(const std::string& path);
    void draw(App& app, const CanvasRect& rect);

private:
    struct Line {
        Kind kind;
        std::string text;
        std::string source;
    };
    void addHistory(const std::string& line);
    void complete(App& app, ImGuiInputTextCallbackData* data);
    void printHelp(App& app, const std::string& topic);
    static int inputCallback(ImGuiInputTextCallbackData* data);

    std::vector<Line> lines_;
    std::vector<std::string> session_;
    std::vector<std::string> history_;
    std::string historyPath_;
    int historyPos_ = -1; // -1: editing a new line
    std::string lastStatus_;
    std::string lastEcho_;     // coalesces a slider that runs the same command every frame
    double lastEchoTime_ = -1.0;
    size_t lastEchoLine_ = 0;
    char input_[1024] = "";
    bool focusInput_ = false;
    bool bringToFront_ = false; // when opened: above any floating window that covers the strip
    int focusFrame_ = -100;     // the backtick that opened the console arrives as text too: ignore it
    bool cursorToEnd_ = false;
    bool scrollToBottom_ = false;
    std::string lastTab_; // the input when Tab was last pressed (a second Tab lists candidates)
    std::string pendingText_; // replaces the input on the next frame (prefill)
    bool hasPendingText_ = false;
    App* app_ = nullptr; // for the input callback
    bool closeRequested_ = false; // backtick typed into the input
    std::mutex pendingMutex_;
    std::vector<std::string> pendingFiles_;
};

} // namespace sf
