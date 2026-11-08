// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Garments (core/Garment.h): pattern panels, even triangulation, seams that weld into one consistently oriented
// garment (a T-shirt sewn from its template is a single surface with exactly four openings: hem, neck, two
// sleeves), the flat rest shape, project and file round trips (SVG, DXF), the SVG reader against
// hostile input, and the garment_* commands.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Garment.h"
#include "core/Primitives.h"
#include "core/Project.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <map>
#include <random>
#include <set>
#include <sstream>

using namespace sf;
using namespace sf::garment;

namespace {

float meshArea(const GarmentMesh& m, uint32_t panel) {
    double a = 0.0;
    for (uint32_t t = m.panelFirstTriangle[panel]; t < m.panelFirstTriangle[panel + 1]; ++t) {
        const glm::uvec3 tri = m.triangles[t];
        const glm::vec2 u = m.flat[tri.y] - m.flat[tri.x], v = m.flat[tri.z] - m.flat[tri.x];
        a += 0.5 * double(u.x * v.y - u.y * v.x);
    }
    return float(a);
}

// Boundary loops and orientation of a closed-up mesh: every edge used once (border) or twice in opposite
// directions (consistently oriented); edges used twice the same way or more than twice break it.
struct Topology {
    int borderLoops = 0;
    bool consistent = true;
    bool manifold = true;
};
Topology topology(const Mesh& m) {
    std::map<std::pair<uint32_t, uint32_t>, int> directed;
    for (const glm::uvec3& t : m.triangles)
        for (int k = 0; k < 3; ++k) ++directed[{t[k], t[(k + 1) % 3]}];
    Topology r;
    std::map<uint32_t, uint32_t> next; // border: a -> b
    for (const auto& [e, n] : directed) {
        if (n > 1) r.consistent = false;
        const auto back = directed.find({e.second, e.first});
        const int other = back == directed.end() ? 0 : back->second;
        if (n + other > 2) r.manifold = false;
        if (other == 0) {
            if (next.count(e.first)) r.manifold = false;
            next[e.first] = e.second;
        }
    }
    std::set<uint32_t> seen;
    for (const auto& [a, b] : next) {
        if (seen.count(a)) continue;
        ++r.borderLoops;
        uint32_t v = a;
        for (size_t guard = 0; guard <= next.size() && !seen.count(v); ++guard) {
            seen.insert(v);
            const auto it = next.find(v);
            if (it == next.end()) break;
            v = it->second;
        }
    }
    return r;
}

std::string readAll(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

} // namespace

TEST(garment_tshirt_template_is_valid_and_matched) {
    const Pattern pat = tshirtPattern();
    CHECK(pat.panels.size() == 4);
    CHECK(pat.seams.size() == 10);
    CHECK(patternProblems(pat).empty());
    for (const Seam& s : pat.seams) {
        const SeamCheck c = checkSeam(pat, s);
        CHECK(c.lengthsMatch);
        CHECK(!c.twisted);
    }
    for (const Panel& p : pat.panels) CHECK(panelArea(p) > 0.0f);
    // Real size: the front is about half the chest plus ease wide and 70 cm long.
    const glm::vec2 size = panelMax(pat.panels[0]) - panelMin(pat.panels[0]);
    CHECK(std::abs(size.x - 520.0f) < 1.0f && std::abs(size.y - 700.0f) < 1.0f);
}

TEST(garment_triangulation_is_even_and_covers_each_panel) {
    Pattern pat = tshirtPattern();
    for (float h : {10.0f, 15.0f, 25.0f}) {
        pat.particleDistance = h;
        GarmentMesh m;
        std::string error;
        CHECK(triangulatePattern(pat, m, error));
        CHECK(error.empty());
        // Exactly the outline's area (the outline is sampled as chords: curves lose a sliver).
        for (uint32_t p = 0; p < pat.panels.size(); ++p) {
            const float want = panelArea(pat.panels[p]), got = meshArea(m, p);
            CHECK(std::abs(got - want) < 0.01f * want);
        }
        const MeshQuality q = meshQuality(m, h);
        CHECK(q.minAngle > 20.0f);
        CHECK(q.minEdge > 0.4f && q.maxEdge < 1.8f);
        // About one particle per equilateral triangle pair: area / (0.866 h^2).
        double area = 0.0;
        for (const Panel& p : pat.panels) area += panelArea(p);
        const double expect = area / (0.866 * h * h);
        CHECK(double(m.vertexCount()) > 0.8 * expect && double(m.vertexCount()) < 1.4 * expect);
        // Every triangle counter-clockwise (faces out once placed).
        bool ccw = true;
        for (const glm::uvec3& t : m.triangles) {
            const glm::vec2 u = m.flat[t.y] - m.flat[t.x], v = m.flat[t.z] - m.flat[t.x];
            ccw = ccw && u.x * v.y - u.y * v.x > 0.0f;
        }
        CHECK(ccw);
    }
}

TEST(garment_sewn_tshirt_welds_into_one_oriented_surface) {
    const Pattern pat = tshirtPattern();
    GarmentMesh m;
    std::string error;
    CHECK(triangulatePattern(pat, m, error));
    // One link per point along each seam, both sides the same count.
    CHECK(!m.links.empty());
    std::vector<glm::vec3> rest;
    flatRest(m, rest);
    CHECK(rest.size() == m.vertexCount() && std::abs(rest[0].x - m.flat[0].x * 0.001f) < 1e-6f && rest[0].z == 0.0f);
    const Mesh flat = garmentToMesh(m, rest, 0.0f);
    CHECK(flat.triangleCount() == m.triangles.size());
    const Topology t = topology(flat);
    CHECK(t.manifold);
    CHECK(t.consistent);     // seams sewn the right way round: no panel turned over
    CHECK(t.borderLoops == 4); // hem, neck, two sleeve hems
    CHECK(flat.uvs.size() == flat.triangleCount() * 3);
    bool inUnit = true;
    for (glm::vec2 uv : flat.uvs) inUnit = inUnit && uv.x >= 0 && uv.x <= 1 && uv.y >= 0 && uv.y <= 1;
    CHECK(inUnit);
    std::set<uint32_t> groups(flat.facegroups.begin(), flat.facegroups.end());
    CHECK(groups == std::set<uint32_t>({1, 2, 3, 4}));
    // Thickened: closed (no border at all) and still consistently oriented.
    const Mesh thick = garmentToMesh(m, rest, 2.0f);
    const Topology tt = topology(thick);
    CHECK(tt.manifold && tt.consistent && tt.borderLoops == 0);
    CHECK(thick.vertexCount() == 2 * flat.vertexCount());
}

TEST(garment_flipped_seam_is_reported_and_twists) {
    Pattern pat = tshirtPattern();
    pat.seams[0].flipped = true;
    CHECK(checkSeam(pat, pat.seams[0]).twisted);
    CHECK(!patternProblems(pat).empty());
    GarmentMesh m;
    std::string error;
    CHECK(triangulatePattern(pat, m, error));
    std::vector<glm::vec3> rest;
    flatRest(m, rest);
    CHECK(!topology(garmentToMesh(m, rest, 0.0f)).consistent);
}

TEST(garment_pattern_round_trips_json_and_project) {
    Pattern pat = tshirtPattern(1000.0f, 650.0f);
    pat.particleDistance = 12.0f;
    pat.fabric = "denim";
    pat.seams[3].flipped = true;
    Pattern back;
    std::string error;
    CHECK(patternFromJson(patternToJson(pat), back, error));
    CHECK(patternToJson(back).dump() == patternToJson(pat).dump());
    // In a project.
    Document doc = makeDoc(makeIcosphere(2));
    setDocumentPattern(doc, pat);
    const std::string path = tempPath("garment_roundtrip.sfproj");
    CHECK(saveProject(path, doc, error));
    Document loaded;
    CHECK(loadProject(path, loaded, error));
    Pattern got;
    CHECK(documentPattern(loaded, got));
    CHECK(patternToJson(got).dump() == patternToJson(pat).dump());
    std::remove(path.c_str());
    // Hostile JSON: wrong types, huge counts, bad seams are refused or dropped, never crash.
    Json j;
    CHECK(Json::parse(R"({"panels":[{"name":"x","points":[0,0,1e30,0,0,5]},{"points":"no"},
        {"name":"ok","points":[0,0,100,0,100,100,0,100],"curves":[[99,0,0,0,0]],"notches":[[-1,2]]}],
        "seams":[{"a":[0,0,0,1],"b":[5,0,0,1]},{"a":[0,0,0.5,0.2],"b":[0,1,0,1]}]})",
                      j, error));
    Pattern bad;
    error.clear();
    CHECK(patternFromJson(j, bad, error));
    CHECK(bad.panels.size() == 1 && bad.panels[0].name == "ok" && bad.seams.empty());
    CHECK(!error.empty());
}

