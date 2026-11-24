// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Origin tools (core/TransformCommands.cpp): center_on_origin and drop_to_floor put the Part's bounding box at the
// world origin / on the floor in one exact undo step, with All Parts together.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Primitives.h"

#include <cmath>
#include <cstring>

using namespace sf;

namespace {

CommandResult run(Document& d, const std::string& name, const Args& a = {}) {
    static CommandRegistry r;
    return r.execute(d, name, a);
}

Document offCentre() {
    Mesh m = makeIcosphere(3, 1.0f);
    for (auto& p : m.positions) p = p * glm::vec3(2.0f, 0.5f, 1.0f) + glm::vec3(3.5f, -1.25f, 7.0f);
    m.recomputeAllNormals();
    return makeDoc(std::move(m));
}

bool sameBits(const Mesh& a, const Mesh& b) {
    return a.positions.size() == b.positions.size() &&
           std::memcmp(a.positions.data(), b.positions.data(), a.positions.size() * sizeof(glm::vec3)) == 0;
}

} // namespace

TEST(center_on_origin_puts_the_box_centre_at_zero_and_undoes_exactly) {
    Document d = offCentre();
    const Mesh before = d.partMesh(0);
    CHECK(run(d, "center_on_origin").ok);
    const AABB b = d.partBounds(0);
    CHECK(glm::length(b.center()) < 1e-5f);
    CHECK(std::abs(b.max.x - b.min.x - 4.0f) < 1e-3f); // moved, not scaled
    CHECK(d.undo.undoCount() == 1);
    CHECK(run(d, "undo").ok);
    CHECK(sameBits(d.partMesh(0), before));
    CHECK(run(d, "center_on_origin").ok);
    CHECK(run(d, "center_on_origin").ok); // already there: nothing more to do
    CHECK(d.undo.undoCount() == 1);
}

TEST(drop_to_floor_rests_the_lowest_point_on_y_zero) {
    Document d = offCentre();
    const glm::vec3 centre = d.partBounds(0).center();
    CHECK(run(d, "drop_to_floor").ok);
    const AABB b = d.partBounds(0);
    CHECK(std::abs(b.min.y) < 1e-5f);
    CHECK(std::abs(b.center().x - centre.x) < 1e-5f && std::abs(b.center().z - centre.z) < 1e-5f); // straight down
    CHECK(d.undo.undoCount() == 1);
}

TEST(origin_tools_move_every_part_together_with_all) {
    Document d = offCentre();
    d.addPart(makeIcosphere(2, 0.5f), "Ball", false);
    d.activePart = 0;
    const glm::vec3 gap = d.partBounds(1).center() - d.partBounds(0).center();
    CHECK(run(d, "center_on_origin", {{"all", true}}).ok);
    AABB scene;
    scene.expand(d.partBounds(0));
    scene.expand(d.partBounds(1));
    CHECK(glm::length(scene.center()) < 1e-5f);
    CHECK(glm::length((d.partBounds(1).center() - d.partBounds(0).center()) - gap) < 1e-5f); // they keep their places
    CHECK(run(d, "drop_to_floor", {{"all", true}}).ok);
    CHECK(std::abs(std::min(d.partBounds(0).min.y, d.partBounds(1).min.y)) < 1e-5f);
    CHECK(run(d, "drop_to_floor", {{"all", true}}).message.find("Already") != std::string::npos);
}
