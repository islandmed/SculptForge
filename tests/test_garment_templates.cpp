// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Garment templates (core/GarmentTemplates.h): every template at default, smallest, largest and random size charts
// is valid, its seams match in length, and sewn it welds into one consistently oriented surface with exactly the
// openings the garment has; the size chart round trips through the project; garment_new / garment_size.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/GarmentTemplates.h"

#include <cmath>
#include <map>
#include <random>
#include <set>

using namespace sf;
using namespace sf::garment;

namespace {

// Border loops of a mesh, and whether every edge is used once (border) or twice in opposite directions.
struct Topology {
    int borderLoops = 0;
    bool consistent = true, manifold = true;
};
Topology topology(const Mesh& m) {
    std::map<std::pair<uint32_t, uint32_t>, int> directed;
    for (const glm::uvec3& t : m.triangles)
        for (int k = 0; k < 3; ++k) ++directed[{t[k], t[(k + 1) % 3]}];
    Topology r;
    std::map<uint32_t, uint32_t> next;
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

// The openings each garment has once sewn (the hoodie's pocket is a loose panel: its own outline).
const std::map<std::string, int> kOpenings = {{"tshirt", 4}, {"trousers", 3}, {"skirt", 2}, {"skirt_aline", 2},
                                              {"hoodie", 5}, {"dress", 4},    {"cloak", 1}, {"glove", 2}};

bool sound(const Pattern& pat, std::string& why) {
    std::string error;
    for (const Panel& p : pat.panels)
        if (!validPanel(p, error)) {
            why = p.name + ": " + error;
            return false;
        }
    for (const Seam& s : pat.seams) {
        const SeamCheck c = checkSeam(pat, s);
        if (!c.lengthsMatch || c.twisted) {
            why = c.text;
            return false;
        }
    }
    const auto problems = patternProblems(pat);
    if (!problems.empty()) {
        why = problems[0];
        return false;
    }
    // Laid out flat: no two panels' boxes overlap.
    for (size_t i = 0; i < pat.panels.size(); ++i)
        for (size_t j = i + 1; j < pat.panels.size(); ++j) {
            const glm::vec2 a0 = panelMin(pat.panels[i]), a1 = panelMax(pat.panels[i]);
            const glm::vec2 b0 = panelMin(pat.panels[j]), b1 = panelMax(pat.panels[j]);
            if (a0.x < b1.x && b0.x < a1.x && a0.y < b1.y && b0.y < a1.y) {
                why = pat.panels[i].name + " overlaps " + pat.panels[j].name;
                return false;
            }
        }
    return true;
}

SizeChart extreme(bool big) {
    SizeChart s;
    const auto& specs = measureSpecs();
    for (size_t i = 0; i < specs.size(); ++i) s.mm[i] = big ? specs[i].max : specs[i].min;
    return s;
}

} // namespace

TEST(garment_mirrored_panel_sews_back_to_its_original) {
    std::string error;
    Pattern pat;
    CHECK(makeTemplate("trousers", SizeChart{}, 0.0f, pat, error));
    const Panel& p = pat.panels[0];
    const Panel m = mirroredPanel(p);
    CHECK(panelArea(m) > 0.0f && std::abs(panelArea(m) - panelArea(p)) < 1.0f);
    const uint32_t n = uint32_t(p.points.size());
    for (uint32_t e = 0; e < n; ++e) {
        CHECK(mirrorEdge(n, mirrorEdge(n, e)) == e);
        CHECK(std::abs(edgeLength(m, mirrorEdge(n, e)) - edgeLength(p, e)) < 0.01f);
        for (float t : {0.0f, 0.3f, 1.0f}) {
            const glm::vec2 a = edgePoint(p, e, t), b = edgePoint(m, mirrorEdge(n, e), 1.0f - t);
            CHECK(std::abs(a.x + b.x) < 0.01f && std::abs(a.y - b.y) < 0.01f);
        }
    }
}

TEST(garment_templates_are_sound_at_any_size) {
    std::mt19937 rng(7);
    const auto& specs = measureSpecs();
    for (const TemplateSpec& t : templateSpecs()) {
        std::vector<SizeChart> charts = {SizeChart{}, extreme(false), extreme(true)};
        for (int k = 0; k < 25; ++k) {
            SizeChart s;
            for (size_t i = 0; i < specs.size(); ++i)
                s.mm[i] = std::uniform_real_distribution<float>(specs[i].min, specs[i].max)(rng);
            charts.push_back(s);
        }
        for (size_t c = 0; c < charts.size(); ++c) {
            Pattern pat;
            std::string error, why;
            const bool made = makeTemplate(t.id, charts[c], 0.0f, pat, error);
            CHECK(made);
            const bool ok = made && sound(pat, why);
            if (!ok) std::printf("  %s chart %zu: %s%s\n", t.id, c, error.c_str(), why.c_str());
            CHECK(ok);
        }
    }
}

TEST(garment_templates_weld_into_the_garments_openings) {
    std::mt19937 rng(11);
    const auto& specs = measureSpecs();
    for (const TemplateSpec& t : templateSpecs())
        for (int c = 0; c < 6; ++c) {
            SizeChart chart = c == 0 ? SizeChart{} : extreme(c == 2);
            if (c >= 3)
                for (size_t i = 0; i < specs.size(); ++i)
                    chart.mm[i] = std::uniform_real_distribution<float>(specs[i].min, specs[i].max)(rng);
            Pattern pat;
            std::string error;
            CHECK(makeTemplate(t.id, chart, 0.0f, pat, error));
            GarmentMesh m;
            const bool tri = triangulatePattern(pat, m, error);
            if (!tri) std::printf("  %s chart %d: %s\n", t.id, c, error.c_str());
            CHECK(tri);
            if (!tri) continue;
            std::vector<glm::vec3> rest;
            flatRest(m, rest);
            const Topology top = topology(garmentToMesh(m, rest, 0.0f));
            if (!top.manifold || !top.consistent || top.borderLoops != kOpenings.at(t.id))
                std::printf("  %s chart %d: manifold %d consistent %d openings %d\n", t.id, c, int(top.manifold),
                            int(top.consistent), top.borderLoops);
            CHECK(top.manifold);
            CHECK(top.consistent); // every seam sewn the right way round
            CHECK(top.borderLoops == kOpenings.at(t.id));
        }
}

TEST(garment_templates_follow_the_size_chart) {
    std::string error;
    SizeChart s;
    Pattern a, b;
    CHECK(makeTemplate("trousers", s, 0.0f, a, error));
    s.set("inseam", 400.0f); // shorts
    s.set("hip", 1200.0f);
    CHECK(makeTemplate("trousers", s, 0.0f, b, error));
    auto height = [](const Panel& p) { return panelMax(p).y - panelMin(p).y; };
    auto width = [](const Panel& p) { return panelMax(p).x - panelMin(p).x; };
    CHECK(std::abs((height(a.panels[0]) - height(b.panels[0])) - 390.0f) < 5.0f);
    CHECK(width(b.panels[0]) > width(a.panels[0]) + 40.0f);
    // The waistband is the skirt's waist round, and the skirt is as long as asked.
    Pattern k;
    CHECK(makeTemplate("skirt", SizeChart{}, 650.0f, k, error));
    CHECK(std::abs(height(k.panels[0]) - 650.0f) < 1.0f);
    CHECK(std::abs(edgeLength(k.panels[2], 0) - (edgeLength(k.panels[0], 3) + edgeLength(k.panels[1], 3))) < 0.5f);
    // Unknown measurements and templates are refused; values are clamped to the chart's range.
    CHECK(!s.set("shoe", 280.0f));
    CHECK(!s.set("hip", NAN));
    s.set("hip", 1e9f);
    CHECK(s.get("hip") == 2200.0f);
    CHECK(!makeTemplate("tuxedo", s, 0.0f, k, error) && error.find("trousers") != std::string::npos);
}

TEST(garment_size_chart_and_templates_by_command) {
    Document doc;
    CommandRegistry reg;
    CHECK(reg.execute(doc, "garment_size", {{"chest_mm", 1100.0}, {"neck_mm", 420.0}}).ok);
    CHECK(documentSizeChart(doc).get("chest") == 1100.0f);
    // The chart is saved in the project settings and read back.
    CHECK(sizeChartFromJson(doc.projectSettings["garment_sizes"]).get("neck") == 420.0f);
    CommandResult r = reg.execute(doc, "garment_new", {{"template", std::string("hoodie")}});
    CHECK(r.ok && r.message.find("hoodie") != std::string::npos);
    Pattern pat;
    CHECK(documentPattern(doc, pat) && pat.panels.size() == 7);
    // An outfit: trousers added beside the hoodie, with a measurement given on the way.
    r = reg.execute(doc, "garment_new", {{"template", std::string("trousers")}, {"add", true}, {"hip_mm", 1050.0}});
    CHECK(r.ok && r.message.find("hip") != std::string::npos);
    CHECK(documentPattern(doc, pat) && pat.panels.size() == 11);
    CHECK(documentSizeChart(doc).get("hip") == 1050.0f && documentSizeChart(doc).get("chest") == 1100.0f);
    std::string why;
    CHECK(sound(pat, why));
    CHECK(!reg.execute(doc, "garment_new", {{"template", std::string("tuxedo")}}).ok);
    r = reg.execute(doc, "garment_size", {{"reset", true}});
    CHECK(r.ok && r.message.find("chest_mm = 960") != std::string::npos);
}
