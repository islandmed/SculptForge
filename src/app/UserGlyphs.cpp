// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/UserGlyphs.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#if defined(__linux__) || defined(__FreeBSD__)
#include <dlfcn.h>
#endif

// stb_truetype (with ImGui) only to ask a font whether it has a character, before the atlas merges it.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wshadow"
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include <imstb_truetype.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

// Called by ImFont::FindGlyph for a character the font lacks (cmake/Dependencies.cmake adds it to imgui_draw.cpp).
extern void (*GImFontMissingGlyph)(ImFont* font, ImWchar c);

namespace sf::glyphs {
namespace {

constexpr size_t kMaxSeen = 20000;       // characters looked up at most (binary junk pasted as a name)
constexpr int kMaxMerged = 2000;         // characters merged at most: the atlas is rebuilt per font size with them
constexpr uintmax_t kMaxFileBytes = 160u << 20; // a system font file read whole (PingFang is ~80 MB)

struct Source {
    std::vector<unsigned char> owned; // a system font file (the embedded ones point into the binary)
    const unsigned char* data = nullptr;
    size_t size = 0;
    int index = 0; // the face in a collection (.ttc)
    stbtt_fontinfo info{};
    std::vector<ImWchar> found;  // characters this source gives, sorted
    std::vector<ImWchar> ranges; // `found` as ImGui ranges, 0-terminated
    bool has(ImWchar c) const { return stbtt_FindGlyphIndex(&info, int(c)) != 0; }
};

struct Candidate {
    std::string path;
    int index = 0;
};

struct State {
    bool testFonts = false;
    std::vector<std::unique_ptr<Source>> sources; // embedded first, then system fonts in the order they were needed
    std::unordered_set<std::string> opened;       // candidate files tried (loaded or not)
    std::unordered_set<ImWchar> seen;
    std::vector<ImWchar> waiting;
    int missingThisFrame = 0, missingLastFrame = 0, merged = 0;
};

State& state() {
    static State s;
    return s;
}

void onMissing(ImFont* font, ImWchar c) {
    if (!font->FallbackGlyph) return;  // the atlas build probing the font (a space in the icons-only font)
    if (c < 0x20 || c == 0x7F) return; // control characters (tab, newline) are never drawn
    State& s = state();
    ++s.missingThisFrame;
    if (c == 0xFFFD) return; // broken UTF-8, not a character: fix the text, don't load a font for it
    if (s.seen.size() < kMaxSeen && s.seen.insert(c).second) s.waiting.push_back(c);
}

std::unique_ptr<Source> makeSource(const unsigned char* data, size_t size, int index) {
    auto src = std::make_unique<Source>();
    src->data = data;
    src->size = size;
    src->index = index;
    const int offset = stbtt_GetFontOffsetForIndex(data, index);
    if (offset < 0 || size_t(offset) >= size || !stbtt_InitFont(&src->info, data, offset)) return nullptr;
    return src;
}

Source* loadFile(const Candidate& c) {
    State& s = state();
    const std::string key = c.path + "#" + std::to_string(c.index);
    if (!s.opened.insert(key).second) return nullptr;
    const std::filesystem::path path(std::u8string(c.path.begin(), c.path.end()));
    std::error_code ec;
    const uintmax_t bytes = std::filesystem::file_size(path, ec);
    if (ec || bytes < 12 || bytes > kMaxFileBytes) return nullptr;
    std::vector<unsigned char> data(static_cast<size_t>(bytes));
    std::ifstream in(path, std::ios::binary);
    if (!in.read(reinterpret_cast<char*>(data.data()), std::streamsize(data.size()))) return nullptr;
    auto src = makeSource(data.data(), data.size(), c.index);
    if (!src) return nullptr;
    src->owned = std::move(data); // (the vector's buffer moves with it: data stays valid)
    SDL_Log("User text font: %s", c.path.c_str());
    s.sources.push_back(std::move(src));
    return s.sources.back().get();
}

// Font files that may have the character, best first, for this platform.
std::vector<Candidate> systemCandidates(bool traditional, size_t& tested) {
    std::vector<Candidate> out;
    if (state().testFonts) // UI tests: the same font on every machine, before the system's
        if (const char* list = std::getenv("SF_FALLBACK_FONTS")) {
            const std::string all = list;
            for (size_t a = 0; a <= all.size();) {
                const size_t b = std::min(all.find(';', a), all.size());
                if (b > a) out.push_back({all.substr(a, b - a), 0});
                a = b + 1;
            }
        }
    tested = out.size();
#if defined(_WIN32)
    const char* windir = std::getenv("WINDIR");
    const std::string dir = std::string(windir && *windir ? windir : "C:\\Windows") + "\\Fonts\\";
    const char* zh[] = {"msyh.ttc", "msjh.ttc"}; // Microsoft YaHei (Simplified), JhengHei (Traditional)
    out.push_back({dir + zh[traditional ? 1 : 0]});
    out.push_back({dir + zh[traditional ? 0 : 1]});
    for (const char* f : {"YuGothR.ttc", "meiryo.ttc", "malgun.ttf", "simsun.ttc", "simsunb.ttf", "mingliub.ttc",
                          "segoeui.ttf", "seguisym.ttf"})
        out.push_back({dir + f}); // Yu Gothic, Meiryo, Malgun Gothic, SimSun (+ Ext-B), Segoe UI (+ Symbol)
#elif defined(__APPLE__)
    out.push_back({"/System/Library/Fonts/PingFang.ttc", traditional ? 1 : 0}); // (faces: SC, TC, HK...; older macOS)
    out.push_back({traditional ? "/System/Library/Fonts/STHeiti Light.ttc" : "/System/Library/Fonts/Hiragino Sans GB.ttc"});
    out.push_back({traditional ? "/System/Library/Fonts/Hiragino Sans GB.ttc" : "/System/Library/Fonts/STHeiti Light.ttc"});
    out.push_back({"/System/Library/Fonts/ヒラギノ角ゴシック W3.ttc"});
    out.push_back({"/System/Library/Fonts/AppleSDGothicNeo.ttc"});
    out.push_back({"/System/Library/Fonts/Supplemental/Arial Unicode.ttf"});
    out.push_back({"/Library/Fonts/Arial Unicode.ttf"});
#else
    // fontconfig picks per character (systemMatch); these are the usual packages' files when it isn't there.
    for (const char* f : {"/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
                          "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
                          "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
                          "/usr/share/fonts/google-noto-sans-cjk-fonts/NotoSansCJK-Regular.ttc",
                          "/usr/share/fonts/noto/NotoSansCJK-Regular.ttc",
                          "/usr/share/fonts/wenquanyi/wqy-microhei/wqy-microhei.ttc",
                          "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
                          "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf"})
        out.push_back({f, traditional ? 3 : 2}); // Noto Sans CJK's faces: JP KR SC TC HK (others: face 0 below)
    for (size_t i = tested; i < out.size(); ++i)
        if (out[i].path.find("NotoSansCJK") == std::string::npos) out[i].index = 0;
#endif
    return out;
}

#if defined(__linux__) || defined(__FreeBSD__)
// fontconfig (loaded at the first lookup, if the system has it): the file the desktop itself would draw `c` with.
bool systemMatch(ImWchar c, bool traditional, Candidate& out) {
    using Pattern = void;
    using CharSet = void;
    struct Fc {
        Pattern* (*nameParse)(const unsigned char*) = nullptr;
        CharSet* (*charSetCreate)() = nullptr;
        int (*charSetAddChar)(CharSet*, unsigned) = nullptr;
        void (*charSetDestroy)(CharSet*) = nullptr;
        int (*patternAddCharSet)(Pattern*, const char*, const CharSet*) = nullptr;
        int (*configSubstitute)(void*, Pattern*, int) = nullptr;
        void (*defaultSubstitute)(Pattern*) = nullptr;
        Pattern* (*fontMatch)(void*, Pattern*, int*) = nullptr;
        int (*patternGetString)(const Pattern*, const char*, int, unsigned char**) = nullptr;
        int (*patternGetInteger)(const Pattern*, const char*, int, int*) = nullptr;
        void (*patternDestroy)(Pattern*) = nullptr;
        bool ok = false;
    };
    static const Fc fc = [] {
        Fc f;
        void* h = dlopen("libfontconfig.so.1", RTLD_NOW | RTLD_LOCAL);
        if (!h) return f;
        auto sym = [h](auto& fn, const char* name) { fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(dlsym(h, name)); };
        sym(f.nameParse, "FcNameParse");
        sym(f.charSetCreate, "FcCharSetCreate");
        sym(f.charSetAddChar, "FcCharSetAddChar");
        sym(f.charSetDestroy, "FcCharSetDestroy");
        sym(f.patternAddCharSet, "FcPatternAddCharSet");
        sym(f.configSubstitute, "FcConfigSubstitute");
        sym(f.defaultSubstitute, "FcDefaultSubstitute");
        sym(f.fontMatch, "FcFontMatch");
        sym(f.patternGetString, "FcPatternGetString");
        sym(f.patternGetInteger, "FcPatternGetInteger");
        sym(f.patternDestroy, "FcPatternDestroy");
        f.ok = f.nameParse && f.charSetCreate && f.charSetAddChar && f.charSetDestroy && f.patternAddCharSet &&
               f.configSubstitute && f.defaultSubstitute && f.fontMatch && f.patternGetString && f.patternGetInteger &&
               f.patternDestroy;
        return f;
    }();
    if (!fc.ok) return false;
    const char* name = traditional ? "sans-serif:lang=zh-tw" : "sans-serif:lang=zh-cn";
    Pattern* p = fc.nameParse(reinterpret_cast<const unsigned char*>(name));
    if (!p) return false;
    CharSet* cs = fc.charSetCreate();
    fc.charSetAddChar(cs, unsigned(c));
    fc.patternAddCharSet(p, "charset", cs);
    fc.configSubstitute(nullptr, p, 0 /* FcMatchPattern */);
    fc.defaultSubstitute(p);
    int result = 0;
    Pattern* m = fc.fontMatch(nullptr, p, &result);
    bool ok = false;
    unsigned char* file = nullptr;
    int index = 0;
    if (m && fc.patternGetString(m, "file", 0, &file) == 0 && file) {
        fc.patternGetInteger(m, "index", 0, &index);
        out = {reinterpret_cast<const char*>(file), index & 0xFFFF}; // (high bits: a variable font's instance)
        ok = true;
    }
    if (m) fc.patternDestroy(m);
    fc.charSetDestroy(cs);
    fc.patternDestroy(p);
    return ok;
}
#else
bool systemMatch(ImWchar, bool, Candidate&) { return false; }
#endif

void rebuildRanges(Source& src) {
    std::sort(src.found.begin(), src.found.end());
    src.ranges.clear();
    for (ImWchar c : src.found) {
        if (!src.ranges.empty() && src.ranges.back() + 1 == c) src.ranges.back() = c;
        else src.ranges.insert(src.ranges.end(), {c, c});
    }
    src.ranges.push_back(0);
}

} // namespace

void install(const Embedded* embedded, int count, bool testFonts) {
    State& s = state();
    s.testFonts = testFonts;
    if (s.sources.empty())
        for (int i = 0; i < count; ++i)
            if (auto src = makeSource(embedded[i].data, embedded[i].size, 0)) s.sources.push_back(std::move(src));
    GImFontMissingGlyph = onMissing;
}

bool pending() { return !state().waiting.empty(); }

bool resolve(bool traditional) {
    State& s = state();
    if (s.waiting.empty()) return false;
    std::vector<ImWchar> waiting;
    waiting.swap(s.waiting);
    std::vector<Candidate> candidates; // listed at the first character no loaded font has
    size_t next = 0, tested = 0; // candidates tried; the first `tested` are SF_FALLBACK_FONTS
    bool listed = false;
    std::vector<Source*> changed;
    for (ImWchar c : waiting) {
        if (s.merged >= kMaxMerged) break;
        Source* found = nullptr;
        for (auto& src : s.sources)
            if (src->has(c)) {
                found = src.get();
                break;
            }
        if (!found && !listed) {
            candidates = systemCandidates(traditional, tested);
            listed = true;
        }
        auto tryFrom = [&](size_t end) {
            while (!found && next < end)
                if (Source* src = loadFile(candidates[next++]); src && src->has(c)) found = src;
        };
        tryFrom(tested); // (UI tests' fonts)
        Candidate match;
        if (!found && systemMatch(c, traditional, match))
            if (Source* src = loadFile(match); src && src->has(c)) found = src;
        tryFrom(candidates.size());
        if (!found) continue; // no font here has it: it stays "?", looked up once
        found->found.push_back(c);
        ++s.merged;
        if (std::find(changed.begin(), changed.end(), found) == changed.end()) changed.push_back(found);
    }
    for (Source* src : changed) rebuildRanges(*src);
    if (!changed.empty()) {
        std::string list; // (the first few, for the log)
        for (size_t i = 0; i < waiting.size() && i < 8; ++i) { char hex[16]; SDL_snprintf(hex, sizeof hex, " U+%04X", unsigned(waiting[i])); list += hex; }
        SDL_Log("Glyphs looked up:%s%s (%d merged in all)", list.c_str(), waiting.size() > 8 ? " ..." : "", s.merged);
    }
    return !changed.empty();
}

void discardPending() {
    State& s = state();
    for (ImWchar c : s.waiting) s.seen.erase(c);
    s.waiting.clear();
}

void merge(float px) {
    for (auto& src : state().sources) {
        if (src->found.empty()) continue;
        ImFontConfig cfg;
        cfg.MergeMode = true;
        cfg.FontDataOwnedByAtlas = false;
        cfg.OversampleH = 1; // as the bundled Chinese glyphs: dense, and oversampling only grows the atlas
        cfg.FontNo = src->index;
        ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(src->data), int(src->size), px, &cfg,
                                                   src->ranges.data());
    }
}

void endFrame() {
    State& s = state();
    s.missingLastFrame = s.missingThisFrame;
    s.missingThisFrame = 0;
}

int missingLastFrame() { return state().missingLastFrame; }
int mergedCount() { return state().merged; }

} // namespace sf::glyphs
