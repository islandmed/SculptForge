// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tiled printing of garment patterns (core/GarmentPrint.h). The pattern is drawn once into a list of strokes and
// labels in sheet millimetres (y down, as in the whole-sheet SVG), then every page draws its cell of that list
// through a clip, plus its own marks, into a PDF or SVG page.
#include "core/GarmentPrint.h"

#include "core/GarmentTemplates.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace sf::garment {

namespace {

struct Stroke {
    std::vector<glm::vec2> pts;
    bool closed = false, dashed = false, filled = false;
    float width = 0.4f;
};

struct Label {
    glm::vec2 at{0.0f};
    float size = 10.0f;
    bool centred = false;
    std::string text;
};

struct Drawing {
    std::vector<Stroke> strokes;
    std::vector<Label> labels;
};

constexpr float kSheetMargin = 10.0f;

struct Sheet {
    glm::vec2 lo{0.0f}, hi{0.0f};
    glm::vec2 map(glm::vec2 p) const { return {p.x - lo.x + kSheetMargin, hi.y - p.y + kSheetMargin}; }
    float width() const { return hi.x - lo.x + 2.0f * kSheetMargin; }
    float height() const { return hi.y - lo.y + 2.0f * kSheetMargin; }
};

// The same sheet as patternToSvg: every cutting line inside, 70 mm on top for the scale square.
Sheet sheetOf(const Pattern& pat) {
    Sheet s;
    glm::vec2 lo(std::numeric_limits<float>::max()), hi(std::numeric_limits<float>::lowest());
    for (const Panel& p : pat.panels)
        for (glm::vec2 q : offsetOutline(p, std::max(0.0f, p.seamAllowance))) {
            lo = glm::min(lo, q);
            hi = glm::max(hi, q);
        }
    if (pat.panels.empty()) lo = hi = glm::vec2(0.0f);
    hi.y += 70.0f;
    s.lo = lo;
    s.hi = hi;
    return s;
}

// Whether `b` is `a` moved (or, with `mirrored`, a's mirror image moved), points and curves within half a mm.
bool samePiece(const Panel& a, const Panel& b, bool mirrored) {
    if (a.points.size() != b.points.size() || a.points.size() < 3) return false;
    const Panel m = mirrored ? mirroredPanel(a) : a;
    const glm::vec2 d = panelMin(b) - panelMin(m);
    auto near = [&](glm::vec2 p, glm::vec2 q) { return glm::length(p + d - q) < 0.5f; };
    for (size_t i = 0; i < m.points.size(); ++i) {
        if (!near(m.points[i], b.points[i])) return false;
        const EdgeCurve& c = m.curves[i];
        const EdgeCurve& e = i < b.curves.size() ? b.curves[i] : EdgeCurve{};
        if (c.curved != e.curved || (c.curved && (!near(c.c1, e.c1) || !near(c.c2, e.c2)))) return false;
    }
    return std::abs(m.seamAllowance - b.seamAllowance) < 0.01f;
}

// The pattern as it is printed: a panel that is another's copy or mirror image is printed once, its name saying
// how many to cut ("Left front: cut 2, 1 reversed (Right front)"); the pieces are packed in shelves (tallest
// first) about as wide as they are tall, the sheet a whole number of `cellW` pages wide.
Pattern printPattern(const Pattern& pat, float cellW) {
    Pattern out;
    std::vector<bool> taken(pat.panels.size(), false);
    for (size_t i = 0; i < pat.panels.size(); ++i) {
        if (taken[i] || pat.panels[i].points.size() < 3) continue;
        Panel p = pat.panels[i];
        int copies = 1, reversed = 0;
        std::string others;
        for (size_t j = i + 1; j < pat.panels.size(); ++j) {
            if (taken[j]) continue;
            const bool same = samePiece(p, pat.panels[j], false), mirror = !same && samePiece(p, pat.panels[j], true);
            if (!same && !mirror) continue;
            taken[j] = true;
            ++copies;
            reversed += mirror;
            others += (others.empty() ? "" : ", ") + pat.panels[j].name;
        }
        if (copies > 1)
            p.name += ": cut " + std::to_string(copies) + (reversed ? ", " + std::to_string(reversed) + " reversed" : "") +
                      " (" + others + ")";
        out.panels.push_back(std::move(p));
    }
    // Shelf packing on the cutting lines' boxes.
    struct Box {
        size_t panel;
        glm::vec2 lo, size;
    };
    std::vector<Box> boxes;
    float area = 0.0f, widest = 0.0f;
    for (size_t i = 0; i < out.panels.size(); ++i) {
        glm::vec2 lo(std::numeric_limits<float>::max()), hi(std::numeric_limits<float>::lowest());
        for (glm::vec2 q : offsetOutline(out.panels[i], std::max(0.0f, out.panels[i].seamAllowance))) {
            lo = glm::min(lo, q);
            hi = glm::max(hi, q);
        }
        boxes.push_back({i, lo, hi - lo});
        area += (hi.x - lo.x + 30.0f) * (hi.y - lo.y + 30.0f);
        widest = std::max(widest, hi.x - lo.x);
    }
    std::stable_sort(boxes.begin(), boxes.end(), [](const Box& a, const Box& b) { return a.size.y > b.size.y; });
    const float want = std::max(widest, std::sqrt(area));
    const float width = std::ceil((want + 2.0f * kSheetMargin) / cellW) * cellW - 2.0f * kSheetMargin;
    float x = 0.0f, y = 0.0f, shelf = 0.0f;
    for (const Box& b : boxes) {
        if (x > 0.0f && x + b.size.x > width) {
            x = 0.0f;
            y -= shelf + 30.0f;
            shelf = 0.0f;
        }
        Panel& p = out.panels[b.panel];
        const glm::vec2 d = glm::vec2(x, y - b.size.y) - b.lo; // shelves go down from y = 0
        for (glm::vec2& q : p.points) q += d;
        for (EdgeCurve& c : p.curves) {
            c.c1 += d;
            c.c2 += d;
        }
        x += b.size.x + 30.0f;
        shelf = std::max(shelf, b.size.y);
    }
    return out;
}

Drawing drawPattern(const Pattern& pat, const Sheet& sh) {
    Drawing d;
    const glm::vec2 sq(kSheetMargin, kSheetMargin);
    d.strokes.push_back({{sq, sq + glm::vec2(50, 0), sq + glm::vec2(50, 50), sq + glm::vec2(0, 50)}, true, false, false, 0.4f});
    d.labels.push_back({sq + glm::vec2(55, 22), 8.0f, false, "50 mm: measure this square"});
    d.labels.push_back({sq + glm::vec2(55, 33), 6.0f, false, "Solid: sewing line. Dashed: cutting line."});
    for (const Panel& p : pat.panels) {
        if (p.points.size() < 3) continue;
        auto mapped = [&](std::vector<glm::vec2> pts) {
            for (glm::vec2& q : pts) q = sh.map(q);
            return pts;
        };
        if (p.seamAllowance > 0.0f) d.strokes.push_back({mapped(offsetOutline(p, p.seamAllowance)), true, true, false, 0.3f});
        d.strokes.push_back({mapped(outline(p, 0.1f)), true, false, false, 0.6f});
        for (const Notch& nt : p.notches) {
            glm::vec2 tan;
            const glm::vec2 at = edgePoint(p, nt.edge, nt.t, &tan), out(tan.y, -tan.x);
            const float len = std::max(5.0f, p.seamAllowance);
            d.strokes.push_back({{sh.map(at - out * 2.0f), sh.map(at + out * len)}, false, false, false, 0.4f});
        }
        const glm::vec2 lo = panelMin(p), hi = panelMax(p), c = 0.5f * (lo + hi);
        const float g = p.grainDegrees * 3.14159265f / 180.0f;
        const glm::vec2 dir(std::cos(g), std::sin(g)), side(-dir.y, dir.x);
        const float half = 0.25f * std::min(hi.y - lo.y, hi.x - lo.x);
        d.strokes.push_back({{sh.map(c - dir * half), sh.map(c + dir * half)}, false, false, false, 0.4f});
        d.strokes.push_back({{sh.map(c + dir * (half - 6.0f) + side * 3.0f), sh.map(c + dir * half),
                              sh.map(c + dir * (half - 6.0f) - side * 3.0f)},
                             false, false, false, 0.4f});
        d.labels.push_back({sh.map(c + glm::vec2(0.0f, -0.12f * (hi.y - lo.y))), 14.0f, true, p.name});
    }
    return d;
}

// --- Page backends ---

struct Canvas {
    virtual ~Canvas() = default;
    virtual void clip(glm::vec2 lo, glm::vec2 hi) = 0; // until unclip
    virtual void unclip() = 0;
    virtual void stroke(const Stroke& s) = 0;
    virtual void label(const Label& l, float grey) = 0;
};

std::string num(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.2f", std::abs(v) < 0.005 ? 0.0 : v);
    std::string s = b;
    while (s.size() > 1 && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

// UTF-8 to the PDF standard fonts' WinAnsi (Latin-1 range; anything else becomes '?').
std::string latin1(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            if (c >= 0x20) o += char(c);
            ++i;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
            const unsigned cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu);
            o += cp >= 0xA0 && cp <= 0xFF ? char(cp) : '?';
            i += 2;
        } else {
            o += '?';
            ++i;
            while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) ++i;
        }
    }
    return o;
}