TEST(garment_svg_round_trips_at_real_size) {
    const Pattern pat = tshirtPattern();
    const std::string svg = patternToSvg(pat);
    CHECK(svg.find("mm\" height=") != std::string::npos);
    CHECK(svg.find("class=\"cut\"") != std::string::npos && svg.find("class=\"notch\"") != std::string::npos);
    std::vector<Panel> panels;
    std::string error;
    CHECK(panelsFromSvg(svg, panels, error) == 4);
    CHECK(error.empty());
    for (size_t i = 0; i < panels.size(); ++i) {
        CHECK(panels[i].name == pat.panels[i].name);
        CHECK(std::abs(panelArea(panels[i]) - panelArea(pat.panels[i])) < 0.002f * panelArea(pat.panels[i]));
        CHECK(std::abs(perimeter(panels[i]) - perimeter(pat.panels[i])) < 1.0f);
        CHECK(panels[i].points.size() == pat.panels[i].points.size());
    }
    const std::string dxf = patternToDxf(pat);
    CHECK(dxf.find("$INSUNITS") != std::string::npos && dxf.find("POLYLINE") != std::string::npos);
    CHECK(dxf.find("SEW") != std::string::npos && dxf.find("CUT") != std::string::npos);
    CHECK(dxf.substr(dxf.size() - 4) == "EOF\n");
}

