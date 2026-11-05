// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The interface language (#44, app/I18n.h): exact lookups, the English fallback, labels whose "##id" must not
// change, icon prefixes and printf tails, and Spanish and Traditional Chinese tables that are safe to hand to printf
// and that the UI fonts can draw.
#include "TestHarness.h"

#include "app/I18n.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <string_view>
#include <vector>

using namespace sf;

namespace {

// Restores the language when a test ends, so the other tests run in English.
struct LanguageScope {
    Language saved = language();
    explicit LanguageScope(Language lang) { setLanguage(lang); }
    ~LanguageScope() { setLanguage(saved); }
};

// The printf conversions of a format string, in order ("%d", "%.1f", "%%" ...). A '%' right after a digit ("10% smaller",
// in help text no one hands to printf) is a plain percent sign; "50%%" is still a conversion.
std::vector<std::string> conversions(std::string_view s) {
    std::vector<std::string> out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '%') continue;
        if (i > 0 && s[i - 1] >= '0' && s[i - 1] <= '9' && (i + 1 >= s.size() || s[i + 1] != '%')) continue;
        size_t j = i + 1;
        while (j < s.size() && std::string_view("-+ #0123456789.*hlzjt").find(s[j]) != std::string_view::npos) ++j;
        if (j < s.size()) out.emplace_back(s.substr(i, j - i + 1));
        i = j;
    }
    return out;
}

// The code points of a UTF-8 string.
std::vector<unsigned> codepoints(std::string_view s) {
    std::vector<unsigned> out;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        unsigned cp = c;
        size_t n = 1;
        if (c >= 0xF0) n = 4, cp = c & 0x07;
        else if (c >= 0xE0) n = 3, cp = c & 0x0F;
        else if (c >= 0xC0) n = 2, cp = c & 0x1F;
        for (size_t k = 1; k < n && i + k < s.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        out.push_back(cp);
        i += n;
    }
    return out;
}

// In Inter as the UI rasterises it (Basic Latin + Latin-1) or the Lucide icons (U+E000-U+F8FF).
bool inLatinFont(unsigned cp) {
    return cp == '\n' || (cp >= 0x20 && cp <= 0x7E) || (cp >= 0xA0 && cp <= 0xFF) || (cp >= 0xE000 && cp <= 0xF8FF);
}

// The BMP code points an OpenType font maps to a glyph (its format 4 cmap), read straight from the file.
std::set<unsigned> fontCodepoints(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    const std::string d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto u16 = [&](size_t o) { return o + 2 <= d.size() ? unsigned(uint8_t(d[o])) << 8 | uint8_t(d[o + 1]) : 0u; };
    auto u32 = [&](size_t o) { return u16(o) << 16 | u16(o + 2); };
    std::set<unsigned> out;
    size_t cmap = 0;
    for (unsigned t = 0, n = u16(4); t < n; ++t)
        if (d.compare(12 + t * 16, 4, "cmap") == 0) cmap = u32(12 + t * 16 + 8);
    if (!cmap) return out;
    for (unsigned t = 0, n = u16(cmap + 2); t < n; ++t) {
        const size_t sub = cmap + u32(cmap + 4 + t * 8 + 4);
        if (u16(sub) != 4) continue;
        const unsigned segs = u16(sub + 6) / 2;
        const size_t ends = sub + 14, starts = ends + segs * 2 + 2, deltas = starts + segs * 2, offsets = deltas + segs * 2;
        for (unsigned i = 0; i < segs; ++i) {
            const unsigned end = u16(ends + i * 2), start = u16(starts + i * 2), delta = u16(deltas + i * 2);
            const unsigned ro = u16(offsets + i * 2);
            for (unsigned c = start; c <= end && c != 0xFFFF; ++c) {
                unsigned glyph = 0;
                if (ro == 0) glyph = (c + delta) & 0xFFFF;
                else if (const unsigned g = u16(offsets + i * 2 + ro + (c - start) * 2)) glyph = (g + delta) & 0xFFFF;
                if (glyph) out.insert(c);
            }
        }
        break;
    }
    return out;
}

} // namespace

TEST(i18n_english_returns_the_argument) {
    LanguageScope scope(Language::English);
    const char* text = "Decimate##remesh";
    CHECK(tr(text) == text); // the very same pointer: no copy, no lookup
    CHECK(tr(std::string("Undo")) == "Undo");
    CHECK(trPath("Tool > Geometry") == "Tool > Geometry");
    CHECK(tr(static_cast<const char*>(nullptr)) == nullptr);
}

