// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "link/DeckCommands.h"

#include "core/Json.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace sf::link {

namespace {

// Printable ASCII, cut: what a log line may quote from a phone.
std::string safeQuote(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (out.size() >= 24) break;
        out += (c >= 32 && c < 127) ? c : '?';
    }
    return out;
}

bool onlyKeys(const Json& j, std::initializer_list<const char*> keys) {
    for (const auto& [key, value] : j.members()) {
        bool known = false;
        for (const char* k : keys) known = known || key == k;
        if (!known) return false;
    }
    return true;
}

} // namespace

const char* deckCommandName(DeckCommand command) {
    switch (command) {
    case DeckCommand::BrushUse: return "brush_use";
    case DeckCommand::Undo: return "undo";
    case DeckCommand::Redo: return "redo";
    case DeckCommand::SizeSet: return "size_set";
    case DeckCommand::IntensitySet: return "intensity_set";
    case DeckCommand::SymmetryToggle: return "symmetry_toggle";
    }
    return "?";
}

float deckSizeToPx(float position) {
    return kDeckMinBrushPx * std::pow(kDeckMaxBrushPx / kDeckMinBrushPx, std::clamp(position, 0.0f, 1.0f));
}

float deckPxToSize(float px) {
    return std::clamp(std::log(std::max(px, 1e-3f) / kDeckMinBrushPx) / std::log(kDeckMaxBrushPx / kDeckMinBrushPx), 0.0f, 1.0f);
}

bool parseDeckMessage(const std::string& text, DeckAction& action, std::string& why) {
    if (text.size() > kMaxDeckMessageBytes) {
        why = "message too large";
        return false;
    }
    Json j;
    std::string error;
    if (!Json::parseStrict(text, j, error) || !j.isObject()) {
        why = "not a JSON object";
        return false;
    }
    const Json& cmd = j["cmd"];
    if (!cmd.isString()) {
        why = "no command";
        return false;
    }
    const std::string& name = cmd.asString();
    action = {};
    if (name == "undo" || name == "redo") {
        if (!onlyKeys(j, {"cmd"})) {
            why = name + " takes no arguments";
            return false;
        }
        action.command = name == "undo" ? DeckCommand::Undo : DeckCommand::Redo;
        return true;
    }
    if (name == "size_set" || name == "intensity_set") {
        const Json& v = j["value"];
        if (!onlyKeys(j, {"cmd", "value"}) || !v.isNumber() || !std::isfinite(v.asNumber()) || v.asNumber() < 0.0 || v.asNumber() > 1.0) {
            why = name + " needs value 0..1";
            return false;
        }
        action.command = name == "size_set" ? DeckCommand::SizeSet : DeckCommand::IntensitySet;
        action.value = float(v.asNumber());
        return true;
    }
    if (name == "symmetry_toggle") {
        const Json& a = j["axis"];
        const std::string axis = a.isString() ? a.asString() : std::string("x");
        if (!onlyKeys(j, {"cmd", "axis"}) || (j.has("axis") && !a.isString()) || (axis != "x" && axis != "y" && axis != "z")) {
            why = "symmetry_toggle axis is x, y or z";
            return false;
        }
        action.command = DeckCommand::SymmetryToggle;
        action.axis = axis[0];
        return true;
    }
    if (name == "brush_use") {
        const Json& n = j["name"];
        if (!onlyKeys(j, {"cmd", "name"}) || !n.isString() || n.asString().empty() || n.asString().size() > kMaxDeckBrushNameBytes) {
            why = "brush_use needs a brush name";
            return false;
        }
        for (char c : n.asString())
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '_' || c == '.')) {
                why = "brush name has characters a brush name doesn't";
                return false;
            }
        action.command = DeckCommand::BrushUse;
        action.name = n.asString();
        return true;
    }
    why = "'" + safeQuote(name) + "' is not a Deck command";
    return false;
}

bool DeckRateLimit::allow(Clock::time_point now) {
    if (!started_) {
        started_ = true;
        last_ = now;
    }
    const double elapsed = std::chrono::duration<double>(now - last_).count();
    last_ = now;
    tokens_ = std::min(burst_, tokens_ + std::max(0.0, elapsed) * rate_);
    if (tokens_ < 1.0) return false;
    tokens_ -= 1.0;
    return true;
}

} // namespace sf::link
