// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// core/LegacyNames.h: the old words files from older versions use are read and mapped to today's names. The old
// words are spelled only in docs/legal/trademarks.md, so these tests take them from there.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/LegacyNames.h"
#include "core/Sculpt.h"

#include <cctype>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <sstream>
#include <string>

using namespace sf;

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
