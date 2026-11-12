// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The command line's parser (core/CommandParse.*): plain words with numbers, typos, another language, ambiguity.
#include "TestHarness.h"

#include "core/CommandParse.h"

#include <cmath>

using namespace sf::cmd;

namespace {

Entry entry(const char* id, const char* label, Role role, std::vector<std::string> names = {}, double lo = 0, double hi = 0,
            int priority = 0) {
    Entry e;
    e.id = id;
    e.label = label;
    e.names = std::move(names);
    for (auto& s : sf::cmd::synonymNames(id)) e.names.push_back(s);
    e.role = role;
    e.lo = lo;
    e.hi = hi;
    e.priority = priority;
    return e;
}

Catalog catalog() {
    Catalog c;
    c.entries = {
        entry("brush.clay", "Clay", Role::Brush, {"粘土"}, 0.5, 800, 2),
        entry("brush.clayblock", "Clay Block", Role::Brush, {}, 0.5, 800, 1),
        entry("brush.smooth", "Smooth", Role::Brush, {"平滑", "Suavizar"}, 0.5, 800, 2),
        entry("geometry.smt", "Smooth Subdivision", Role::Plain),
        entry("@size", "Size", Role::Value, {"大小"}, 0.5, 800),
        entry("@intensity", "Intensity", Role::Value, {"强度", "Intensidad"}, 0, 100),
        entry("@symmetry", "Symmetry", Role::Switch, {"对称", "Simetría"}),
        entry("view.wireframe", "Wireframe", Role::Switch, {"线框"}),
        entry("view.xray", "X-Ray", Role::Switch, {"X 光"}),
        entry("view.ortho", "Orthographic", Role::Plain, {"正交"}),
        entry("view.orbit_lock_y", "Y (turntable)", Role::Plain, {"转盘"}),
        entry("geometry.remesh", "Voxel Remesh", Role::Amount, {"体素重新网格化"}, 32, 4096),
        entry("@hide", "Hide", Role::Visibility, {"隐藏", "Ocultar"}),
        entry("@show", "Show", Role::Visibility, {"显示"}),
        entry("@isolate", "Isolate", Role::Visibility),
    };
    c.parts = {"Head", "Body", "Arm L", "Arm R", "Eye"};
    return c;
}

const Interpretation* top(const std::vector<Interpretation>& r) { return r.empty() ? nullptr : &r.front(); }

std::string topId(const Catalog& c, const std::vector<Interpretation>& r) { return r.empty() ? "" : c.entries[r.front().entry].id; }

bool near(double a, double b) { return std::abs(a - b) < 1e-6; }

} // namespace

TEST(command_parse_numbers_and_units) {
    double v = 0;
    Unit u = Unit::None;
    CHECK(parseNumber("40", v, u) && near(v, 40) && u == Unit::None);
    CHECK(parseNumber("200k", v, u) && near(v, 200000) && u == Unit::Triangles);
    CHECK(parseNumber("1.5m", v, u) && near(v, 1.5e6) && u == Unit::Triangles);
    CHECK(parseNumber("30%", v, u) && near(v, 30) && u == Unit::Percent);
    CHECK(parseNumber("12px", v, u) && near(v, 12) && u == Unit::Px);
    CHECK(parseNumber("12 px", v, u) && u == Unit::Px);
    CHECK(parseNumber(".5", v, u) && near(v, 0.5));
    CHECK(parseNumber("４０", v, u) && near(v, 40)); // full-width digits
    CHECK(parseNumber("1.5m", v, u) && near(v, 1.5e6));
    CHECK(parseNumber("1,5m", v, u) && near(v, 1.5e6)); // a decimal comma
    CHECK(!parseNumber("clay", v, u));
    CHECK(!parseNumber("40 50", v, u));
    CHECK(editDistance("smooth", "smoth") == 1);
    CHECK(editDistance("smooth", "smotoh") == 1); // two letters swapped
    CHECK(editDistance("Simetría", "simetria") == 0);
}

