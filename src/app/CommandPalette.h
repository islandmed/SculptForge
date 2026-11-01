// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Command palette (F3 / Ctrl+Shift+P): fuzzy search over every action and
// command. Enter runs; commands that need parameters open in the console.
#pragma once

#include "app/UiLayout.h"

#include <functional>
#include <string>
#include <vector>

namespace sf {

class App;
struct Action;

class CommandPalette {
public:
    void open() { openRequested_ = true; }
    bool isOpen() const { return open_; }
    void draw(App& app, const CanvasRect& area);

    // Ranks actions for QUERY (best first); exposed for tests.
    static int score(const Action& action, const std::string& query);
    // "(ZBrush users: ZSpheres)" when `query` found the action by the ZBrush name of a renamed feature, else "".
    static std::string zbrushHint(const Action& action, const std::string& query);
    // The palette's search: indices into ActionRegistry::all() of the actions matching QUERY, best first, at most
    // LIMIT, only those ACCEPT takes (null: all). Radial menus pick their items with it.
    static std::vector<size_t> find(const App& app, const std::string& query, size_t limit,
                                    const std::function<bool(const Action&)>& accept = nullptr);

private:
    void search(App& app);
    bool openRequested_ = false;
    bool open_ = false;
    char query_[256] = "";
    std::string lastQuery_ = "\x01";
    unsigned lastRevision_ = ~0u;
    std::vector<size_t> results_; // indices into ActionRegistry::all()
    int selected_ = 0;
    bool scrollToSelected_ = false;
};

} // namespace sf
