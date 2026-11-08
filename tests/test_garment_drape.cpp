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

#include <algorithm>
#include <cmath>

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
    CHECK(defaultWrap(tube, 0) == 300.0f && defaultWrap(pat, 0) == 0.0f);
    CHECK(guessUnitsPerMetre(167.0f) == 100.0f && guessUnitsPerMetre(1.8f) == 1.0f);
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
    for (int f = 0; f < kSewFrames; ++f) CHECK(garmentStep(sim, f, base)); // (never at rest while sewing)
    std::printf("  sewn: widest seam gap %.1f mm, mean height %+.1f mm\n", widestGap(sim.positions()) * 1000.0f,
                (meanY(sim.positions()) - meanY(scene.setup.positions)) * 1000.0f);
    CHECK(widestGap(sim.positions()) < 0.3f * 0.020f);
    CHECK(meanY(sim.positions()) - meanY(scene.setup.positions) > -0.01f); // (closing pulls it up a little; it does not fall)
    // Draped (gravity throughout here): it rests on the ball, sewn shut, nothing inside it.
    const DrapeResult r = garmentDrapeToRest(scene, base, 900, 0);
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
