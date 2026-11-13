// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The persistent brush library: load order and precedence, bad files, brush
// codes, install / update / rename / delete, read-only studio folders.
#include "TestHarness.h"

#include "core/BrushLibrary.h"
#include "core/Commands.h"
#include "core/FileReplace.h"
#include "core/Project.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

using namespace sf;
namespace fs = std::filesystem;

namespace {

// A fresh user library and two studio folders under the temp directory, used by
// the brush commands for the lifetime of the fixture.
struct LibraryFixture {
    fs::path root, user, studioA, studioB;
    BrushLibraryPaths saved = brushLibraryPaths();

    explicit LibraryFixture(const char* name) {
        root = fs::temp_directory_path() / (std::string("sculptforge_brushlib_") + name);
        fs::remove_all(root);
        user = root / "user";
        studioA = root / "studioA";
        studioB = root / "studioB";
        fs::create_directories(studioA);
        fs::create_directories(studioB);
        brushLibraryPaths() = paths();
    }
    ~LibraryFixture() {
        brushLibraryPaths() = saved;
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    BrushLibraryPaths paths() const { return {user.string(), {studioA.string(), studioB.string()}}; }
};

void writeText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

void writeBrush(const fs::path& path, const std::string& name, float depth) {
    BrushRecipe r;
    r.name = name;
    r.depth = depth;
    std::string err;
    fs::create_directories(path.parent_path());
    CHECK(r.save(path.string(), err));
}

const BrushRecipe* find(const Document& doc, const std::string& name) {
    for (const auto& b : doc.brushes)
        if (b.name == name) return &b;
    return nullptr;
}

int count(const Document& doc, const std::string& name) {
    int n = 0;
    for (const auto& b : doc.brushes) n += b.name == name;
    return n;
}

// Snapshot of every file under a folder (path -> contents), to prove nothing was written.
std::map<std::string, std::string> snapshot(const fs::path& dir) {
    std::map<std::string, std::string> out;
    for (const auto& e : fs::recursive_directory_iterator(dir)) {
        if (!e.is_regular_file()) continue;
        std::ifstream f(e.path(), std::ios::binary);
        out[e.path().string()] = std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }
    return out;
}

} // namespace

TEST(brush_library_precedence_user_over_studio_over_builtin) {
    LibraryFixture lib("precedence");
    writeBrush(lib.studioA / "wrinkle.sfbrush", "Wrinkle", 0.11f);   // overrides the built-in
    writeBrush(lib.studioA / "a.sfbrush", "Studio Only", 0.2f);
    writeBrush(lib.studioB / "a.sfbrush", "Studio Only", 0.9f);      // later folder loses
    writeBrush(lib.studioB / "both.sfbrush", "Shared", 0.3f);
    writeBrush(lib.user / "Shared.sfbrush", "Shared", 0.44f);        // the user's version wins
    writeBrush(lib.user / "Mine.sfbrush", "Mine", -0.5f);

    Document doc;
    const auto messages = loadBrushLibrary(doc, lib.paths());
    CHECK(messages.empty());
    const size_t builtins = builtinBrushRecipes().size();
    CHECK(doc.brushes.size() == builtins + 3);

    const BrushRecipe* wrinkle = find(doc, "Wrinkle");
    CHECK(wrinkle && wrinkle->origin.source == BrushSource::Studio && wrinkle->origin.overrides == BrushSource::Builtin);
    CHECK(wrinkle && std::abs(wrinkle->depth - 0.11f) < 1e-4f);
    const BrushRecipe* studio = find(doc, "Studio Only");
    CHECK(studio && std::abs(studio->depth - 0.2f) < 1e-4f && studio->origin.source == BrushSource::Studio);
    const BrushRecipe* shared = find(doc, "Shared");
    CHECK(shared && shared->origin.source == BrushSource::User && shared->origin.overrides == BrushSource::Studio);
    CHECK(shared && std::abs(shared->depth - 0.44f) < 1e-4f);
    CHECK(count(doc, "Shared") == 1);
    const BrushRecipe* mine = find(doc, "Mine");
    CHECK(mine && mine->origin.source == BrushSource::User && mine->origin.overrides == BrushSource::Session);
    CHECK(mine && fs::path(mine->origin.path).filename() == "Mine.sfbrush");
    const BrushRecipe* palette = find(doc, "Palette Knife");
    CHECK(palette && palette->origin.source == BrushSource::Builtin);

    // The active brush survives a reload by name.
    doc.activeBrush = int(mine - doc.brushes.data());
    loadBrushLibrary(doc, lib.paths());
    CHECK(doc.activeBrush >= 0 && doc.brushes[size_t(doc.activeBrush)].name == "Mine");
}

TEST(brush_library_skips_bad_files) {
    LibraryFixture lib("badfiles");
    writeText(lib.user / "garbage.sfbrush", "this is not json {{{");
    writeText(lib.user / "array.sfbrush", "[1, 2, 3]");
    writeText(lib.user / "other.sfbrush", R"({"format": "sfproj", "name": "Nope"})");
    writeText(lib.user / "empty.sfbrush", "");
    writeText(lib.user / "huge.sfbrush", std::string(kMaxBrushFileSize + 10, ' '));
    writeText(lib.user / "notes.txt", "ignored: not a brush file");
    // Out-of-range values are clamped, unknown keys and wrong types ignored.
    writeText(lib.user / "wild.sfbrush",
              R"({"format": "sfbrush", "name": "Wild", "depth": 99, "spacing": -5, "pattern": "lasers", "jitter": "lots", "extra": 1})");
    writeBrush(lib.user / "dup-a.sfbrush", "Twin", 0.1f);
    writeBrush(lib.user / "dup-b.sfbrush", "Twin", 0.9f);
    fs::create_directories(lib.user / "folder.sfbrush"); // a directory with a brush-like name

    Document doc;
    BrushLibraryPaths paths = lib.paths();
    paths.studioDirs.push_back((lib.root / "missing").string());
    const auto messages = loadBrushLibrary(doc, paths);
    CHECK(messages.size() == 7); // garbage, array, other, empty, huge, the second Twin, the missing folder
    const BrushRecipe* wild = find(doc, "Wild");
    CHECK(wild && wild->depth == 1.0f && wild->spacing == 0.02f && wild->pattern == BrushRecipe::Pattern::None);
    CHECK(count(doc, "Twin") == 1);
    const BrushRecipe* twin = find(doc, "Twin");
    CHECK(twin && std::abs(twin->depth - 0.1f) < 1e-4f); // first file by name wins
    CHECK(!find(doc, "Nope"));
}

TEST(brush_codes_round_trip) {
    BrushRecipe r = builtinBrushRecipes()[2];
    r.name = "Drachenschuppen – ñ"; // UTF-8 survives
    r.jitter = 0.37f;
    r.frontFacesOnly = true;
    const std::string code = encodeBrushCode(r);
    CHECK(code.rfind("sfbrush1:", 0) == 0);
    CHECK(code.find_first_of("+/= \n") == std::string::npos); // base64url, no padding, one line
    BrushRecipe back;
    std::string err;
    CHECK(decodeBrushCode(code, back, err));
    CHECK(back.toJson().dump() == r.toJson().dump());
    CHECK(back.origin.source == BrushSource::Session);

    // Surrounding text, wrapping and padding from chat or email are fine.
    CHECK(parseBrushText("Try my brush: " + code + "\nhave fun", back, err) && back.name == r.name);
    std::string wrapped = code;
    wrapped.insert(20, "\n  ");
    CHECK(decodeBrushCode("  " + wrapped + "==\n", back, err) && back.name == r.name);
    // .sfbrush JSON pastes too.
    CHECK(parseBrushText(r.toJson().dump(true), back, err) && back.jitter == r.jitter);
}

TEST(brush_codes_reject_garbage) {
    BrushRecipe out;
    std::string err;
    CHECK(!decodeBrushCode("", out, err));
    CHECK(!decodeBrushCode("hello world", out, err));
    CHECK(!decodeBrushCode("sfbrush1:", out, err));
    CHECK(!decodeBrushCode("sfbrush1:!!!not*base64", out, err));
    CHECK(!decodeBrushCode("sfbrush1:QQ", out, err)); // decodes to "A": not JSON
    CHECK(!decodeBrushCode("sfbrush1:WzEsMl0", out, err)); // [1,2]: not a brush
    CHECK(!decodeBrushCode("sfbrush2:eyJuYW1lIjoiWCJ9", out, err));
    CHECK(err.find("newer") != std::string::npos);
    // Truncated code: base64 of half a JSON object.
    const std::string code = encodeBrushCode(builtinBrushRecipes()[0]);
    CHECK(!decodeBrushCode(code.substr(0, code.size() / 2), out, err));
    // Oversize: rejected before decoding.
    CHECK(!decodeBrushCode("sfbrush1:" + std::string(kMaxBrushCodeSize, 'A'), out, err));
    CHECK(err.find("too long") != std::string::npos);
    CHECK(!parseBrushText("just some text", out, err));
    CHECK(!parseBrushText("{\"not\": \"a brush\"}", out, err));
    CHECK(!parseBrushText("{" + std::string(kMaxBrushFileSize, ' ') + "}", out, err));
    // Values in a code are clamped like a file.
    const std::string wild = "sfbrush1:eyJuYW1lIjoiWCIsImRlcHRoIjo1MH0"; // {"name":"X","depth":50}
    CHECK(decodeBrushCode(wild, out, err) && out.name == "X" && out.depth == 1.0f);
}

TEST(brush_file_names_are_safe) {
    CHECK(brushFileName("Dragon Scales") == "Dragon Scales.sfbrush");
    CHECK(brushFileName("a/b\\c:d*e?f\"g<h>i|j") == "a_b_c_d_e_f_g_h_i_j.sfbrush");
    CHECK(brushFileName("../../etc/passwd") == "_.._etc_passwd.sfbrush");
    CHECK(brushFileName("...") == "Brush.sfbrush");
    CHECK(brushFileName("con") == "_con.sfbrush");
    CHECK(brushFileName("Trailing. ") == "Trailing.sfbrush");
}

TEST(brush_install_update_and_delete) {
    LibraryFixture lib("install");
    CommandRegistry reg;
    Document doc;
    loadBrushLibrary(doc, lib.paths());
    const size_t builtins = doc.brushes.size();

    BrushRecipe r;
    r.name = "Bark";
    r.depth = 0.25f;
    r.pattern = BrushRecipe::Pattern::Ridges;
    const std::string code = encodeBrushCode(r);
    auto res = reg.execute(doc, "brush_install", {{"code", code}});
    CHECK(res.ok && res.message.find("Installed") != std::string::npos);
    CHECK(doc.brushes.size() == builtins + 1);
    CHECK(doc.activeBrush >= 0 && doc.brushes[size_t(doc.activeBrush)].name == "Bark");
    CHECK(doc.brushes[size_t(doc.activeBrush)].origin.source == BrushSource::User);
    CHECK(fs::exists(lib.user / "Bark.sfbrush"));

    // Re-installing the same name updates the same file.
    r.depth = -0.6f;
    res = reg.execute(doc, "brush_install", {{"json", r.toJson().dump()}});
    CHECK(res.ok && res.message.find("Updated") != std::string::npos);
    CHECK(doc.brushes.size() == builtins + 1);
    CHECK(std::count_if(fs::directory_iterator(lib.user), fs::directory_iterator(), [](const fs::directory_entry& e) { return e.is_regular_file(); }) == 1);
    CHECK(fs::exists(lib.user / "backups" / "Bark.sfbrush")); // (the first version, one generation back)
    Document fresh;
    loadBrushLibrary(fresh, lib.paths());
    CHECK(find(fresh, "Bark") && std::abs(find(fresh, "Bark")->depth + 0.6f) < 1e-4f);

    // From a file, marked as a gallery brush.
    BrushRecipe g;
    g.name = "Gallery Moss";
    std::string err;
    const fs::path download = lib.root / "download.sfbrush";
    CHECK(g.save(download.string(), err));
    CHECK(reg.execute(doc, "brush_install", {{"path", download.string()}, {"source", std::string("gallery")}}).ok);
    loadBrushLibrary(fresh, lib.paths());
    CHECK(find(fresh, "Gallery Moss") && find(fresh, "Gallery Moss")->origin.source == BrushSource::Gallery);

    // Argument checks.
    CHECK(!reg.execute(doc, "brush_install", {}).ok);
    CHECK(!reg.execute(doc, "brush_install", {{"code", code}, {"json", std::string("{}")}}).ok);
    CHECK(!reg.execute(doc, "brush_install", {{"code", std::string("sfbrush1:@@")}}).ok);
    CHECK(!reg.execute(doc, "brush_install", {{"path", (lib.root / "nope.sfbrush").string()}}).ok);

    // brush_code prints a code that installs the same brush.
    res = reg.execute(doc, "brush_code", {{"name", std::string("Bark")}});
    CHECK(res.ok && res.message.rfind("sfbrush1:", 0) == 0);
    BrushRecipe decoded;
    CHECK(decodeBrushCode(res.message, decoded, err) && std::abs(decoded.depth + 0.6f) < 1e-4f);
    CHECK(!reg.execute(doc, "brush_code", {{"name", std::string("Nobody")}}).ok);

    // brush_library lists source and path.
    res = reg.execute(doc, "brush_library", {});
    CHECK(res.ok && res.message.find("Bark [user]") != std::string::npos);
    CHECK(res.message.find("Gallery Moss [gallery]") != std::string::npos);
    CHECK(res.message.find("Wrinkle [built-in]") != std::string::npos);
    CHECK(reg.execute(doc, "brush_list", {}).message.find("Bark [user]") != std::string::npos);

    // Delete: user brushes only, file removed.
    doc.activeBrush = int(find(doc, "Bark") - doc.brushes.data());
    CHECK(reg.execute(doc, "brush_delete", {{"name", std::string("Bark")}}).ok);
    CHECK(!find(doc, "Bark") && !fs::exists(lib.user / "Bark.sfbrush"));
    CHECK(doc.activeBrush == -1);
    CHECK(!reg.execute(doc, "brush_delete", {{"name", std::string("Wrinkle")}}).ok);
    CHECK(find(doc, "Wrinkle"));
}

TEST(brush_library_overrides_and_restores) {
    LibraryFixture lib("override");
    writeBrush(lib.studioA / "carve.sfbrush", "Stone Carve", 0.05f);
    Document doc;
    loadBrushLibrary(doc, lib.paths());
    const size_t n = doc.brushes.size();

    // Refining a studio brush saves the artist's version; the studio file is untouched.
    const auto before = snapshot(lib.studioA);
    const int i = int(find(doc, "Stone Carve") - doc.brushes.data());
    doc.brushes[size_t(i)].depth = -0.9f;
    std::string err;
    CHECK(saveBrushToLibrary(doc, i, err, false, lib.paths()));
    CHECK(doc.brushes[size_t(i)].origin.source == BrushSource::User);
    CHECK(doc.brushes[size_t(i)].origin.overrides == BrushSource::Studio);
    CHECK(fs::path(doc.brushes[size_t(i)].origin.path).parent_path() == lib.user);
    CHECK(snapshot(lib.studioA) == before);
    CHECK(doc.brushes.size() == n);

    // Deleting the user's version brings the studio one back.
    CHECK(deleteBrush(doc, i, err, lib.paths()));
    CHECK(doc.brushes.size() == n);
    CHECK(doc.brushes[size_t(i)].origin.source == BrushSource::Studio);
    CHECK(std::abs(doc.brushes[size_t(i)].depth - 0.05f) < 1e-4f);
    CHECK(snapshot(lib.studioA) == before);

    // Studio brushes can't be deleted.
    CHECK(!deleteBrush(doc, i, err, lib.paths()));
    CHECK(err.find("studio") != std::string::npos);

    // Same for a built-in: edit, save, delete -> the built-in is back.
    const int w = int(find(doc, "Wrinkle") - doc.brushes.data());
    doc.brushes[size_t(w)].pinch = -0.3f;
    CHECK(saveBrushToLibrary(doc, w, err, false, lib.paths()));
    CHECK(doc.brushes[size_t(w)].origin.overrides == BrushSource::Builtin);
    Document reloaded;
    loadBrushLibrary(reloaded, lib.paths());
    CHECK(find(reloaded, "Wrinkle")->origin.source == BrushSource::User);
    CHECK(deleteBrush(doc, w, err, lib.paths()));
    CHECK(doc.brushes[size_t(w)].origin.source == BrushSource::Builtin && doc.brushes[size_t(w)].pinch > 0.5f);
    CHECK(snapshot(lib.studioA) == before);
}

TEST(brush_rename) {
    LibraryFixture lib("rename");
    CommandRegistry reg;
    Document doc;
    loadBrushLibrary(doc, lib.paths());
    const size_t n = doc.brushes.size();
    BrushRecipe r;
    r.name = "Moss";
    std::string err;
    installBrush(doc, r, err, false, lib.paths());
    CHECK(err.empty() && fs::exists(lib.user / "Moss.sfbrush"));

    CHECK(reg.execute(doc, "brush_rename", {{"name", std::string("Moss")}, {"to", std::string("Lichen")}}).ok);
    CHECK(!find(doc, "Moss") && find(doc, "Lichen"));
    CHECK(!fs::exists(lib.user / "Moss.sfbrush") && fs::exists(lib.user / "Lichen.sfbrush"));
    CHECK(doc.brushes[size_t(doc.activeBrush)].name == "Lichen");
    // Clashes and empty names are refused.
    CHECK(!reg.execute(doc, "brush_rename", {{"name", std::string("Lichen")}, {"to", std::string("Wrinkle")}}).ok);
    CHECK(!reg.execute(doc, "brush_rename", {{"name", std::string("Lichen")}, {"to", std::string("   ")}}).ok);

    // Renaming a built-in keeps it and saves a renamed copy.
    CHECK(reg.execute(doc, "brush_rename", {{"name", std::string("Edge Wear")}, {"to", std::string("My Wear")}}).ok);
    CHECK(find(doc, "Edge Wear") && find(doc, "Edge Wear")->origin.source == BrushSource::Builtin);
    CHECK(find(doc, "My Wear") && find(doc, "My Wear")->origin.source == BrushSource::User);
    CHECK(doc.brushes.size() == n + 2);
    CHECK(fs::exists(lib.user / "My Wear.sfbrush"));

    // Renaming a user brush that overrode a built-in brings the built-in back.
    const int w = int(find(doc, "Wrinkle") - doc.brushes.data());
    CHECK(saveBrushToLibrary(doc, w, err, false, lib.paths()));
    CHECK(reg.execute(doc, "brush_rename", {{"name", std::string("Wrinkle")}, {"to", std::string("Deep Wrinkle")}}).ok);
    CHECK(find(doc, "Wrinkle") && find(doc, "Wrinkle")->origin.source == BrushSource::Builtin);
    CHECK(find(doc, "Deep Wrinkle") && find(doc, "Deep Wrinkle")->origin.source == BrushSource::User);
    CHECK(!fs::exists(lib.user / "Wrinkle.sfbrush"));
}

TEST(brush_library_file_name_clash) {
    LibraryFixture lib("clash");
    Document doc;
    loadBrushLibrary(doc, lib.paths());
    std::string err;
    BrushRecipe a, b;
    a.name = "Rock/Moss";
    b.name = "Rock_Moss"; // same safe file name, different brush
    installBrush(doc, a, err, false, lib.paths());
    installBrush(doc, b, err, false, lib.paths());
    CHECK(fs::exists(lib.user / "Rock_Moss.sfbrush") && fs::exists(lib.user / "Rock_Moss 2.sfbrush"));
    Document fresh;
    CHECK(loadBrushLibrary(fresh, lib.paths()).empty());
    CHECK(find(fresh, "Rock/Moss") && find(fresh, "Rock_Moss"));
}

TEST(brush_library_without_user_dir_saves_nothing) {
    LibraryFixture lib("nodir");
    brushLibraryPaths().userDir.clear();
    CommandRegistry reg;
    Document doc;
    BrushRecipe r;
    r.name = "Temp";
    auto res = reg.execute(doc, "brush_install", {{"code", encodeBrushCode(r)}});
    CHECK(res.ok && find(doc, "Temp"));
    CHECK(!fs::exists(lib.user));
}

TEST(brush_library_projects_keep_library_origins) {
    LibraryFixture lib("project");
    Document doc;
    loadBrushLibrary(doc, lib.paths());
    std::string err;
    BrushRecipe mine;
    mine.name = "Mine";
    installBrush(doc, mine, err, false, lib.paths());
    BrushRecipe extra;
    extra.name = "From Project";
    doc.addBrush(extra);
    const std::string path = tempPath("sculptforge_brushlib.sfproj");
    CHECK(saveProject(path, doc, err));

    Document opened;
    loadBrushLibrary(opened, lib.paths());
    CHECK(loadProject(path, opened, err));
    CHECK(find(opened, "Mine") && find(opened, "Mine")->origin.source == BrushSource::User);
    CHECK(find(opened, "Wrinkle")->origin.source == BrushSource::Builtin);
    CHECK(find(opened, "From Project") && find(opened, "From Project")->origin.source == BrushSource::Project);
    fs::remove(path);
}

TEST(brush_search_path_split) {
#if defined(_WIN32)
    const auto parts = splitSearchPath("C:\\a;;  D:\\b ;");
    CHECK(parts.size() == 2 && parts[0] == "C:\\a" && parts[1] == "D:\\b");
#else
    const auto parts = splitSearchPath("/a::  /b c :");
    CHECK(parts.size() == 2 && parts[0] == "/a" && parts[1] == "/b c");
#endif
    CHECK(splitSearchPath("").empty());
}

// A user brush file that stops reading (a cut-off write, a bad sync) is kept as it is, listed, and the brush comes back
// from the last good copy; nothing deletes a user brush on load.
TEST(brush_library_unreadable_file_is_kept_and_restored) {
    LibraryFixture lib("damaged");
    Document doc;
    loadBrushLibrary(doc, lib.paths());
    BrushRecipe r;
    r.name = "MyBrush";
    std::string err;
    installBrush(doc, r, err, false, lib.paths());
    CHECK(err.empty());
    doc.brushes[size_t(doc.activeBrush)].depth = 0.31f;
    CHECK(saveBrushToLibrary(doc, doc.activeBrush, err, false, lib.paths())); // the first version is now in backups/
    const fs::path file = lib.user / "MyBrush.sfbrush";
    CHECK(fs::exists(lib.user / "backups" / "MyBrush.sfbrush"));

    writeText(file, "{\"format\": \"sfbrush\", \"name\": \"hPol"); // cut off
    Document again;
    const auto messages = loadBrushLibrary(again, lib.paths());
    CHECK(find(again, "MyBrush") != nullptr);
    CHECK(!messages.empty());
    CHECK(fs::exists(file)); // not deleted

    // With no backup the file is still kept, and the brush is reported rather than silently gone.
    fs::remove_all(lib.user / "backups");
    Document third;
    const auto lost = loadBrushLibrary(third, lib.paths());
    CHECK(find(third, "MyBrush") == nullptr);
    CHECK(lost.size() == 1 && lost[0].find("Kept") == 0);
    CHECK(fs::exists(file));
}

// A shelf slot's brush that isn't loaded: recoverBrush finds its file, or its last good copy when the file vanished; a brush deleted on purpose stays deleted.
TEST(brush_library_recover_brush) {
    LibraryFixture lib("recover");
    Document doc;
    loadBrushLibrary(doc, lib.paths());
    std::string err, note;
    CHECK(recoverBrush(doc, "MyBrush", note, lib.paths()) < 0);

    BrushRecipe r;
    r.name = "MyBrush";
    r.depth = 0.4f;
    installBrush(doc, r, err, false, lib.paths());
    r.depth = 0.5f;
    installBrush(doc, r, err, false, lib.paths()); // a second save: the first version is in backups/
    // The file vanishes (a sync tool, a clean-up): the brush is still loaded now, and the slot gets it back after a restart.
    Document restarted;
    fs::remove(lib.user / "MyBrush.sfbrush");
    loadBrushLibrary(restarted, lib.paths());
    CHECK(find(restarted, "MyBrush") == nullptr);
    const int back = recoverBrush(restarted, "MyBrush", note, lib.paths());
    CHECK(back >= 0 && restarted.brushes[size_t(back)].name == "MyBrush");
    CHECK(restarted.brushes[size_t(back)].origin.source == BrushSource::User);
    CHECK(fs::exists(lib.user / "MyBrush.sfbrush")); // saved again

    // Deleted on purpose: it stays deleted (a copy is kept in backups/deleted).
    CHECK(deleteBrush(restarted, back, err, lib.paths()));
    CHECK(recoverBrush(restarted, "MyBrush", note, lib.paths()) < 0);
    CHECK(fs::exists(lib.user / "backups" / "deleted" / "MyBrush.sfbrush"));

    // The file appears on disk after the start (copied in): the next look loads it.
    Document other;
    loadBrushLibrary(other, lib.paths());
    writeBrush(lib.user / "Late.sfbrush", "Late", 0.2f);
    const int late = recoverBrush(other, "Late", note, lib.paths());
    CHECK(late >= 0 && other.brushes[size_t(late)].origin.path == (lib.user / "Late.sfbrush").string());
}

// Reloading the library (a pack installed or switched on) keeps the brushes that exist only in this session.
TEST(brush_library_reload_keeps_session_brushes) {
    LibraryFixture lib("session");
    Document doc;
    loadBrushLibrary(doc, lib.paths());
    BrushRecipe r;
    r.name = "Unsaved";
    doc.addBrush(r);
    CHECK(find(doc, "Unsaved")->origin.source == BrushSource::Session);
    loadBrushLibrary(doc, lib.paths());
    CHECK(find(doc, "Unsaved") != nullptr);
}

// A shelf slot keeps pointing at its brush (relinkBrush: the stable id in the .sfbrush first, the name as a hint). The
// bottom shelf stores "brush.recipe.<name>" and the id it learned (UiLayout::brushIds); these are the moves that broke
// it for @rand_gogh's slot 8: the brush's file still on disk, the slot marked missing.
namespace {
int indexOf(const Document& doc, const std::string& name) {
    for (size_t i = 0; i < doc.brushes.size(); ++i)
        if (doc.brushes[i].name == name) return int(i);
    return -1;
}

// A .sfbrush file with the given name and id, as the library writes it.
void writeIdBrush(const fs::path& path, const std::string& name, const std::string& id) {
    BrushRecipe r;
    r.name = name;
    Json j = r.toJson();
    j.set("format", "sfbrush");
    j.set("version", 1);
    j.set("id", id);
    writeText(path, j.dump(true) + "\n");
}

BrushRecipe installed(Document& doc, const LibraryFixture& lib, const std::string& name) {
    BrushRecipe r;
    r.name = name;
    std::string err;
    const int i = installBrush(doc, r, err, false, lib.paths());
    CHECK(err.empty());
    return doc.brushes[size_t(i)];
}
} // namespace

TEST(brush_slot_identity_is_stored_and_survives_restart) {
    LibraryFixture lib("slotid");
    Document doc;
    loadBrushLibrary(doc, lib.paths());
    const BrushRecipe made = installed(doc, lib, "Finish - Classic");
    CHECK(!made.id.empty());
    Document restarted;
    CHECK(loadBrushLibrary(restarted, lib.paths()).empty());
    const BrushRecipe* back = find(restarted, "Finish - Classic");
    CHECK(back && back->id == made.id);
    std::string note;
    const int i = relinkBrush(restarted, "Finish - Classic", made.id, note, lib.paths());
    CHECK(i >= 0 && restarted.brushes[size_t(i)].id == made.id);
    // A brush code (chat, forum) never carries the id: two installs of one code are two brushes.
    std::string err;
    BrushRecipe fromCode;
    CHECK(decodeBrushCode(encodeBrushCode(restarted.brushes[size_t(i)]), fromCode, err));
    CHECK(fromCode.id.empty());
}

TEST(brush_slot_follows_a_rename) {
    LibraryFixture lib("slotrename");
    Document doc;
    loadBrushLibrary(doc, lib.paths());
    const BrushRecipe made = installed(doc, lib, "Old Name");
    std::string err, note;
    const int renamed = renameBrush(doc, indexOf(doc, "Old Name"), "New Name", err, lib.paths());
    CHECK(renamed >= 0 && err.empty());
    CHECK(doc.brushes[size_t(renamed)].id == made.id);
    // The slot still says "Old Name": the id finds the brush, loaded or after a restart.
    CHECK(relinkBrush(doc, "Old Name", made.id, note, lib.paths()) == renamed);
    Document restarted;
    loadBrushLibrary(restarted, lib.paths());
    const int i = relinkBrush(restarted, "Old Name", made.id, note, lib.paths());
    CHECK(i >= 0 && restarted.brushes[size_t(i)].name == "New Name");
    // An older slot with no id has nothing to follow, but is still never dropped: it is reported missing (-1).
    CHECK(relinkBrush(restarted, "Old Name", "", note, lib.paths()) < 0);
}

TEST(brush_slot_follows_a_file_moved_or_renamed_by_hand) {
    LibraryFixture lib("slotmove");
    Document doc;
    loadBrushLibrary(doc, lib.paths());
    const BrushRecipe made = installed(doc, lib, "Mover");
    const fs::path file = lib.user / "Mover.sfbrush";
    std::string note;

    // The file renamed by hand: the brush inside is the same.
    fs::rename(file, lib.user / "mover copy.sfbrush");
    Document a;
    loadBrushLibrary(a, lib.paths());
    CHECK(relinkBrush(a, "Mover", made.id, note, lib.paths()) >= 0);

    // Moved into a folder of the user library (the library sorted into folders): the library doesn't list it, the slot finds it.
    fs::create_directories(lib.user / "Polish");
    fs::rename(lib.user / "mover copy.sfbrush", lib.user / "Polish" / "Mover.sfbrush");
    Document b;
    loadBrushLibrary(b, lib.paths());
    CHECK(find(b, "Mover") == nullptr);
    const int ib = relinkBrush(b, "Mover", made.id, note, lib.paths());
    CHECK(ib >= 0 && b.brushes[size_t(ib)].origin.path == (lib.user / "Polish" / "Mover.sfbrush").string());
    CHECK(b.brushes[size_t(ib)].origin.source == BrushSource::User);
    CHECK(fs::exists(lib.user / "Polish" / "Mover.sfbrush")); // looked at, never moved or changed

    // Moved to a studio folder: loaded from there, the slot finds it by id and name.
    fs::rename(lib.user / "Polish" / "Mover.sfbrush", lib.studioA / "Mover.sfbrush");
    Document c;
    loadBrushLibrary(c, lib.paths());
    const int ic = relinkBrush(c, "Mover", made.id, note, lib.paths());
    CHECK(ic >= 0 && c.brushes[size_t(ic)].origin.source == BrushSource::Studio);
}

TEST(brush_slot_follows_the_internal_name_and_its_case) {
    LibraryFixture lib("slotname");
    Document doc;
    loadBrushLibrary(doc, lib.paths());
    const BrushRecipe made = installed(doc, lib, "Finish - Classic");
    std::string note;
    // The name inside the file changed (edited by hand, a sync tool): same id, the slot follows it.
    writeIdBrush(lib.user / "Finish - Classic.sfbrush", "Finish - Classic v2", made.id);
    Document a;
    loadBrushLibrary(a, lib.paths());
    CHECK(find(a, "Finish - Classic") == nullptr);
    const int ia = relinkBrush(a, "Finish - Classic", made.id, note, lib.paths());
    CHECK(ia >= 0 && a.brushes[size_t(ia)].name == "Finish - Classic v2");

    // Case-only change (Windows and macOS don't tell the files apart), with and without an id on the slot.
    writeIdBrush(lib.user / "Finish - Classic.sfbrush", "finish - classic", made.id);
    for (const std::string& id : {made.id, std::string()}) {
        Document c;
        loadBrushLibrary(c, lib.paths());
        const int ic = relinkBrush(c, "Finish - Classic", id, note, lib.paths());
        CHECK(ic >= 0 && c.brushes[size_t(ic)].name == "finish - classic");
    }
    // Same, with the file in a subfolder and not loaded.
    fs::create_directories(lib.user / "sub");
    fs::rename(lib.user / "Finish - Classic.sfbrush", lib.user / "sub" / "x.sfbrush");
    Document d;
    loadBrushLibrary(d, lib.paths());
    CHECK(relinkBrush(d, "Finish - Classic", "", note, lib.paths()) >= 0);
}

TEST(brush_slot_of_a_deleted_brush_stays_missing) {
    LibraryFixture lib("slotdeleted");
    Document doc;
    loadBrushLibrary(doc, lib.paths());
    const BrushRecipe made = installed(doc, lib, "Gone");
    std::string err, note;
    CHECK(deleteBrush(doc, indexOf(doc, "Gone"), err, lib.paths()));
    CHECK(relinkBrush(doc, "Gone", made.id, note, lib.paths()) < 0); // missing: the slot is kept, the UI says so
    CHECK(relinkBrush(doc, "Gone", "", note, lib.paths()) < 0);
    CHECK(fs::exists(lib.user / "backups" / "deleted" / "Gone.sfbrush"));
    // Another brush with a different id and name never answers for it.
    installed(doc, lib, "Other");
    CHECK(relinkBrush(doc, "Gone", made.id, note, lib.paths()) < 0);
}

TEST(brush_files_get_their_own_identity) {
    LibraryFixture lib("slotcopy");
    // A file from an older version (no id) gets one on load, written back, and it reads back as the same brush.
    writeBrush(lib.user / "Old.sfbrush", "Old", 0.3f);
    Document a;
    CHECK(loadBrushLibrary(a, lib.paths()).empty());
    const BrushRecipe* old = find(a, "Old");
    CHECK(old && !old->id.empty());
    BrushRecipe disk;
    std::string err;
    CHECK(readBrushFile((lib.user / "Old.sfbrush").string(), disk, err) && disk.id == old->id && disk.depth == 0.3f);
    // The same file copied (a duplicate in the folder): the copy gets an identity of its own.
    fs::copy_file(lib.user / "Old.sfbrush", lib.user / "Old (copy).sfbrush");
    writeIdBrush(lib.user / "Old (copy).sfbrush", "Old (copy)", disk.id);
    Document b;
    loadBrushLibrary(b, lib.paths());
    const BrushRecipe* one = find(b, "Old");
    const BrushRecipe* two = find(b, "Old (copy)");
    CHECK(one && two && one->id != two->id && !one->id.empty() && !two->id.empty());
    CHECK(one->id == disk.id || two->id == disk.id); // one keeps the original identity
}

// The id is bookkeeping: a brush with an id and the same brush without one are the same recipe (the JSON the sculpting
// engine, projects and brush codes read), and the library's id writes never change a brush's parameters.
TEST(brush_identity_invariant_recipe_is_unchanged) {
    for (const BrushRecipe& builtin : builtinBrushRecipes()) {
        BrushRecipe with = builtin;
        with.id = "0123456789abcdef";
        CHECK(with.toJson().dump() == builtin.toJson().dump());
        CHECK(encodeBrushCode(with) == encodeBrushCode(builtin));
    }
    LibraryFixture lib("idinvariant");
    BrushRecipe r = builtinBrushRecipes()[2];
    r.name = "Keeps Its Look";
    r.jitter = 0.37f;
    r.frontFacesOnly = true;
    r.alpha = "";
    writeText(lib.user / "Keeps Its Look.sfbrush", r.toJson().dump(true)); // an older file: no id
    Document doc;
    CHECK(loadBrushLibrary(doc, lib.paths()).empty()); // gives it an id, writes it back
    const BrushRecipe* got = find(doc, "Keeps Its Look");
    CHECK(got && !got->id.empty());
    BrushRecipe disk;
    std::string err;
    CHECK(readBrushFile((lib.user / "Keeps Its Look.sfbrush").string(), disk, err));
    CHECK(disk.toJson().dump() == got->toJson().dump());
    CHECK(got->toJson().dump() == BrushRecipe::fromJson(r.toJson()).toJson().dump());
}

// Every save writes next to the target and replaces it (core/FileReplace.h). On Windows a read-only target, or one something
// else holds for a moment, made the rename fail with "Access is denied" and the save was lost (a failed write must either
// land or tell the user which file).
TEST(file_replace_lands_or_names_the_file) {
    const fs::path dir = fs::temp_directory_path() / "sculptforge_filereplace";
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::string err;
    // A new target, and an existing one: the temp file is gone, the target holds the new bytes.
    writeText(dir / "a.tmp", "new");
    CHECK(replaceFile(dir / "a.tmp", dir / "a.json", err) && err.empty());
    writeText(dir / "a.tmp", "newer");
    CHECK(replaceFile(dir / "a.tmp", dir / "a.json", err));
    std::ifstream in(dir / "a.json");
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(text == "newer" && !fs::exists(dir / "a.tmp"));
    in.close();
    // A read-only target (the flag a copied or synced file can carry): replaced all the same.
    fs::permissions(dir / "a.json", fs::perms::owner_read, fs::perm_options::replace);
    writeText(dir / "a.tmp", "after read-only");
    CHECK(replaceFile(dir / "a.tmp", dir / "a.json", err));
    std::ifstream in2(dir / "a.json");
    CHECK(std::string((std::istreambuf_iterator<char>(in2)), std::istreambuf_iterator<char>()) == "after read-only");
    in2.close();
    // The target held open by a reader that allows other readers and writers but not delete (what std::ifstream does on
    // Windows: the cause of "Access is denied" at start-up when project.json was still open while it was saved).
    {
        writeText(dir / "held.json", "old");
        std::ifstream holder(dir / "held.json", std::ios::binary);
        CHECK(holder.is_open());
        writeText(dir / "held.tmp", "after open");
        CHECK(replaceFile(dir / "held.tmp", dir / "held.json", err));
        holder.close();
        std::ifstream check(dir / "held.json");
        CHECK(std::string((std::istreambuf_iterator<char>(check)), std::istreambuf_iterator<char>()) == "after open");
    }
#ifdef _WIN32
    { // ... and by a second handle without FILE_SHARE_DELETE (a scanner, a sync client), opened the Win32 way.
        writeText(dir / "held2.json", "old");
        HANDLE h = CreateFileW((dir / "held2.json").c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        CHECK(h != INVALID_HANDLE_VALUE);
        writeText(dir / "held2.tmp", "after handle");
        CHECK(replaceFile(dir / "held2.tmp", dir / "held2.json", err));
        CloseHandle(h);
        std::ifstream check(dir / "held2.json");
        CHECK(std::string((std::istreambuf_iterator<char>(check)), std::istreambuf_iterator<char>()) == "after handle");
    }
#endif
    // A target that can't be replaced (a folder is in the way): false, the error names the file, the temp file is gone.
    fs::create_directories(dir / "blocked" / "inner");
    writeText(dir / "b.tmp", "x");
    CHECK(!replaceFile(dir / "b.tmp", dir / "blocked", err));
    CHECK(err.find("blocked") != std::string::npos && !fs::exists(dir / "b.tmp"));
    // The brush library goes through it: a read-only brush file still takes its edit.
    LibraryFixture lib("replacero");
    Document doc;
    loadBrushLibrary(doc, lib.paths());
    BrushRecipe r;
    r.name = "Locked";
    std::string e2;
    const int i = installBrush(doc, r, e2, false, lib.paths());
    CHECK(e2.empty());
    fs::permissions(lib.user / "Locked.sfbrush", fs::perms::owner_read, fs::perm_options::replace);
    doc.brushes[size_t(i)].depth = 0.31f;
    CHECK(saveBrushToLibrary(doc, i, e2, false, lib.paths()) && e2.empty());
    BrushRecipe back;
    CHECK(readBrushFile((lib.user / "Locked.sfbrush").string(), back, e2) && std::abs(back.depth - 0.31f) < 1e-4f);
    fs::permissions(dir / "a.json", fs::perms::owner_all, fs::perm_options::replace);
    fs::remove_all(dir);
}