TEST(i18n_spanish_lookup_and_fallback) {
    LanguageScope scope(Language::Spanish);
    CHECK(std::string(tr("Preferences")) == "Preferencias");
    CHECK(std::string(tr("Undo")) == "Deshacer");
    CHECK(std::string(tr("Masking")) == "Enmascarado");
    // No entry: the English text, unchanged (and the same pointer).
    const char* missing = "Frobnicate the widget";
    CHECK(tr(missing) == missing);
    CHECK(std::string(tr("Unknown##some.id")) == "Unknown##some.id");
    CHECK(std::string(tr("")) == "");
    // Brand and tool names stay as they are.
    CHECK(std::string(tr("Forge Spheres")) == "Forge Spheres");
    CHECK(std::string(tr("Quad Remesh")) == "Quad Remesh");
    CHECK(std::string(tr("Library")) == "Biblioteca");
    CHECK(std::string(tr("Part")) == "Pieza");
    // Brush names are never translated, even where the word has an entry ("Move" the tool is "Mover").
    const std::string move = "Move";
    CHECK(std::string(trAction("brush.move", move)) == "Move");
    const std::string on = "On"; // a brush setting, not a brush: translated
    CHECK(std::string(trAction("brush.pen_size_on", on)) == "Activado");
    CHECK(std::string(trAction("tool.move", move)) == "Mover");
    CHECK(trPath("Tool > Geometry") == "Herramienta > Geometría");
    CHECK(trPath("Tool > Frobnicate") == "Herramienta > Frobnicate");
}

TEST(i18n_keeps_ids_icons_and_tails) {
    LanguageScope scope(Language::Spanish);
    // Everything from "##" on is kept exactly, so the ImGui id of the item stays the same.
    CHECK(std::string(tr("Decimate##decimate")) == "Diezmar##decimate");
    CHECK(std::string(tr("Keep Masked##remesh")) == "Conservar máscara##remesh");
    CHECK(std::string(tr("Show Face Groups##render")) == "Mostrar grupos de caras##render");
    CHECK(std::string(tr("Display###window.display")) == "Visualización###window.display");
    CHECK(std::string(tr("##only.an.id")) == "##only.an.id");
    // An icon in front is kept, the words after it translated.
    CHECK(std::string(tr("\xee\x80\xb8  Filter")) == "\xee\x80\xb8  Filtrar");
    // "..." and ":" endings, and printf tails, when there is no exact entry.
    CHECK(std::string(tr("Save As...")) == "Guardar como...");
    CHECK(std::string(tr("Open...##file.open")) == "Abrir...##file.open");
    CHECK(std::string(tr("Shelf tiles:")) == "Fichas del estante:");
    CHECK(std::string(tr("Size %.0f")) == "Tamaño de dibujo %.0f");
    CHECK(std::string(tr("Resolution %.0f")) == "Resolución %.0f");
    // An exact entry wins over the pieces.
    CHECK(std::string(tr("Every %d min")) == "Cada %d min");
    // The pointer stays valid and the same on later calls (labels are asked for every frame).
    const char* first = tr("Decimate##decimate");
    CHECK(first == tr("Decimate##decimate"));
    // Back in English the original text comes back.
    setLanguage(Language::English);
    CHECK(std::string(tr("Decimate##decimate")) == "Decimate##decimate");
}

TEST(i18n_language_codes) {
    CHECK(std::string(languageCode(Language::English)) == "en");
    CHECK(std::string(languageCode(Language::Spanish)) == "es");
    CHECK(languageFromCode("es") == Language::Spanish);
    CHECK(languageFromCode("es-MX") == Language::Spanish);
    CHECK(languageFromCode("en") == Language::English);
    CHECK(languageFromCode("klingon") == Language::English);
    CHECK(languageFromCode("") == Language::English);
    CHECK(std::string(languageName(Language::Spanish)) == "Espa\xc3\xb1ol");
    CHECK(std::string(languageCode(Language::TraditionalChinese)) == "zh-TW");
    CHECK(languageFromCode("zh-TW") == Language::TraditionalChinese);
    CHECK(languageFromCode("zh_TW") == Language::TraditionalChinese);
    CHECK(languageFromCode("zh-HK") == Language::TraditionalChinese);
    CHECK(languageFromCode("zh-Hant-TW") == Language::TraditionalChinese);
    CHECK(languageFromCode("zh-CN") == Language::English); // Simplified: no table yet
    CHECK(languageFromCode("zh") == Language::English);
    CHECK(languageFromCode("esperanto") == Language::English);
    CHECK(languageFromCode(languageCode(Language::TraditionalChinese)) == Language::TraditionalChinese);
}

