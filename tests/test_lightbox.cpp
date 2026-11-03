// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// LightBox library logic: thumbnail cache keys, project thumbnails in .sfproj,
// folder listing, QuickSave files and the brush pop-up's letter keys.
#include "TestHarness.h"

#include "app/AssetLibrary.h"
#include "core/Primitives.h"
#include "core/Project.h"

#include <chrono>
#include <filesystem>
#include <fstream>

using namespace sf;

namespace {

namespace fs = std::filesystem;

void touch(const fs::path& p, const std::string& text = "x") { std::ofstream(p, std::ios::binary) << text; }

Image gradient(int w, int h) {
    Image img;
    img.width = w;
    img.height = h;
    img.rgba.resize(size_t(w) * size_t(h) * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            uint8_t* p = &img.rgba[(size_t(y) * size_t(w) + size_t(x)) * 4];
            p[0] = uint8_t(x * 255 / std::max(w - 1, 1));
            p[1] = uint8_t(y * 255 / std::max(h - 1, 1));
            p[2] = 77;
            p[3] = 255;
        }
    return img;
}

} // namespace

TEST(thumbnail_cache_keys) {
    // FNV-1a 64 reference values.
    CHECK(fnv1a64("") == 0xcbf29ce484222325ull);
    CHECK(fnv1a64("a") == 0xaf63dc4c8601ec8cull);
    CHECK(hex64(0xaf63dc4c8601ec8cull) == "af63dc4c8601ec8c");

    const std::string k = fileThumbnailKey("mesh", "/lib/Tools/head.obj", 1234, 1700000000, 192);
    CHECK(k.size() == 16);
    CHECK(k == fileThumbnailKey("mesh", "/lib/Tools/head.obj", 1234, 1700000000, 192)); // stable
    // Anything that changes the picture changes the key.
    CHECK(k != fileThumbnailKey("mesh", "/lib/Tools/head.obj", 1235, 1700000000, 192));    // rewritten (size)
    CHECK(k != fileThumbnailKey("mesh", "/lib/Tools/head.obj", 1234, 1700000001, 192));    // rewritten (time)
    CHECK(k != fileThumbnailKey("mesh", "/lib/Tools/head2.obj", 1234, 1700000000, 192));   // another file
    CHECK(k != fileThumbnailKey("project", "/lib/Tools/head.obj", 1234, 1700000000, 192)); // another kind
    CHECK(k != fileThumbnailKey("mesh", "/lib/Tools/head.obj", 1234, 1700000000, 96));     // another size
    CHECK(k != fileThumbnailKey("mesh", "/lib/Tools/head.obj", 1234, 1700000000, 192, kThumbnailVersion + 1));
    const std::string path = thumbnailCachePath("/prefs/thumbs", k);
    CHECK(fs::path(path).filename().string() == k + ".png");
}

TEST(project_thumbnail_round_trip) {
    const fs::path dir = fs::path(tempPath("sf_lightbox_thumb"));
    fs::remove_all(dir);
    fs::create_directories(dir);
    Document doc = makeDoc(makeIcosphere(2));
    const Image picture = gradient(32, 24);

    // With a provider, saving embeds the picture and loadProjectThumbnail reads it back exactly.
    int calls = 0;
    setProjectThumbnailProvider([&](const Document& d, Image& out) {
        ++calls;
        CHECK(d.mesh.triangleCount() == doc.mesh.triangleCount());
        out = picture;
        return true;
    });
    const std::string with = (dir / "with.sfproj").string();
    std::string err;
    CHECK(saveProject(with, doc, err));
    CHECK(calls == 1);
    Image back;
    CHECK(loadProjectThumbnail(with, back, err));
    CHECK(back.width == 32 && back.height == 24);
    CHECK(back.rgba == picture.rgba);
    // The project itself still loads (the extra entry is ignored).
    Document loaded;
    CHECK(loadProject(with, loaded, err));
    CHECK(loaded.mesh.triangleCount() == doc.mesh.triangleCount());

    // A provider that declines (e.g. not on the UI thread), or none at all (the CLI): no thumbnail.
    setProjectThumbnailProvider([](const Document&, Image&) { return false; });
    const std::string declined = (dir / "declined.sfproj").string();
    CHECK(saveProject(declined, doc, err));
    CHECK(!loadProjectThumbnail(declined, back, err));
    setProjectThumbnailProvider(nullptr);
    const std::string without = (dir / "without.sfproj").string();
    CHECK(saveProject(without, doc, err));
    CHECK(!loadProjectThumbnail(without, back, err));
    CHECK(err.find("no thumbnail") != std::string::npos);
    CHECK(loadProject(without, loaded, err));

    // Not a project at all.
    touch(dir / "junk.sfproj", "definitely not a zip");
    CHECK(!loadProjectThumbnail((dir / "junk.sfproj").string(), back, err));
    CHECK(!loadProjectThumbnail((dir / "missing.sfproj").string(), back, err));
    fs::remove_all(dir);
}

