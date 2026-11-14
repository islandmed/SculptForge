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

bool parseSequence(std::string_view text, KeySequence& out) {
    out = KeySequence{};
    const size_t at = text.find(kThen);
    if (at == std::string_view::npos) return false;
    if (!parseChord(text.substr(0, at), out.first) || !parseChord(text.substr(at + kThen.size()), out.second)) return false;
    return out.first.key != out.second.key; // two different keys: no double tap
}

std::string sequenceText(const KeySequence& s) { return chordText(s.first) + std::string(kThen) + chordText(s.second); }

std::vector<std::string> shortcutSteps(std::string_view alternative) {
    const size_t at = alternative.find(kThen);
    if (at == std::string_view::npos) return {std::string(alternative)};
    return {std::string(alternative.substr(0, at)), std::string(alternative.substr(at + kThen.size()))};
}

std::string canonicalShortcut(std::string_view alternative) {
    KeySequence seq;
    if (parseSequence(alternative, seq)) return sequenceText(seq);
    Chord c;
    return parseChord(alternative, c) ? chordText(c) : std::string();
}

std::vector<std::string> shortcutAlternatives(std::string_view text) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t at = text.find(" / ", start);
        const std::string c = canonicalShortcut(text.substr(start, at == std::string_view::npos ? std::string_view::npos : at - start));
        if (!c.empty()) out.push_back(c);
        if (at == std::string_view::npos) break;
        start = at + 3;
    }
    return out;
}

namespace {

std::string joinShortcuts(const std::vector<std::string>& alternatives) {
    std::string out;
    for (const std::string& a : alternatives) out += (out.empty() ? "" : " / ") + a;
    return out;
}

} // namespace

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
    if (const std::string now = legacy::modernizeId(id); now != id) return find(now); // an id an older version saved
    return nullptr;
}

void ActionRegistry::rebuildChords() const {
    if (chordsRevision_ == revision_ && chords_.size() == actions_.size()) return;
    chords_.clear();
    sequences_.clear();
    for (const auto& a : actions_) {
        const std::string text = shortcutText(a.id);
        chords_.push_back(parseChords(text));
        std::vector<KeySequence> seqs;
        for (const std::string& alt : shortcutAlternatives(text))
            if (KeySequence seq; parseSequence(alt, seq)) seqs.push_back(seq);
        sequences_.push_back(std::move(seqs));
    }
    chordsRevision_ = revision_;
}

const std::vector<Chord>& ActionRegistry::chordsOf(size_t index) const {
    rebuildChords();
    return chords_[index];
}

const Action* ActionRegistry::actionForSequence(const KeySequence& sequence) const {
    rebuildChords();
    for (size_t i = 0; i < actions_.size(); ++i)
        for (const KeySequence& s : sequences_[i])
            if (s == sequence) return &actions_[i];
    return nullptr;
}

bool ActionRegistry::isSequenceStart(const Chord& chord) const {
    rebuildChords();
    for (const auto& seqs : sequences_)
        for (const KeySequence& s : seqs)
            if (s.first == chord) return true;
    return false;
}