TEST(i18n_tables_are_well_formed) {
  for (const Language lang : {Language::Spanish, Language::TraditionalChinese}) {
    std::set<std::string_view> seen;
    int problems = 0;
    for (const i18n::Entry& e : i18n::table(lang)) {
        const std::string_view en = e.english, es = e.translated;
        // One entry per English text, both sides non-empty, no ImGui ids in the table (tr() keeps those).
        if (!seen.insert(en).second || en.empty() || es.empty() || en.find("##") != std::string_view::npos ||
            es.find("##") != std::string_view::npos) {
            std::printf("  bad entry: \"%.*s\"\n", int(en.size()), en.data());
            ++problems;
        }
        // Formats are handed to printf: the translation has the same conversions, in the same order.
        if (conversions(en) != conversions(es)) {
            std::printf("  printf conversions differ: \"%.*s\"\n", int(en.size()), en.data());
            ++problems;
        }
        // Leading and trailing spaces line up with the English (they are layout).
        if ((en.front() == ' ') != (es.front() == ' ') || (en.back() == ' ') != (es.back() == ' ')) {
            std::printf("  spacing differs: \"%.*s\"\n", int(en.size()), en.data());
            ++problems;
        }
    }
    CHECK(problems == 0);
    CHECK(seen.size() > 300);
  }
}

TEST(i18n_spanish_table_fits_the_font) {
    // The UI font is Inter rasterised with ImGui's default ranges, Basic Latin + Latin-1 (U+0020-U+00FF), plus the
    // Lucide icons (U+E000-U+F8FF): every character of the table has to be in those, or it draws as "?".
    int outside = 0;
    for (const i18n::Entry& e : i18n::spanishTable())
        for (const unsigned cp : codepoints(e.translated))
            if (!inLatinFont(cp)) {
                std::printf("  U+%04X is not in the UI font: \"%s\"\n", cp, e.translated);
                ++outside;
            }
    CHECK(outside == 0);
}

TEST(i18n_chinese_table_fits_the_font) {
    // Chinese text is Inter, the icons, and assets/fonts/NotoSansTC-UI.otf merged in (App::mergeCjkFont): a subset with
    // only the characters the table uses. A character it lacks draws as "?": run tools/i18n/subset_cjk_font.py.
    const std::set<unsigned> cjk = fontCodepoints(std::string(SF_TEST_DATA_DIR) + "/../../assets/fonts/NotoSansTC-UI.otf");
    CHECK(cjk.size() > 500);
    std::set<unsigned> missing;
    for (const i18n::Entry& e : i18n::traditionalChineseTable())
        for (const unsigned cp : codepoints(e.translated))
            if (!inLatinFont(cp) && !cjk.count(cp) && missing.insert(cp).second)
                std::printf("  U+%04X is not in NotoSansTC-UI.otf: \"%s\"\n", cp, e.translated);
    // The Preferences button shows the language's own name in every language (App::mergeCjkFont's kNameRanges).
    for (const unsigned cp : codepoints(languageName(Language::TraditionalChinese)))
        if (!cjk.count(cp)) missing.insert(cp);
    CHECK(missing.empty());
}

TEST(i18n_chinese_lookup_keeps_ids_and_brush_names) {
    LanguageScope scope(Language::TraditionalChinese);
    CHECK(std::string(tr("Preferences##palette.preferences")) == "\u504f\u597d\u8a2d\u5b9a##palette.preferences");
    CHECK(std::string(tr("Open...##file.open")) == "\u958b\u555f...##file.open");
    CHECK(std::string(tr("Frobnicate")) == "Frobnicate"); // no entry: English
    const std::string move = "Move";
    CHECK(std::string(trAction("brush.move", move)) == "Move");
    const std::string on = "On"; // a brush setting, not a brush: translated
    CHECK(std::string(trAction("brush.pen_size_on", on)) == "Activado");
    CHECK(trPath("Tool > Geometry") == "\u5de5\u5177 > \u5e7e\u4f55");
}
