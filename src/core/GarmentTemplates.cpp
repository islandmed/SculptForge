// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Garment templates from a size chart (core/GarmentTemplates.h). The proportions (ease, rise split, crotch
// extensions, finger lengths, ...) are written down in docs/design/garment-templates.md; keep the two in step.
#include "core/GarmentTemplates.h"

#include "core/Document.h"

#include <algorithm>
#include <cmath>

namespace sf::garment {

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kArc = 0.5522847f; // cubic control distance for a quarter circle (over the radius)
constexpr float kGap = 60.0f;      // mm between panels in the layout

void movePanel(Panel& p, glm::vec2 d) {
    for (glm::vec2& q : p.points) q += d;
    for (EdgeCurve& c : p.curves) {
        c.c1 += d;
        c.c2 += d;
    }
}

Panel makePanel(const std::string& name, std::vector<glm::vec2> pts) {
    Panel p;
    p.name = name;
    p.points = std::move(pts);
    p.curves.resize(p.points.size());
    return p;
}

void curve(Panel& p, uint32_t e, glm::vec2 c1, glm::vec2 c2) { p.curves[e] = {true, c1, c2}; }

// Edge names as the wearer has them for a front or back piece whose +x edges are on one side: a front's +x side is
// the wearer's left, a back's (seen from behind) their right. `side(front, true)`: the +x side's word.
const char* side(bool front, bool plusX) { return front == plusX ? "left" : "right"; }
std::string sided(bool front, bool plusX, const char* what) { return std::string(side(front, plusX)) + " " + what; }

struct Builder {
    Pattern pat;
    std::string error;
    uint32_t add(Panel p) {
        normalizePanel(p);
        pat.panels.push_back(std::move(p));
        return uint32_t(pat.panels.size() - 1);
    }
    void sew(uint32_t pa, uint32_t ea, float a0, float a1, uint32_t pb, uint32_t eb, float b0, float b1) {
        addSeam(pat, {{pa, ea, a0, a1}, {pb, eb, b0, b1}, false}, error);
    }
    void sew(uint32_t pa, uint32_t ea, uint32_t pb, uint32_t eb) { sew(pa, ea, 0, 1, pb, eb, 0, 1); }
    // `b` is mirroredPanel(a): sews a's edges `edges` to their mirror images on b.
    void sewMirror(uint32_t a, uint32_t b, std::initializer_list<uint32_t> edges) {
        const uint32_t n = uint32_t(pat.panels[a].points.size());
        for (uint32_t e : edges) sew(a, e, b, mirrorEdge(n, e));
    }
    float len(uint32_t panel, uint32_t edge) const { return edgeLength(pat.panels[panel], edge); }
};

// Lays the panels out flat in rows (in order, `kGap` apart, a new row past 2.4 m), lower-left at the origin.
void layout(Pattern& pat) {
    float x = 0.0f, y = 0.0f, rowHeight = 0.0f;
    for (Panel& p : pat.panels) {
        const glm::vec2 lo = panelMin(p), size = panelMax(p) - lo;
        if (x > 0.0f && x + size.x > 2400.0f) {
            x = 0.0f;
            y += rowHeight + kGap;
            rowHeight = 0.0f;
        }
        movePanel(p, glm::vec2(x, y) - lo);
        x += size.x + kGap;
        rowHeight = std::max(rowHeight, size.y);
    }
}

// --- Pieces ---

// A body panel centred on x = 0, hem at y = 0, edges as the T-shirt's: hem (0), side (1), armhole (2), shoulder (3),
// neckline (4), shoulder (5), armhole (6), side (7). The sides run from the hem's half width to the chest's.
Panel bodice(const std::string& name, bool front, float hemHalf, float chestHalf, float length, float shoulderHalf,
             float neckHalf, float neckDepth, float armholeDepth) {
    const float drop = std::clamp(0.1f * shoulderHalf, 15.0f, 60.0f);
    const float armY = std::max(length - armholeDepth, 0.3f * length);
    shoulderHalf = std::clamp(shoulderHalf, neckHalf + 30.0f, chestHalf - 10.0f);
    Panel p = makePanel(name, {{-hemHalf, 0}, {hemHalf, 0}, {chestHalf, armY}, {shoulderHalf, length - drop},
                               {neckHalf, length}, {-neckHalf, length}, {-shoulderHalf, length - drop},
                               {-chestHalf, armY}});
    // The armhole scoops in from the underarm and rises to the shoulder point.
    const float in = shoulderHalf - 0.25f * (chestHalf - shoulderHalf) - 8.0f;
    const float rise = length - drop - armY;
    curve(p, 2, {in, armY}, {in, armY + 0.5f * rise});
    curve(p, 6, {-in, armY + 0.5f * rise}, {-in, armY});
    const float cy = length - 4.0f * std::min(neckDepth, 0.6f * rise) / 3.0f; // the cubic dips 3/4 of this
    curve(p, 4, {neckHalf - 5.0f, cy}, {-neckHalf + 5.0f, cy});
    p.notches = {{1, 0.5f}, {7, 0.5f}};
    p.edgeNames = {"hem", sided(front, true, "side"), sided(front, true, "armhole"), sided(front, true, "shoulder"),
                   "neckline", sided(front, false, "shoulder"), sided(front, false, "armhole"), sided(front, false, "side")};
    return p;
}

// A sleeve centred on x = 0: hem (0), underarm (1), cap (2, over the top from the +x underarm), underarm (3); the
// cap's height is found so the cap is `capLength` long.
Panel sleeve(const std::string& name, float halfWidth, float hemHalf, float underarmLength, float capLength,
             const char* hemName = "hem") {
    halfWidth = std::min(halfWidth, 0.45f * capLength);
    hemHalf = std::min(hemHalf, halfWidth);
    Panel p = makePanel(name, {{-hemHalf, 0}, {hemHalf, 0}, {halfWidth, underarmLength}, {-halfWidth, underarmLength}});
    float lo = 0.0f, hi = capLength;
    for (int it = 0; it < 48; ++it) {
        const float m = 0.5f * (lo + hi);
        curve(p, 2, {0.55f * halfWidth, underarmLength + m}, {-0.55f * halfWidth, underarmLength + m});
        (edgeLength(p, 2) < capLength ? lo : hi) = m;
    }
    p.notches = {{2, 0.5f}};
    p.edgeNames = {hemName, "underarm", "cap", "underarm"};
    return p;
}

// A skirt panel centred on x = 0: hem (0), side (1), hip curve (2), waist (3), hip curve (4), side (5).
Panel skirtPanel(const std::string& name, bool front, float waistHalf, float hipHalf, float hemHalf, float length) {
    const float hipDepth = std::min(190.0f, 0.45f * length), hipY = length - hipDepth;
    Panel p = makePanel(name, {{-hemHalf, 0}, {hemHalf, 0}, {hipHalf, hipY}, {waistHalf, length}, {-waistHalf, length},
                               {-hipHalf, hipY}});
    const float mid = waistHalf + 0.35f * (hipHalf - waistHalf);
    curve(p, 2, {hipHalf, hipY + 0.45f * hipDepth}, {mid, length - 0.1f * hipDepth});
    curve(p, 4, {-mid, length - 0.1f * hipDepth}, {-hipHalf, hipY + 0.45f * hipDepth});
    p.notches = {{1, 0.9f}, {5, 0.1f}};
    p.edgeNames = {"hem", sided(front, true, "side"), sided(front, true, "hip"), "waist", sided(front, false, "hip"),
                   sided(front, false, "side")};
    return p;
}

// The left half of a hood, seen from outside (the face on the left): neck (0, from the centre front to the centre
// back, `neck` long), back (1), crown (2), top (3), face opening (4).
Panel hoodHalf(const std::string& name, float neck, float head) {
    const float depth = std::max(0.42f * head + 30.0f, neck + 20.0f), height = 0.62f * head + 20.0f;
    const float back = depth + 10.0f;
    Panel p = makePanel(name, {{0, 0}, {neck, 0}, {back, 0.5f * height}, {0.75f * depth, height}, {0, height}});
    curve(p, 1, {neck + 0.6f * (back - neck), 0.12f * height}, {back, 0.3f * height});
    curve(p, 2, {back, 0.8f * height}, {0.92f * depth, height});
    p.grainDegrees = 90.0f;
    p.edgeNames = {"neck", "back", "crown", "top", "face opening"};
    return p;
}

// --- Templates ---

void tshirt(const SizeChart& s, float length, Builder& b) {
    b.pat = tshirtPattern(s.get("chest"), length > 0.0f ? length : 700.0f);
}

void trousers(const SizeChart& s, float length, Builder& b) {
    const float hip = s.get("hip") + 40.0f, rise = s.get("rise") + 15.0f;
    const float inseam = length > 0.0f ? std::clamp(length, 100.0f, 1200.0f) : s.get("inseam");
    const float yHip = inseam + 0.4f * rise, waistY = inseam + rise;
    // Front and back of one leg, outseam on +x and the hip's side point at x = 0. The two share the outseam's and
    // the inseam's run exactly, so those seams match whatever the sizes: the back is wider at the hip and the
    // crotch, and as much wider at the hem.
    const float wF = 0.25f * hip - 10.0f, wB = 0.25f * hip + 10.0f, eF = 0.06f * hip, eB = 0.11f * hip;
    const float hemF = std::max(0.2f * hip, 120.0f), hemB = hemF + (wB + eB) - (wF + eF);
    const float dOut = 0.35f * (wF + eF - hemF);
    const float inF = 10.0f, inB = 25.0f, raise = 20.0f; // centre front/back in from the hip line; back rise up
    float dW = 0.5f * (wF + wB - inF - inB - 0.5f * (s.get("waist") + 20.0f));
    dW = std::clamp(dW, -0.25f * wF, wF - inF - 60.0f);
    auto leg = [&](const std::string& name, float w, float e, float hem, float in, float up) {
        Panel p = makePanel(name, {{-dOut - hem, 0}, {-dOut, 0}, {0, yHip}, {-dW, waistY}, {-w + in, waistY + up},
                                   {-w - e, inseam}});
        curve(p, 4, {-w, inseam + 0.3f * rise}, {-w - 0.5f * e, inseam + 0.02f * rise});
        p.notches = {{1, 0.45f}};
        p.edgeNames = {"hem", "outseam", "hip", "waist", "crotch", "inseam"};
        return p;
    };
    const Panel front = leg("Front", wF, eF, hemF, inF, 0.0f), back = leg("Back", wB, eB, hemB, inB, raise);
    auto named = [](Panel p, const char* name) {
        p.name = name;
        return p;
    };
    // Front left and back right as drawn; the other two mirrored (the back is seen from behind).
    const uint32_t fl = b.add(named(front, "Left front")), fr = b.add(named(mirroredPanel(front), "Right front"));
    const uint32_t bl = b.add(named(mirroredPanel(back), "Left back")), br = b.add(named(back, "Right back"));
    const uint32_t n = 6;
    for (uint32_t e : {1u, 2u, 5u}) {           // outseam (hem to hip, hip to waist) and inseam
        b.sew(fl, e, bl, mirrorEdge(n, e));      // left leg
        b.sew(br, e, fr, mirrorEdge(n, e));      // right leg
    }
    b.sewMirror(fl, fr, {4}); // centre front
    b.sewMirror(br, bl, {4}); // centre back
}

void skirt(const SizeChart& s, float length, bool aline, Builder& b, float waistHalf = 0.0f) {
    const float len = length > 0.0f ? std::clamp(length, 150.0f, 1400.0f) : s.get("waist_to_knee");
    const float waistH = waistHalf > 0.0f ? waistHalf : 0.25f * (s.get("waist") + 20.0f);
    const float hipH = std::max(0.25f * (s.get("hip") + 40.0f), waistH);
    const float hipDepth = std::min(190.0f, 0.45f * len);
    const float hemH = aline ? hipH + 0.25f * (len - hipDepth) : hipH;
    const uint32_t f = b.add(skirtPanel("Skirt front", true, waistH, hipH, hemH, len));
    const uint32_t k = b.add(skirtPanel("Skirt back", false, waistH, hipH, hemH, len));
    // Sides as the T-shirt's: the front's +x (the wearer's left) meets the back's -x.
    b.sew(f, 1, k, 5);
    b.sew(f, 2, k, 4);
    b.sew(f, 4, k, 2);
    b.sew(f, 5, k, 1);
    if (waistHalf > 0.0f) return; // a dress: the bodice sews on instead of a band
    // Waistband (doubled: 40 mm finished), round the front then the back, its ends joined at the side.
    const float wf = b.len(f, 3), wk = b.len(k, 3);
    const uint32_t band = b.add(rectanglePanel("Waistband", wf + wk, 80.0f));
    b.pat.panels[band].grainDegrees = 0.0f;
    b.pat.panels[band].edgeNames = {"skirt edge", "end", "fold", "end"};
    b.sew(band, 0, 0.0f, wf / (wf + wk), f, 3, 0.0f, 1.0f);
    b.sew(band, 0, wf / (wf + wk), 1.0f, k, 3, 0.0f, 1.0f);
    b.sew(band, 1, band, 3);
}

// Sews a hood's two halves together and onto a neckline: the left half's neck runs from the centre front to the
// centre back, so along the wearer's left it meets `left` (front part first); the right half's mirrored neck runs
// back to front and meets `right`.
void sewHood(Builder& b, uint32_t hl, uint32_t hr, const std::vector<SeamSide>& left, const std::vector<SeamSide>& right) {
    b.sewMirror(hl, hr, {1, 2, 3});
    auto onto = [&](uint32_t hood, uint32_t edge, const std::vector<SeamSide>& sides) {
        float total = 0.0f;
        for (const SeamSide& sd : sides) total += seamSideLength(b.pat, sd);
        float at = 0.0f;
        for (const SeamSide& sd : sides) {
            const float next = at + seamSideLength(b.pat, sd) / total;
            b.sew(hood, edge, at, std::min(next, 1.0f), sd.panel, sd.edge, sd.t0, sd.t1);
            at = next;
        }
    };
    onto(hl, 0, left);
    onto(hr, mirrorEdge(5, 0), right);
}

void hoodie(const SizeChart& s, float length, Builder& b) {
    const float half = 0.25f * (s.get("chest") + 120.0f);
    const float len = length > 0.0f ? std::clamp(length, 300.0f, 1300.0f) : s.get("back_length") + 230.0f;
    const float neckHalf = std::clamp(0.24f * s.get("neck"), 55.0f, 0.5f * half);
    const float shoulderHalf = 0.5f * s.get("shoulder") + 10.0f, armDepth = 0.3f * s.get("chest") + 10.0f;
    const uint32_t f = b.add(bodice("Front", true, half, half, len, shoulderHalf, neckHalf, 1.05f * neckHalf, armDepth));
    const uint32_t k = b.add(bodice("Back", false, half, half, len, shoulderHalf, neckHalf, 0.25f * neckHalf, armDepth));
    b.sew(f, 1, k, 7);
    b.sew(f, 7, k, 1);
    b.sew(f, 3, k, 5);
    b.sew(f, 5, k, 3);
    const float armhole = b.len(f, 2) + b.len(k, 6), cap = 1.015f * armhole;
    const float sleeveLen = s.get("arm") + 30.0f, upper = 0.5f * (s.get("upper_arm") + 90.0f);
    const float underarm = std::max(120.0f, sleeveLen - 0.3f * cap);
    const float wristHalf = 0.5f * (s.get("wrist") + 70.0f);
    const uint32_t ls = b.add(sleeve("Left sleeve", upper, wristHalf, underarm, cap, "cuff"));
    const uint32_t rs = b.add(sleeve("Right sleeve", upper, wristHalf, underarm, cap, "cuff"));
    // As the T-shirt: the left sleeve's +x half of the cap faces back, the right's faces front.
    b.sew(f, 2, 0, 1, ls, 2, 0.5f, 1);
    b.sew(k, 6, 0, 1, ls, 2, 0, 0.5f);
    b.sew(f, 6, 0, 1, rs, 2, 0, 0.5f);
    b.sew(k, 2, 0, 1, rs, 2, 0.5f, 1);
    b.sew(ls, 1, ls, 3);
    b.sew(rs, 1, rs, 3);
    // Hood: each half's neck is half the neckline (front edge 4 runs from the wearer's left; the back's, seen from
    // behind, from the wearer's right).
    const float neck = 0.5f * (b.len(f, 4) + b.len(k, 4));
    const Panel hood = hoodHalf("Left hood", neck, s.get("head"));
    const uint32_t hl = b.add(hood);
    Panel right = mirroredPanel(hood);
    right.name = "Right hood";
    const uint32_t hr = b.add(right);
    sewHood(b, hl, hr, {{f, 4, 0.0f, 0.5f}, {k, 4, 0.5f, 1.0f}}, {{k, 4, 0.0f, 0.5f}, {f, 4, 0.5f, 1.0f}});
    // Kangaroo pocket, topstitched onto the front by the user (no seam: it lies on the fabric, not edge to edge).
    const float pw = std::min(1.2f * half, 1.8f * half - 40.0f), ph = std::clamp(0.3f * len, 120.0f, 0.45f * len);
    Panel pocket = makePanel("Pocket", {{0, 0}, {pw, 0}, {pw, 0.35f * ph}, {0.8f * pw, ph}, {0.2f * pw, ph},
                                        {0, 0.35f * ph}});
    curve(pocket, 2, {0.86f * pw, 0.45f * ph}, {0.8f * pw, 0.75f * ph}); // the hand openings scoop in
    curve(pocket, 4, {0.2f * pw, 0.75f * ph}, {0.14f * pw, 0.45f * ph});
    pocket.edgeNames = {"bottom", "side", "hand opening", "top", "hand opening", "side"};
    b.add(pocket);
}

void dress(const SizeChart& s, float length, Builder& b) {
    const float half = 0.25f * (s.get("chest") + 60.0f);
    const float waistHalf = std::min(0.25f * (s.get("waist") + 50.0f), half);
    const float len = s.get("back_length") + 15.0f;
    const float neckHalf = std::clamp(0.22f * s.get("neck"), 50.0f, 0.5f * half);
    const float shoulderHalf = 0.5f * s.get("shoulder") - 5.0f, armDepth = 0.24f * s.get("chest");
    const uint32_t f = b.add(bodice("Bodice front", true, waistHalf, half, len, shoulderHalf, neckHalf, 1.3f * neckHalf, armDepth));
    const uint32_t k = b.add(bodice("Bodice back", false, waistHalf, half, len, shoulderHalf, neckHalf, 0.3f * neckHalf, armDepth));
    b.pat.panels[f].edgeNames[0] = b.pat.panels[k].edgeNames[0] = "waist"; // (the bodice ends at the waist seam)
    b.sew(f, 1, k, 7);
    b.sew(f, 7, k, 1);
    b.sew(f, 3, k, 5);
    b.sew(f, 5, k, 3);
    const uint32_t first = uint32_t(b.pat.panels.size());
    skirt(s, length > 0.0f ? length : s.get("waist_to_knee") + 50.0f, true, b, waistHalf);
    b.sew(f, 0, first, 3);     // waist seams (the bodice's hem runs +x, the skirt's waist -x)
    b.sew(k, 0, first + 1, 3);
}

void cloak(const SizeChart& s, float length, Builder& b) {
    // A half circle round the shoulders, open at the front, cut as two quarters joined at the centre back.
    const float r = (s.get("neck") + 40.0f) / kPi;
    const float R = r + (length > 0.0f ? std::clamp(length, 200.0f, 2000.0f) : 0.6f * s.get("height"));
    // Left half, seen from behind (circle centre at the origin): front edge (0), hem (1), centre back (2), neck (3).
    Panel left = makePanel("Left cape", {{-r, 0}, {-R, 0}, {0, -R}, {0, -r}});
    curve(left, 1, {-R, -kArc * R}, {-kArc * R, -R});
    curve(left, 3, {-kArc * r, -r}, {-r, -kArc * r});
    left.grainDegrees = 90.0f;
    left.edgeNames = {"front edge", "hem", "centre back", "neck"};
    const uint32_t cl = b.add(left);
    Panel right = mirroredPanel(left);
    right.name = "Right cape";
    const uint32_t cr = b.add(right);
    b.sewMirror(cl, cr, {2});
    const float neck = b.len(cl, 3);
    const Panel hood = hoodHalf("Left hood", neck, s.get("head"));
    const uint32_t hl = b.add(hood);
    Panel rh = mirroredPanel(hood);
    rh.name = "Right hood";
    const uint32_t hr = b.add(rh);
    sewHood(b, hl, hr, {{cl, 3, 0.0f, 1.0f}}, {{cr, mirrorEdge(4, 3), 0.0f, 1.0f}});
}

// One glove's outline (the back of the left hand, thumb on +x), from the wrist edge (0) counter-clockwise.
Panel gloveBack(const SizeChart& s) {
    const float Lh = s.get("hand_length"), width = 0.5f * s.get("hand") + 12.0f;
    const float cuff = 50.0f, knuckle = cuff + 0.55f * Lh, wristHalf = std::min(0.25f * s.get("wrist") + 12.0f, 0.5f * width);
    const float fw = 0.25f * width, gap = 0.18f * fw;
    const float tips[4] = {0.40f, 0.45f, 0.42f, 0.33f}; // index, middle, ring, little (of the hand length)
    std::vector<glm::vec2> pts = {{0.5f * width - wristHalf, 0}, {0.5f * width + wristHalf, 0}};
    // names[i]: the edge from pts[i] to the next point.
    std::vector<std::string> names = {"wrist", "side", "thumb", "thumb tip", "thumb"};
    const char* fingers[4] = {"index finger", "middle finger", "ring finger", "little finger"};
    // Thumb: out and up from the side, 0.36 of the hand long on its lower side.
    const glm::vec2 d = glm::normalize(glm::vec2(0.55f, 1.0f));
    const float t0 = cuff + 0.2f * Lh, t1 = cuff + 0.45f * Lh, tl = 0.36f * Lh;
    const glm::vec2 base0(width, t0), base1(width, t1);
    pts.push_back(base0);
    pts.push_back(base0 + d * tl);
    pts.push_back(base1 + d * (tl - (t1 - t0) * d.y)); // the tip square to the thumb (no sharp corner to weld)
    pts.push_back(base1);
    std::vector<uint32_t> tipEdges = {3};
    for (int k = 0; k < 4; ++k) {
        const float xr = width - float(k) * fw, xl = xr - fw, tip = knuckle + tips[k] * Lh;
        names.push_back(fingers[k]); // (from the point before: up the finger's thumb side)
        pts.push_back({xr - 0.5f * gap, tip});
        tipEdges.push_back(uint32_t(pts.size() - 1));
        names.push_back(std::string(fingers[k]) + " tip");
        pts.push_back({xl + 0.5f * gap, tip});
        if (k < 3) {
            names.push_back(fingers[k]);
            pts.push_back({xl, knuckle});
        }
    }
    names.push_back(fingers[3]);
    pts.push_back({0, cuff + 0.45f * Lh});
    names.push_back("side");
    Panel p = makePanel("Left glove back", std::move(pts));
    p.edgeNames = std::move(names);
    for (uint32_t e : tipEdges) {
        const glm::vec2 a = p.points[e], c = p.points[(e + 1) % p.points.size()];
        const float bulge = 0.45f * glm::length(c - a);
        const glm::vec2 out = e == 3 ? d : glm::vec2(0, 1);
        curve(p, e, a + out * bulge, c + out * bulge);
    }
    return p;
}

void glove(const SizeChart& s, float, Builder& b) {
    // A pair: each glove is a back and a palm (its mirror), sewn all round but the wrist.
    const Panel back = gloveBack(s);
    const uint32_t n = uint32_t(back.points.size());
    auto pair = [&](Panel bk, uint32_t wrist, const char* backName, const char* palmName) {
        bk.seamAllowance = 5.0f; // 10 mm would run the cutting lines between the fingers together
        Panel palm = mirroredPanel(bk);
        bk.name = backName;
        palm.name = palmName;
        const uint32_t a = b.add(bk), c = b.add(palm);
        for (uint32_t e = 0; e < n; ++e)
            if (e != wrist) b.sew(a, e, c, mirrorEdge(n, e));
    };
    pair(back, 0, "Left glove back", "Left glove palm");
    pair(mirroredPanel(back), mirrorEdge(n, 0), "Right glove back", "Right glove palm"); // mirrored: the wrist moves
    // A finger needs about five particles across, or the back and the palm weld into a strip with no inside.
    b.pat.particleDistance = std::clamp(0.05f * (0.5f * s.get("hand") + 12.0f), 3.0f, 6.0f);
}

const std::vector<MeasureSpec> kMeasures = {
    {"chest", "Chest girth, round the fullest part", 960, 400, 2000},
    {"waist", "Waist girth, round the natural waist", 820, 350, 2000},
    {"hip", "Hip girth, round the fullest part", 1000, 450, 2200},
    {"neck", "Neck girth, round the base of the neck", 380, 220, 600},
    {"shoulder", "Shoulder width, shoulder point to shoulder point across the back", 440, 250, 700},
    {"arm", "Arm length, shoulder point to wrist with the arm hanging", 600, 250, 900},
    {"upper_arm", "Upper arm girth, round the fullest part", 320, 150, 700},
    {"wrist", "Wrist girth", 170, 100, 350},
    {"back_length", "Back length, the bone at the base of the neck to the waist", 430, 200, 700},
    {"inseam", "Inseam, crotch to the floor (or to where the trousers end)", 790, 200, 1100},
    {"rise", "Rise, waist to the chair seat, sitting", 260, 120, 450},
    {"waist_to_knee", "Waist to knee, down the side", 580, 200, 800},
    {"height", "Height", 1750, 800, 2300},
    {"head", "Head girth, round the forehead and the back of the head", 570, 400, 700},
    {"hand", "Hand girth, round the knuckles (thumb out)", 210, 120, 320},
    {"hand_length", "Hand length, wrist crease to the tip of the middle finger", 190, 100, 260},
};

const std::vector<TemplateSpec> kTemplates = {
    {"tshirt", "T-shirt: front, back, two short sleeves (chest; length = shoulder to hem, default 700)"},
    {"trousers", "Trousers: front and back of each leg (waist, hip, rise, inseam; length = inseam)"},
    {"skirt", "Straight skirt: front, back, waistband (waist, hip; length = waist to hem, default waist to knee)"},
    {"skirt_aline", "A-line skirt: flared front and back, waistband (waist, hip; length = waist to hem)"},
    {"hoodie", "Hoodie: front, back, long sleeves, two-piece hood, kangaroo pocket to topstitch (chest, neck, "
               "shoulder, arm, upper arm, wrist, head; length = shoulder to hem)"},
    {"dress", "Dress: sleeveless bodice and A-line skirt joined at the waist (chest, waist, hip, neck, shoulder, "
              "back length; length = waist to hem)"},
    {"cloak", "Cloak: a half-circle cape in two quarters with a hood (neck, head, height; length = neck to hem)"},
    {"glove", "Gloves: a pair, each a back and a palm sewn round the fingers (hand, hand length, wrist)"},
};

} // namespace