TEST(garment_svg_import_reads_units_transforms_and_shapes) {
    // A 10 x 20 cm rectangle in a cm document, a translated triangle path (relative commands), a polygon in a
    // scaled group, an open path (ignored) and a quadratic curve.
    const std::string svg = R"svg(<?xml version="1.0"?>
<!DOCTYPE svg [ <!ENTITY big "aaaaaaaaaaaaaaaaaaaa"> ]>
<svg xmlns="http://www.w3.org/2000/svg" width="50cm" height="50cm" viewBox="0 0 50 50">
  <!-- a comment with <rect x="0" y="0" width="1" height="1"/> inside -->
  <rect id="Front" x="1" y="1" width="10" height="20"/>
  <path id="tri" transform="translate(30 5)" d="m0,0 l10,0 l-5-8.66z"/>
  <g transform="scale(2)"><polygon id="poly" points="1,30 6,30 6,35"/></g>
  <path d="M0 0 L 5 5"/>
  <path id="curve" d="M 20 40 Q 25 30 30 40 Z"/>
</svg>)svg";
    std::vector<Panel> panels;
    std::string error;
    CHECK(panelsFromSvg(svg, panels, error) == 4);
    if (panels.size() != 4) {
        std::printf("  svg import: %zu panels (%s)\n", panels.size(), error.c_str());
        for (const Panel& p : panels) std::printf("    %s %zu points area %.1f\n", p.name.c_str(), p.points.size(), double(panelArea(p)));
        return;
    }
    CHECK(panels[0].name == "Front");
    CHECK(std::abs(panelArea(panels[0]) - 100.0f * 200.0f) < 1.0f); // mm^2
    CHECK(std::abs(panelArea(panels[1]) - 0.5f * 100.0f * 86.6f) < 5.0f);
    CHECK(std::abs(panelArea(panels[2]) - 0.5f * 100.0f * 100.0f) < 5.0f); // scaled 2x: 10 x 10 cm triangle
    CHECK(std::any_of(panels[3].curves.begin(), panels[3].curves.end(), [](const EdgeCurve& c) { return c.curved; }));
    for (const Panel& p : panels) CHECK(panelArea(p) > 0.0f); // counter-clockwise after the y flip
    // px documents (no units): 96 per inch.
    panels.clear();
    CHECK(panelsFromSvg(R"(<svg width="200" height="200"><rect width="96" height="96"/></svg>)", panels, error) == 1);
    CHECK(!panels.empty() && std::abs(panelArea(panels[0]) - 25.4f * 25.4f) < 0.5f);
}

