// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The interface language (I18n.h): exact lookups in the compiled-in table, and
// labels put together from a translated part and the parts that never change
// (an icon in front, "##id" behind, "..." or ":" at the end, a printf tail).
#include "app/I18n.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

namespace sf {

namespace {

std::atomic<int> g_language{int(Language::English)};

struct TransparentHash {
    using is_transparent = void;
    size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>{}(s); }
};

const std::unordered_map<std::string_view, const char*>& spanishMap() {
    static const std::unordered_map<std::string_view, const char*> map = [] {
        std::unordered_map<std::string_view, const char*> m;
        for (const i18n::Entry& e : i18n::spanishTable()) m.emplace(e.english, e.translated);
        return m;
    }();
    return map;
}

// Labels put together from a translation (or found to have none), per language, kept for the life of the program
// so the pointers tr() hands out stay valid. Capped: past it, new results go round a small ring instead.
struct Composed {
    bool translated = false;
    std::string text;
};
struct Cache {
    std::mutex mutex;
    std::array<std::unordered_map<std::string, Composed, TransparentHash, std::equal_to<>>, kLanguageCount> byLanguage;
    std::array<std::string, 256> ring;
    size_t ringNext = 0;
};
Cache& cache() {
    static Cache c;
    return c;
}
constexpr size_t kCacheLimit = 1u << 16;

// The translation of `text` appended to `out`: the exact entry, or one for the words before a trailing "...", ":"
// or printf conversion ("Draw Size %.0f" from "Draw Size"). False when there is none.
bool translateCore(Language lang, std::string_view text, std::string& out) {
    if (text.empty()) return false;
    if (const char* exact = i18n::lookup(lang, text)) {
        out += exact;
        return true;
    }
    auto withTail = [&](size_t headEnd, size_t tailStart) {
        const size_t mark = out.size();
        if (headEnd == 0 || !translateCore(lang, text.substr(0, headEnd), out)) {
            out.resize(mark);
            return false;
        }
        out += text.substr(tailStart);
        return true;
    };
    if (text.size() > 3 && text.substr(text.size() - 3) == "...") return withTail(text.size() - 3, text.size() - 3);
    if (text.size() > 1 && text.back() == ':') return withTail(text.size() - 1, text.size() - 1);
    if (const size_t pct = text.find('%'); pct != std::string_view::npos && pct > 0) {
        size_t head = pct;
        while (head > 0 && text[head - 1] == ' ') --head;
        return withTail(head, head);
    }
    return false;
}

// A label in parts: [icon + spaces][words][##id...]. Returns false when the words have no translation.
bool compose(Language lang, std::string_view label, std::string& out) {
    size_t start = 0;
    // Lucide icons (U+E038-U+E798) are UTF-8 0xEE xx xx; the label's words follow after spaces.
    while (start + 3 <= label.size() && static_cast<unsigned char>(label[start]) == 0xEE) {
        start += 3;
        while (start < label.size() && label[start] == ' ') ++start;
    }
    const size_t hash = label.find("##", start);
    const size_t end = hash == std::string_view::npos ? label.size() : hash;
    out.assign(label.substr(0, start));
    if (!translateCore(lang, label.substr(start, end - start), out)) return false;
    out += label.substr(end);
    return true;
}

} // namespace

void setLanguage(Language language) { g_language.store(int(language)); }

Language language() { return Language(g_language.load()); }

const char* languageCode(Language language) { return language == Language::Spanish ? "es" : "en"; }

Language languageFromCode(std::string_view code) {
    if (code == "es" || code.substr(0, 3) == "es-" || code.substr(0, 3) == "es_") return Language::Spanish;
    return Language::English;
}

const char* languageName(Language language) { return language == Language::Spanish ? "Español" : "English"; }

namespace i18n {

const char* lookup(Language language, std::string_view english) {
    if (language != Language::Spanish) return nullptr;
    const auto& map = spanishMap();
    const auto it = map.find(english);
    return it == map.end() ? nullptr : it->second;
}

} // namespace i18n

const char* tr(const char* english) {
    const Language lang = language();
    if (lang == Language::English || !english || !*english) return english;
    const std::string_view key(english);
    if (const char* exact = i18n::lookup(lang, key)) return exact;
    Cache& c = cache();
    std::lock_guard lock(c.mutex);
    auto& map = c.byLanguage[size_t(lang)];
    if (const auto it = map.find(key); it != map.end()) return it->second.translated ? it->second.text.c_str() : english;
    Composed composed;
    composed.translated = compose(lang, key, composed.text);
    if (map.size() < kCacheLimit) {
        const auto it = map.emplace(std::string(key), std::move(composed)).first;
        return it->second.translated ? it->second.text.c_str() : english;
    }
    if (!composed.translated) return english;
    std::string& slot = c.ring[c.ringNext++ % c.ring.size()];
    slot = std::move(composed.text);
    return slot.c_str();
}

std::string tr(const std::string& english) {
    if (language() == Language::English) return english;
    return tr(english.c_str());
}

const char* trAction(std::string_view actionId, const std::string& label) {
    if (actionId.substr(0, 6) == "brush.") return label.c_str();
    return tr(label.c_str());
}

std::string trPath(std::string_view path) {
    if (language() == Language::English) return std::string(path);
    std::string out;
    size_t start = 0;
    while (true) {
        const size_t sep = path.find(" > ", start);
        out += tr(std::string(path.substr(start, sep == std::string_view::npos ? std::string_view::npos : sep - start)));
        if (sep == std::string_view::npos) break;
        out += " > ";
        start = sep + 3;
    }
    return out;
}

} // namespace sf