struct PdfPage : Canvas {
    std::string ops;
    void clip(glm::vec2 lo, glm::vec2 hi) override {
        ops += "q " + num(lo.x) + " " + num(lo.y) + " " + num(hi.x - lo.x) + " " + num(hi.y - lo.y) + " re W n\n";
    }
    void unclip() override { ops += "Q\n"; }
    void stroke(const Stroke& s) override {
        if (s.pts.empty()) return;
        ops += num(s.width) + " w " + (s.dashed ? "[4 2] 0 d " : "[] 0 d ");
        ops += num(s.pts[0].x) + " " + num(s.pts[0].y) + " m";
        for (size_t i = 1; i < s.pts.size(); ++i) ops += " " + num(s.pts[i].x) + " " + num(s.pts[i].y) + " l";
        ops += s.filled ? " h f\n" : s.closed ? " h S\n" : " S\n";
    }
    void label(const Label& l, float grey) override {
        std::string t;
        for (char c : latin1(l.text)) {
            if (c == '(' || c == ')' || c == '\\') t += '\\';
            t += c;
        }
        // Helvetica's mean advance is about half the size: close enough to centre a name.
        const float x = l.centred ? l.at.x - 0.26f * l.size * float(latin1(l.text).size()) : l.at.x;
        // The page's matrix is y down, so the text matrix flips y back. Font size in mm (the page unit).
        ops += num(grey) + " g BT /F1 " + num(l.size * 0.3528f) + " Tf 1 0 0 -1 " + num(x) + " " + num(l.at.y) +
               " Tm (" + t + ") Tj ET 0 g\n";
    }
};

