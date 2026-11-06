// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The interface language (#44): English, the default, Spanish, Traditional Chinese
// (#114) and Simplified Chinese. A small, dependency-free layer: the English text is
// the key, the translations are tables compiled into the binary (I18nSpanish.cpp,
// I18nTraditionalChinese.cpp, I18nSimplifiedChinese.cpp), and anything without an
// entry stays in English, so a missing line never breaks a label.
//
//   ImGui::Button(tr("Decimate"))             "Diezmar" in Spanish
//   ImGui::Checkbox(tr("Keep Masked##remesh")) only "Keep Masked" is translated; "##remesh" is kept as it is
//   tr(ICON_LC_SAVE " Save")                    the icon prefix is kept, the words translated
//   tr("Size %.0f")                        an exact entry first, else "Size" and " %.0f" kept
//   tr("Save As...")                            an exact entry first, else "Save As" + "..."
//
// tr() never changes anything in English (it returns its argument), so UI tests
// and layouts keyed by English labels keep working. In Spanish the visible part
// changes and everything from "##" on stays the same.
#pragma once

#include <span>
#include <string>
#include <string_view>

namespace sf {

enum class Language { English, Spanish, TraditionalChinese, SimplifiedChinese };
inline constexpr int kLanguageCount = 4;

// The language tr() translates into (process-wide; Preferences > Interface sets it).
void setLanguage(Language language);
Language language();
const char* languageCode(Language language);    // "en", "es", "zh-TW", "zh-CN" (ui.json)
Language languageFromCode(std::string_view code); // unknown codes are English
const char* languageName(Language language);    // in that language: "English", "Español", "繁體中文", "简体中文"
// True for a language whose text needs the CJK font merged into the UI fonts (App::buildUiFonts).
bool languageNeedsCjkFont(Language language);

// The text in the current language. The pointer is the argument itself (English, or no translation), a table
// entry, or a string kept for the life of the program: safe to keep across frames.
const char* tr(const char* english);
std::string tr(const std::string& english);
// An action's label: brushes keep their names (Clay Block, Move, Smooth...), everything else is translated (brush
// settings such as brush.pen_size_on too).
const char* trAction(std::string_view actionId, const std::string& label);
// A palette path with each step translated: "Tool > Geometry" -> "Herramienta > Geometría".
std::string trPath(std::string_view path);

namespace i18n {
struct Entry {
    const char* english;
    const char* translated;
};
// The compiled-in tables (I18nSpanish.cpp, I18nTraditionalChinese.cpp, I18nSimplifiedChinese.cpp), for tests.
std::span<const Entry> spanishTable();
std::span<const Entry> traditionalChineseTable();
std::span<const Entry> simplifiedChineseTable();
std::span<const Entry> table(Language language); // empty for English
// The exact entry for `english` in that language's table, or nullptr (English has no table).
const char* lookup(Language language, std::string_view english);
} // namespace i18n

} // namespace sf
