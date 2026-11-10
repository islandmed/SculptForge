// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Garments, part 3 (core/GarmentDrape.h): panels placed by hand (in front of the clicked point, facing the view,
// curled for tubes), placements saved with the pattern, a sewn bag sewn with gravity off, then draped over a ball (seams closed, nothing
// inside the ball, at rest), garment_drape making the Part in one undo step.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/GarmentDrape.h"
#include "core/Primitives.h"
#include "core/StableMath.h"
#include "core/Units.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

using namespace sf;
using namespace sf::garment;

namespace {

Placement placeAt(glm::vec3 point, glm::vec3 facing, float wrap = 0.0f) {
    Placement p;
    p.placed = true;
    p.point = point;
    p.facing = facing;
    p.wrapDegrees = wrap;
    return p;
}

// Front and back 400 x 500 mm, sewn at both sides and across the top: a bag.
Pattern bag() {
    Pattern pat;
    pat.panels.push_back(rectanglePanel("Front", 400.0f, 500.0f));
    pat.panels.push_back(rectanglePanel("Back", 400.0f, 500.0f, {460.0f, 0.0f}));
    pat.particleDistance = 20.0f;
    std::string error;
    // Edges: 0 bottom, 1 right, 2 top, 3 left. Front's right meets the back's left (seen from behind it is on the
    // same side), the tops meet.
    CHECK(addSeam(pat, {{0, 1, 0, 1}, {1, 3, 0, 1}, false}, error));
    CHECK(addSeam(pat, {{0, 3, 0, 1}, {1, 1, 0, 1}, false}, error));
    CHECK(addSeam(pat, {{0, 2, 0, 1}, {1, 2, 0, 1}, false}, error));
    return pat;
}

} // namespace

TEST(garment_place_puts_the_panel_in_front_of_the_point) {
    Panel p = rectanglePanel("Front", 400.0f, 500.0f, {1000.0f, 300.0f});
    p.place = placeAt({0.0f, 1.2f, 0.1f}, {0.0f, 0.0f, 1.0f});
    p.place.gapMm = 50.0f;
    // The panel's centre sits 50 mm in front; its sheet +x goes to the viewer's right (+X seen from +Z), +y up.
    const glm::vec3 c = placedPoint(p, {1200.0f, 550.0f}, 1.0f);
    CHECK(glm::length(c - glm::vec3(0.0f, 1.2f, 0.15f)) < 1e-4f);
    const glm::vec3 corner = placedPoint(p, {1400.0f, 800.0f}, 1.0f);
    CHECK(glm::length(corner - glm::vec3(0.2f, 1.45f, 0.15f)) < 1e-4f);
    // In centimetres (100 units per metre) everything scales.
    CHECK(glm::length(placedPoint(p, {1400.0f, 800.0f}, 100.0f) - glm::vec3(20.0f, 1.2f + 25.0f, 0.1f + 5.0f)) < 1e-3f);
    // Placed from behind: the sheet's +x is the viewer's right there (-X).
    p.place.facing = {0.0f, 0.0f, -1.0f};
    CHECK(placedPoint(p, {1400.0f, 550.0f}, 1.0f).x < -0.19f);
    // A quarter turn puts the sheet's +x up.
    p.place.facing = {0.0f, 0.0f, 1.0f};
    p.place.turnDegrees = 90.0f;
    CHECK(glm::length(placedPoint(p, {1400.0f, 550.0f}, 1.0f) - glm::vec3(0.0f, 1.4f, 0.15f)) < 1e-4f);
    // Wrapped all the way round: the two side edges meet behind, the width is the circumference.
    p.place.turnDegrees = 0.0f;
    p.place.wrapDegrees = 360.0f;
    const glm::vec3 l = placedPoint(p, {1000.0f, 550.0f}, 1.0f), r = placedPoint(p, {1400.0f, 550.0f}, 1.0f);
    CHECK(glm::length(l - r) < 1e-4f);
    const float radius = 0.4f / glm::two_pi<float>();
    CHECK(std::abs(l.z - (0.15f - 2.0f * radius)) < 1e-4f);
}

