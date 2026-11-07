// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pattern files (core/Garment.h): SVG at real size for printing and sewing, DXF outlines for cutters, and SVG
// panels read back in. The SVG reader is small and strict on purpose (the file may come from anywhere): no
// entities, no external references, no scripts are ever followed; it only reads the shapes' geometry, in bounded
// amounts.
#include "core/Garment.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <limits>
#include <cstring>

namespace sf::garment {

namespace {

// --- Writing ---

std::string fmt(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.2f", std::abs(v) < 0.005 ? 0.0 : v);
    std::string s = b;
    while (s.size() > 1 && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

std::string xmlEscape(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
        case '&': o += "&amp;"; break;
        case '<': o += "&lt;"; break;
        case '>': o += "&gt;"; break;
        case '"': o += "&quot;"; break;
        default:
            if (static_cast<unsigned char>(c) >= 0x20 || c == '\t') o += c;
        }
    }
    return o;
}

struct Sheet {
    glm::vec2 lo{0.0f}, hi{0.0f};
    float margin = 10.0f;
    glm::vec2 map(glm::vec2 p) const { return {p.x - lo.x + margin, hi.y - p.y + margin}; } // y down
    float width() const { return hi.x - lo.x + 2.0f * margin; }
    float height() const { return hi.y - lo.y + 2.0f * margin; }
};

Sheet sheetOf(const Pattern& pat) {
    Sheet s;
    glm::vec2 lo(std::numeric_limits<float>::max()), hi(std::numeric_limits<float>::lowest());
    for (const Panel& p : pat.panels)
        for (glm::vec2 q : offsetOutline(p, std::max(0.0f, p.seamAllowance))) {
            lo = glm::min(lo, q);
            hi = glm::max(hi, q);
        }
    if (pat.panels.empty()) lo = hi = glm::vec2(0.0f);
    hi.y += 70.0f; // room for the 50 mm scale square above the panels
    s.lo = lo;
    s.hi = hi;
    return s;
}

} // namespace

std::string patternToSvg(const Pattern& pat) {
    const Sheet sh = sheetOf(pat);
    std::string o;
    o += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    o += "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + fmt(sh.width()) + "mm\" height=\"" + fmt(sh.height()) +
         "mm\" viewBox=\"0 0 " + fmt(sh.width()) + " " + fmt(sh.height()) + "\">\n";
    o += "<!-- SculptForge garment pattern, 1 unit = 1 mm. Print at 100% (no fit to page): the square is 50 mm. "
         "Solid: sewing line. Dashed: cutting line (seam allowance). -->\n";
    o += "<g fill=\"none\" stroke=\"#000\" stroke-width=\"0.4\" font-family=\"sans-serif\">\n";
    // The scale square, top left.
    o += "<rect x=\"" + fmt(sh.margin) + "\" y=\"" + fmt(sh.margin) + "\" width=\"50\" height=\"50\"/>\n";
    o += "<text x=\"" + fmt(sh.margin + 55.0f) + "\" y=\"" + fmt(sh.margin + 28.0f) +
         "\" font-size=\"8\" fill=\"#000\" stroke=\"none\">50 mm</text>\n";
    for (size_t i = 0; i < pat.panels.size(); ++i) {
        const Panel& p = pat.panels[i];
        if (p.points.size() < 3) continue;
        o += "<g id=\"panel-" + std::to_string(i + 1) + "\">\n";
        if (p.seamAllowance > 0.0f) {
            const std::vector<glm::vec2> cut = offsetOutline(p, p.seamAllowance);
            o += "<polygon class=\"cut\" stroke-dasharray=\"4 2\" points=\"";
            for (glm::vec2 q : cut) {
                const glm::vec2 m = sh.map(q);
                o += fmt(m.x) + "," + fmt(m.y) + " ";
            }
            o.back() = '"';
            o += "/>\n";
        }
        // The sewing line, curves exact.
        o += "<path class=\"sew\" data-name=\"" + xmlEscape(p.name) + "\" stroke-width=\"0.6\" d=\"";
        glm::vec2 m = sh.map(p.points[0]);
        o += "M" + fmt(m.x) + " " + fmt(m.y);
        for (size_t e = 0; e < p.points.size(); ++e) {
            const glm::vec2 b = sh.map(p.points[(e + 1) % p.points.size()]);
            const EdgeCurve& c = e < p.curves.size() ? p.curves[e] : EdgeCurve{};
            if (c.curved) {
                const glm::vec2 c1 = sh.map(c.c1), c2 = sh.map(c.c2);
                o += " C" + fmt(c1.x) + " " + fmt(c1.y) + " " + fmt(c2.x) + " " + fmt(c2.y) + " " + fmt(b.x) + " " + fmt(b.y);
            } else if (e + 1 < p.points.size()) {
                o += " L" + fmt(b.x) + " " + fmt(b.y);
            }
        }
        o += " Z\"/>\n";
        // Notches: a short tick across the seam allowance.
        for (const Notch& nt : p.notches) {
            glm::vec2 tan;
            const glm::vec2 at = edgePoint(p, nt.edge, nt.t, &tan);
            const glm::vec2 out(tan.y, -tan.x);
            const float len = std::max(5.0f, p.seamAllowance);
            const glm::vec2 a = sh.map(at - out * 2.0f), b = sh.map(at + out * len);
            o += "<line class=\"notch\" x1=\"" + fmt(a.x) + "\" y1=\"" + fmt(a.y) + "\" x2=\"" + fmt(b.x) + "\" y2=\"" + fmt(b.y) + "\"/>\n";
        }
        // Grain line: an arrow through the middle along the warp, half the panel's height.
        const glm::vec2 lo = panelMin(p), hi = panelMax(p), c = 0.5f * (lo + hi);
        const float g = p.grainDegrees * 3.14159265f / 180.0f;
        const glm::vec2 d(std::cos(g), std::sin(g));
        const float half = 0.25f * std::min(hi.y - lo.y, hi.x - lo.x) + 0.0f;
        const glm::vec2 g0 = sh.map(c - d * half), g1 = sh.map(c + d * half);
        const glm::vec2 side(-d.y, d.x);
        const glm::vec2 h1 = sh.map(c + d * (half - 6.0f) + side * 3.0f), h2 = sh.map(c + d * (half - 6.0f) - side * 3.0f);
        o += "<path class=\"grain\" d=\"M" + fmt(g0.x) + " " + fmt(g0.y) + " L" + fmt(g1.x) + " " + fmt(g1.y) + " M" + fmt(h1.x) + " " +
             fmt(h1.y) + " L" + fmt(g1.x) + " " + fmt(g1.y) + " L" + fmt(h2.x) + " " + fmt(h2.y) + "\"/>\n";
        const glm::vec2 t = sh.map(c + glm::vec2(0.0f, -0.12f * (hi.y - lo.y)));
        o += "<text x=\"" + fmt(t.x) + "\" y=\"" + fmt(t.y) + "\" font-size=\"10\" text-anchor=\"middle\" fill=\"#000\" stroke=\"none\">" +
             xmlEscape(p.name) + "</text>\n";
        o += "</g>\n";
    }
    o += "</g>\n</svg>\n";
    return o;
}

std::string patternToDxf(const Pattern& pat) {
    std::string o;
    auto pair = [&](int code, const std::string& v) { o += std::to_string(code) + "\n" + v + "\n"; };
    pair(0, "SECTION");
    pair(2, "HEADER");
    pair(9, "$INSUNITS");
    pair(70, "4"); // millimetres
    pair(0, "ENDSEC");
    pair(0, "SECTION");
    pair(2, "ENTITIES");
    auto polyline = [&](const std::vector<glm::vec2>& pts, const char* layer) {
        pair(0, "POLYLINE");
        pair(8, layer);
        pair(66, "1");
        pair(70, "1"); // closed
        for (glm::vec2 q : pts) {
            pair(0, "VERTEX");
            pair(8, layer);
            pair(10, fmt(q.x));
            pair(20, fmt(q.y));
        }
        pair(0, "SEQEND");
        pair(8, layer);
    };
    for (const Panel& p : pat.panels) {
        if (p.points.size() < 3) continue;
        polyline(outline(p, 0.1f), "SEW");
        if (p.seamAllowance > 0.0f) polyline(offsetOutline(p, p.seamAllowance), "CUT");
        const glm::vec2 c = 0.5f * (panelMin(p) + panelMax(p));
        pair(0, "TEXT");
        pair(8, "LABEL");
        pair(10, fmt(c.x));
        pair(20, fmt(c.y));
        pair(40, "10");
        std::string name;
        for (char ch : p.name) name += (ch == '\n' || ch == '\r') ? ' ' : ch;
        pair(1, name);
    }
    pair(0, "ENDSEC");
    pair(0, "EOF");
    return o;
}

// ---------------------------------------------------------------------------
// SVG import
// ---------------------------------------------------------------------------

namespace {

struct Affine { // x' = a x + c y + e, y' = b x + d y + f (SVG's matrix(a b c d e f))
    double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
    glm::dvec2 apply(glm::dvec2 p) const { return {a * p.x + c * p.y + e, b * p.x + d * p.y + f}; }
    Affine then(const Affine& m) const { // m applied after this
        return {m.a * a + m.c * b, m.b * a + m.d * b, m.a * c + m.c * d, m.b * c + m.d * d, m.a * e + m.c * f + m.e,
                m.b * e + m.d * f + m.f};
    }
};

// Numbers the SVG way: "1.5.5" is 1.5 and .5, "-1-2" is -1 and -2, exponents allowed. Locale-independent.
struct Numbers {
    const char* p;
    const char* end;
    void skip() {
        while (p < end && (*p == ' ' || *p == ',' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    }
    bool number(double& out) {
        skip();
        const char* s = p;
        double sign = 1.0;
        if (p < end && (*p == '+' || *p == '-')) sign = *p++ == '-' ? -1.0 : 1.0;
        double v = 0.0;
        bool digits = false;
        while (p < end && *p >= '0' && *p <= '9') {
            v = v * 10.0 + (*p++ - '0');
            digits = true;
        }
        if (p < end && *p == '.') {
            ++p;
            double scale = 0.1;
            while (p < end && *p >= '0' && *p <= '9') {
                v += (*p++ - '0') * scale;
                scale *= 0.1;
                digits = true;
            }
        }
        if (!digits) {
            p = s;
            return false;
        }
        if (p < end && (*p == 'e' || *p == 'E') && p + 1 < end &&
            ((p[1] >= '0' && p[1] <= '9') || ((p[1] == '-' || p[1] == '+') && p + 2 < end && p[2] >= '0' && p[2] <= '9'))) {
            ++p;
            int es = 1;
            if (*p == '+' || *p == '-') es = *p++ == '-' ? -1 : 1;
            int ex = 0;
            while (p < end && *p >= '0' && *p <= '9') ex = std::min(400, ex * 10 + (*p++ - '0'));
            v *= std::pow(10.0, es * ex);
        }
        out = sign * v;
        return std::isfinite(out);
    }
    bool flag(double& out) { // arc flags may be written without separators ("a1 1 0 01 ...")
        skip();
        if (p < end && (*p == '0' || *p == '1')) {
            out = *p++ - '0';
            return true;
        }
        return false;
    }
};

// One closed outline: points and the curve of the edge leaving each.
struct Shape {
    std::vector<glm::dvec2> pts;
    std::vector<std::array<glm::dvec2, 2>> ctrl;
    std::vector<bool> curved;
    bool closed = false;
    std::string name;
};

std::vector<Shape> parsePath(const std::string& d, size_t& budget) {
    std::vector<Shape> out;
    Numbers n{d.data(), d.data() + d.size()};
    Shape cur;
    glm::dvec2 pos(0.0), start(0.0), lastCtrl(0.0);
    char cmd = 0, prev = 0;
    auto finish = [&](bool closed) {
        if (cur.pts.size() >= 2) {
            // A last point back on the first closes it too.
            if (glm::length(cur.pts.back() - cur.pts.front()) < 1e-6 * (1.0 + glm::length(cur.pts.front()))) {
                cur.pts.pop_back();
                closed = true;
            } else {
                cur.ctrl.pop_back();
                cur.curved.pop_back();
                cur.ctrl.push_back({glm::dvec2(0.0), glm::dvec2(0.0)});
                cur.curved.push_back(false);
            }
            cur.closed = closed;
            cur.ctrl.resize(cur.pts.size());
            cur.curved.resize(cur.pts.size());
            if (cur.pts.size() >= 2) out.push_back(cur); // (two points: a D shape, split when made a panel)
        }
        cur = {};
    };
    // Adds the edge from `pos` to `to` (curved with c1, c2 when given).
    auto edge = [&](glm::dvec2 to, bool curvedEdge, glm::dvec2 c1, glm::dvec2 c2) {
        if (cur.pts.empty()) {
            cur.pts.push_back(pos);
            cur.ctrl.push_back({});
            cur.curved.push_back(false);
        }
        cur.ctrl.back() = {c1, c2};
        cur.curved.back() = curvedEdge;
        cur.pts.push_back(to);
        cur.ctrl.push_back({});
        cur.curved.push_back(false);
        pos = to;
    };
    while (true) {
        n.skip();
        if (n.p >= n.end) break;
        if (budget == 0) break;
        --budget;
        const char c = *n.p;
        if (std::isalpha(static_cast<unsigned char>(c))) {
            cmd = c;
            ++n.p;
        } else if (!cmd) {
            break;
        }
        const bool rel = std::islower(static_cast<unsigned char>(cmd)) != 0;
        const glm::dvec2 o = rel ? pos : glm::dvec2(0.0);
        double v[7];
        auto nums = [&](int k) {
            for (int i = 0; i < k; ++i)
                if (!n.number(v[i])) return false;
            return true;
        };
        switch (std::toupper(static_cast<unsigned char>(cmd))) {
        case 'M':
            if (!nums(2)) return out;
            finish(false);
            pos = start = o + glm::dvec2(v[0], v[1]);
            cmd = rel ? 'l' : 'L'; // following pairs are lines
            break;
        case 'L':
            if (!nums(2)) return out;
            edge(o + glm::dvec2(v[0], v[1]), false, {}, {});
            break;
        case 'H':
            if (!nums(1)) return out;
            edge({(rel ? pos.x : 0.0) + v[0], pos.y}, false, {}, {});
            break;
        case 'V':
            if (!nums(1)) return out;
            edge({pos.x, (rel ? pos.y : 0.0) + v[0]}, false, {}, {});
            break;
        case 'C':
            if (!nums(6)) return out;
            lastCtrl = o + glm::dvec2(v[2], v[3]);
            edge(o + glm::dvec2(v[4], v[5]), true, o + glm::dvec2(v[0], v[1]), lastCtrl);
            break;
        case 'S': {
            if (!nums(4)) return out;
            const char pu = char(std::toupper(static_cast<unsigned char>(prev)));
            const glm::dvec2 c1 = (pu == 'C' || pu == 'S') ? 2.0 * pos - lastCtrl : pos;
            lastCtrl = o + glm::dvec2(v[0], v[1]);
            edge(o + glm::dvec2(v[2], v[3]), true, c1, lastCtrl);
            break;
        }
        case 'Q':
        case 'T': {
            glm::dvec2 q, to;
            if (std::toupper(static_cast<unsigned char>(cmd)) == 'Q') {
                if (!nums(4)) return out;
                q = o + glm::dvec2(v[0], v[1]);
                to = o + glm::dvec2(v[2], v[3]);
            } else {
                if (!nums(2)) return out;
                const char pu = char(std::toupper(static_cast<unsigned char>(prev)));
                q = (pu == 'Q' || pu == 'T') ? 2.0 * pos - lastCtrl : pos;
                to = o + glm::dvec2(v[0], v[1]);
            }
            lastCtrl = q;
            const glm::dvec2 from = pos;
            edge(to, true, from + (q - from) * (2.0 / 3.0), to + (q - to) * (2.0 / 3.0));
            break;
        }
        case 'A': { // arcs: radii, rotation, flags, end point; drawn as a straight edge to the end point
            if (!nums(3)) return out;
            double f1, f2;
            if (!n.flag(f1) || !n.flag(f2) || !n.number(v[5]) || !n.number(v[6])) return out;
            edge(o + glm::dvec2(v[5], v[6]), false, {}, {});
            break;
        }
        case 'Z':
            if (!cur.pts.empty()) {
                if (glm::length(pos - start) > 1e-9) edge(start, false, {}, {});
                finish(true);
            }
            pos = start;
            cmd = 0;
            break;
        default: return out;
        }
        prev = cmd ? cmd : 'Z';
    }
    finish(false);
    return out;
}

// The value of attribute `name` in tag text `tag` ("" when absent). Exact names only (so "d" never matches "id").
std::string attr(const std::string& tag, const char* name) {
    const size_t nl = std::strlen(name);
    for (size_t i = 0; i + nl < tag.size(); ++i) {
        if (tag.compare(i, nl, name) != 0) continue;
        if (i > 0 && !std::isspace(static_cast<unsigned char>(tag[i - 1]))) continue;
        size_t j = i + nl;
        while (j < tag.size() && std::isspace(static_cast<unsigned char>(tag[j]))) ++j;
        if (j >= tag.size() || tag[j] != '=') continue;
        ++j;
        while (j < tag.size() && std::isspace(static_cast<unsigned char>(tag[j]))) ++j;
        if (j >= tag.size() || (tag[j] != '"' && tag[j] != '\'')) continue;
        const char q = tag[j];
        const size_t e = tag.find(q, j + 1);
        if (e == std::string::npos) return {};
        return tag.substr(j + 1, e - j - 1);
    }
    return {};
}

Affine parseTransform(const std::string& t) {
    Affine m;
    size_t i = 0;
    while (i < t.size()) {
        const size_t open = t.find('(', i);
        if (open == std::string::npos) break;
        const size_t close = t.find(')', open);
        if (close == std::string::npos) break;
        std::string fn = t.substr(i, open - i);
        fn.erase(std::remove_if(fn.begin(), fn.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)) || c == ','; }), fn.end());
        const std::string args = t.substr(open + 1, close - open - 1);
        Numbers n{args.data(), args.data() + args.size()};
        double v[6];
        int k = 0;
        while (k < 6 && n.number(v[k])) ++k;
        Affine s;
        if (fn == "matrix" && k == 6) s = {v[0], v[1], v[2], v[3], v[4], v[5]};
        else if (fn == "translate" && k >= 1) s = {1, 0, 0, 1, v[0], k > 1 ? v[1] : 0.0};
        else if (fn == "scale" && k >= 1) s = {v[0], 0, 0, k > 1 ? v[1] : v[0], 0, 0};
        else if (fn == "rotate" && k >= 1) {
            const double r = v[0] * 3.14159265358979 / 180.0, cs = std::cos(r), sn = std::sin(r);
            const double cx = k >= 3 ? v[1] : 0.0, cy = k >= 3 ? v[2] : 0.0;
            s = Affine{1, 0, 0, 1, -cx, -cy}.then({cs, sn, -sn, cs, 0, 0}).then({1, 0, 0, 1, cx, cy});
        }
        m = s.then(m); // the rightmost transform applies first: this one is applied before those before it
        i = close + 1;
    }
    // (Each later function in the list applies first, so the composition above builds left * right.)
    return m;
}

// Millimetres per unit of a length attribute ("210mm" -> 1 per mm, "8.5in" -> 25.4, "300" -> px at 96/in).
double lengthMm(const std::string& s, double& value) {
    Numbers n{s.data(), s.data() + s.size()};
    if (!n.number(value)) return 0.0;
    std::string unit(n.p, n.end);
    unit.erase(std::remove_if(unit.begin(), unit.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)); }), unit.end());
    if (unit == "mm") return 1.0;
    if (unit == "cm") return 10.0;
    if (unit == "in") return 25.4;
    if (unit == "pt") return 25.4 / 72.0;
    if (unit == "pc") return 25.4 / 6.0;
    if (unit.empty() || unit == "px") return 25.4 / 96.0;
    return 0.0; // % or em: no real size
}

} // namespace

