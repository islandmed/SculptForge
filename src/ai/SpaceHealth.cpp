// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/SpaceHealth.h"

#include <map>
#include <mutex>

namespace sf::ai::health {

namespace {
std::mutex gMutex;
std::map<std::string, Entry, std::less<>>& table() {
    static std::map<std::string, Entry, std::less<>> t;
    return t;
}
} // namespace

void set(const std::vector<SpaceHealthFeed>& feed) {
    std::map<std::string, Entry, std::less<>> next;
    for (const SpaceHealthFeed& f : feed) {
        Entry e;
        e.state = f.state == "off" ? State::Off : f.state == "slow" ? State::Slow : State::Up;
        e.note = f.note;
        next[f.id] = std::move(e);
    }
    std::lock_guard lock(gMutex);
    table() = std::move(next);
}

Entry of(std::string_view presetId) {
    std::lock_guard lock(gMutex);
    auto it = table().find(presetId);
    return it == table().end() ? Entry{} : it->second;
}

const char* label(State state) {
    return state == State::Off ? "Switched off" : state == State::Slow ? "May be slow to start" : "";
}

std::string reason(const Entry& e) {
    if (e.state == State::Up) return {};
    std::string text = e.state == State::Off ? "switched off" : "may be slow to start";
    if (!e.note.empty()) text += ": " + e.note;
    return text;
}

} // namespace sf::ai::health