std::string xmlEscape(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
        case '&': o += "&amp;"; break;
        case '<': o += "&lt;"; break;
        case '>': o += "&gt;"; break;
        case '"': o += "&quot;"; break;
        default:
            if (static_cast<unsigned char>(c) >= 0x20) o += c;
        }
    }
    return o;
}

struct SvgPage : Canvas {
    std::string body;
    int clips = 0;
    void clip(glm::vec2 lo, glm::vec2 hi) override {
        const std::string id = "cell" + std::to_string(++clips);
        body += "<clipPath id=\"" + id + "\"><rect x=\"" + num(lo.x) + "\" y=\"" + num(lo.y) + "\" width=\"" +
                num(hi.x - lo.x) + "\" height=\"" + num(hi.y - lo.y) + "\"/></clipPath>\n<g clip-path=\"url(#" + id + ")\">\n";
    }
    void unclip() override { body += "</g>\n"; }
    void stroke(const Stroke& s) override {
        if (s.pts.empty()) return;
        std::string pts;
        for (glm::vec2 q : s.pts) pts += num(q.x) + "," + num(q.y) + " ";
        pts.pop_back();
        body += std::string(s.closed || s.filled ? "<polygon" : "<polyline") + " points=\"" + pts + "\" stroke-width=\"" +
                num(s.width) + "\"" + (s.dashed ? " stroke-dasharray=\"4 2\"" : "") +
                (s.filled ? " fill=\"#000\" stroke=\"none\"" : "") + "/>\n";
    }
    void label(const Label& l, float grey) override {
        const int g = int(std::round(grey * 255.0f));
        char colour[8];
        std::snprintf(colour, sizeof colour, "#%02x%02x%02x", g, g, g);
        body += "<text x=\"" + num(l.at.x) + "\" y=\"" + num(l.at.y) + "\" font-size=\"" + num(l.size * 0.3528f) + "\"" +
                (l.centred ? " text-anchor=\"middle\"" : "") + " fill=\"" + colour + "\" stroke=\"none\">" +
                xmlEscape(l.text) + "</text>\n";
    }
};

