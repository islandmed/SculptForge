// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Script menu's list (core/ScriptPath.h): the Scripts folder, then SCULPTFORGE_SCRIPT_PATH's folders.
#include "TestHarness.h"

#include "core/ScriptPath.h"
#include "core/StudioProfile.h"
#include "core/Utf8Path.h"

#include <filesystem>
#include <fstream>

using namespace sf;
namespace fs = std::filesystem;

namespace {

struct Tree {
    fs::path root;
    Tree() {
        root = fs::temp_directory_path() / "sculptforge_script_path";
        fs::remove_all(root);
        for (const char* d : {"scripts", "one/sub", "two"}) fs::create_directories(root / d);
        for (const char* f : {"scripts/gamma.py", "scripts/zed.py", "one/alpha.py", "one/beta.py", "one/_helper.py", "one/.hidden.py",
                              "one/notes.txt", "one/sub/deep.py", "two/beta.py", "two/gamma.py", "two/Delta.PY"})
            std::ofstream(root / f) << "pass\n";
    }
    ~Tree() { fs::remove_all(root); }
    std::string dir(const char* d) const { return pathToUtf8(root / d); }
};

std::vector<std::string> names(const std::vector<ScriptFile>& list, bool fromSearchPath) {
    std::vector<std::string> out;
    for (const ScriptFile& f : list)
        if (f.fromSearchPath == fromSearchPath) out.push_back(fileNameUtf8(f.path));
    return out;
}

} // namespace

TEST(script_path_lists_the_scripts_folder_alone) {
    Tree t;
    const auto list = listScripts(t.dir("scripts"), {});
    CHECK((names(list, false) == std::vector<std::string>{"gamma.py", "zed.py"}) && names(list, true).empty());
}

TEST(script_path_lists_folders_in_order_and_the_scripts_folder_wins) {
    Tree t;
    std::vector<std::string> skipped;
    // two before one; one repeated and the Scripts folder named again: each read once; a missing folder skipped, named once.
    const std::vector<std::string> path = {t.dir("two"), t.dir("nope"), t.dir("one"), t.dir("one"), t.dir("scripts"), t.dir("nope")};
    const auto list = listScripts(t.dir("scripts"), path, &skipped);
    CHECK((names(list, false) == std::vector<std::string>{"gamma.py", "zed.py"})); // (the Scripts folder's own, first)
    // two's Delta.PY (any case of the extension, sorted before the lower-case) and beta; gamma is hidden by the Scripts folder; one's beta by two's.
    CHECK((names(list, true) == std::vector<std::string>{"Delta.PY", "beta.py", "alpha.py"}));
    CHECK(skipped.size() == 1 && skipped[0] == t.dir("nope"));
    // Not recursive, and no hidden files, helpers or other types.
    for (const ScriptFile& f : list) CHECK(f.path.find("deep") == std::string::npos && f.path.find("_helper") == std::string::npos &&
                                           f.path.find(".hidden") == std::string::npos && f.path.find("notes") == std::string::npos);
}

TEST(script_path_a_file_is_not_a_folder) {
    Tree t;
    std::vector<std::string> skipped;
    const auto list = listScripts(t.dir("scripts"), {t.dir("one/alpha.py")}, &skipped);
    CHECK(names(list, true).empty() && skipped.size() == 1);
}

TEST(studio_profile_reads_script_paths) {
    StudioProfile p;
    std::string error;
    CHECK(parseStudioProfile(R"({"format":"sculptforge-studio","version":1,"id":"acme","name":"Acme","scriptPaths":["/studio/scripts","/show/scripts"]})",
                             "/opt/acme", p, error));
    CHECK(p.scriptPaths.size() == 2 && p.scriptPaths[0] == "/studio/scripts" && p.scriptPaths[1] == "/show/scripts");
    CHECK(!parseStudioProfile(R"({"format":"sculptforge-studio","version":1,"id":"acme","name":"Acme","scriptPaths":[""]})", "/opt/acme", p, error) &&
          error.find("scriptPaths") != std::string::npos);
}
