// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Actions.h"

#include "core/Commands.h"
#include "core/LegacyNames.h"

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

// "part_append" -> "Part append": the command's words, with the interface's names for what they name.
std::string humanize(const std::string& name) {
    static const std::pair<std::string_view, std::string_view> kWords[] = {
        {"forgesphere", "forge sphere"}, {"forgespheres", "forge spheres"}, {"facegroup", "face group"},
        {"facegroups", "face groups"},   {"vertexpaint", "vertex paint"},   {"qremesh", "quad remesh"},
        {"litsphere", "lit sphere"},     {"litspheres", "lit spheres"},     {"quicksave", "snapshot"}};
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
    for (const auto& [id, chord] : styleDefaults_)
        if (id == a.id) a.shortcut = chord;
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
    if (legacy::isOld(id)) return find(legacy::modernize(id)); // an id an older version saved
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

const Action* ActionRegistry::actionForKey(SDL_Keycode key, SDL_Keymod mods) const {
    if (isModifierKey(key)) return nullptr;
    Chord chord = chordFromEvent(key, mods);
    const Action* a = actionForChord(chord);
    if (!a && !chord.ctrl && (chord.shift || chord.alt)) {
        // Exact chords win; otherwise plain keys ignore Shift and Alt as they always have (Shift+[ while
        // smoothing still resizes the brush).
        chord.shift = chord.alt = false;
        a = actionForChord(chord);
    }
    return a;
}

int brushSizeStep(const Action* a) {
    if (!a) return 0;
    if (a->id == "brush.size_down" || a->id == "brush.size_down_fine") return -1;
    if (a->id == "brush.size_up" || a->id == "brush.size_up_fine") return 1;
    return 0;
}

bool keyNavMotion(const Action* a, float& panX, float& panY, float& zoomSteps) {
    panX = panY = zoomSteps = 0.0f;
    if (!a) return false;
    if (a->id == "view.pan_left") panX = -kKeyNavPanPixels;
    else if (a->id == "view.pan_right") panX = kKeyNavPanPixels;
    else if (a->id == "view.pan_up") panY = -kKeyNavPanPixels;
    else if (a->id == "view.pan_down") panY = kKeyNavPanPixels;
    else if (a->id == "view.zoom_in") zoomSteps = kKeyNavZoomSteps;
    else if (a->id == "view.zoom_out") zoomSteps = -kKeyNavZoomSteps;
    else return false;
    return true;
}

bool ActionRegistry::handleKey(SDL_Keycode key, SDL_Keymod mods) {
    const Action* a = actionForKey(key, mods);
    if (!a || !a->run) return false;
    if (a->enabled && !a->enabled()) return true; // bound, just not available right now
    // Copy: running an action may register or replace actions.
    const std::function<void()> run = a->run;
    run();
    return true;
}

void ActionRegistry::setDefaultShortcut(std::string_view id, std::string chord) {
    auto it = std::find_if(styleDefaults_.begin(), styleDefaults_.end(), [&](const auto& d) { return d.first == id; });
    if (it == styleDefaults_.end()) styleDefaults_.emplace_back(std::string(id), chord);
    else it->second = chord;
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

std::vector<const Action*> ActionRegistry::holdersOf(const Chord& chord, std::string_view exceptId) const {
    std::vector<const Action*> out;
    for (const auto& other : actions_) {
        if (other.id == exceptId || !other.run) continue;
        const std::vector<Chord> theirs = parseChords(shortcutText(other.id));
        if (std::find(theirs.begin(), theirs.end(), chord) != theirs.end()) out.push_back(&other);
    }
    return out;
}

std::string ActionRegistry::resetShortcut(std::string_view id) {
    const Action* a = find(id);
    if (!a) return {};
    std::string lost;
    for (const Chord& c : parseChords(a->shortcut))
        for (const Action* other : holdersOf(c, a->id)) {
            std::string keep;
            for (const Chord& t : parseChords(shortcutText(other->id)))
                if (!(t == c)) keep += (keep.empty() ? "" : " / ") + chordText(t);
            lost += (lost.empty() ? "" : ", ") + other->label;
            setShortcut(other->id, keep);
        }
    for (auto it = overrides_.begin(); it != overrides_.end(); ++it)
        if (it->first == a->id) {
            overrides_.erase(it);
            break;
        }
    ++revision_;
    return lost;
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

namespace {
// "A then S / F5" -> "F5": the alternatives of a saved shortcut without its two-key ones (0.12.8 to 0.12.10 wrote them).
std::string withoutTwoKey(const std::string& text, bool& dropped) {
    std::string kept;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find(" / ", pos);
        if (end == std::string::npos) end = text.size();
        const std::string alt = text.substr(pos, end - pos);
        if (lower(alt).find(" then ") != std::string::npos) {
            dropped = true;
        } else {
            if (!kept.empty()) kept += " / ";
            kept += alt;
        }
        pos = end + 3;
    }
    return kept;
}
} // namespace

void ActionRegistry::overridesFromJson(const Json& j) {
    overrides_.clear();
    droppedTwoKey_ = 0;
    ++revision_;
    if (!j.isObject()) return;
    for (const auto& [id, chord] : j.members())
        if (chord.isString()) {
            bool dropped = false;
            std::string keys = withoutTwoKey(chord.asString(), dropped);
            if (dropped) ++droppedTwoKey_;
            overrides_.emplace_back(legacy::modernize(id), std::move(keys));
        }
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