TEST(garment_placements_save_and_copies_start_unplaced) {
    Pattern pat = bag();
    pat.unitsPerMetre = 100.0f;
    pat.panels[0].place = placeAt({1.0f, 2.0f, 3.0f}, {0.0f, 0.0f, 1.0f}, 120.0f);
    pat.panels[0].place.turnDegrees = 15.0f;
    pat.panels[0].place.gapMm = 12.0f;
    Pattern back;
    std::string error;
    CHECK(patternFromJson(patternToJson(pat), back, error));
    CHECK(back.unitsPerMetre == 100.0f);
    CHECK(back.panels[0].place.placed && !back.panels[1].place.placed);
    CHECK(back.panels[0].place.point == glm::vec3(1.0f, 2.0f, 3.0f));
    CHECK(back.panels[0].place.wrapDegrees == 120.0f && back.panels[0].place.turnDegrees == 15.0f);
    CHECK(back.panels[0].place.gapMm == 12.0f);
    // Hostile placement values are refused, not trusted.
    Json j = patternToJson(pat);
    std::string text = j.dump();
    const size_t at = text.find("\"facing\"");
    CHECK(at != std::string::npos);
    text.replace(at, std::string("\"facing\"").size(), "\"facing_\"");
    Json bad;
    CHECK(Json::parse(text, bad, error));
    CHECK(patternFromJson(bad, back, error) && !back.panels[0].place.placed);
    // A copy (2D only) is not placed: the user places it.
    CHECK(!pat.panels[duplicatePanel(pat, 0, {500.0f, 0.0f})].place.placed);
    CHECK(!pat.panels[mirroredCopy(pat, 0)].place.placed);
    // A panel sewn to itself starts curled into a tube.
    Pattern tube;
    tube.panels.push_back(rectanglePanel("Sleeve", 300.0f, 200.0f));
    CHECK(addSeam(tube, {{0, 1, 0, 1}, {0, 3, 0, 1}, false}, error));
    // ... nearly closed: its own seam's edges 1.5 particle spacings apart (300 mm wide at 15 mm: 333 degrees).
    const float wrap = defaultWrap(tube, 0);
    CHECK(std::abs(300.0f * (1.0f - wrap / 360.0f) - 1.5f * tube.particleDistance) < 0.5f && defaultWrap(pat, 0) == 0.0f);
    // The tube's own seam is on the far side from the click: the side edges meet behind the centre.
    tube.panels[0].place = placeAt({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, wrap);
    glm::vec3 n;
    const glm::vec3 edge = placedPoint(tube.panels[0], {0.0f, 100.0f}, 1.0f, nullptr, &n);
    const glm::vec3 middle = placedPoint(tube.panels[0], {150.0f, 100.0f}, 1.0f);
    CHECK(edge.z < middle.z - 0.08f && n.z < -0.95f); // (behind, and its normal points away from the viewer)
    // A body's size gives its unit: metres, centimetres (the starter meshes), millimetres.
    CHECK(unitsPerMetreForSize(1.8f) == 1.0f && unitsPerMetreForSize(180.0f) == 100.0f && unitsPerMetreForSize(1800.0f) == 1000.0f);
}

TEST(garment_bag_sews_then_drapes_over_a_ball) {
    const float radius = 0.1f; // (the bag is 800 mm round: it closes under a ball 630 mm round)
    Document doc = makeDoc(makeIcosphere(4, radius));
    Pattern pat = bag();
    pat.unitsPerMetre = 1.0f;
    pat.thickness = 0.0f;
    GarmentDrapeScene scene;
    std::string error;
    setDocumentPattern(doc, pat);
    CHECK(!garmentDrapeScene(doc, pat, scene, error) && error.find("Front") != std::string::npos);
    pat.panels[0].place = placeAt({0.0f, 0.0f, radius}, {0.0f, 0.0f, 1.0f});
    pat.panels[1].place = placeAt({0.0f, 0.0f, -radius}, {0.0f, 0.0f, -1.0f});
    CHECK(garmentDrapeScene(doc, pat, scene, error));
    CHECK(!scene.colliders.empty() && !scene.setup.seams.empty());
    CHECK(scene.setup.rest.size() == scene.setup.positions.size());
    // Placed in front of the ball: nothing starts inside it.
    for (const glm::vec3& p : scene.setup.positions) CHECK(glm::length(p) > radius);
    auto widestGap = [&](const std::vector<glm::vec3>& x) {
        float gap = 0.0f;
        for (const ClothSeam& s : scene.setup.seams) gap = std::max(gap, glm::length(x[s.a] - x[s.b]));
        return gap;
    };
    auto meanY = [](const std::vector<glm::vec3>& x) {
        double y = 0.0;
        for (const glm::vec3& p : x) y += p.y;
        return float(y / double(x.size()));
    };
    // Sewing (gravity off): the seams close where the panels were put; nothing falls meanwhile.
    const ClothSettings base = garmentSettings(pat);
    ClothSim sim;
    CHECK(sim.init(scene.setup, scene.fabric, base));
    sim.setColliders(&scene.colliders);
    for (int f = 0; f < kSewFrames; ++f) CHECK(garmentStep(sim, f, base, scene.fabric)); // (never at rest while sewing)
    std::printf("  sewn: widest seam gap %.1f mm, mean height %+.1f mm\n", widestGap(sim.positions()) * 1000.0f,
                (meanY(sim.positions()) - meanY(scene.setup.positions)) * 1000.0f);
    CHECK(widestGap(sim.positions()) < 0.5f * 0.020f); // (from 300 mm apart; the settling closes the rest)
    CHECK(meanY(sim.positions()) - meanY(scene.setup.positions) > -0.01f); // (closing pulls it up a little; it does not fall)
    // Draped as Garments drapes (sewn first, then gravity): it rests on the ball, sewn shut, nothing inside it.
    const DrapeResult r = garmentDrapeToRest(scene, base, 900);
    float inside = 1.0f, top = -1.0f;
    for (const glm::vec3& p : r.positions) {
        inside = std::min(inside, glm::length(p) - radius);
        top = std::max(top, p.y);
    }
    const float gap = widestGap(r.positions);
    std::printf("  bag: %s after %d frames (%.0f ms), widest seam gap %.1f mm, deepest %.1f mm, top %.3f m\n",
                r.rested ? "at rest" : "stopped", r.frames, r.milliseconds, gap * 1000.0f, -inside * 1000.0f, top);
    CHECK(r.rested);
    CHECK(gap < 0.3f * 0.020f);   // sewn shut (within a third of the particle distance; the Part welds them)
    CHECK(inside > -0.003f);      // on the ball, not through it
    CHECK(top > radius && top < radius + 0.08f); // hanging from the ball's top (its corners stand up), not fallen off
    // The Part: welded (fewer vertices than particles), UVs.
    const Mesh m = drapedMesh(pat, scene.mesh, r.positions);
    CHECK(m.vertexCount() < scene.mesh.vertexCount() && !m.uvs.empty());
}

TEST(garment_drape_command_adds_the_part_in_one_undo_step) {
    CommandRegistry reg;
    Document doc = makeDoc(makeIcosphere(3, 0.2f));
    setDocumentPattern(doc, bag());
    CHECK(!reg.execute(doc, "garment_drape").ok); // nothing placed
    CHECK(!reg.execute(doc, "garment_place", {{"panel", std::string("Front")}}).ok); // no point
    CommandResult r = reg.execute(doc, "garment_place", {{"panel", std::string("Front")}, {"point", std::string("0,0,0.2")}});
    CHECK(r.ok && r.message.find("1 of 2") != std::string::npos && r.message.find(" 1 units per metre") != std::string::npos);
    r = reg.execute(doc, "garment_drape");
    CHECK(!r.ok && r.message.find("Back") != std::string::npos);
    CHECK(reg.execute(doc, "garment_place", {{"panel", std::string("2")}, {"point", std::string("0,0,-0.2")},
                                             {"facing", std::string("0,0,-1")}}).ok);
    CHECK(!reg.execute(doc, "garment_place", {{"panel", std::string("2")}, {"facing", std::string("0,1,0")}}).ok); // along up
    r = reg.execute(doc, "garment_drape", {{"frames", 400.0}});
    std::printf("  %s\n", r.message.c_str());
    CHECK(r.ok && doc.partCount() == 2 && doc.parts[1].name == "Garment" && doc.activePart == 0);
    CHECK(doc.partMesh(1).triangleCount() > 1000);
    CHECK(reg.execute(doc, "undo").ok && doc.partCount() == 1);
    CHECK(reg.execute(doc, "redo").ok && doc.partCount() == 2);
    CHECK(reg.execute(doc, "garment_place", {{"panel", std::string("Front")}, {"clear", true}}).ok);
    Pattern pat;
    CHECK(documentPattern(doc, pat) && !pat.panels[0].place.placed && pat.panels[1].place.placed);
}

TEST(stable_sin_cos_match_the_library) {
    // Placement's sine and cosine (the same bits on every OS) are as good as the library's.
    double worst = 0.0;
    for (int k = -20000; k <= 20000; ++k) {
        const double x = k * 0.000731 * 3.0;
        double s, c;
        stableSinCos(x, s, c);
        worst = std::max({worst, std::abs(s - std::sin(x)), std::abs(c - std::cos(x))});
    }
    std::printf("  stable sin / cos: worst %.2g off the library's over +-44 rad\n", worst);
    CHECK(worst < 1e-14);
    CHECK(stableSin(0.0f) == 0.0f && stableCos(0.0f) == 1.0f);
}

TEST(garment_panels_anchor_at_their_widest_line) {
    // A bodice goes at the click by its chest line (under the armholes), a sleeve under its cap; a rectangle by its
    // middle. (The panel's own shape only.)
    const Pattern tee = tshirtPattern();
    const glm::vec2 front = panelAnchor(tee.panels[0]);
    const float hem = panelMin(tee.panels[0]).y, top = panelMax(tee.panels[0]).y;
    std::printf("  front anchor %.0f mm up a %.0f mm panel; sleeve %.0f of %.0f\n", front.y - hem, top - hem,
                panelAnchor(tee.panels[2]).y - panelMin(tee.panels[2]).y, panelMax(tee.panels[2]).y - panelMin(tee.panels[2]).y);
    CHECK(std::abs(front.y - (top - 270.0f)) < 10.0f); // the armhole's foot, 270 mm under the top
    CHECK(std::abs(panelAnchor(tee.panels[2]).y - (panelMin(tee.panels[2]).y + 200.0f)) < 10.0f);
    const Panel r = rectanglePanel("R", 400.0f, 500.0f);
    CHECK(glm::length(panelAnchor(r) - glm::vec2(200.0f, 250.0f)) < 2.0f);
}

namespace {

// The T-shirt template on the Full Body starter (centimetres), placed as a user would: Front and Back clicked on the
// chest from the front and from behind (the Back's Gap raised until its hem clears the hips), each sleeve on the top
// of its upper arm seen from outside and above; `jitter` (cm) moves both sleeve clicks.
struct TeeDrape {
    Document doc = makeDoc(makeIcosphere(1));
    Pattern pat;
    GarmentDrapeScene scene;
    DrapeResult r;
    std::map<std::pair<uint32_t, uint32_t>, float> worst; // widest gap per panel pair, mm
    glm::vec3 worstAt{0.0f};                              // where the widest one is (cm)
};
void drapeTee(TeeDrape& t, glm::vec3 jitter, float backZ = -15.0f) {
    CommandRegistry reg;
    CHECK(reg.execute(t.doc, "new_starter_mesh", {{"name", std::string("full-body")}}).ok);
    CHECK(reg.execute(t.doc, "garment_new", {{"template", std::string("tshirt")}}).ok);
    auto place = [&](const char* panel, glm::vec3 point, const char* facing, const char* up, double gap) {
        char p[96];
        std::snprintf(p, sizeof p, "%g,%g,%g", double(point.x), double(point.y), double(point.z));
        Args a{{"panel", std::string(panel)}, {"point", std::string(p)}, {"facing", std::string(facing)}, {"gap_mm", gap}};
        if (up) a["up"] = std::string(up);
        CHECK(reg.execute(t.doc, "garment_place", a).ok);
    };
    place("Front", {0.0f, 116.0f, 5.0f}, "0,0,1", nullptr, 30.0);
    place("Back", {0.0f, 116.0f, backZ}, "0,0,-1", nullptr, 70.0);
    place("Left sleeve", glm::vec3(27.0f, 133.0f, -9.5f) + jitter, "0.88,0.47,0", "-0.47,0.88,0", 30.0);
    place("Right sleeve", glm::vec3(-27.0f, 133.0f, -9.5f) + jitter, "-0.88,0.47,0", "0.47,0.88,0", 30.0);
    CHECK(documentPattern(t.doc, t.pat) && t.pat.unitsPerMetre == 100.0f);
    std::string error;
    CHECK(garmentDrapeScene(t.doc, t.pat, t.scene, error));
    t.r = garmentDrapeToRest(t.scene, garmentSettings(t.pat), 900);
    for (const ClothSeam& s : t.scene.setup.seams) {
        const auto key = std::minmax(t.scene.mesh.panelOf[s.a], t.scene.mesh.panelOf[s.b]);
        const float mm = glm::length(t.r.positions[s.a] - t.r.positions[s.b]) * 1000.0f;
        float widest = 0.0f;
        for (const auto& [k, w] : t.worst) widest = std::max(widest, w);
        if (mm > widest) t.worstAt = 50.0f * (t.r.positions[s.a] + t.r.positions[s.b]);
        t.worst[key] = std::max(t.worst[key], mm);
    }
}
// The share of a sleeve's particles within 10 cm of its upper arm's axis (shoulder to above the elbow).
float roundTheArm(const TeeDrape& t, uint32_t sleeve) {
    const float side = sleeve == 2 ? 1.0f : -1.0f;
    const glm::vec3 a0(20.0f * side, 135.0f, -9.5f), a1(31.0f * side, 115.0f, -9.0f), d = a1 - a0;
    int near = 0, all = 0;
    for (size_t v = 0; v < t.r.positions.size(); ++v) {
        if (t.scene.mesh.panelOf[v] != sleeve) continue;
        const glm::vec3 p = t.r.positions[v] * 100.0f;
        const float u = std::clamp(glm::dot(p - a0, d) / glm::dot(d, d), 0.0f, 1.0f);
        ++all;
        near += glm::length(p - (a0 + u * d)) < 10.0f ? 1 : 0;
    }
    return float(near) / float(std::max(all, 1));
}

// How far the body pokes out through the cloth between particles (mm, the deepest): body vertices in front of the
// cloth that covers them (cloth facing the same way as the body there; an inward fold is not a poke).
float deepestPoke(const TeeDrape& t, int* count = nullptr) {
    ClothColliders cloth;
    cloth.add(t.r.positions, t.scene.mesh.triangles);
    cloth.build();
    Mesh body = t.doc.mesh;
    body.recomputeAllNormals();
    float deepest = 0.0f;
    int n = 0;
    for (size_t v = 0; v < body.vertexCount(); ++v) {
        const glm::vec3 b = body.positions[v] * 0.01f;
        glm::vec3 point, normal;
        if (!cloth.closest(b, 0.01f, point, normal) || glm::dot(normal, body.normals[v]) < 0.5f) continue;
        const float d = glm::dot(b - point, normal) * 1000.0f;
        if (d > 0.0f) ++n, deepest = std::max(deepest, d);
    }
    if (count) *count = n;
    return deepest;
}

} // namespace

TEST(garment_tshirt_drapes_on_the_full_body_sewn_shut) {
    // The cosplayer's path in commands; every seam closes, the Part is one surface.
    TeeDrape t;
    drapeTee(t, glm::vec3(0.0f));
    const Pattern& pat = t.pat;
    const DrapeResult& r = t.r;
    for (const auto& [k, mm] : t.worst)
        std::printf("  %s / %s: widest gap %.1f mm\n", pat.panels[k.first].name.c_str(), pat.panels[k.second].name.c_str(), mm);
    std::printf("  tee: %s after %d frames (%.0f ms), %s\n", r.rested ? "at rest" : "stopped", r.frames, r.milliseconds,
                seamGapText(pat, t.scene, r.positions).c_str());
    CHECK(r.rested);
    CHECK(particlesInside(t.scene, r.positions) == 0);
    // Nothing of the body shows through: it stays under the finished Part's outer skin (the pattern's thickness).
    int pokes = 0;
    const float poke = deepestPoke(t, &pokes);
    std::printf("  body through the cloth: %d vertices, deepest %.1f mm\n", pokes, poke);
    CHECK(poke < pat.thickness);
    CHECK(t.worst.size() == 7);
    for (const auto& [k, mm] : t.worst) CHECK(mm < 0.5f * pat.particleDistance);
    // Convert welds every seam shut: one surface, open only at the hem, the neck and the two sleeve hems.
    Pattern single = pat;
    single.thickness = 0.0f; // (one surface: a thick one is closed by its rim)
    const Mesh part = drapedMesh(single, t.scene.mesh, r.positions);
    std::map<std::pair<uint32_t, uint32_t>, int> uses;
    for (const glm::uvec3& tri : part.triangles)
        for (int k = 0; k < 3; ++k) ++uses[std::minmax(tri[k], tri[(k + 1) % 3])];
    std::map<uint32_t, std::vector<uint32_t>> next; // border edges
    int nonManifold = 0;
    for (const auto& [e, n] : uses) {
        if (n == 1) next[e.first].push_back(e.second), next[e.second].push_back(e.first);
        if (n > 2) ++nonManifold;
    }
    int loops = 0;
    std::set<uint32_t> seen;
    for (const auto& [v, ns] : next) {
        if (seen.count(v)) continue;
        ++loops;
        std::vector<uint32_t> stack{v};
        while (!stack.empty()) {
            const uint32_t u = stack.back();
            stack.pop_back();
            if (!seen.insert(u).second) continue;
            for (uint32_t w : next[u]) stack.push_back(w);
        }
    }
    std::printf("  the Part: %zu vertices, %d openings, %d edges used more than twice\n", part.vertexCount(), loops, nonManifold);
    CHECK(loops == 4 && nonManifold == 0);
    // The shoulders on the body's shoulders (139 cm at 12 cm out), not above them.
    float shoulder = 0.0f;
    for (size_t v = 0; v < r.positions.size(); ++v) {
        const glm::vec3 p = r.positions[v] * 100.0f;
        if (std::abs(std::abs(p.x) - 12.0f) < 1.5f && t.scene.mesh.panelOf[v] < 2) shoulder = std::max(shoulder, p.y);
    }
    std::printf("  shoulder seam height %.1f cm\n", shoulder);
    CHECK(shoulder > 136.0f && shoulder < 143.0f);
    for (uint32_t s = 2; s < 4; ++s) {
        std::printf("  %s: %.0f%% round the arm\n", pat.panels[s].name.c_str(), 100.0f * roundTheArm(t, s));
        CHECK(roundTheArm(t, s) > 0.75f);
    }
}

// The sleeve clicks 5 mm off in each direction: the tee still rests, sewn shut, nothing inside or through it, the
// sleeves round the arms. (One test per offset: each drape takes a few seconds.)
static void drapeTeeWithSleevesOff(glm::vec3 o) {
    TeeDrape t;
    drapeTee(t, o);
    float gap = 0.0f;
    std::string where;
    for (const auto& [k, mm] : t.worst)
        if (mm > gap) gap = mm, where = t.pat.panels[k.first].name + " / " + t.pat.panels[k.second].name;
    const float poke = deepestPoke(t);
    std::printf("  sleeves %+.0f %+.0f %+.0f mm: %s after %d frames, widest seam gap %.1f mm (%s at %.0f,%.0f,%.0f cm), "
                "round the arm %.0f%% / %.0f%%, deepest poke %.1f mm\n",
                double(o.x * 10.0f), double(o.y * 10.0f), double(o.z * 10.0f), t.r.rested ? "at rest" : "stopped",
                t.r.frames, gap, where.c_str(), double(t.worstAt.x), double(t.worstAt.y), double(t.worstAt.z),
                100.0f * roundTheArm(t, 2), 100.0f * roundTheArm(t, 3), poke);
    CHECK(t.r.rested);
    CHECK(gap < 0.5f * t.pat.particleDistance);
    CHECK(particlesInside(t.scene, t.r.positions) == 0);
    CHECK(poke < t.pat.thickness);
    CHECK(roundTheArm(t, 2) > 0.75f && roundTheArm(t, 3) > 0.75f);
}

TEST(garment_tshirt_sleeves_clicked_5mm_out) { drapeTeeWithSleevesOff({0.5f, 0.0f, 0.0f}); }
TEST(garment_tshirt_sleeves_clicked_5mm_in) { drapeTeeWithSleevesOff({-0.5f, 0.0f, 0.0f}); }
TEST(garment_tshirt_sleeves_clicked_5mm_up) { drapeTeeWithSleevesOff({0.0f, 0.5f, 0.0f}); }
TEST(garment_tshirt_sleeves_clicked_5mm_down) { drapeTeeWithSleevesOff({0.0f, -0.5f, 0.0f}); }
TEST(garment_tshirt_sleeves_clicked_5mm_forward) { drapeTeeWithSleevesOff({0.0f, 0.0f, 0.5f}); }
TEST(garment_tshirt_sleeves_clicked_5mm_back) { drapeTeeWithSleevesOff({0.0f, 0.0f, -0.5f}); }