bool ActionRegistry::isSequenceEnd(const Chord& chord) const {
    rebuildChords();
    for (const auto& seqs : sequences_)
        for (const KeySequence& s : seqs)
            if (s.second == chord) return true;
    return false;
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

bool ActionRegistry::runAction(const Action* a) {
    if (!a || !a->run) return false;
    if (a->enabled && !a->enabled()) return true; // bound, just not available right now
    // Copy: running an action may register or replace actions.
    const std::function<void()> run = a->run;
    run();
    return true;
}

void ActionRegistry::flushPending() {
    if (!pending_.on) return;
    const std::string id = pending_.aloneId;
    const SDL_Keycode key = pending_.first.key;
    pending_ = {};
    if (id.empty()) return;
    if (aloneHook_) aloneHook_(key, true);
    runAction(find(id));
    if (aloneHook_) aloneHook_(key, false);
}

void ActionRegistry::tick(float dt) {
    clock_ += double(dt);
    if (pending_.on && clock_ >= pending_.until) flushPending();
}

bool ActionRegistry::handleKey(SDL_Keycode key, SDL_Keymod mods, bool repeat) {
    if (isModifierKey(key)) return false; // Shift or Ctrl pressed on the way to the second key changes nothing
    const Chord chord = chordFromEvent(key, mods);
    if (repeat && pending_.on && pending_.first.key == key) return true; // C held: its repeats are not a second press
    if (pending_.on) {
        const Chord first = pending_.first;
        const bool inTime = clock_ < pending_.until;
        if (inTime) {
            // The waiting key then this one; or this key then the waiting one, when the waiting key is still held
            // (V went down before C, both down together: "C then V").
            const Action* seq = actionForSequence({first, chord});
            if (!seq && pending_.down) seq = actionForSequence({chord, first});
            if (seq) {
                pending_ = {}; // the sequence replaces both keys' own actions
                runAction(seq);
                return true;
            }
        }
        flushPending(); // not the second key (or too late): the first key alone, then this key as usual
    }
    if (isSequenceStart(chord) || isSequenceEnd(chord)) { // (the ending key waits for a key that begins it, pressed with it)
        const Action* alone = actionForKey(key, mods);
        pending_ = {true, chord, true, clock_ + double(window_), alone && alone->run ? alone->id : std::string()};
        return true;
    }
    return runAction(actionForKey(key, mods));
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

std::string ActionRegistry::assignShortcut(std::string_view id, const std::string& shortcut, bool keepOthers) {
    const std::string mine = canonicalShortcut(shortcut);
    if (mine.empty()) {
        if (!keepOthers) setShortcut(id, "");
        return {};
    }
    std::string lost;
    for (const auto& other : actions_) {
        if (other.id == id) continue;
        std::vector<std::string> theirs = shortcutAlternatives(shortcutText(other.id));
        const size_t before = theirs.size();
        theirs.erase(std::remove(theirs.begin(), theirs.end(), mine), theirs.end());
        if (theirs.size() == before) continue;
        lost = other.label;
        setShortcut(other.id, joinShortcuts(theirs));
    }
    std::vector<std::string> kept{mine};
    if (keepOthers)
        for (const std::string& a : shortcutAlternatives(shortcutText(id)))
            if (a != mine) kept.push_back(a);
    setShortcut(id, joinShortcuts(kept));
    return lost;
}

bool ActionRegistry::hasOverride(std::string_view id) const {
    for (const auto& o : overrides_)
        if (o.first == id) return true;
    return false;
}

std::vector<const Action*> ActionRegistry::holdersOf(const Chord& chord, std::string_view exceptId) const {
    return holdersOfShortcut(chordText(chord), exceptId);
}

std::vector<const Action*> ActionRegistry::holdersOfShortcut(std::string_view alternative, std::string_view exceptId) const {
    std::vector<const Action*> out;
    const std::string want = canonicalShortcut(alternative);
    if (want.empty()) return out;
    for (const auto& other : actions_) {
        if (other.id == exceptId || !other.run) continue;
        const std::vector<std::string> theirs = shortcutAlternatives(shortcutText(other.id));
        if (std::find(theirs.begin(), theirs.end(), want) != theirs.end()) out.push_back(&other);
    }
    return out;
}

std::string ActionRegistry::resetShortcut(std::string_view id) {
    const Action* a = find(id);
    if (!a) return {};
    std::string lost;
    for (const std::string& alt : shortcutAlternatives(a->shortcut))
        for (const Action* other : holdersOfShortcut(alt, a->id)) {
            std::vector<std::string> keep = shortcutAlternatives(shortcutText(other->id));
            keep.erase(std::remove(keep.begin(), keep.end(), alt), keep.end());
            lost += (lost.empty() ? "" : ", ") + other->label;
            setShortcut(other->id, joinShortcuts(keep));
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

void ActionRegistry::overridesFromJson(const Json& j) {
    overrides_.clear();
    ++revision_;
    if (!j.isObject()) return;
    for (const auto& [id, chord] : j.members())
        if (chord.isString()) {
            overrides_.emplace_back(legacy::modernizeId(id), chord.asString());
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