const std::vector<MeasureSpec>& measureSpecs() { return kMeasures; }
const std::vector<TemplateSpec>& templateSpecs() { return kTemplates; }

SizeChart::SizeChart() {
    for (const MeasureSpec& m : kMeasures) mm.push_back(m.fallback);
}

float SizeChart::get(const std::string& key) const {
    for (size_t i = 0; i < kMeasures.size(); ++i)
        if (key == kMeasures[i].key) return mm[i];
    return 0.0f;
}

bool SizeChart::set(const std::string& key, float value) {
    if (!std::isfinite(value)) return false;
    for (size_t i = 0; i < kMeasures.size(); ++i)
        if (key == kMeasures[i].key) {
            mm[i] = std::clamp(value, kMeasures[i].min, kMeasures[i].max);
            return true;
        }
    return false;
}

Json sizeChartToJson(const SizeChart& s) {
    Json j = Json::object();
    for (size_t i = 0; i < kMeasures.size(); ++i) j.set(kMeasures[i].key, double(std::round(s.mm[i] * 10.0f) / 10.0f));
    return j;
}

SizeChart sizeChartFromJson(const Json& j) {
    SizeChart s;
    if (!j.isObject()) return s;
    for (const MeasureSpec& m : kMeasures)
        if (j.has(m.key) && j[m.key].isNumber()) s.set(m.key, float(j[m.key].asNumber()));
    return s;
}