TEST(command_parse_brush_with_size_and_intensity) {
    const Catalog c = catalog();
    auto r = interpret(c, "clay 40");
    CHECK(topId(c, r) == "brush.clay");
    CHECK(top(r) && top(r)->hasSize && near(top(r)->size, 40) && !top(r)->hasIntensity);
    r = interpret(c, "Clay 40 30%");
    CHECK(top(r) && near(top(r)->size, 40) && top(r)->hasIntensity && near(top(r)->intensity, 30));
    r = interpret(c, "smooth 30%");
    CHECK(topId(c, r) == "brush.smooth" && top(r)->hasIntensity && !top(r)->hasSize);
    r = interpret(c, "clay size 25 intensity 60");
    CHECK(topId(c, r) == "brush.clay" && near(top(r)->size, 25) && near(top(r)->intensity, 60));
    r = interpret(c, "use the clay brush at 40"); // filler words
    CHECK(topId(c, r) == "brush.clay" && top(r) && near(top(r)->size, 40));
    r = interpret(c, "clay 5000"); // validated: the size is moved into its range
    CHECK(top(r) && near(top(r)->size, 800) && top(r)->clamped);
    r = interpret(c, "clay 40 50");
    for (const auto& i : interpret(c, "clay 40 50")) CHECK(!(c.entries[i.entry].id == "brush.clay"));
}

TEST(command_parse_settings) {
    const Catalog c = catalog();
    auto r = interpret(c, "intensity 30");
    CHECK(topId(c, r) == "@intensity" && top(r)->hasNumber && near(top(r)->number, 30));
    r = interpret(c, "set intensity to 130"); // clamped to 100, and it says so
    CHECK(topId(c, r) == "@intensity" && near(top(r)->number, 100) && top(r)->clamped);
    r = interpret(c, "size 40px");
    CHECK(topId(c, r) == "@size" && near(top(r)->number, 40) && top(r)->unit == Unit::Px);
    r = interpret(c, "intensity"); // the name without the number: still a reading, flagged
    CHECK(topId(c, r) == "@intensity" && top(r)->needsNumber);
}

TEST(command_parse_switches) {
    const Catalog c = catalog();
    auto r = interpret(c, "mirror x");
    CHECK(topId(c, r) == "@symmetry" && top(r)->axis == 'x' && top(r)->state == -1);
    r = interpret(c, "symmetry off");
    CHECK(topId(c, r) == "@symmetry" && top(r)->state == 0 && top(r)->axis == 0);
    r = interpret(c, "mirror y on");
    CHECK(topId(c, r) == "@symmetry" && top(r)->state == 1 && top(r)->axis == 'y');
    r = interpret(c, "wireframe on");
    CHECK(topId(c, r) == "view.wireframe" && top(r)->state == 1);
    r = interpret(c, "xray"); // an axis letter inside a name is not an axis
    CHECK(topId(c, r) == "view.xray");
    r = interpret(c, "x-ray off");
    CHECK(topId(c, r) == "view.xray" && top(r)->state == 0 && top(r)->axis == 0);
    r = interpret(c, "mirror on off"); // contradicting words: not a command
    for (const auto& i : r) CHECK(c.entries[i.entry].id != "@symmetry");
}

TEST(command_parse_plain_names_typos_and_synonyms) {
    const Catalog c = catalog();
    CHECK(topId(c, interpret(c, "ortho")) == "view.ortho");
    CHECK(topId(c, interpret(c, "orthografic")) == "view.ortho"); // typo
    CHECK(topId(c, interpret(c, "no perspective")) == "view.ortho"); // synonym
    CHECK(topId(c, interpret(c, "turntable")) == "view.orbit_lock_y");
    CHECK(topId(c, interpret(c, "smoth")) == "brush.smooth");
    CHECK(topId(c, interpret(c, "claay 40")) == "brush.clay");
    CHECK(topId(c, interpret(c, "wirefrmae")) == "view.wireframe");
    CHECK(topId(c, interpret(c, "symetry off")) == "@symmetry");
    CHECK(interpret(c, "qzxv").empty());                       // nothing close: a normal search
    CHECK(interpret(c, "ortho 40").empty());                   // a number it cannot use: not this
    CHECK(interpret(c, "").empty());
}