// One page: the sheet's cell (row, col) inside the trim line, the matching triangles and the neighbours' names.
void drawPage(const Drawing& d, const TileLayout& t, const PrintOptions& opt, int row, int col, Canvas& c) {
    const float m = opt.marginMm;
    const glm::vec2 lo(m, m), hi(m + t.cellW, m + t.cellH);
    const glm::vec2 shift = lo - glm::vec2(float(col) * t.cellW, float(row) * t.cellH); // sheet -> page
    c.clip(lo, hi);
    for (Stroke s : d.strokes) {
        for (glm::vec2& q : s.pts) q += shift;
        c.stroke(s);
    }
    for (Label l : d.labels) {
        l.at += shift;
        c.label(l, 0.0f);
    }
    c.unclip();
    c.stroke({{lo, {hi.x, lo.y}, hi, {lo.x, hi.y}}, true, false, false, 0.15f}); // trim line
    // A half triangle (8 mm base) inside the trim line at the middle of each side with a neighbour.
    const glm::vec2 mid = 0.5f * (lo + hi);
    struct Side {
        int dr, dc;
        glm::vec2 a, b, in;
    };
    const Side sides[4] = {{-1, 0, {mid.x - 4, lo.y}, {mid.x + 4, lo.y}, {0, 1}},
                           {1, 0, {mid.x - 4, hi.y}, {mid.x + 4, hi.y}, {0, -1}},
                           {0, -1, {lo.x, mid.y - 4}, {lo.x, mid.y + 4}, {1, 0}},
                           {0, 1, {hi.x, mid.y - 4}, {hi.x, mid.y + 4}, {-1, 0}}};
    for (const Side& s : sides) {
        const int r = row + s.dr, k = col + s.dc;
        if (!t.isUsed(r, k)) continue;
        const glm::vec2 tip = 0.5f * (s.a + s.b) + s.in * 4.0f;
        c.stroke({{s.a, s.b, tip}, true, false, true, 0.1f});
        const glm::vec2 at = tip + s.in * (s.in.y > 0 ? 6.0f : s.in.y < 0 ? 2.0f : 4.0f) + glm::vec2(0, s.in.y == 0 ? 1.2f : 0);
        Label l{at, 9.0f, true, pageName(r, k)};
        if (s.in.x > 0) l.at.x += 3.0f;
        if (s.in.x < 0) l.at.x -= 3.0f;
        c.label(l, 0.35f);
    }
    char head[200];
    std::snprintf(head, sizeof head, "SculptForge pattern, page %s (%d rows x %d columns). Print at 100%%, no fit to page.",
                  pageName(row, col).c_str(), t.rows, t.cols);
    c.label({{lo.x, lo.y - 4.5f}, 7.0f, false, head}, 0.0f);
    c.label({{lo.x, hi.y + 6.0f}, 6.0f, false,
              "Trim on the line at the left and the top, lay the page over its neighbours' lines, match the triangles."},
             0.0f);
    c.label({{hi.x - 12.0f, hi.y - 4.0f}, 14.0f, true, pageName(row, col)}, 0.8f);
}

