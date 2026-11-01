// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Actions.h"

#include "core/Commands.h"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <utility>

namespace sf {

namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = char(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string trim(std::string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

// "subtool_append" -> "Part append": the command's words, with the interface's names for what they name.
std::string humanize(const std::string& name) {
    static const std::pair<std::string_view, std::string_view> kWords[] = {
        {"subtool", "part"},           {"subtools", "parts"},       {"zsphere", "forge sphere"},
        {"zspheres", "forge spheres"}, {"polygroup", "face group"}, {"polygroups", "face groups"},
        {"polypaint", "vertex paint"}, {"qremesh", "quad remesh"},  {"matcap", "lit sphere"},
        {"matcaps", "lit spheres"},    {"lightbox", "library"},     {"goz", "zbrush"},
        {"quicksave", "snapshot"}};
    std::string out;
    size_t start = 0;
    while (start <= name.size()) {
        const size_t end = std::min(name.find('_', start), name.size());
        std::string_view word(name.data() + start, end - start);
        for (const auto& [from, to] : kWords)
            if (word == from) word = to;
        if (!out.empty()) out += ' ';
        out += word;
        start = end + 1;
    }
    if (!out.empty()) out[0] = char(std::toupper(static_cast<unsigned char>(out[0])));
    return out;
}

} // namespace

bool isModifierKey(SDL_Keycode key) {
    switch (key) {
    case SDLK_LCTRL: case SDLK_RCTRL: case SDLK_LSHIFT: case SDLK_RSHIFT: case SDLK_LALT: case SDLK_RALT:
    case SDLK_LGUI: case SDLK_RGUI: case SDLK_MODE: case SDLK_CAPSLOCK:
        return true;
    default:
        return false;
    }
}

namespace {

// "Numpad 1", "Numpad .", "Numpad Plus" (also "Numpad1", "Keypad 1") -> the keypad's key.
SDL_Keycode keypadKey(const std::string& lowerName) {
    std::string rest;
    for (const char* prefix : {"numpad", "keypad", "kp"})
        if (lowerName.rfind(prefix, 0) == 0) {
            rest = trim(lowerName.substr(std::string_view(prefix).size()));
            break;
        }
    if (rest.empty()) return SDLK_UNKNOWN;
    if (rest.size() == 1 && rest[0] >= '1' && rest[0] <= '9') return SDL_Keycode(SDLK_KP_1 + (rest[0] - '1'));
    if (rest == "0") return SDLK_KP_0;
    if (rest == "." || rest == "period" || rest == "del") return SDLK_KP_PERIOD;
    if (rest == "plus") return SDLK_KP_PLUS;
    if (rest == "minus" || rest == "-") return SDLK_KP_MINUS;
    if (rest == "/" || rest == "slash") return SDLK_KP_DIVIDE;
    if (rest == "*" || rest == "asterisk") return SDLK_KP_MULTIPLY;
    if (rest == "enter") return SDLK_KP_ENTER;
    return SDLK_UNKNOWN;
}

} // namespace

bool parseChord(std::string_view text, Chord& out) {
    out = Chord{};
    std::string t = trim(text);
    if (t.empty()) return false;
    // Split on '+', but a trailing "+" (or "Ctrl++") is the plus key itself.
    std::vector<std::string> parts;
    std::string cur;
    for (size_t i = 0; i < t.size(); ++i) {
        if (t[i] == '+' && !cur.empty()) {
            parts.push_back(trim(cur));
            cur.clear();
        } else {
            cur += t[i];
        }
    }
    parts.push_back(trim(cur));
    std::string key;
    for (size_t i = 0; i < parts.size(); ++i) {
        const std::string p = lower(parts[i]);
        if (i + 1 < parts.size()) {
            if (p == "ctrl" || p == "control" || p == "cmd" || p == "command") out.ctrl = true;
            else if (p == "shift") out.shift = true;
            else if (p == "alt" || p == "option" || p == "opt") out.alt = true;
            else return false;
        } else {
            key = parts[i];
        }
    }
    if (key.empty()) return false;
    if (key.size() == 1) {
        const unsigned char c = static_cast<unsigned char>(key[0]);
        if (!std::isprint(c) || c == ' ') return false;
        out.key = SDL_Keycode(std::tolower(c)); // SDL keycodes of printable keys are their (lower-case) characters
        return true;
    }
    const std::string k = lower(key);
    if (const SDL_Keycode kp = keypadKey(k); kp != SDLK_UNKNOWN) {
        out.key = kp;
        return true;
    }
    if (k == "enter") key = "Return";
    else if (k == "esc") key = "Escape";
    else if (k == "comma") key = ",";
    else if (k == "backtick" || k == "grave") key = "`";
    else if (k == "del") key = "Delete";
    out.key = SDL_GetKeyFromName(key.c_str());
    return out.key != SDLK_UNKNOWN;
}

std::vector<Chord> parseChords(std::string_view text) {
    std::vector<Chord> out;
    size_t start = 0;
    while (start <= text.size()) {
        size_t at = text.find(" / ", start);
        const std::string_view part = text.substr(start, at == std::string_view::npos ? std::string_view::npos : at - start);
        Chord c;
        if (parseChord(part, c)) out.push_back(c);
        if (at == std::string_view::npos) break;
        start = at + 3;
    }
    return out;
}

namespace {

std::string keyName(SDL_Keycode key) {
    if (key > 32 && key < 127) return std::string(1, char(std::toupper(int(key))));
    if (key == SDLK_RETURN) return "Enter";
    if (key == SDLK_ESCAPE) return "Esc";
    if (key >= SDLK_KP_1 && key <= SDLK_KP_9) return "Numpad " + std::to_string(int(key - SDLK_KP_1) + 1);
    switch (key) { // spelled so that parseChord reads them back (a '+' would split the chord)
    case SDLK_KP_0: return "Numpad 0";
    case SDLK_KP_PERIOD: return "Numpad .";
    case SDLK_KP_PLUS: return "Numpad Plus";
    case SDLK_KP_MINUS: return "Numpad Minus";
    case SDLK_KP_DIVIDE: return "Numpad /";
    case SDLK_KP_MULTIPLY: return "Numpad *";
    case SDLK_KP_ENTER: return "Numpad Enter";
    default: break;
    }
    const char* name = SDL_GetKeyName(key);
    return name && *name ? name : "?";
}

} // namespace

std::string chordText(const Chord& c) {
    if (c.key == SDLK_UNKNOWN) return {};
    std::string s;
    if (c.ctrl) s += "Ctrl+";
    if (c.alt) s += "Alt+";
    if (c.shift) s += "Shift+";
    return s + keyName(c.key);
}

Chord chordFromEvent(SDL_Keycode key, SDL_Keymod mods) {
    Chord c;
    c.key = key;
    c.ctrl = (mods & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
    c.shift = (mods & SDL_KMOD_SHIFT) != 0;
    c.alt = (mods & SDL_KMOD_ALT) != 0;
    return c;
}

std::vector<std::string> chordKeys(std::string_view chord) {
    std::vector<std::string> keys;
    Chord c;
    if (!parseChord(chord, c)) {
        if (!chord.empty()) keys.emplace_back(chord);
        return keys;
    }
    if (c.ctrl) keys.emplace_back("Ctrl");
    if (c.alt) keys.emplace_back("Alt");
    if (c.shift) keys.emplace_back("Shift");
    keys.push_back(keyName(c.key));
    return keys;
}

void ActionRegistry::add(Action a) {
    ++revision_;
    for (auto& existing : actions_)
        if (existing.id == a.id) {
            existing = std::move(a);
            return;
        }
    actions_.push_back(std::move(a));
}

void ActionRegistry::removePrefix(std::string_view prefix) {
    ++revision_;
    std::erase_if(actions_, [&](const Action& a) { return std::string_view(a.id).substr(0, prefix.size()) == prefix; });
}

const Action* ActionRegistry::find(std::string_view id) const {
    for (const auto& a : actions_)
        if (a.id == id) return &a;
    return nullptr;
}

const std::vector<Chord>& ActionRegistry::chordsOf(size_t index) const {
    if (chordsRevision_ != revision_ || chords_.size() != actions_.size()) {
        chords_.clear();
        for (const auto& a : actions_) chords_.push_back(parseChords(shortcutText(a.id)));
        chordsRevision_ = revision_;
    }
    return chords_[index];
}

const Action* ActionRegistry::actionForChord(const Chord& chord) const {
    for (size_t i = 0; i < actions_.size(); ++i)
        for (const Chord& c : chordsOf(i))
            if (c == chord) return &actions_[i];
    return nullptr;
}

bool ActionRegistry::handleKey(SDL_Keycode key, SDL_Keymod mods) {
    if (isModifierKey(key)) return false;
    Chord chord = chordFromEvent(key, mods);
    const Action* a = actionForChord(chord);
    if (!a && !chord.ctrl && (chord.shift || chord.alt)) {
        // Exact chords win; otherwise plain keys ignore Shift and Alt as they always have (Shift+[ while
        // smoothing still resizes the brush).
        chord.shift = chord.alt = false;
        a = actionForChord(chord);
    }
    if (!a || !a->run) return false;
    if (a->enabled && !a->enabled()) return true; // bound, just not available right now
    // Copy: running an action may register or replace actions.
    const std::function<void()> run = a->run;
    run();
    return true;
}

void ActionRegistry::setDefaultShortcut(std::string_view id, std::string chord) {
    for (auto& a : actions_)
        if (a.id == id && a.shortcut != chord) {
            a.shortcut = std::move(chord);
            ++revision_;
            return;
        }
}

std::string ActionRegistry::shortcutText(std::string_view id) const {
    for (const auto& [oid, chord] : overrides_)
        if (oid == id) return chord;
    const Action* a = find(id);
    return a ? a->shortcut : std::string();
}

void ActionRegistry::setShortcut(std::string_view id, std::string chord) {
    ++revision_;
    const Action* a = find(id);
    const bool isDefault = a && a->shortcut == chord;
    for (auto it = overrides_.begin(); it != overrides_.end(); ++it)
        if (it->first == id) {
            if (isDefault) overrides_.erase(it);
            else it->second = std::move(chord);
            return;
        }
    if (!isDefault) overrides_.emplace_back(std::string(id), std::move(chord));
}

std::string ActionRegistry::assignShortcut(std::string_view id, const std::string& chordString, bool keepOthers) {
    Chord chord;
    if (!parseChord(chordString, chord)) {
        if (!keepOthers) setShortcut(id, "");
        return {};
    }
    std::string lost;
    for (const auto& other : actions_) {
        if (other.id == id) continue;
        const std::vector<Chord> theirs = parseChords(shortcutText(other.id));
        if (std::find(theirs.begin(), theirs.end(), chord) == theirs.end()) continue;
        std::string keep;
        for (const Chord& c : theirs)
            if (!(c == chord)) keep += (keep.empty() ? "" : " / ") + chordText(c);
        lost = other.label;
        setShortcut(other.id, keep);
    }
    std::string mine = chordText(chord);
    if (keepOthers)
        for (const Chord& c : parseChords(shortcutText(id)))
            if (!(c == chord)) mine += " / " + chordText(c);
    setShortcut(id, mine);
    return lost;
}

bool ActionRegistry::hasOverride(std::string_view id) const {
    for (const auto& o : overrides_)
        if (o.first == id) return true;
    return false;
}

void ActionRegistry::clearOverrides() {
    overrides_.clear();
    ++revision_;
}

Json ActionRegistry::overridesToJson() const {
    Json j = Json::object();
    for (const auto& [id, chord] : overrides_) j.set(id, chord);
    return j;
}

void ActionRegistry::overridesFromJson(const Json& j) {
    overrides_.clear();
    ++revision_;
    if (!j.isObject()) return;
    for (const auto& [id, chord] : j.members())
        if (chord.isString()) overrides_.emplace_back(id, chord.asString());
}

void ActionRegistry::touch(std::string_view id) {
    recent_.erase(std::remove(recent_.begin(), recent_.end(), std::string(id)), recent_.end());
    recent_.insert(recent_.begin(), std::string(id));
    if (recent_.size() > 20) recent_.resize(20);
}

void ActionRegistry::registerCommands(const CommandRegistry& commands, std::function<void(const std::string&)> run,
                                      std::function<void(const std::string&)> prefill) {
    for (const Command& c : commands.all()) {
        const bool needsArgs = std::any_of(c.params.begin(), c.params.end(), [](const ParamSpec& p) { return p.required; });
        Action a;
        a.id = "cmd." + c.name;
        a.label = humanize(c.name) + (needsArgs ? "..." : "");
        a.description = c.description;
        a.palette = "Script";
        a.command = c.name;
        const std::string name = c.name;
        if (needsArgs) a.run = [prefill, name] { prefill(name); };
        else a.run = [run, name] { run(name); };
        if (const Action* existing = find(a.id)) { // keep a shortcut someone gave it
            a.shortcut = existing->shortcut;
            if (existing->icon) a.icon = existing->icon;
        }
        add(std::move(a));
    }
}

} // namespace sf
