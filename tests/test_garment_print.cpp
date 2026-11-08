// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tiled printing of garment patterns (core/GarmentPrint.h): the page grid covers the sheet, page names, a
// well-formed PDF (every xref offset lands on its object, real-size millimetre matrix), the 50 mm square on A1 at
// its true size, blank pages left out, a matching triangle on both sides of every join, the page limit, garment_export.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/GarmentPrint.h"
#include "core/GarmentTemplates.h"

#include <cmath>
#include <fstream>
#include <sstream>

using namespace sf;
using namespace sf::garment;

namespace {

size_t countOf(const std::string& s, const std::string& what) {
    size_t n = 0;
    for (size_t at = s.find(what); at != std::string::npos; at = s.find(what, at + what.size())) ++n;
    return n;
}

std::string readAll(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

} // namespace

TEST(garment_print_page_grid_covers_the_sheet) {
    CHECK(pageName(0, 0) == "A1" && pageName(1, 11) == "B12" && pageName(25, 0) == "Z1" && pageName(26, 2) == "AA3");
    const Pattern pat = tshirtPattern();
    for (Paper paper : {Paper::A4, Paper::Letter})
        for (bool landscape : {false, true}) {
            PrintOptions opt;
            opt.paper = paper;
            opt.landscape = landscape;
            const TileLayout t = tileLayout(pat, opt);
            CHECK(t.paperW == (paper == Paper::A4 ? (landscape ? 297.0f : 210.0f) : (landscape ? 279.4f : 215.9f)));
            CHECK(std::abs(t.cellW - (t.paperW - 24.0f)) < 1e-3f);
            CHECK(float(t.cols) * t.cellW >= t.sheetW && float(t.cols - 1) * t.cellW < t.sheetW);
            CHECK(float(t.rows) * t.cellH >= t.sheetH && float(t.rows - 1) * t.cellH < t.sheetH);
        }
}

TEST(garment_print_pdf_is_well_formed_and_real_size) {
    const Pattern pat = tshirtPattern();
    PrintOptions opt;
    std::string pdf, error;
    CHECK(patternToTiledPdf(pat, opt, pdf, error));
    const TileLayout t = tileLayout(pat, opt);
    CHECK(pdf.rfind("%PDF-1.4", 0) == 0 && pdf.find("%%EOF") != std::string::npos);
    CHECK(int(countOf(pdf, "/Type /Page ")) == t.pages());
    CHECK(pdf.find("/Count " + std::to_string(t.pages())) != std::string::npos);
    CHECK(t.pages() <= t.rows * t.cols);
    // Every xref entry points at "<n> 0 obj", and startxref at the table.
    const size_t xref = pdf.rfind("\nxref\n") + 1;
    CHECK(std::stoul(pdf.substr(pdf.rfind("startxref\n") + 10)) == xref);
    std::istringstream table(pdf.substr(xref));
    std::string word;
    int first = 0, count = 0;
    table >> word >> first >> count;
    CHECK(count == 2 * t.pages() + 4);
    std::string off, gen, kind;
    table >> off >> gen >> kind; // the free entry
    for (int i = 1; i < count; ++i) {
        table >> off >> gen >> kind;
        CHECK(pdf.compare(std::stoul(off), std::to_string(i).size() + 6, std::to_string(i) + " 0 obj") == 0);
    }
    // Millimetres: 72 / 25.4 points each, y down from the top.
    CHECK(pdf.find("2.83465 0 0 -2.83465 0 841.88976 cm") != std::string::npos);
    // The 50 mm square, on page A1 at its true size (sheet margin 10 + page margin 12).
    CHECK(pdf.find("22 22 m 72 22 l 72 72 l 22 72 l h S") != std::string::npos);
    // Every content stream's /Length is its byte count.
    for (size_t at = pdf.find("/Length "); at != std::string::npos; at = pdf.find("/Length ", at + 1)) {
        const size_t len = std::stoul(pdf.substr(at + 8));
        const size_t start = pdf.find("stream\n", at) + 7;
        CHECK(pdf.compare(start + len, 9, "endstream") == 0);
    }
}

TEST(garment_print_pages_match_at_every_join) {
    Pattern pat;
    std::string error;
    CHECK(makeTemplate("trousers", SizeChart{}, 0.0f, pat, error));
    PrintOptions opt;
    opt.paper = Paper::Letter;
    std::vector<SvgPageFile> pages;
    CHECK(patternToTiledSvgs(pat, opt, pages, error));
    const TileLayout t = tileLayout(pat, opt);
    CHECK(int(pages.size()) == t.pages() && t.rows > 1 && t.cols > 1);
    // Blank cells are left out, but never A1 (the scale square) or a cell a panel crosses.
    CHECK(t.pages() < t.rows * t.cols && t.isUsed(0, 0) && pages[0].name == "A1");
    // A filled half triangle on both sides of each join between two printed pages.
    int joins = 0;
    for (int r = 0; r < t.rows; ++r)
        for (int c = 0; c < t.cols; ++c)
            if (t.isUsed(r, c)) joins += int(t.isUsed(r, c + 1)) + int(t.isUsed(r + 1, c));
    size_t triangles = 0;
    for (const SvgPageFile& p : pages) triangles += countOf(p.svg, "fill=\"#000\" stroke=\"none\"/>");
    CHECK(int(triangles) == 2 * joins);
    CHECK(pages[0].svg.find("width=\"215.9mm\" height=\"279.4mm\"") != std::string::npos);
    for (const SvgPageFile& p : pages) CHECK(p.svg.find("page " + p.name + " ") != std::string::npos);
    // Every panel's name is on some page (a mirrored copy's in its original's "cut 2" note).
    for (const Panel& p : pat.panels) {
        bool found = false;
        for (const SvgPageFile& pg : pages) found = found || pg.svg.find(p.name) != std::string::npos;
        CHECK(found);
    }
}

TEST(garment_print_refuses_too_many_pages) {
    Pattern pat;
    pat.panels.push_back(rectanglePanel("Sail", 9000.0f, 9000.0f));
    std::string pdf, error;
    CHECK(!patternToTiledPdf(pat, PrintOptions{}, pdf, error) && error.find("pages") != std::string::npos);
    CHECK(!patternToTiledPdf(Pattern{}, PrintOptions{}, pdf, error));
}

TEST(garment_export_prints_pdf_and_svg_pages) {
    Document doc;
    CommandRegistry reg;
    CHECK(reg.execute(doc, "garment_new", {{"template", std::string("skirt")}}).ok);
    const std::string pdf = tempPath("garment_print.pdf");
    CommandResult r = reg.execute(doc, "garment_export", {{"path", pdf}, {"paper", std::string("letter")}});
    CHECK(r.ok && r.message.find("Letter pages") != std::string::npos);
    CHECK(readAll(pdf).rfind("%PDF-1.4", 0) == 0);
    const std::string stem = tempPath("garment_print_pages");
    r = reg.execute(doc, "garment_export", {{"path", stem + ".svg"}, {"format", std::string("svg_pages")}});
    CHECK(r.ok);
    CHECK(readAll(stem + "-A1.svg").find("<svg") != std::string::npos);
    CHECK(!reg.execute(doc, "garment_export", {{"path", pdf}, {"paper", std::string("a3")}}).ok);
    CHECK(!reg.execute(doc, "garment_export", {{"path", pdf}, {"format", std::string("png")}}).ok);
}