// Marks the cells something is drawn on (every stroke point and label, and the segments between stroke points).
void markUsed(const Drawing& d, TileLayout& t) {
    t.used.assign(size_t(t.rows * t.cols), 0);
    auto mark = [&](glm::vec2 q) {
        const int c = std::clamp(int(std::floor(q.x / t.cellW)), 0, t.cols - 1);
        const int r = std::clamp(int(std::floor(q.y / t.cellH)), 0, t.rows - 1);
        t.used[size_t(r * t.cols + c)] = 1;
    };
    for (const Stroke& s : d.strokes)
        for (size_t i = 0; i < s.pts.size(); ++i) {
            const glm::vec2 a = s.pts[i], b = s.pts[(i + 1) % s.pts.size()];
            if (i + 1 == s.pts.size() && !s.closed) {
                mark(a);
                break;
            }
            const int steps = 1 + int(glm::length(b - a) / (0.25f * std::min(t.cellW, t.cellH)));
            for (int k = 0; k <= steps; ++k) mark(a + (b - a) * (float(k) / float(steps)));
        }
    for (const Label& l : d.labels) mark(l.at);
}

bool prepare(const Pattern& pat, const PrintOptions& opt, TileLayout& t, Drawing& d, std::string& error) {
    if (pat.empty()) {
        error = "The pattern has no panels";
        return false;
    }
    t = tileLayout(pat, opt);
    if (t.rows * t.cols > kMaxPrintPages) {
        error = "The pattern needs " + std::to_string(t.rows * t.cols) + " pages (at most " + std::to_string(kMaxPrintPages) +
                "): export DXF or a whole-sheet SVG for a plotter";
        return false;
    }
    const Pattern sheet = printPattern(pat, t.cellW);
    d = drawPattern(sheet, sheetOf(sheet));
    return true;
}

} // namespace

std::string pageName(int row, int col) {
    std::string r;
    for (int v = row + 1; v > 0; v = (v - 1) / 26) r.insert(r.begin(), char('A' + (v - 1) % 26));
    return r + std::to_string(col + 1);
}

TileLayout tileLayout(const Pattern& pat, const PrintOptions& opt) {
    TileLayout t;
    t.paperW = opt.paper == Paper::Letter ? 215.9f : 210.0f;
    t.paperH = opt.paper == Paper::Letter ? 279.4f : 297.0f;
    if (opt.landscape) std::swap(t.paperW, t.paperH);
    const float m = std::clamp(opt.marginMm, 5.0f, 40.0f);
    t.cellW = t.paperW - 2.0f * m;
    t.cellH = t.paperH - 2.0f * m;
    const Pattern sheet = printPattern(pat, t.cellW);
    const Sheet sh = sheetOf(sheet);
    t.sheetW = sh.width();
    t.sheetH = sh.height();
    t.cols = std::max(1, int(std::ceil(t.sheetW / t.cellW - 1e-4f)));
    t.rows = std::max(1, int(std::ceil(t.sheetH / t.cellH - 1e-4f)));
    t.used.assign(size_t(t.rows) * size_t(t.cols), 1);
    if (t.rows * t.cols <= kMaxPrintPages) markUsed(drawPattern(sheet, sh), t);
    return t;
}

int TileLayout::pages() const {
    int n = 0;
    for (uint8_t u : used) n += u;
    return n;
}

bool TileLayout::isUsed(int row, int col) const {
    return row >= 0 && col >= 0 && row < rows && col < cols && used[size_t(row * cols + col)];
}