TEST(garment_svg_reader_survives_hostile_input) {
    std::vector<Panel> panels;
    std::string error;
    CHECK(panelsFromSvg("", panels, error) == 0);
    CHECK(panelsFromSvg("<svg><path d=\"M 1e999 0 L 0 1e999 L 5 5 Z\"/></svg>", panels, error) == 0);
    CHECK(panelsFromSvg("<svg><path d=\"M0 0 L" + std::string(100000, '9') + " 0 Z\"/></svg>", panels, error) == 0);
    CHECK(panelsFromSvg(std::string(5u << 20, '<'), panels, error) == 0); // over 4 MB
    // Mutations of a real pattern: random bytes changed, inserted, cut. Never a crash or a hang.
    const std::string base = patternToSvg(tshirtPattern());
    std::mt19937 rng(7);
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 300; ++i) {
        std::string s = base;
        const int edits = 1 + int(rng() % 20);
        for (int e = 0; e < edits; ++e) {
            const size_t at = rng() % s.size();
            switch (rng() % 3) {
            case 0: s[at] = char(rng() % 256); break;
            case 1: s.insert(at, 1, "<>\"'=-.eE0zZmMcC/"[rng() % 17]); break;
            default: s.erase(at, std::min<size_t>(s.size() - at, rng() % 40)); break;
            }
        }
        panels.clear();
        panelsFromSvg(s, panels, error);
        for (const Panel& p : panels) CHECK(validPanel(p, error));
    }
    CHECK(std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() < timeLimit(20.0));
}

TEST(garment_svg_import_bounds_flattened_curves) {
    // 512 panels of 39 wild cubic edges each: under the 20,000 input-point cap, but every curve flattens to up to
    // 512 segments. The import stops at 50,000 outline points in all and says so, quickly.
    std::string svg = "<svg width=\"100cm\" viewBox=\"0 0 1000 1000\">";
    std::mt19937 rng(3);
    for (int k = 0; k < 512; ++k) {
        svg += "<path d=\"M 0 0";
        for (int e = 0; e < 39; ++e)
            svg += " C " + std::to_string(rng() % 1000) + " " + std::to_string(rng() % 1000) + " " + std::to_string(rng() % 1000) + " " +
                   std::to_string(rng() % 1000) + " " + std::to_string(rng() % 1000) + " " + std::to_string(rng() % 1000);
        svg += " Z\"/>";
    }
    svg += "</svg>";
    std::vector<Panel> panels;
    std::string error;
    const auto start = std::chrono::steady_clock::now();
    panelsFromSvg(svg, panels, error);
    CHECK(std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() < timeLimit(2.0));
    CHECK(error.find("not read") != std::string::npos);
    // Groups nested past the 64-deep stack: their closing tags don't pop the real groups' transforms.
    std::string deep = "<svg width=\"100mm\" viewBox=\"0 0 100 100\"><g transform=\"translate(10 0)\">";
    for (int k = 0; k < 100; ++k) deep += "<g>";
    for (int k = 0; k < 100; ++k) deep += "</g>";
    deep += "<rect x=\"0\" y=\"0\" width=\"10\" height=\"10\"/></g></svg>";
    panels.clear();
    CHECK(panelsFromSvg(deep, panels, error) == 1);
    CHECK(!panels.empty() && std::abs(panelMin(panels[0]).x - 10.0f) < 0.01f);
}

TEST(garment_crossing_check_matches_brute_force) {
    // The grid crossing check against an all-pairs test on random polygons (many cross, some don't).
    std::mt19937 rng(11);
    int crossing = 0, clean = 0;
    for (int t = 0; t < 400; ++t) {
        Panel p;
        const int n = 3 + int(rng() % 40);
        const bool star = t % 2; // star-shaped: never crosses; else random points: usually crosses
        for (int k = 0; k < n; ++k) {
            const float a = 6.2831853f * float(k) / float(n);
            const float r = 50.0f + float(rng() % 1000);
            p.points.push_back(star ? glm::vec2(std::cos(a), std::sin(a)) * r : glm::vec2(float(rng() % 2000), float(rng() % 2000)));
            p.curves.push_back({});
        }
        const std::vector<glm::vec2> o = outline(p, 0.5f);
        const size_t m = o.size();
        auto cross = [](glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec2 d) {
            auto x = [](glm::vec2 u, glm::vec2 v) { return u.x * v.y - u.y * v.x; };
            const float d1 = x(b - a, c - a), d2 = x(b - a, d - a), d3 = x(d - c, a - c), d4 = x(d - c, b - c);
            return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0)) && d1 != 0 && d2 != 0 && d3 != 0 && d4 != 0;
        };
        bool brute = false;
        for (size_t i = 0; i < m && !brute; ++i)
            for (size_t j = i + 2; j < m && !brute; ++j)
                if (!(i == 0 && j == m - 1)) brute = cross(o[i], o[(i + 1) % m], o[j], o[(j + 1) % m]);
        std::string why;
        const bool ok = validPanel(p, why);
        if (why.find("crosses") != std::string::npos || ok) CHECK(ok == !brute);
        (brute ? crossing : clean)++;
    }
    CHECK(crossing > 50 && clean > 50);
}