TEST(command_parse_remesh_amounts) {
    const Catalog c = catalog();
    auto r = interpret(c, "remesh 200k");
    CHECK(topId(c, r) == "geometry.remesh" && top(r)->unit == Unit::Triangles && near(top(r)->number, 200000));
    r = interpret(c, "remesh 300");
    CHECK(topId(c, r) == "geometry.remesh" && top(r)->unit == Unit::Px && near(top(r)->number, 300));
    r = interpret(c, "remesh 1000"); // from 1000 a bare number is triangles: never a 1000-voxel grid by surprise
    CHECK(topId(c, r) == "geometry.remesh" && top(r)->unit == Unit::Triangles && near(top(r)->number, 1000));
    r = interpret(c, "remesh 20");
    CHECK(top(r) && near(top(r)->number, 32) && top(r)->clamped); // voxels: 32 to 4096
    r = interpret(c, "remesh 9000m");
    CHECK(top(r) && top(r)->unit == Unit::Triangles && near(top(r)->number, 50e6) && top(r)->clamped);
    r = interpret(c, "remesh");
    CHECK(topId(c, r) == "geometry.remesh" && !top(r)->hasNumber);
}

TEST(command_parse_hide_and_show_parts) {
    const Catalog c = catalog();
    auto r = interpret(c, "hide all but head");
    CHECK(topId(c, r) == "@hide" && top(r)->all && top(r)->except && top(r)->parts.size() == 1 && c.parts[top(r)->parts[0]] == "Head");
    r = interpret(c, "hide everything except eye");
    CHECK(topId(c, r) == "@hide" && top(r)->all && top(r)->except && c.parts[top(r)->parts[0]] == "Eye");
    r = interpret(c, "show all");
    CHECK(topId(c, r) == "@show" && top(r)->all && !top(r)->except && top(r)->parts.empty());
    r = interpret(c, "hide hed"); // typo in a Part name
    CHECK(topId(c, r) == "@hide" && !top(r)->all && top(r)->parts.size() == 1 && c.parts[top(r)->parts[0]] == "Head");
    r = interpret(c, "isolate body");
    CHECK(topId(c, r) == "@isolate" && c.parts[top(r)->parts[0]] == "Body");
    r = interpret(c, "hide arm"); // two Parts match equally: both, and the preview says which
    CHECK(topId(c, r) == "@hide" && top(r)->parts.size() == 2);
    r = interpret(c, "hide arm r");
    CHECK(top(r) && top(r)->parts.size() == 1 && c.parts[top(r)->parts[0]] == "Arm R");
    r = interpret(c, "hide");
    CHECK(topId(c, r) == "@hide" && top(r)->needsTarget);
    r = interpret(c, "hide the moon"); // no such Part: not a command
    for (const auto& i : r) CHECK(c.entries[i.entry].id != "@hide");
}

TEST(command_parse_another_language) {
    const Catalog c = catalog();
    // Simplified Chinese: no spaces between words, the number straight after.
    auto r = interpret(c, "粘土40");
    CHECK(topId(c, r) == "brush.clay" && top(r)->hasSize && near(top(r)->size, 40));
    r = interpret(c, "平滑 30%");
    CHECK(topId(c, r) == "brush.smooth" && top(r)->hasIntensity && near(top(r)->intensity, 30));
    r = interpret(c, "强度 55");
    CHECK(topId(c, r) == "@intensity" && near(top(r)->number, 55));
    r = interpret(c, "对称 关"); // "symmetry off"
    CHECK(topId(c, r) == "@symmetry" && top(r)->state == 0);
    r = interpret(c, "正交");
    CHECK(topId(c, r) == "view.ortho");
    r = interpret(c, "体素重新网格化 200k");
    CHECK(topId(c, r) == "geometry.remesh" && top(r)->unit == Unit::Triangles);
    r = interpret(c, "隐藏 眼"); // Part names stay as they are named
    CHECK(r.empty() || c.entries[r.front().entry].id != "@hide" || r.front().parts.empty());
    r = interpret(c, "对称x关"); // Chinese next to Latin letters: words of their own
    CHECK(topId(c, r) == "@symmetry" && top(r)->axis == 'x' && top(r)->state == 0);
    r = interpret(c, "隐藏 全部"); // "hide all", and Spanish below
    CHECK(topId(c, r) == "@hide" && top(r)->all && top(r)->parts.empty());
    r = interpret(c, "ocultar todo menos eye");
    CHECK(topId(c, r) == "@hide" && top(r)->all && top(r)->except && c.parts[top(r)->parts[0]] == "Eye");
    // Spanish: accents optional, a typo forgiven.
    r = interpret(c, "intensidad 20");
    CHECK(topId(c, r) == "@intensity" && near(top(r)->number, 20));
    r = interpret(c, "simetria x");
    CHECK(topId(c, r) == "@symmetry" && top(r)->axis == 'x');
    r = interpret(c, "suavisar 10"); // s for z
    CHECK(topId(c, r) == "brush.smooth");
}

