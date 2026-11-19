// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// gh#223: SculptForge closed with no message importing a .glb with a Japanese name on Windows (a narrow path conversion
// of a UTF-8 name fails outside the system code page, and nothing caught it). Every format we import and export, project save and open, and pack create and
// install, under file and folder names in Japanese, Chinese, Korean, Cyrillic, accents and emoji (runs on Windows, Linux
// and macOS CI). Every path below is built with sf::pathFromUtf8 / pathToUtf8: never path(std::string) or path::string().
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Pack.h"
#include "core/Primitives.h"
#include "core/Utf8Path.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>

using namespace sf;

namespace {

namespace fs = std::filesystem;

// u8"..." literals are char8_t in C++20: spelled as escapes so the source encoding never matters.
const std::vector<std::string> kNames = {
    "\xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88",                                // テスト
    "\xE6\xB5\x8B\xE8\xAF\x95\xE6\xA8\xA1\xE5\x9E\x8B",                    // 测试模型
    "\xED\x95\x9C\xEA\xB8\x80\xEB\xAA\xA8\xEB\x8D\xB8",                    // 한글모델
    "\xD0\x9C\xD0\xBE\xD0\xB4\xD0\xB5\xD0\xBB\xD1\x8C",                    // Модель
    "Caf\xC3\xA9 \xC3\x9C" "ber \xC3\xB1" "and\xC3\xBA",                   // Café Über ñandú
    "Bust \xF0\x9F\x98\x80 \xF0\x9F\x97\xBF",                              // Bust (emoji)
};

struct Root {
    fs::path dir;
    explicit Root(const std::string& leaf) : dir(fs::temp_directory_path() / pathFromUtf8(leaf)) {
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
    }
    ~Root() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

bool has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

Document sphereDoc() {
    Document d;
    d.setMesh(makeIcosphere(2), "Sphere");
    return d;
}

} // namespace

TEST(unicode_path_helpers_round_trip) {
    for (const std::string& n : kNames) {
        const std::string path = "C:/Models/" + n + "/" + n + ".GLB";
        CHECK(fileNameUtf8(path) == n + ".GLB");
        CHECK(stemUtf8(path) == n);
        CHECK(extensionUtf8(path) == ".GLB");
        CHECK(parentUtf8(path) == "C:/Models/" + n);
        CHECK(pathToUtf8(pathFromUtf8(n)) == n);
    }
}

TEST(unicode_paths_every_format_exports_and_opens) {
    const char* const exports[] = {"export_glb", "save_obj", "export_stl", "export_ply", "export_usd", "export_fbx", "save_project"};
    const char* const exts[] = {".glb", ".obj", ".stl", ".ply", ".usda", ".fbx", ".sfproj"};
    for (const std::string& n : kNames) {
        Root root("sf_unicode_" + n);
        const fs::path folder = root.dir / pathFromUtf8(n) / pathFromUtf8(n + " folder"); // non-ASCII at two levels
        std::error_code ec;
        fs::create_directories(folder, ec);
        CHECK(!ec);
        for (size_t i = 0; i < std::size(exports); ++i) {
            const std::string file = pathToUtf8(folder / pathFromUtf8(n + exts[i]));
            CommandRegistry reg;
            Document src = sphereDoc();
            const CommandResult saved = reg.execute(src, exports[i], {{"path", file}});
            CHECK(saved.ok);
            if (!saved.ok) {
                std::fprintf(stderr, "  %s -> %s: %s\n", exports[i], file.c_str(), saved.message.c_str());
                continue;
            }
            CHECK(fs::exists(pathFromUtf8(file), ec)); // written under exactly that name, not a code-page-mangled one
            Document back;
            const CommandResult opened = reg.execute(back, "open", {{"path", file}});
            CHECK(opened.ok);
            if (!opened.ok) std::fprintf(stderr, "  open %s: %s\n", file.c_str(), opened.message.c_str());
            CHECK(back.mesh.triangleCount() > 0);
            CHECK(has(opened.message, n + exts[i])); // the status names the file, in UTF-8
        }
    }
}

TEST(unicode_paths_missing_file_fails_with_a_reason) {
    CommandRegistry reg;
    Document d;
    const std::string gone = pathToUtf8(fs::temp_directory_path() / pathFromUtf8(kNames[0] + "-missing.glb"));
    const CommandResult r = reg.execute(d, "open", {{"path", gone}});
    CHECK(!r.ok);
    CHECK(!r.message.empty());
}

TEST(unicode_paths_pack_create_and_install) {
    for (const std::string& n : kNames) {
        Root root("sf_unicode_pack_" + n);
        setPacksRoot(pathToUtf8(root.dir / pathFromUtf8(n + " packs")));
        const fs::path layout = root.dir / pathFromUtf8(n + " layout");
        fs::create_directories(layout / "brushes");
        {
            std::ofstream f(layout / "brushes" / pathFromUtf8(n + ".sfbrush"), std::ios::binary);
            f << "{\"name\":\"Unicode Brush\",\"base\":\"Clay\"}";
        }
        {
            std::ofstream f(layout / "LICENSE.txt");
            f << "CC0\n";
        }
        CommandRegistry reg;
        Document doc;
        const std::string out = pathToUtf8(root.dir / pathFromUtf8(n + ".sfpack"));
        CommandResult r = reg.execute(doc, "pack_create",
                                      {{"folder", pathToUtf8(layout)}, {"path", out}, {"id", std::string("unicode-pack")},
                                       {"name", n}, {"author", std::string("Me")}, {"license", std::string("CC0-1.0")}});
        CHECK(r.ok);
        if (!r.ok) std::fprintf(stderr, "  pack_create %s: %s\n", n.c_str(), r.message.c_str());
        r = reg.execute(doc, "pack_install", {{"path", out}});
        CHECK(r.ok);
        if (!r.ok) std::fprintf(stderr, "  pack_install %s: %s\n", out.c_str(), r.message.c_str());
        CHECK(has(reg.execute(doc, "pack_list").message, "unicode-pack"));
        CHECK(reg.execute(doc, "pack_remove", {{"id", std::string("unicode-pack")}}).ok);
        setPacksRoot("");
    }
}

TEST(a_command_that_throws_fails_with_the_reason) {
    CommandRegistry reg;
    reg.add({"throws_for_test", "Throws.", {}, [](Document&, const Args&) -> CommandResult {
                 throw std::runtime_error("No mapping for the Unicode character exists in the target multi-byte code page");
             }});
    Document d;
    const CommandResult r = reg.execute(d, "throws_for_test");
    CHECK(!r.ok);
    CHECK(has(r.message, "No mapping for the Unicode character"));
    CHECK(has(r.message, "throws_for_test"));
}
