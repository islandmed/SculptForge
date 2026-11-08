// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Printing a garment pattern at home (docs/design/garments.md, "Printing"): the real-size pattern sheet cut into
// A4 or Letter pages. Each page shows one cell of the sheet inside a trim line, a half triangle at the middle of
// every side that has a neighbour (the neighbour's half completes it), the neighbours' page names, and a header
// with the page's name (rows A, B, ... down, columns 1, 2, ... across) and how to put the pages together. Cells
// with nothing of the pattern on them are not printed (the others keep their grid names). A panel that is another's
// copy or mirror image (the other leg, the other sleeve) is printed once, marked "cut 2, 1 reversed", and the
// pieces are packed closer than in the pattern sheet. The 50 mm
// scale square is on page A1. Written as one multi-page PDF (our own small writer: vector lines, the standard
// Helvetica font, nothing embedded) or as one SVG per page. DXF (core/Garment.h) stays the format for cutters.
#pragma once

#include "core/Garment.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sf::garment {

enum class Paper { A4, Letter };

struct PrintOptions {
    Paper paper = Paper::A4;
    bool landscape = false;
    float marginMm = 12.0f; // around the trim line on every side (printers cannot print to the edge)
};

struct TileLayout {
    float paperW = 0.0f, paperH = 0.0f; // mm
    float cellW = 0.0f, cellH = 0.0f;   // the part of the sheet each page shows (inside the trim line)
    float sheetW = 0.0f, sheetH = 0.0f; // the whole pattern sheet
    int rows = 0, cols = 0;
    std::vector<uint8_t> used; // row by row: 1 when something of the pattern falls on that cell (blank pages are left out)
    int pages() const;         // the pages printed (the used cells)
    bool isUsed(int row, int col) const;
};

constexpr int kMaxPrintPages = 600;

TileLayout tileLayout(const Pattern& pat, const PrintOptions& opt);
// "A1" for row 0, column 0; rows past Z are AA, AB, ...
std::string pageName(int row, int col);

// False with `error` for an empty pattern or more than kMaxPrintPages pages.
bool patternToTiledPdf(const Pattern& pat, const PrintOptions& opt, std::string& pdf, std::string& error);
// One SVG per printed page, row by row (A1, A2, ..., B1, ...; blank cells left out), with each page's name.
struct SvgPageFile {
    std::string name, svg;
};
bool patternToTiledSvgs(const Pattern& pat, const PrintOptions& opt, std::vector<SvgPageFile>& pages, std::string& error);

} // namespace sf::garment
