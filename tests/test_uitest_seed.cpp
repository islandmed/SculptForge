// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The UI-test prefs seed (core/UiTestSeed.h): a `# prefs:` line is written only into an isolated profile; it refuses
// (and writes nothing) without an isolated folder, with SF_ISOLATE=0, with a settings folder outside the isolated
// one, or with a line that is not a JSON object; a script without the line is "none".
#include "TestHarness.h"

#include "core/Json.h"
#include "core/UiTestSeed.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>

using namespace sf;

namespace {

struct Dirs {
    std::filesystem::path root, isolated, data, outside, script;
    Dirs() {
        root = std::filesystem::temp_directory_path() / ("sf-seed-" + std::to_string(uint64_t(std::chrono::steady_clock::now().time_since_epoch().count())));
        isolated = root / "iso";
        data = isolated / "SculptForge" / "SculptForge";
        outside = root / "real-user";
        std::filesystem::create_directories(data);
        std::filesystem::create_directories(outside);
        script = root / "t.sfui";
    }
    ~Dirs() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
    void write(const std::string& text) const { std::ofstream(script) << text; }
    static bool exists(const std::filesystem::path& dir) { return std::filesystem::exists(dir / "project.json"); }
    std::string dataDir() const { return (data / "").string(); }
    std::string outsideDir() const { return (outside / "").string(); }
};

} // namespace

TEST(uitest_seed_writes_only_into_the_isolated_profile) {
    Dirs d;
    d.write("# prefs: {\"discord_file_name_shown\": true, \"n\": 3}\n# a comment\nrun ui_look\n");
    // Refusals first: nothing written by any of them.
    CHECK(uitest::seedPrefs(d.script.string(), "", d.dataDir(), nullptr, "9.9.9").rfind("refused", 0) == 0); // not isolated
    CHECK(uitest::seedPrefs(d.script.string(), d.isolated.string(), d.dataDir(), "0", "9.9.9").rfind("refused", 0) == 0); // SF_ISOLATE=0
    CHECK(uitest::seedPrefs(d.script.string(), d.isolated.string(), d.outsideDir(), nullptr, "9.9.9").rfind("refused", 0) == 0); // outside
    CHECK(uitest::seedPrefs(d.script.string(), d.isolated.string(), (d.root / "iso-not" / "").string(), nullptr, "9.9.9").rfind("refused", 0) == 0); // prefix trick
    CHECK(!Dirs::exists(d.data) && !Dirs::exists(d.outside));
    // The good case: the JSON lands in the isolated profile.
    CHECK(uitest::seedPrefs(d.script.string(), d.isolated.string(), d.dataDir(), nullptr, "9.9.9").empty());
    CHECK(uitest::seedPrefs(d.script.string(), d.isolated.string(), d.dataDir(), "1", "9.9.9").empty());
    CHECK(Dirs::exists(d.data) && !Dirs::exists(d.outside));
    std::ifstream in(d.data / "project.json");
    std::string text((std::istreambuf_iterator<char>(in)), {});
    Json j;
    std::string err;
    CHECK(Json::parseStrict(text, j, err));
    CHECK(j["discord_file_name_shown"].asBool(false) && j["n"].asNumber() == 3.0);
    // Counts as an install of this version that has seen the new-look card (no What's New over the test).
    CHECK(j["last_seen_version"].asString() == "9.9.9" && j["new_look_card_done"].asBool(false));
    // An explicit value in the line wins.
    d.write("# prefs: {\"last_seen_version\": \"0.11.0\", \"new_look_card_done\": false}\n");
    CHECK(uitest::seedPrefs(d.script.string(), d.isolated.string(), d.dataDir(), nullptr, "9.9.9").empty());
    std::ifstream in2(d.data / "project.json");
    std::string text2((std::istreambuf_iterator<char>(in2)), {});
    CHECK(Json::parseStrict(text2, j, err) && j["last_seen_version"].asString() == "0.11.0" && !j["new_look_card_done"].asBool(true));
}

TEST(uitest_seed_refuses_bad_lines_and_skips_scripts_without_one) {
    Dirs d;
    d.write("run ui_look\nclick \"x\"\n");
    CHECK(uitest::seedPrefs(d.script.string(), d.isolated.string(), d.dataDir(), nullptr, "9.9.9") == "none");
    d.write("# prefs: [1, 2]\n");
    CHECK(uitest::seedPrefs(d.script.string(), d.isolated.string(), d.dataDir(), nullptr, "9.9.9").rfind("refused", 0) == 0);
    d.write("# prefs: {not json\n");
    CHECK(uitest::seedPrefs(d.script.string(), d.isolated.string(), d.dataDir(), nullptr, "9.9.9").rfind("refused", 0) == 0);
    CHECK(!Dirs::exists(d.data));
    CHECK(uitest::seedPrefs("", d.isolated.string(), d.dataDir(), nullptr, "9.9.9") == "no script");
}