SizeChart documentSizeChart(const Document& doc) {
    if (!doc.projectSettings.isObject() || !doc.projectSettings.has("garment_sizes")) return {};
    return sizeChartFromJson(doc.projectSettings["garment_sizes"]);
}

void setDocumentSizeChart(Document& doc, const SizeChart& s) {
    if (!doc.projectSettings.isObject()) doc.projectSettings = Json::object();
    doc.projectSettings.set("garment_sizes", sizeChartToJson(s));
    doc.modified = true;
}

Panel mirroredPanel(const Panel& src) {
    const uint32_t n = uint32_t(src.points.size());
    Panel p = src;
    if (n < 3) return p;
    p.curves.resize(n);
    auto M = [](glm::vec2 q) { return glm::vec2(-q.x, q.y); };
    const std::vector<EdgeCurve> curves = src.curves.size() == n ? src.curves : std::vector<EdgeCurve>(n);
    for (uint32_t j = 0; j < n; ++j) {
        p.points[j] = M(src.points[n - 1 - j]);
        // Edge j is the mirror of edge mirrorEdge(n, j), run backwards: its control points swap.
        const EdgeCurve& c = curves[mirrorEdge(n, j)];
        p.curves[j] = {c.curved, M(c.c2), M(c.c1)};
    }
    for (Notch& nt : p.notches) nt = {mirrorEdge(n, nt.edge % n), 1.0f - nt.t};
    if (src.edgeNames.size() == n)
        for (uint32_t j = 0; j < n; ++j) p.edgeNames[j] = src.edgeNames[mirrorEdge(n, j)];
    p.grainDegrees = 180.0f - src.grainDegrees;
    return p;
}

