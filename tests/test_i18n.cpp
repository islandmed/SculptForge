// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The interface language (#44, app/I18n.h): exact lookups, the English fallback, labels whose "##id" must not
// change, icon prefixes and printf tails, and a Spanish table that is safe to hand to printf.
#include "TestHarness.h"

#include "app/I18n.h"

#include <cstdio>
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

// The printf conversions of a format string, in order ("%d", "%.1f", "%%" ...).
std::vector<std::string> conversions(std::string_view s) {
    std::vector<std::string> out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '%') continue;
        size_t j = i + 1;
        while (j < s.size() && std::string_view("-+ #0123456789.*hlzjt").find(s[j]) != std::string_view::npos) ++j;
        if (j < s.size()) out.emplace_back(s.substr(i, j - i + 1));
        i = j;
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
    CHECK(std::string(tr("Draw Size %.0f")) == "Tamaño de dibujo %.0f");
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
}

TEST(i18n_spanish_table_is_well_formed) {
    std::set<std::string_view> seen;
    int problems = 0;
    for (const i18n::Entry& e : i18n::spanishTable()) {
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

TEST(i18n_spanish_table_fits_the_font) {
    // The UI font is Inter rasterised with ImGui's default ranges, Basic Latin + Latin-1 (U+0020-U+00FF), plus the
    // Lucide icons (U+E000-U+F8FF): every character of the table has to be in those, or it draws as "?".
    int outside = 0;
    for (const i18n::Entry& e : i18n::spanishTable()) {
        const std::string_view s = e.translated;
        for (size_t i = 0; i < s.size();) {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            unsigned cp = c;
            size_t n = 1;
            if (c >= 0xF0) n = 4, cp = c & 0x07;
            else if (c >= 0xE0) n = 3, cp = c & 0x0F;
            else if (c >= 0xC0) n = 2, cp = c & 0x1F;
            for (size_t k = 1; k < n && i + k < s.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
            const bool ok = cp == '\n' || (cp >= 0x20 && cp <= 0x7E) || (cp >= 0xA0 && cp <= 0xFF) || (cp >= 0xE000 && cp <= 0xF8FF);
            if (!ok) {
                std::printf("  U+%04X is not in the UI font: \"%s\"\n", cp, e.translated);
                ++outside;
            }
            i += n;
        }
    }
    CHECK(outside == 0);
}