bool patternToTiledPdf(const Pattern& pat, const PrintOptions& optIn, std::string& pdf, std::string& error) {
    PrintOptions opt = optIn;
    opt.marginMm = std::clamp(opt.marginMm, 5.0f, 40.0f);
    TileLayout t;
    Drawing d;
    if (!prepare(pat, opt, t, d, error)) return false;
    // Objects: 1 catalog, 2 page tree, 3 font, then a page and its content per page.
    std::vector<int> cells; // row * cols + col of each printed page
    for (int i = 0; i < t.rows * t.cols; ++i)
        if (t.used[size_t(i)]) cells.push_back(i);
    const int pages = int(cells.size());
    std::vector<std::string> objects(size_t(3 + 2 * pages));
    objects[0] = "<< /Type /Catalog /Pages 2 0 R >>";
    std::string kids;
    for (int i = 0; i < pages; ++i) kids += std::to_string(4 + 2 * i) + " 0 R ";
    objects[1] = "<< /Type /Pages /Kids [" + kids + "] /Count " + std::to_string(pages) + " >>";
    objects[2] = "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>";
    const double k = 72.0 / 25.4;
    for (int i = 0; i < pages; ++i) {
        PdfPage page;
        // Millimetres, y down from the top-left corner.
        char matrix[96];
        std::snprintf(matrix, sizeof matrix, "%.5f 0 0 %.5f 0 %.5f cm 1 J 1 j\n", double(k), -double(k), double(t.paperH * k));
        page.ops = matrix;
        drawPage(d, t, opt, cells[size_t(i)] / t.cols, cells[size_t(i)] % t.cols, page);
        objects[size_t(3 + 2 * i)] = "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + num(t.paperW * k) + " " +
                                     num(t.paperH * k) + "] /Resources << /Font << /F1 3 0 R >> >> /Contents " +
                                     std::to_string(5 + 2 * i) + " 0 R >>";
        objects[size_t(4 + 2 * i)] =
            "<< /Length " + std::to_string(page.ops.size()) + " >>\nstream\n" + page.ops + "endstream";
    }
    pdf = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
    std::vector<size_t> offsets;
    for (size_t i = 0; i < objects.size(); ++i) {
        offsets.push_back(pdf.size());
        pdf += std::to_string(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }
    const size_t xref = pdf.size();
    pdf += "xref\n0 " + std::to_string(objects.size() + 1) + "\n0000000000 65535 f \n";
    for (size_t o : offsets) {
        char line[24];
        std::snprintf(line, sizeof line, "%010zu 00000 n \n", o);
        pdf += line;
    }
    pdf += "trailer\n<< /Size " + std::to_string(objects.size() + 1) + " /Root 1 0 R >>\nstartxref\n" +
           std::to_string(xref) + "\n%%EOF\n";
    return true;
}

bool patternToTiledSvgs(const Pattern& pat, const PrintOptions& optIn, std::vector<SvgPageFile>& out, std::string& error) {
    PrintOptions opt = optIn;
    opt.marginMm = std::clamp(opt.marginMm, 5.0f, 40.0f);
    TileLayout t;
    Drawing d;
    if (!prepare(pat, opt, t, d, error)) return false;
    out.clear();
    for (int i = 0; i < t.rows * t.cols; ++i) {
        if (!t.used[size_t(i)]) continue;
        SvgPage page;
        drawPage(d, t, opt, i / t.cols, i % t.cols, page);
        out.push_back({pageName(i / t.cols, i % t.cols), "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" +
                      num(t.paperW) + "mm\" height=\"" + num(t.paperH) + "mm\" viewBox=\"0 0 " + num(t.paperW) + " " +
                      num(t.paperH) + "\">\n<g fill=\"none\" stroke=\"#000\" stroke-linejoin=\"round\" " +
                      "font-family=\"Helvetica, Arial, sans-serif\">\n" + page.body + "</g>\n</svg>\n"});
    }
    return true;
}

} // namespace sf::garment