TEST(library_folders_and_listing) {
    const fs::path root = fs::path(tempPath("sf_lightbox_lib"));
    fs::remove_all(root);
    std::string err;
    CHECK(ensureUserLibrary(root.string(), err));
    for (const char* sub : {"Projects", "Tools", "Brushes", "Alphas", "Images", "Splats", "QuickSave"})
        CHECK(fs::is_directory(root / sub));
    CHECK(defaultUserLibrary("/home/a/Documents", "/prefs/") ==
          (fs::path("/home/a/Documents") / "SculptForge").string());
    CHECK(!defaultUserLibrary("", "/prefs/").empty());

    // Folders first (by name, any case), then the files the tab takes; hidden files skipped.
    const fs::path tools = root / "Tools";
    fs::create_directories(tools / "creatures");
    fs::create_directories(tools / "Armor");
    touch(tools / "zebra.obj");
    touch(tools / "Apple.glb");
    touch(tools / "notes.txt");
    touch(tools / ".hidden.obj");
    const auto list =
        listFolder(tools.string(), [](const std::string& p) { return fileKindForTab(LibraryTab::Tool, p); });
    CHECK(list.size() == 4);
    if (list.size() == 4) {
        CHECK(list[0].kind == AssetKind::Folder && list[0].name == "Armor");
        CHECK(list[1].kind == AssetKind::Folder && list[1].name == "creatures");
        CHECK(list[2].kind == AssetKind::Mesh && list[2].name == "Apple");
        CHECK(list[3].kind == AssetKind::Mesh && list[3].name == "zebra");
        CHECK(list[3].size == 1);
    }
    CHECK(listFolder((root / "nope").string(), nullptr).empty());

    // Tabs and file kinds.
    for (int i = 0; i < int(LibraryTab::Count); ++i)
        CHECK(libraryTabFromId(libraryTabId(LibraryTab(i))) == LibraryTab(i));
    CHECK(libraryTabFromId("bogus") == LibraryTab::Project);
    CHECK(fileKindForTab(LibraryTab::Recent, "/a/b.SFPROJ") == AssetKind::Project);
    CHECK(fileKindForTab(LibraryTab::Splats, "/a/capture.ply") == AssetKind::Splat);
    CHECK(fileKindForTab(LibraryTab::Tool, "/a/capture.ply") == AssetKind::Mesh);
    CHECK(fileKindForTab(LibraryTab::Image, "/a/sketch.PNG") == AssetKind::Image);
    CHECK(fileKindForTab(LibraryTab::Image, "/a/head.obj") == AssetKind::Folder);
    CHECK(findBuiltinAssets("").empty());
    fs::remove_all(root);
}

TEST(quicksave_files) {
    const fs::path dir = fs::path(tempPath("sf_lightbox_quick"));
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string name = quickSaveName(1700000000);
    CHECK(name.rfind("Snapshot ", 0) == 0);
    CHECK(name.size() > 10 && name.substr(name.size() - 7) == ".sfproj");
    CHECK(quickSaveName(1700000000, 2).find(" (2).sfproj") != std::string::npos);

    // Two in the same second get different names.
    const std::string a = nextQuickSavePath(dir.string(), 1700000000);
    touch(fs::path(a));
    const std::string b = nextQuickSavePath(dir.string(), 1700000000);
    CHECK(a != b);
    fs::remove(fs::path(a));

    // Only the newest ten QuickSaves stay; other projects in the folder are never touched.
    const auto now = fs::file_time_type::clock::now();
    std::vector<fs::path> files;
    for (int i = 0; i < 12; ++i) {
        const fs::path p = dir / quickSaveName(1700000000 + i * 60);
        touch(p);
        fs::last_write_time(p, now - std::chrono::minutes(12 - i)); // i = 11 is the newest
        files.push_back(p);
    }
    touch(dir / "my project.sfproj");
    fs::last_write_time(dir / "my project.sfproj", now - std::chrono::hours(5));
    CHECK(pruneQuickSaves(dir.string()) == 2);
    CHECK(!fs::exists(files[0]) && !fs::exists(files[1]));
    for (int i = 2; i < 12; ++i)
        CHECK(fs::exists(files[size_t(i)]));
    CHECK(fs::exists(dir / "my project.sfproj"));
    CHECK(pruneQuickSaves(dir.string()) == 0);
    fs::remove_all(dir);
}

