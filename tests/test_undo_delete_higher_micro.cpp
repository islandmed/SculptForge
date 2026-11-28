// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// gh#184 (0.14.4, Linux): open -> Lower -> Remove Above -> Micro divide -> alpha -> brush strokes -> Undo crashed
// (SIGSEGV in a memcpy). The sequence as a test, with every step undone and redone back and forth: the undo steps of
// the level operations and of the micro level must stay valid across each other.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Micro.h"
#include "core/Multires.h"
#include "core/Primitives.h"
#include "core/Project.h"

#include <cstring>
#include <filesystem>

using namespace sf;

namespace {

const CommandRegistry& registry() {
    static CommandRegistry r;
    return r;
}

bool run(Document& d, const std::string& name, const Args& a = {}) {
    const CommandResult r = registry().execute(d, name, a);
    if (!r.ok) std::printf("  (%s: %s)\n", name.c_str(), r.message.c_str());
    return r.ok;
}

bool stroke(Document& d, const std::string& points, double radius, const std::string& brush = "Standard") {
    return run(d, "stroke", {{"points", points}, {"brush", brush}, {"radius", radius}, {"strength", 0.8}});
}

struct Shape {
    int level = -1, levels = 0;
    size_t triangles = 0;
    int microLevel = 0;
    std::vector<int16_t> disp;
    std::vector<glm::vec3> positions;
};

Shape shapeOf(const Document& d) {
    Shape s;
    s.level = subdivCurrentLevel(d.mesh);
    s.levels = subdivLevelCount(d.mesh);
    s.triangles = d.mesh.triangleCount();
    if (d.mesh.micro) {
        s.microLevel = d.mesh.micro->level;
        s.disp = d.mesh.micro->disp.toVector();
    }
    s.positions = d.mesh.positions;
    return s;
}

bool sameShape(const Shape& a, const Shape& b) {
    return a.level == b.level && a.levels == b.levels && a.triangles == b.triangles && a.microLevel == b.microLevel &&
           a.disp == b.disp && a.positions.size() == b.positions.size() &&
           (a.positions.empty() ||
            std::memcmp(a.positions.data(), b.positions.data(), a.positions.size() * sizeof(glm::vec3)) == 0);
}

// The reporter's sequence (three levels, then a micro level on the one that is left).
Document sequenceDoc(std::vector<Shape>& states, bool strokes, bool reopened = false) {
    Document d = makeDoc(makeCube(3));
    CHECK(run(d, "subdiv_divide"));
    CHECK(run(d, "subdiv_divide"));
    CHECK(run(d, "subdiv_divide"));
    if (reopened) { // (the reporter opened a saved project: levels as a file brings them)
        const std::string path = (std::filesystem::temp_directory_path() / "sf_undo_delete_higher.sfproj").string();
        std::string err;
        CHECK(saveProject(path, d, err));
        Document loaded;
        CHECK(loadProject(path, loaded, err));
        std::filesystem::remove(path);
        d = std::move(loaded);
    }
    states.push_back(shapeOf(d));
    CHECK(run(d, "subdiv_lower"));
    states.push_back(shapeOf(d));
    CHECK(run(d, "subdiv_delete_higher"));
    states.push_back(shapeOf(d));
    CHECK(run(d, "micro_divide", {{"level", 3.0}}));
    states.push_back(shapeOf(d));
    CHECK(run(d, "brush_set_alpha", {{"name", "pores"}}));
    if (strokes) {
        CHECK(stroke(d, "0,0,2 0.2,0.1,2", 0.3));
        states.push_back(shapeOf(d));
        CHECK(stroke(d, "-0.2,0.1,2 0.1,-0.2,2", 0.5, "Clay"));
        states.push_back(shapeOf(d));
    }
    return d;
}

void walkBackAndForth(Document& d, const std::vector<Shape>& states) {
    // Every step back, checking the state it returns to, then every step forward, twice.
    for (int round = 0; round < 2; ++round) {
        for (size_t i = states.size() - 1; i > 0; --i) {
            CHECK(run(d, "undo"));
            CHECK(sameShape(shapeOf(d), states[i - 1]));
        }
        for (size_t i = 1; i < states.size(); ++i) {
            CHECK(run(d, "redo"));
            CHECK(sameShape(shapeOf(d), states[i]));
        }
    }
}

} // namespace

TEST(undo_delete_higher_then_micro_divide_undo_redo_back_and_forth) {
    std::vector<Shape> states;
    Document d = sequenceDoc(states, false);
    CHECK(d.mesh.micro != nullptr);
    walkBackAndForth(d, states);
}

TEST(undo_delete_higher_then_micro_divide_and_strokes_undo_redo_back_and_forth) {
    std::vector<Shape> states;
    Document d = sequenceDoc(states, true);
    CHECK(d.mesh.micro != nullptr);
    walkBackAndForth(d, states);
}

// The brush size changes between the alpha and the undo (the reporter's last steps): the undo step must not depend on it.
TEST(undo_after_brush_size_change_on_a_micro_level_made_after_delete_higher) {
    std::vector<Shape> states;
    Document d = sequenceDoc(states, true);
    for (double radius : {0.1, 0.9, 0.35}) {
        CHECK(stroke(d, "0.1,0.1,2 -0.1,0.0,2", radius));
        CHECK(run(d, "undo"));
    }
    CHECK(run(d, "undo"));
    CHECK(run(d, "undo"));
    CHECK(sameShape(shapeOf(d), states[3]));
}

TEST(undo_delete_higher_then_micro_divide_after_opening_a_project) {
    std::vector<Shape> states;
    Document d = sequenceDoc(states, true, true);
    CHECK(d.mesh.micro != nullptr);
    walkBackAndForth(d, states);
}