TEST(command_parse_ambiguity_and_ranking) {
    const Catalog c = catalog();
    auto r = interpret(c, "clay");
    CHECK(r.size() >= 2 && topId(c, r) == "brush.clay"); // Clay before Clay Block
    CHECK(r[0].exact);
    r = interpret(c, "smooth");
    CHECK(topId(c, r) == "brush.smooth"); // the brush, not Smooth Subdivision; both are offered
    bool offered = false;
    for (const auto& i : r) offered = offered || c.entries[i.entry].id == "geometry.smt";
    CHECK(offered);
    r = interpret(c, "smooth 3"); // a number: only what takes one
    CHECK(topId(c, r) == "brush.smooth" && near(top(r)->size, 3));
    for (const auto& i : r) CHECK(c.entries[i.entry].id != "geometry.smt");
    r = interpret(c, "cla");
    CHECK(topId(c, r) == "brush.clay");
    // Deterministic: the same words, the same order.
    const auto a = interpret(c, "sm"), b = interpret(c, "sm");
    CHECK(a.size() == b.size());
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) CHECK(a[i].entry == b[i].entry);
    CHECK(interpret(c, "clay", 1).size() == 1);
}

TEST(command_parse_canonical_lines_round_trip) {
    const Catalog c = catalog();
    for (const char* q : {"clay 40 30%", "smooth 30%", "intensity 30", "size 40px", "mirror x", "symmetry off", "wireframe on",
                          "remesh 200k", "remesh 300", "hide all but head", "show all", "isolate body", "hide arm r", "ortho"}) {
        const auto first = interpret(c, q);
        CHECK(!first.empty());
        if (first.empty()) continue;
        const std::string line = canonicalLine(c, first.front());
        const auto again = interpret(c, line);
        CHECK(!again.empty());
        if (again.empty()) continue;
        CHECK(again.front().entry == first.front().entry);
        CHECK(canonicalLine(c, again.front()) == line);
        CHECK(again.front().state == first.front().state && again.front().axis == first.front().axis);
        CHECK(again.front().parts == first.front().parts);
        CHECK(near(again.front().number, first.front().number) && near(again.front().size, first.front().size) &&
              near(again.front().intensity, first.front().intensity));
    }
}

TEST(command_history_keeps_fifty_and_walks_back) {
    CommandHistory h;
    CHECK(h.older().empty() && h.newer().empty());
    h.push("clay 40");
    h.push("mirror x");
    h.push("remesh 200k");
    CHECK(h.lines().size() == 3 && h.lines()[0] == "remesh 200k");
    CHECK(h.older() == "remesh 200k" && h.browsing());
    CHECK(h.older() == "mirror x");
    CHECK(h.older() == "clay 40");
    CHECK(h.older() == "clay 40"); // the oldest stays
    CHECK(h.newer() == "mirror x");
    CHECK(h.newer() == "remesh 200k");
    CHECK(h.newer().empty() && !h.browsing());
    h.push("clay 40"); // ran again: moves to the front, no copy
    CHECK(h.lines().size() == 3 && h.lines()[0] == "clay 40");
    h.push("");
    CHECK(h.lines().size() == 3);
    for (int i = 0; i < 80; ++i) h.push("line " + std::to_string(i));
    CHECK(h.lines().size() == CommandHistory::kMax && h.lines()[0] == "line 79");
    CommandHistory g;
    g.fromText(h.toText());
    CHECK(g.lines() == h.lines());
}