int panelsFromSvg(const std::string& svg, std::vector<Panel>& out, std::string& error) {
    if (svg.size() > 4u * 1024u * 1024u) {
        error = "The SVG is larger than 4 MB";
        return 0;
    }
    double mmPerUnit = 25.4 / 96.0;
    std::vector<Affine> stack{Affine{}};
    std::vector<Shape> shapes;
    std::vector<bool> ours; // shapes marked class="sew" (a SculptForge pattern: only those are panels)
    size_t budget = 200000;  // path commands
    size_t points = 0;
    bool sawSvg = false;
    for (size_t i = 0; i < svg.size();) {
        const size_t lt = svg.find('<', i);
        if (lt == std::string::npos) break;
        if (svg.compare(lt, 4, "<!--") == 0) {
            const size_t e = svg.find("-->", lt + 4);
            i = e == std::string::npos ? svg.size() : e + 3;
            continue;
        }
        if (svg.compare(lt, 9, "<![CDATA[") == 0) {
            const size_t e = svg.find("]]>", lt + 9);
            i = e == std::string::npos ? svg.size() : e + 3;
            continue;
        }
        size_t gt = lt + 1;
        char quote = 0;
        while (gt < svg.size() && (quote || svg[gt] != '>')) { // '>' inside a quoted attribute isn't the end
            if (quote && svg[gt] == quote) quote = 0;
            else if (!quote && (svg[gt] == '"' || svg[gt] == '\'')) quote = svg[gt];
            ++gt;
        }
        if (gt >= svg.size()) break;
        const std::string tag = svg.substr(lt + 1, gt - lt - 1);
        i = gt + 1;
        if (tag.empty() || tag[0] == '?' || tag[0] == '!') continue; // declarations, DOCTYPE (never expanded)
        if (tag[0] == '/') {
            if (tag.compare(1, 1, "g") == 0 && (tag.size() == 2 || std::isspace(static_cast<unsigned char>(tag[2])))) {
                if (stack.size() > 1) stack.pop_back();
            }
            continue;
        }
        size_t ne = 0;
        while (ne < tag.size() && !std::isspace(static_cast<unsigned char>(tag[ne])) && tag[ne] != '/') ++ne;
        std::string el = tag.substr(0, ne);
        if (const size_t colon = el.find(':'); colon != std::string::npos) el = el.substr(colon + 1);
        const bool selfClosing = tag.back() == '/';
        const Affine m = parseTransform(attr(tag, "transform")).then(stack.back());
        if (el == "svg" && !sawSvg) {
            sawSvg = true;
            double w = 0.0, vb[4] = {0, 0, 0, 0};
            const double unit = lengthMm(attr(tag, "width"), w);
            const std::string vbs = attr(tag, "viewBox");
            Numbers n{vbs.data(), vbs.data() + vbs.size()};
            int k = 0;
            while (k < 4 && n.number(vb[k])) ++k;
            if (unit > 0.0 && w > 0.0 && k == 4 && vb[2] > 0.0) mmPerUnit = w * unit / vb[2];
            else if (unit > 0.0 && k != 4) mmPerUnit = 25.4 / 96.0; // no viewBox: user units are px
            continue;
        }
        if (el == "g") {
            if (!selfClosing) stack.push_back(m);
            if (stack.size() > 64) stack.pop_back();
            continue;
        }
        std::vector<Shape> found;
        if (el == "path") {
            found = parsePath(attr(tag, "d"), budget);
        } else if (el == "polygon" || el == "polyline") {
            const std::string ps = attr(tag, "points");
            Numbers n{ps.data(), ps.data() + ps.size()};
            Shape s;
            double x, y;
            while (s.pts.size() < 20000 && n.number(x) && n.number(y)) s.pts.push_back({x, y});
            if (s.pts.size() >= 2 && glm::length(s.pts.back() - s.pts.front()) < 1e-9) s.pts.pop_back();
            s.closed = el == "polygon" || s.pts.size() >= 3;
            s.ctrl.resize(s.pts.size());
            s.curved.assign(s.pts.size(), false);
            if (s.pts.size() >= 3) found.push_back(s);
        } else if (el == "rect") {
            double x = 0, y = 0, w = 0, h = 0;
            auto get = [&](const char* a, double& v) {
                const std::string s = attr(tag, a);
                Numbers n{s.data(), s.data() + s.size()};
                return n.number(v);
            };
            get("x", x);
            get("y", y);
            if (get("width", w) && get("height", h) && w > 0 && h > 0) {
                Shape s;
                s.pts = {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}};
                s.ctrl.resize(4);
                s.curved.assign(4, false);
                s.closed = true;
                found.push_back(s);
            }
        }
        const std::string cls = attr(tag, "class");
        const bool sew = cls == "sew";
        std::string name = attr(tag, "data-name");
        if (name.empty()) name = attr(tag, "id");
        for (Shape& s : found) {
            if (!s.closed) continue;
            points += s.pts.size();
            if (points > 20000 || shapes.size() >= 512) break;
            for (glm::dvec2& q : s.pts) q = m.apply(q);
            for (auto& c : s.ctrl)
                for (glm::dvec2& q : c) q = m.apply(q);
            s.name = name;
            shapes.push_back(std::move(s));
            ours.push_back(sew);
        }
    }
    if (!sawSvg) {
        error = "Not an SVG file";
        return 0;
    }
    const bool onlyOurs = std::find(ours.begin(), ours.end(), true) != ours.end();
    int added = 0, rejected = 0;
    for (size_t k = 0; k < shapes.size(); ++k) {
        if (onlyOurs && !ours[k]) continue;
        const Shape& s = shapes[k];
        Panel p;
        p.name = s.name.empty() ? "Panel " + std::to_string(out.size() + 1) : s.name.substr(0, 128);
        auto conv = [&](glm::dvec2 q) { return glm::vec2(float(q.x * mmPerUnit), float(-q.y * mmPerUnit)); };
        for (size_t e = 0; e < s.pts.size(); ++e) {
            p.points.push_back(conv(s.pts[e]));
            p.curves.push_back({bool(s.curved[e]), conv(s.ctrl[e][0]), conv(s.ctrl[e][1])});
        }
        // Drop points closer than 1 mm to the one before (exporters repeat points).
        for (size_t e = p.points.size(); e-- > 1 && p.points.size() > 3;)
            if (glm::length(p.points[e] - p.points[e - 1]) < 1.0f && !p.curves[e - 1].curved) {
                p.points.erase(p.points.begin() + long(e));
                p.curves[e - 1] = p.curves[e];
                p.curves.erase(p.curves.begin() + long(e));
            }
        if (p.points.size() == 2) { // two points joined by a curve and a line (or two curves): split each curve
            Pattern one;
            one.panels.push_back(std::move(p));
            for (uint32_t e = 2; e-- > 0;)
                if (one.panels[0].curves[e].curved) splitEdge(one, 0, e, 0.5f);
            p = std::move(one.panels[0]);
        }
        normalizePanel(p);
        std::string why;
        if (!validPanel(p, why)) {
            ++rejected;
            continue;
        }
        out.push_back(std::move(p));
        ++added;
    }
    if (!added) error = shapes.empty() ? "No closed shapes in the SVG" : "No usable panels in the SVG (" + std::to_string(rejected) + " shapes skipped)";
    else if (rejected) error = std::to_string(rejected) + " shapes skipped (open, crossing or too small)";
    return added;
}

} // namespace sf::garment
