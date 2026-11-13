// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// core/LegacyNames.h: the old words files from older versions use are read and mapped to today's names. The old
// words are spelled only in docs/legal/trademarks.md, so these tests take them from there.
#include "TestHarness.h"

#include "app/UiLayout.h"
#include "core/BrushLibrary.h"
#include "core/Commands.h"
#include "core/LegacyNames.h"
#include "core/Sculpt.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <sstream>
#include <string>

using namespace sf;
namespace fs = std::filesystem;

namespace {

// Today's word -> the old word, from the table in docs/legal/trademarks.md.
std::map<std::string, std::string> oldWords() {
    std::ifstream f(std::string(SF_TEST_DATA_DIR) + "/../../docs/legal/trademarks.md");
    std::stringstream s;
    s << f.rdbuf();
    const std::string text = s.str();
    std::map<std::string, std::string> out;
    const std::regex row(R"(\| W\d+ \| `([^`]+)` \| `([^`]+)` \| `0x([0-9A-F]+)` \|)");
    for (std::sregex_iterator it(text.begin(), text.end(), row), end; it != end; ++it) out[(*it)[2]] = (*it)[1];
    return out;
}

std::string capitalised(std::string s) {
    if (!s.empty()) s[0] = char(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

} // namespace

TEST(legacy_names_match_the_trademarks_table) {
    const auto words = oldWords();
    CHECK(words.size() == std::size(legacy::kWords));
    for (const legacy::Word& w : legacy::kWords) {
        const auto it = words.find(std::string(w.lower));
        CHECK(it != words.end());
        if (it == words.end()) continue;
        CHECK(legacy::hash(it->second) == w.hash && it->second.size() == w.length);
        CHECK(legacy::modernize(it->second) == w.lower);
        CHECK(legacy::modernize(capitalised(it->second)) == w.title);
    }
    CHECK(legacy::modernize("brush.standard") == "brush.standard");
    CHECK(!legacy::isOld("part_list layout=classic Lit Spheres"));
}

TEST(legacy_names_read_old_files_and_scripts) {
    const auto old = oldWords();
    if (old.size() < 12) {
        CHECK(false);
        return;
    }
    // Ids, keys, command names and folder names keep their case and the text around them.
    CHECK(legacy::modernize("brush." + old.at("sharpcrease")) == "brush.sharpcrease");
    CHECK(legacy::modernize("active" + capitalised(old.at("part"))) == "activePart");
    CHECK(legacy::modernize(old.at("part") + "s/0/mesh/" + old.at("facegroup") + "s.bin") == "parts/0/mesh/facegroups.bin");
    std::string upper = old.at("litsphere");
    for (char& c : upper) c = char(std::toupper(static_cast<unsigned char>(c)));
    CHECK(legacy::modernize("view." + upper) == "view.LITSPHERE");
    CHECK(legacy::modernize(capitalised(old.at("litsphere")) + "s") == "LitSpheres");

    // Brush names in projects and brush files.
    BrushType t = BrushType::Standard;
    CHECK(brushFromName(old.at("sharpcrease"), t) && t == BrushType::SharpCrease);
    CHECK(brushFromName(capitalised(old.at("clayblock")), t) && t == BrushType::ClayBlock);
    // ... spelled with a space, hyphen, underscore or dot between its words, in any case.
    const std::string clay = old.at("clayblock");
    for (const char* sep : {" ", "  ", "-", "_", "."}) {
        t = BrushType::Standard;
        CHECK(brushFromName(capitalised(clay.substr(0, 4)) + sep + capitalised(clay.substr(4)), t) &&
              t == BrushType::ClayBlock);
    }

    // JSON keys (projects, ui.json); values stay as they are.
    Json j = Json::object();
    j.set(old.at("library") + "Open", true);
    j.set("name", old.at("classic") + " head");
    const Json m = legacy::modernizeKeys(j);
    CHECK(m["libraryOpen"].asBool() && m["name"].asString() == old.at("classic") + " head");

    // Scripts and macros: old command names, and old values the command refuses as they are.
    CommandRegistry reg;
    CHECK(reg.find(old.at("part") + "_list") == reg.find("part_list") && reg.find("part_list") != nullptr);
    Document doc;
    CHECK(reg.execute(doc, old.at("part") + "_list", {}).ok);
    CHECK(!reg.execute(doc, "no_such_" + old.at("part"), {}).ok);
    // A path with an old word in it (a user's folder) is never rewritten, and a failure keeps its own message.
    const std::string path = "/no/such/" + capitalised(old.at("classic")) + "/model.obj";
    const CommandResult missing = reg.execute(doc, "open", {{"path", path}});
    CHECK(!missing.ok && missing.message.find(capitalised(old.at("classic"))) != std::string::npos);
}

// Whole names (kNames): the old name, in any case, is today's; the word alone, or inside another name (a user's own
// brush), stays as it is.
TEST(legacy_whole_names_match_the_trademarks_table) {
    std::ifstream f(std::string(SF_TEST_DATA_DIR) + "/../../docs/legal/trademarks.md");
    std::stringstream s;
    s << f.rdbuf();
    const std::string text = s.str();
    const std::regex row(R"(\| N\d+ \| `([^`]+)` \| `([^`]+)` \| `0x([0-9A-F]+)` \|)");
    size_t rows = 0;
    for (std::sregex_iterator it(text.begin(), text.end(), row), end; it != end; ++it, ++rows) {
        const std::string old = (*it)[1], now = (*it)[2];
        CHECK(legacy::modernize(old) == now);
        std::string mixed = old; // as the app wrote it: words capitalised
        for (size_t i = 0; i < mixed.size(); ++i)
            if (i == 0 || mixed[i - 1] == ' ' || mixed[i - 1] == '.')
                mixed[i] = char(std::toupper(static_cast<unsigned char>(mixed[i])));
        CHECK(legacy::modernize(mixed) == now);
        CHECK(legacy::isOld(old));
        if (old.find(' ') != std::string::npos) { // a name of words: each alone stays
            const std::string word = old.substr(old.rfind(' ') + 1);
            CHECK(legacy::modernize(word) == word);
            CHECK(legacy::modernize("brush.recipe.My " + capitalised(word)) == "brush.recipe.My " + capitalised(word));
        }
        CHECK(legacy::modernize(old + " 2") == old + " 2");
        CHECK(legacy::modernize("my_" + old) == "my_" + old);
    }
    CHECK(rows == std::size(legacy::kNames));
}

// Extract's old argument names (N3, N4) still work in a script: the same shell as today's names.
TEST(legacy_extract_arguments) {
    std::ifstream f(std::string(SF_TEST_DATA_DIR) + "/../../docs/legal/trademarks.md");
    std::stringstream s;
    s << f.rdbuf();
    const std::string text = s.str();
    std::map<std::string, std::string> was; // today's name -> the old one
    const std::regex row(R"(\| N\d+ \| `([^`]+)` \| `([^`]+)` \|)");
    for (std::sregex_iterator it(text.begin(), text.end(), row), end; it != end; ++it) was[(*it)[2]] = (*it)[1];
    CHECK(was.count("triangle_corners") && was.count("triangle_border"));
    if (!was.count("triangle_corners") || !was.count("triangle_border")) return;
    auto quads = [](const std::string& corners, const std::string& border) { // triangles in quads in the new Part
        CommandRegistry reg;
        Document doc;
        CHECK(reg.execute(doc, "new_sphere", {{"subdivisions", 3.0}}).ok);
        doc.mesh.ensureMask();
        for (uint32_t v = 0; v < doc.mesh.vertexCount(); ++v) doc.mesh.mask[v] = doc.mesh.positions[v].y > 0.5f ? 1.0f : 0.0f;
        CHECK(reg.execute(doc, "extract", {{"smooth", 0.0}, {corners, true}, {border, true}, {"accept", true}}).ok);
        size_t n = 0;
        if (doc.partCount() == 2)
            for (uint32_t t = 0; t < doc.partMesh(1).triangleCount(); ++t) n += doc.partMesh(1).quadPartnerAt(t) != Mesh::kNoQuad;
        return n;
    };
    const size_t today = quads("triangle_corners", "triangle_border");
    CHECK(quads(was["triangle_corners"], was["triangle_border"]) == today);
    CHECK(quads("no_corners", "no_border") > today); // (names it doesn't know: the rim stays quads)
}

// A custom brush's name is the user's own text: a brush called after an old word (the first of the table, in a made-up
// name) keeps its shelf slot and its key. The layout loader used to rewrite it to today's spelling, so the slot looked for
// a name no brush file has and showed "missing" with the file still in the library.
TEST(legacy_names_leave_custom_brush_names_alone) {
    const auto old = oldWords();
    if (!old.count("classic")) {
        CHECK(false);
        return;
    }
    const std::string name = "Polish - " + capitalised(old.at("classic")) + " Soft"; // made up
    const std::string today = "Polish - Classic Soft";
    CHECK(legacy::modernize(name) == today); // the rewrite itself is right for SculptForge's own words
    for (const std::string prefix : {"brush.recipe.", "recipe:", "brush.file.", "file:"}) {
        CHECK(legacy::modernizeId(prefix + name) == prefix + name);
        CHECK(legacy::modernizeId(prefix + "Plain") == prefix + "Plain");
    }
    CHECK(legacy::modernizeId("brush." + old.at("sharpcrease")) == "brush.sharpcrease"); // ids still map
    CHECK(legacy::modernizeId("brush.recipe.Stone Carve") == "brush.recipe.Stone Carve");

    // The layout file: the shelf item and the slot's learned id keep the typed name.
    UiLayout l = standardLayout();
    l.bottomShelf.push_back("brush.recipe." + name);
    l.bottomShelf.push_back("recipe:" + name);
    l.brushIds[name] = "0123456789abcdef";
    Json ui = Json::object();
    ui.set("layout", toJson(l));
    ui = legacy::modernizeKeys(ui); // what App::readUiConfig does to the whole file
    UiLayout back;
    std::string err;
    CHECK(fromJson(ui["layout"], back, err));
    CHECK(std::find(back.bottomShelf.begin(), back.bottomShelf.end(), "brush.recipe." + name) != back.bottomShelf.end());
    CHECK(std::find(back.bottomShelf.begin(), back.bottomShelf.end(), "recipe:" + name) != back.bottomShelf.end());
    CHECK(back.brushIds.count(name) == 1 && back.brushIds[name] == "0123456789abcdef");
}

// The slot of a brush called after an old word, end to end: install, put on the shelf, save and read the layout, restart.
// A slot a version that rewrote the name already saved (today's spelling, no id) still finds the brush and then follows it.
TEST(brush_slot_of_a_brush_named_after_an_old_word_survives_restart) {
    const auto old = oldWords();
    if (!old.count("classic")) {
        CHECK(false);
        return;
    }
    const std::string name = "Polish - " + capitalised(old.at("classic")) + " Soft";
    const fs::path root = fs::temp_directory_path() / "sculptforge_oldword_slot";
    fs::remove_all(root);
    BrushLibraryPaths paths;
    paths.userDir = (root / "user").string();
    Document doc;
    loadBrushLibrary(doc, paths);
    BrushRecipe r;
    r.name = name;
    std::string err, note;
    const int made = installBrush(doc, r, err, false, paths);
    CHECK(err.empty() && made >= 0 && !doc.brushes[size_t(made)].id.empty());
    const std::string id = doc.brushes[size_t(made)].id;

    // Shelf and ids written, read back the way the app reads ui.json, the library loaded again.
    UiLayout l = standardLayout();
    l.bottomShelf.push_back("brush.recipe." + name);
    l.brushIds[name] = id;
    Json ui = legacy::modernizeKeys(toJson(l));
    UiLayout back;
    CHECK(fromJson(ui, back, err));
    Document restarted;
    loadBrushLibrary(restarted, paths);
    const std::string item = back.bottomShelf.back();
    const std::string slotName = item.substr(std::string("brush.recipe.").size());
    CHECK(slotName == name);
    const int found = findBrushForSlot(restarted, slotName, back.brushIds.count(slotName) ? back.brushIds[slotName] : "");
    CHECK(found >= 0 && restarted.brushes[size_t(found)].name == name);

    // A slot with no id (never found since it was made) in today's spelling: found by the file's name through the rewrite.
    CHECK(findBrushForSlot(restarted, "Polish - Classic Soft", "") >= 0);
    CHECK(relinkBrush(restarted, "Polish - Classic Soft", "", note, paths) >= 0);
    Document cold; // not loaded yet: the library is searched
    CHECK(relinkBrush(cold, "Polish - Classic Soft", "", note, paths) >= 0);
    CHECK(relinkBrush(cold, "Polish - Other Soft", "", note, paths) < 0); // a different brush is never taken for it
    CHECK(describeMissingSlot(cold, "Polish - Other Soft", "").find("Polish - Other Soft") != std::string::npos);
    fs::remove_all(root);
}