TEST(garment_edits_keep_seams_and_shapes) {
    // Mirror twice: the same panel; area unchanged.
    Pattern pat = tshirtPattern();
    mirrorPanel(pat, 2);
    CHECK(panelArea(pat.panels[2]) > 0.0f);
    CHECK(patternProblems(pat).empty());
    mirrorPanel(pat, 2);
    std::string error;
    CHECK(std::abs(panelArea(pat.panels[2]) - panelArea(tshirtPattern().panels[2])) < 1.0f);
    CHECK(pat.seams.size() == 10);
    // Unfold a half: a 100 x 300 half about its left edge (edge 4, from (0,300) down to (0,0)) doubles.
    Pattern half;
    half.panels.push_back(rectanglePanel("Front", 100.0f, 300.0f));
    CHECK(unfoldPanel(half, 0, 3, error));
    CHECK(std::abs(panelArea(half.panels[0]) - 2.0f * 100.0f * 300.0f) < 1.0f);
    CHECK(half.panels[0].points.size() == 6); // the fold's two points stay as corners of the hem and top
    CHECK(validPanel(half.panels[0], error));
    // Split an edge under a seam end: the seam moves with its piece; removing a point drops seams on it.
    Pattern s;
    s.panels.push_back(rectanglePanel("A", 100.0f, 100.0f));
    s.panels.push_back(rectanglePanel("B", 100.0f, 100.0f, {200.0f, 0.0f}));
    CHECK(addSeam(s, {{0, 1, 0.0f, 0.5f}, {1, 3, 0.5f, 1.0f}, false}, error));
    CHECK(!addSeam(s, {{0, 1, 0.25f, 0.75f}, {1, 1, 0.0f, 1.0f}, false}, error)); // overlaps
    splitEdge(s, 0, 1, 0.5f);
    CHECK(s.panels[0].points.size() == 5 && s.seams.size() == 1);
    CHECK(s.seams[0].a.edge == 1 && std::abs(s.seams[0].a.t1 - 1.0f) < 1e-4f);
    CHECK(std::abs(seamSideLength(s, s.seams[0].a) - 50.0f) < 0.01f);
    CHECK(removePoint(s, 0, 2));
    CHECK(s.seams.empty() && s.panels[0].points.size() == 4);
    // Mirrored copy: the other arm.
    Pattern t = tshirtPattern();
    const uint32_t c = mirroredCopy(t, 2, "Extra");
    CHECK(t.panels[c].name == "Extra" && t.seams.size() == tshirtPattern().seams.size()); // (no seams added)
}

