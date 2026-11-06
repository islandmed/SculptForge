// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Brush sets (core/BrushSets.h, #143): the .sfset files in the user library's sets/ folder.
#include "TestHarness.h"

#include "core/BrushSets.h"

#include <filesystem>
#include <fstream>

using namespace sf;
namespace fs = std::filesystem;

namespace {

struct SetsDir {
    fs::path root;
    explicit SetsDir(const char* name) : root(fs::temp_directory_path() / (std::string("sculptforge_brushsets_") + name)) {
        fs::remove_all(root);
    }
    ~SetsDir() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    std::string dir() const { return (root / "sets").string(); }
};

BrushSet woodSet() {
    BrushSet s;
    s.name = "Wood";
    s.shelf = {"brush.clay", "brush.recipe.Rivet Row", "brush.paint", "widget.add_brush"};
    s.keys = {{"brush.clay", "1"}, {"brush.recipe.Rivet Row", "Shift+W / W"}, {"draw.paint", ""}};
    return s;
}

} // namespace

TEST(brush_sets_round_trip) {
    SetsDir d("round_trip");
    CHECK(brushSetsDir({}).empty()); // no user library: nothing is saved
    CHECK(brushSetsDir({(d.root / "lib").string(), {}, {}}) == (d.root / "lib" / "sets").string());
    BrushSet wood = woodSet();
    std::string err;
    CHECK(saveBrushSet(d.dir(), wood, err));
    CHECK(fs::path(wood.path).filename() == "Wood.sfset");
    BrushSet metal = woodSet();
    metal.name = "metal";
    metal.shelf = {"brush.flatten"};
    metal.keys.clear();
    CHECK(saveBrushSet(d.dir(), metal, err));
    const std::vector<BrushSet> sets = loadBrushSets(d.dir());
    CHECK(sets.size() == 2);
    CHECK(sets[0].name == "metal"); // A-Z, ignoring case
    CHECK(sets[1].name == "Wood");
    CHECK(sets[1].shelf == woodSet().shelf);
    CHECK(sets[1].keys == woodSet().keys);

    // Saving under the same name replaces the set (its file), never adds a second one.
    wood.shelf = {"brush.move"};
    CHECK(saveBrushSet(d.dir(), wood, err));
    CHECK(loadBrushSets(d.dir()).size() == 2);
    CHECK(loadBrushSets(d.dir())[1].shelf == std::vector<std::string>{"brush.move"});

    // Names that make the same file name ("A/B" and "A_B") get files of their own.
    BrushSet slash = metal, underscore = metal;
    slash.name = "A/B";
    underscore.name = "A_B";
    CHECK(saveBrushSet(d.dir(), slash, err));
    CHECK(saveBrushSet(d.dir(), underscore, err));
    CHECK(slash.path != underscore.path);
    CHECK(loadBrushSets(d.dir()).size() == 4);

    CHECK(deleteBrushSetFile(slash, err));
    CHECK(loadBrushSets(d.dir()).size() == 3);
    CHECK(!deleteBrushSetFile(slash, err));
}

TEST(brush_sets_reject_bad_files) {
    SetsDir d("bad_files");
    BrushSet s = woodSet();
    std::string err;
    s.name = "";
    CHECK(!saveBrushSet(d.dir(), s, err));
    s.name = std::string(65, 'x');
    CHECK(!saveBrushSet(d.dir(), s, err));
    BrushSet fine = woodSet();
    CHECK(!saveBrushSet("", fine, err)); // no library folder

    fs::create_directories(d.dir());
    const auto write = [&](const char* file, const std::string& text) { std::ofstream(fs::path(d.dir()) / file, std::ios::binary) << text; };
    write("a.sfset", "not json");
    write("b.sfset", R"({"format":"sculptforge.brush","name":"X","shelf":[]})");
    write("c.sfset", R"({"format":"sculptforge.brushset","version":1,"name":"X","shelf":[1]})");
    write("d.sfset", R"({"format":"sculptforge.brushset","version":1,"name":"X","shelf":[],"keys":{"brush.clay":3}})");
    write("e.sfset", R"({"format":"sculptforge.brushset","version":2,"name":"X","shelf":[]})");
    write("f.sfset", R"({"format":"sculptforge.brushset","version":1,"name":"X","name":"Y","shelf":[]})"); // strict JSON
    write("g.sfset", std::string(kMaxBrushSetFileSize + 1, ' '));
    write("ok.sfset", R"({"format":"sculptforge.brushset","version":1,"name":"Stone","shelf":["brush.trim"]})");
    write("notes.txt", "ignored");
    std::vector<std::string> messages;
    const std::vector<BrushSet> sets = loadBrushSets(d.dir(), &messages);
    CHECK(sets.size() == 1);
    CHECK(sets[0].name == "Stone");
    CHECK(sets[0].keys.empty());
    CHECK(messages.size() == 7);
    CHECK(loadBrushSets("").empty());
    CHECK(loadBrushSets((d.root / "missing").string()).empty());
}