TEST(brush_popup_letter_keys) {
    // The built-in brushes in the pop-up's order, plus a custom one.
    const std::vector<std::string> names = {"Standard", "Clay",  "Smooth",      "Move",        "Inflate",   "Crease",
                                            "Flatten",  "Pinch", "DamStandard", "ClayBuildup", "SnakeHook", "Trim",
                                            "Layer",    "Nudge", "Paint",       "Clay Buildup"};
    auto keyOf = [&](const std::vector<std::pair<char, size_t>>& badges, const std::string& brush) {
        for (const auto& [c, i] : badges)
            if (names[i] == brush) return c;
        return '\0';
    };
    // B, then S: Standard is T, Smooth M, SnakeHook H (ZBrush's B S T, B S M, B S H).
    bool substring = true;
    auto kept = filterByTyped(names, "s", &substring);
    CHECK(!substring);
    CHECK(kept.size() == 3);
    auto badges = letterBadges(names, kept, "s");
    CHECK(keyOf(badges, "Standard") == 't');
    CHECK(keyOf(badges, "Smooth") == 'm');
    CHECK(keyOf(badges, "SnakeHook") == 'h');
    // C: Clay L, Crease R, ClayBuildup B (ZBrush's B C B), and the custom "Clay Buildup" the first free letter (A).
    kept = filterByTyped(names, "c");
    badges = letterBadges(names, kept, "c");
    CHECK(kept.size() == 4);
    CHECK(keyOf(badges, "Clay") == 'l');
    CHECK(keyOf(badges, "Crease") == 'r');
    CHECK(keyOf(badges, "ClayBuildup") == 'b');
    CHECK(keyOf(badges, "Clay Buildup") == 'a');
    // ZBrush's keys for the brushes both apps have: B D S, B M V, B I N, B P I.
    CHECK(keyOf(letterBadges(names, filterByTyped(names, "d"), "d"), "DamStandard") == 's');
    CHECK(keyOf(letterBadges(names, filterByTyped(names, "m"), "m"), "Move") == 'v');
    CHECK(keyOf(letterBadges(names, filterByTyped(names, "i"), "i"), "Inflate") == 'n');
    CHECK(keyOf(letterBadges(names, filterByTyped(names, "p"), "p"), "Pinch") == 'i');
    CHECK(keyOf(letterBadges(names, filterByTyped(names, "s"), "s"), "SnakeHook") == 'h');
    // Every key is unique.
    for (size_t i = 0; i < badges.size(); ++i)
        for (size_t j = i + 1; j < badges.size(); ++j)
            CHECK(badges[i].first != badges[j].first);
    // Nothing starts with "hook": fall back to names containing it, without keys.
    kept = filterByTyped(names, "hook", &substring);
    CHECK(substring);
    CHECK(kept.size() == 1 && names[kept[0]] == "SnakeHook");
    CHECK(letterBadges(names, kept, "hook").empty());
    // Nothing typed: everything, no keys.
    CHECK(filterByTyped(names, "").size() == names.size());
    CHECK(letterBadges(names, filterByTyped(names, ""), "").empty());
    CHECK(typedName("Clay Buildup!") == "claybuildup");
}

// #90: Library > Projects listed each project twice on Windows: the User folder is joined with "/", recent files are
// native "\" paths, and the two were compared as text.
TEST(library_same_path) {
    const bool win = true;
    CHECK(samePath("C:\\Users\\Ann\\Documents\\SculptForge\\Projects\\Head.sfproj",
                   "C:/Users/Ann/Documents/SculptForge/Projects/Head.sfproj", win));
    CHECK(samePath("C:\\Users\\Ann\\Documents\\SculptForge/Projects/head.SFPROJ",
                   "c:/users/ann/documents/sculptforge\\projects\\Head.sfproj", win));
    CHECK(samePath("C:/Users/Ann//Documents/./SculptForge/Projects/../Projects/Head.sfproj",
                   "C:\\Users\\Ann\\Documents\\SculptForge\\Projects\\Head.sfproj", win));
    CHECK(!samePath("C:/Users/Ann/Projects/Head.sfproj", "C:/Users/Ann/Projects/Hand.sfproj", win));
    CHECK(pathKey("C:\\A\\B\\", win) == "c:/a/b");
    // Elsewhere case matters and "\" is a character of the name, but the rest still normalises.
    CHECK(samePath("/home/ann/SculptForge//Projects/./Head.sfproj", "/home/ann/SculptForge/Projects/Head.sfproj", false));
    CHECK(!samePath("/home/ann/Projects/Head.sfproj", "/home/ann/Projects/head.sfproj", false));
    CHECK(samePath("/a/b.sfproj", "/a/b.sfproj"));
}