TEST(garment_edge_names_follow_every_edit) {
    // The template names its edges as the wearer has them; seams read by those names.
    Pattern pat = tshirtPattern();
    CHECK(edgeName(pat, 0, 1) == "Front left side");
    CHECK(edgeName(pat, 1, 7) == "Back left side");
    CHECK(edgeName(pat, 2, 2) == "Left sleeve cap");
    CHECK(checkSeam(pat, pat.seams[0]).names == "Front left side <-> Back left side");
    CHECK(checkSeam(pat, pat.seams[0]).text.find(" mm") != std::string::npos);
    // Two rows: the sheet is about as tall as it is wide (the window frames it well).
    glm::vec2 lo(1e9f), hi(-1e9f);
    for (const Panel& p : pat.panels) lo = glm::min(lo, panelMin(p)), hi = glm::max(hi, panelMax(p));
    CHECK((hi.y - lo.y) > 0.7f * (hi.x - lo.x) && (hi.y - lo.y) < 1.4f * (hi.x - lo.x));
    CHECK(panelMin(pat.panels[0]).y > panelMax(pat.panels[2]).y); // the sleeves under the body panels
    // Saved and loaded.
    Pattern back;
    std::string error;
    CHECK(patternFromJson(patternToJson(pat), back, error));
    CHECK(back.panels[1].edgeNames == pat.panels[1].edgeNames);
    // Split: both halves keep the name; removing the new point merges them back.
    splitEdge(pat, 0, 1, 0.5f);
    CHECK(edgeName(pat, 0, 1) == "Front left side" && edgeName(pat, 0, 2) == "Front left side");
    CHECK(edgeName(pat, 0, 3) == "Front left armhole" && pat.panels[0].edgeNames.size() == 9);
    CHECK(removePoint(pat, 0, 2));
    CHECK(pat.panels[0].edgeNames == tshirtPattern().panels[0].edgeNames);
    // Removing point 0 merges the last edge into it: the merged edge keeps the last edge's name.
    Pattern r = tshirtPattern();
    CHECK(removePoint(r, 0, 0));
    CHECK(r.panels[0].edgeNames.size() == 7 && r.panels[0].edgeNames.front() == "left side" &&
          r.panels[0].edgeNames.back() == "right side");
    // Mirror: the names stay on their edges (reversed with the outline).
    Pattern m = tshirtPattern();
    std::vector<std::string> sewn;
    for (const Seam& s : m.seams) sewn.push_back(checkSeam(m, s).names);
    mirrorPanel(m, 0);
    for (size_t k = 0; k < m.seams.size(); ++k) CHECK(checkSeam(m, m.seams[k]).names == sewn[k]);
    // Unfold: each half keeps its edges' names; the fold edge's name goes.
    Pattern h;
    h.panels.push_back(rectanglePanel("Half", 100.0f, 300.0f));
    h.panels[0].edgeNames = {"hem", "side", "top", "fold"};
    CHECK(unfoldPanel(h, 0, 3, error));
    CHECK(h.panels[0].edgeNames.size() == 6);
    CHECK(std::count(h.panels[0].edgeNames.begin(), h.panels[0].edgeNames.end(), "fold") == 0);
    CHECK(std::count(h.panels[0].edgeNames.begin(), h.panels[0].edgeNames.end(), "side") == 2);
    // No names: "edge N".
    CHECK(edgeName(h, 0, 0).find("Half ") == 0);
    Pattern plain;
    plain.panels.push_back(rectanglePanel("Plain", 100.0f, 100.0f));
    CHECK(edgeName(plain, 0, 2) == "Plain edge 3");
}

TEST(garment_triangulates_quickly) {
    Pattern pat = tshirtPattern();
    pat.particleDistance = 10.0f; // about 9,000 particles
    GarmentMesh m;
    std::string error;
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(triangulatePattern(pat, m, error));
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  garment: %zu particles, %zu triangles in %.0f ms\n", m.vertexCount(), m.triangles.size(), s * 1000.0);
    CHECK(s < timeLimit(1.0));
}