void appendPattern(Pattern& pat, const Pattern& add) {
    if (add.panels.empty()) return;
    if (pat.panels.empty()) {
        pat = add;
        return;
    }
    glm::vec2 lo(1e30f), hi(-1e30f), addLo(1e30f);
    for (const Panel& p : pat.panels) {
        lo = glm::min(lo, panelMin(p));
        hi = glm::max(hi, panelMax(p));
    }
    for (const Panel& p : add.panels) addLo = glm::min(addLo, panelMin(p));
    const glm::vec2 d(hi.x + kGap - addLo.x, lo.y - addLo.y);
    const uint32_t base = uint32_t(pat.panels.size());
    for (Panel p : add.panels) {
        movePanel(p, d);
        pat.panels.push_back(std::move(p));
    }
    for (Seam s : add.seams) {
        s.a.panel += base;
        s.b.panel += base;
        pat.seams.push_back(s);
    }
}

bool makeTemplate(const std::string& id, const SizeChart& size, float lengthMm, Pattern& out, std::string& error) {
    Builder b;
    if (id == "tshirt") tshirt(size, lengthMm, b);
    else if (id == "trousers") trousers(size, lengthMm, b);
    else if (id == "skirt") skirt(size, lengthMm, false, b);
    else if (id == "skirt_aline") skirt(size, lengthMm, true, b);
    else if (id == "hoodie") hoodie(size, lengthMm, b);
    else if (id == "dress") dress(size, lengthMm, b);
    else if (id == "cloak") cloak(size, lengthMm, b);
    else if (id == "glove") glove(size, lengthMm, b);
    else {
        std::string ids;
        for (const TemplateSpec& t : kTemplates) ids += (ids.empty() ? "" : ", ") + std::string(t.id);
        error = "Unknown template \"" + id + "\" (" + ids + ")";
        return false;
    }
    if (!b.error.empty()) {
        error = b.error;
        return false;
    }
    if (id != "tshirt") layout(b.pat); // the T-shirt keeps its own layout
    out = std::move(b.pat);
    return true;
}

} // namespace sf::garment
