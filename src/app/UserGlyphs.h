// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Glyphs for text people type (Part, folder, layer and brush-set names, file names, Library items, pasted text).
// The bundled fonts hold Latin and the Chinese subsets of our own strings only, so a character outside them drew as
// "?". ImGui reports each glyph it draws as the fallback (the hook cmake/Dependencies.cmake patches into
// ImFont::FindGlyph); between frames resolve() looks those characters up, first in the bundled fonts, then in the
// system's own (Windows: Microsoft YaHei / JhengHei, Yu Gothic, Malgun Gothic; macOS: PingFang, Hiragino, Apple SD
// Gothic; Linux: fontconfig, e.g. Noto Sans CJK), and the atlas is built again with them merged in (merge()).
// Nothing is read or allocated until such a character is drawn.
#pragma once

#include <imgui.h>

#include <cstddef>

namespace sf::glyphs {

struct Embedded {
    const unsigned char* data;
    size_t size;
};

// Hooks ImGui (once). `embedded`: the bundled fonts to look in first, in order; `testFonts`: honour
// SF_FALLBACK_FONTS (font files, ';'-separated, looked in before the system's: UI tests only).
void install(const Embedded* embedded, int count, bool testFonts);
// Characters drawn as "?" that haven't been looked up yet (resolve() before the next frame).
bool pending();
// Looks them up; true when at least one was found and the atlas needs building again. `traditional`: Traditional
// Chinese glyph shapes first (the interface language), otherwise Simplified.
bool resolve(bool traditional);
// Forgets what waits (the frame was drawn with the previous language's atlas): still missing, it comes back.
void discardPending();
// Merges the characters found into the font added just before (MergeMode), at `px`.
void merge(float px);
// Glyphs drawn as "?" in the last finished frame (UI tests: expect_value glyphs_missing) and characters merged.
void endFrame();
int missingLastFrame();
int mergedCount();

} // namespace sf::glyphs