TEST(garment_commands_make_sew_and_export) {
    CommandRegistry reg;
    Document doc = makeDoc(makeIcosphere(2));
    CHECK(!reg.execute(doc, "garment_info").ok);
    CommandResult r = reg.execute(doc, "garment_new", {{"template", std::string("tshirt")}});
    CHECK(r.ok && r.message.find("4 panels, 10 seams") != std::string::npos);
    CHECK(reg.execute(doc, "garment_info").message.find("Left sleeve") != std::string::npos);
    r = reg.execute(doc, "garment_add_panel", {{"name", std::string("Pocket")}, {"width_mm", 120.0}, {"height_mm", 140.0}});
    CHECK(r.ok);
    r = reg.execute(doc, "garment_add_panel", {{"name", std::string("Bad")}, {"points", std::string("[0,0,1,1]")}});
    CHECK(!r.ok);
    r = reg.execute(doc, "garment_sew", {{"a_panel", std::string("Pocket")}, {"a_edge", 1.0}, {"b_panel", std::string("front")},
                                          {"b_edge", 1.0}, {"b_from", 0.1}, {"b_to", 0.3}});
    CHECK(r.ok); // the front hem is free
    CHECK(reg.execute(doc, "garment_set", {{"particle_mm", 20.0}, {"fabric", std::string("silk")}}).ok);
    CHECK(!reg.execute(doc, "garment_set", {{"fabric", std::string("tweed")}}).ok);
    const std::string svg = tempPath("garment_cmd.svg"), dxf = tempPath("garment_cmd.dxf");
    CHECK(reg.execute(doc, "garment_export", {{"path", svg}}).ok);
    CHECK(reg.execute(doc, "garment_export", {{"path", dxf}}).ok);
    CHECK(readAll(dxf).find("POLYLINE") != std::string::npos);
    // Import our own file into a fresh pattern: the five panels come back.
    CHECK(reg.execute(doc, "garment_new", {{"template", std::string("empty")}}).ok);
    r = reg.execute(doc, "garment_import", {{"path", svg}});
    CHECK(r.ok && r.message.find("Imported 5 panels") != std::string::npos);
    Pattern got;
    CHECK(documentPattern(doc, got) && got.panels.size() == 5);
    // garment_edit: the Pattern window's edits.
    auto edit = [&](Args a) { return reg.execute(doc, "garment_edit", a); };
    const size_t edges0 = got.panels[0].points.size();
    CHECK(edit({{"op", std::string("split")}, {"panel", std::string("1")}, {"edge", 1.0}, {"t", 0.5}}).ok);
    CHECK(documentPattern(doc, got) && got.panels[0].points.size() == edges0 + 1);
    CHECK(edit({{"op", std::string("remove_point")}, {"panel", std::string("1")}, {"point", 2.0}}).ok);
    CHECK(documentPattern(doc, got) && got.panels[0].points.size() == edges0);
    const glm::vec2 before = panelMin(got.panels[0]);
    CHECK(edit({{"op", std::string("move")}, {"panel", std::string("1")}, {"dx", 10.0}, {"dy", -5.0}}).ok);
    CHECK(documentPattern(doc, got) && glm::length(panelMin(got.panels[0]) - before - glm::vec2(10, -5)) < 0.01f);
    CHECK(edit({{"op", std::string("rename")}, {"panel", std::string("1")}, {"name", std::string("Bodice")}}).ok);
    CHECK(edit({{"op", std::string("duplicate")}, {"panel", std::string("Bodice")}}).ok);
    CHECK(documentPattern(doc, got) && got.panels.size() == 6);
    CHECK(edit({{"op", std::string("set_panel")}, {"panel", std::string("6")}, {"points", std::string("[0,0, 100,0, 100,80, 0,80]")},
                {"curves", std::string("[[2, 70,100, 30,100]]")}}).ok);
    CHECK(documentPattern(doc, got) && got.panels[5].points.size() == 4 && got.panels[5].curves[2].curved);
    CHECK(!edit({{"op", std::string("set_panel")}, {"panel", std::string("6")}, {"points", std::string("[0,0, 0,80, 100,80, 100,0]")}}).ok); // clockwise
    CHECK(!edit({{"op", std::string("set_panel")}, {"panel", std::string("6")}, {"points", std::string("[0,0, 100,80, 100,0, 0,80]")}}).ok); // crossing
    CHECK(!edit({{"op", std::string("split")}, {"panel", std::string("6")}, {"edge", 9.0}}).ok);
    CHECK(!edit({{"op", std::string("explode")}, {"panel", std::string("6")}}).ok);
    // Reshaping with the same edges keeps seams; a different count drops that panel's seams.
    CHECK(reg.execute(doc, "garment_new").ok);
    CHECK(documentPattern(doc, got));
    const size_t seams0 = got.seams.size();
    std::string pts;
    for (glm::vec2 q : got.panels[0].points) pts += (pts.empty() ? "[" : ",") + std::to_string(q.x * 1.01f) + "," + std::to_string(q.y);
    std::string cur;
    for (size_t e = 0; e < got.panels[0].curves.size(); ++e)
        if (const EdgeCurve& c = got.panels[0].curves[e]; c.curved)
            cur += std::string(cur.empty() ? "[" : ",") + "[" + std::to_string(e) + "," + std::to_string(c.c1.x * 1.01f) + "," +
                   std::to_string(c.c1.y) + "," + std::to_string(c.c2.x * 1.01f) + "," + std::to_string(c.c2.y) + "]";
    r = edit({{"op", std::string("set_panel")}, {"panel", std::string("1")}, {"points", pts + "]"}, {"curves", cur.empty() ? "[]" : cur + "]"}});
    CHECK(r.ok);
    if (!r.ok) std::printf("  set_panel: %s\n", r.message.c_str());
    CHECK(documentPattern(doc, got) && got.seams.size() == seams0);
    CHECK(reg.execute(doc, "garment_clear").ok && !documentPattern(doc, got));
    std::remove(svg.c_str());
    std::remove(dxf.c_str());
}
