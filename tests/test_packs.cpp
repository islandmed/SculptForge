// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Packs (.sfpack) and macros (.sfmacro): the strict zip reader and pack validator against hostile archives
// (traversal, symbolic links, zip bombs, bad JSON, wrong file types, duplicates, encryption), install / enable /
// disable / remove, the brush library tagging pack brushes, and macros: validation, path confinement, record and
// play back, one undo step.
#include "TestHarness.h"

#include "core/BrushLibrary.h"
#include "core/BrushRecipe.h"
#include "core/Commands.h"
#include "core/Image.h"
#include "core/Macro.h"
#include "core/Pack.h"
#include "core/Primitives.h"
#include "core/RadialMenu.h"
#include "core/Vdm.h"
#include "io/Zip.h"

#include <cstring>
#include <filesystem>
#include <fstream>

using namespace sf;
namespace fs = std::filesystem;

namespace {

// --- A zip writer that can write what a hostile tool would ---

struct RawEntry {
    std::string name;
    std::vector<uint8_t> data;   // unpacked (for the CRC and, when stored, the bytes)
    std::vector<uint8_t> packed; // method 8: the deflate stream
    uint16_t method = 0;
    uint16_t madeBy = 20;
    uint16_t flags = 0;
    uint32_t external = 0;
    int64_t declaredSize = -1; // -1: data.size()
};

void put16(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(uint8_t(v));
    b.push_back(uint8_t(v >> 8));
}
void put32(std::vector<uint8_t>& b, uint32_t v) {
    put16(b, v & 0xFFFF);
    put16(b, v >> 16);
}

std::vector<uint8_t> buildZip(const std::vector<RawEntry>& entries) {
    std::vector<uint8_t> out, central;
    for (const RawEntry& e : entries) {
        const uint32_t offset = uint32_t(out.size());
        const std::vector<uint8_t>& body = e.method == 8 ? e.packed : e.data;
        const uint32_t usize = e.declaredSize >= 0 ? uint32_t(e.declaredSize) : uint32_t(e.data.size());
        const uint32_t crc = crc32(e.data.data(), e.data.size());
        put32(out, 0x04034B50);
        put16(out, 20);
        put16(out, e.flags);
        put16(out, e.method);
        put16(out, 0);
        put16(out, 0x21);
        put32(out, crc);
        put32(out, uint32_t(body.size()));
        put32(out, usize);
        put16(out, uint32_t(e.name.size()));
        put16(out, 0);
        out.insert(out.end(), e.name.begin(), e.name.end());
        out.insert(out.end(), body.begin(), body.end());
        put32(central, 0x02014B50);
        put16(central, e.madeBy);
        put16(central, 20);
        put16(central, e.flags);
        put16(central, e.method);
        put16(central, 0);
        put16(central, 0x21);
        put32(central, crc);
        put32(central, uint32_t(body.size()));
        put32(central, usize);
        put16(central, uint32_t(e.name.size()));
        put16(central, 0);
        put16(central, 0);
        put16(central, 0);
        put16(central, 0);
        put32(central, e.external);
        put32(central, offset);
        central.insert(central.end(), e.name.begin(), e.name.end());
    }
    const uint32_t cdOffset = uint32_t(out.size());
    out.insert(out.end(), central.begin(), central.end());
    put32(out, 0x06054B50);
    put16(out, 0);
    put16(out, 0);
    put16(out, uint32_t(entries.size()));
    put16(out, uint32_t(entries.size()));
    put32(out, uint32_t(central.size()));
    put32(out, cdOffset);
    put16(out, 0);
    return out;
}

// Deflate with fixed Huffman codes: one literal zero, then RUNS copies of 258 zeros (a zip bomb's shape).
std::vector<uint8_t> deflateZeros(int runs) {
    std::vector<uint8_t> out;
    uint32_t acc = 0;
    int bits = 0;
    auto put = [&](uint32_t v, int n) { // LSB first
        for (int i = 0; i < n; ++i) {
            acc |= ((v >> i) & 1u) << bits;
            if (++bits == 8) out.push_back(uint8_t(acc)), acc = 0, bits = 0;
        }
    };
    auto huff = [&](uint32_t code, int n) { // Huffman codes go MSB first
        for (int i = n - 1; i >= 0; --i) put((code >> i) & 1u, 1);
    };
    put(1, 1);           // BFINAL
    put(1, 2);           // fixed Huffman
    huff(0x30, 8);       // literal 0
    for (int i = 0; i < runs; ++i) {
        huff(0xC5, 8);   // length 258 (symbol 285)
        huff(0, 5);      // distance 1
    }
    huff(0, 7);          // end of block
    if (bits) out.push_back(uint8_t(acc));
    return out;
}

// Deflate's stored block: the same bytes, through the inflate path.
std::vector<uint8_t> deflateStored(const std::vector<uint8_t>& d) {
    std::vector<uint8_t> out = {1};
    put16(out, uint32_t(d.size()));
    put16(out, uint32_t(~d.size() & 0xFFFF));
    out.insert(out.end(), d.begin(), d.end());
    return out;
}

std::vector<uint8_t> bytes(const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); }

std::string brushJson(const std::string& name) {
    BrushRecipe r;
    r.name = name;
    r.depth = 0.4f;
    return r.toJson().dump(true);
}

std::vector<uint8_t> pngBytes(int side) {
    Image img;
    img.width = img.height = side;
    img.rgba.assign(size_t(side) * side * 4, 200);
    std::vector<uint8_t> png;
    img.encodePng(png);
    return png;
}

std::string manifest(const std::string& contents, const std::string& extra = "") {
    return "{\"format\":\"sculptforge-pack\",\"formatVersion\":1,\"id\":\"test-pack\",\"name\":\"Test Pack\","
           "\"version\":\"1.0\",\"author\":\"Someone\",\"license\":\"CC0-1.0\"" + extra + ",\"contents\":[" + contents + "]}";
}

// A good pack's entries: a brush, an alpha and a macro.
std::vector<RawEntry> goodEntries() {
    std::vector<RawEntry> e;
    e.push_back({"pack.json", bytes(manifest("{\"type\":\"brush\",\"path\":\"brushes/Pack Clay.sfbrush\"},"
                                             "{\"type\":\"alpha\",\"path\":\"alphas/Dots.png\"},"
                                             "{\"type\":\"macro\",\"path\":\"macros/Prep.sfmacro\"}")), {}});
    e.push_back({"LICENSE.txt", bytes("CC0 1.0 Universal\n"), {}});
    e.push_back({"brushes/Pack Clay.sfbrush", bytes(brushJson("Pack Clay")), {}});
    e.push_back({"alphas/Dots.png", pngBytes(16), {}});
    e.push_back({"macros/Prep.sfmacro", bytes("# name: Prep\nmask_clear\nmask_all\n"), {}});
    return e;
}

bool review(const std::vector<RawEntry>& entries, std::string& error, PackReview* out = nullptr,
            const PackLimits& limits = {}) {
    std::vector<PackFile> files;
    if (!readPackArchiveBytes(buildZip(entries), files, error, limits)) return false;
    PackReview r;
    if (!reviewPackFiles(std::move(files), r, error)) return false;
    if (out) *out = std::move(r);
    return true;
}

bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

struct TempRoot {
    fs::path dir;
    explicit TempRoot(const char* name) : dir(fs::temp_directory_path() / name) {
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir);
        setPacksRoot((dir / "packs").string());
        setUserMacroDir((dir / "macros").string());
    }
    ~TempRoot() {
        setPacksRoot("");
        setUserMacroDir("");
        setPacksSafeMode(false);
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

} // namespace

TEST(pack_good_archive_reviews) {
    TempRoot root("sf_pack_good");
    std::string err;
    PackReview r;
    CHECK(review(goodEntries(), err, &r));
    CHECK(err.empty());
    CHECK(r.manifest.id == "test-pack");
    CHECK(r.counts[size_t(PackItemType::Brush)] == 1);
    CHECK(r.counts[size_t(PackItemType::Alpha)] == 1);
    CHECK(r.counts[size_t(PackItemType::Macro)] == 1);
    CHECK(r.licenseText == "CC0 1.0 Universal\n");
    CHECK(packCountsText(r.counts) == "1 brush, 1 alpha, 1 macro");
    CHECK(r.installedVersion.empty());
}

TEST(pack_deflated_entries_read) {
    TempRoot root("sf_pack_deflate");
    auto e = goodEntries();
    e[2].method = 8;
    e[2].packed = deflateStored(e[2].data);
    std::string err;
    CHECK(review(e, err));
    // Flipped byte: the checksum catches it.
    e[2].packed.back() ^= 1;
    CHECK(!review(e, err));
    CHECK(contains(err, "checksum"));
}

TEST(pack_rejects_traversal_and_odd_names) {
    TempRoot root("sf_pack_names");
    const char* bad[] = {"../evil.sfbrush",       "brushes/../../evil.sfbrush", "/etc/passwd",        "C:/evil.sfbrush",
                         "brushes\\..\\evil.sfbrush", "~/evil.sfbrush",          "brushes/./x.sfbrush", ".hidden",
                         "brushes/CON.sfbrush",   "brushes/a|b.sfbrush",       "brushes/x.sfbrush ", "a/b/c/d.sfbrush",
                         "brushes//x.sfbrush"};
    for (const char* name : bad) {
        auto e = goodEntries();
        e.push_back({name, bytes(brushJson("X")), {}});
        std::string err;
        CHECK(!review(e, err));
        CHECK(!err.empty());
    }
    std::string err;
    CHECK(!packEntryNameOk("../x", err));
    CHECK(contains(err, ".."));
    CHECK(!packEntryNameOk("/abs", err));
    CHECK(contains(err, "absolute"));
    CHECK(!packEntryNameOk("D:x", err));
    CHECK(contains(err, "drive"));
    CHECK(!packEntryNameOk(std::string("bad\x01name"), err));
    CHECK(contains(err, "control"));
    CHECK(packEntryNameOk("brushes/Dragon Scales (v2).sfbrush", err));
    // A NUL inside a name (some tools stop reading there).
    auto e = goodEntries();
    e.push_back({std::string("brushes/a.sfbrush\0.png", 23), bytes(brushJson("X")), {}});
    CHECK(!review(e, err));
    CHECK(contains(err, "NUL"));
}

TEST(pack_rejects_symlinks_and_devices) {
    TempRoot root("sf_pack_links");
    auto e = goodEntries();
    e[2].madeBy = 3 << 8;              // Unix
    e[2].external = 0120777u << 16;    // symbolic link
    e[2].data = bytes("/etc/passwd");
    std::string err;
    CHECK(!review(e, err));
    CHECK(contains(err, "symbolic link"));
    e = goodEntries();
    e[2].madeBy = 3 << 8;
    e[2].external = 0020644u << 16; // character device
    CHECK(!review(e, err));
    CHECK(contains(err, "special file"));
    e = goodEntries();
    e[2].madeBy = 3 << 8;
    e[2].external = 0100644u << 16; // a plain file is fine
    CHECK(review(e, err));
}

TEST(pack_rejects_zip_bombs) {
    TempRoot root("sf_pack_bomb");
    // Honest sizes, huge ratio: refused before anything is unpacked.
    auto e = goodEntries();
    RawEntry bomb{"alphas/Bomb.png", {}, deflateZeros(40000), 8};
    bomb.data.assign(1 + 258 * 40000, 0);
    e.push_back(bomb);
    std::string err;
    CHECK(!review(e, err));
    CHECK(contains(err, "zip bomb"));
    // Lying sizes: the stream inflates to more than it says; the bounded buffer stops it.
    e = goodEntries();
    RawEntry liar{"alphas/Liar.png", std::vector<uint8_t>(1000, 0), deflateZeros(40000), 8};
    e.push_back(liar);
    CHECK(!review(e, err));
    CHECK(contains(err, "stated size"));
    // Too big unpacked, too many files, too big a file.
    PackLimits small;
    small.totalBytes = 300;
    CHECK(!review(goodEntries(), err, nullptr, small));
    CHECK(contains(err, "too big unpacked"));
    small = {};
    small.entries = 3;
    CHECK(!review(goodEntries(), err, nullptr, small));
    CHECK(contains(err, "too many files"));
    small = {};
    small.fileBytes = 100;
    CHECK(!review(goodEntries(), err, nullptr, small));
    CHECK(contains(err, "too big"));
}

TEST(pack_rejects_overlapping_entries) {
    TempRoot root("sf_pack_overlap");
    // Two directory records pointing at the same local data (the "better zip bomb" trick).
    std::vector<uint8_t> z = buildZip(goodEntries());
    // Patch the second central record's local offset to the first's (0).
    size_t cd = 0;
    for (size_t i = z.size() - 22; i > 0; --i)
        if (z[i] == 0x50 && z[i + 1] == 0x4B && z[i + 2] == 0x05 && z[i + 3] == 0x06) {
            cd = size_t(z[i + 16] | z[i + 17] << 8 | z[i + 18] << 16 | uint32_t(z[i + 19]) << 24);
            break;
        }
    const size_t first = cd;
    const size_t second = first + 46 + size_t(z[first + 28] | z[first + 29] << 8);
    for (int k = 0; k < 4; ++k) z[second + 42 + size_t(k)] = 0;
    std::vector<PackFile> files;
    std::string err;
    CHECK(!readPackArchiveBytes(z, files, err));
    CHECK(!err.empty());
}

TEST(pack_rejects_bad_manifests) {
    TempRoot root("sf_pack_json");
    auto withManifest = [](const std::string& m) {
        auto e = goodEntries();
        e[0].data = bytes(m);
        return e;
    };
    std::string err;
    CHECK(!review(withManifest("{"), err));
    CHECK(contains(err, "not valid JSON"));
    CHECK(!review(withManifest("[1,2]"), err));
    CHECK(contains(err, "object"));
    CHECK(!review(withManifest(manifest("", ",\"id\":\"other\"")), err));
    CHECK(contains(err, "Duplicate key"));
    CHECK(!review(withManifest(manifest("", ",\"run\":\"install.sh\"")), err));
    CHECK(contains(err, "unknown key"));
    CHECK(!review(withManifest(manifest("{\"type\":\"script\",\"path\":\"brushes/x.sfbrush\"}")), err));
    CHECK(contains(err, "unknown type"));
    std::string m = manifest("");
    m.replace(m.find("\"formatVersion\":1"), 17, "\"formatVersion\":2");
    CHECK(!review(withManifest(m), err));
    CHECK(contains(err, "formatVersion"));
    m = manifest("");
    m.replace(m.find("test-pack"), 9, "../evil");
    CHECK(!review(withManifest(m), err));
    CHECK(contains(err, "\"id\""));
    CHECK(!review(withManifest(manifest("", ",\"minAppVersion\":\"99.0\"")), err));
    CHECK(contains(err, "needs SculptForge 99.0"));
    CHECK(!review(withManifest(manifest("", ",\"version\":2")), err)); // also a duplicate
    CHECK(!review(withManifest(manifest("", ",\"description\":\"a\\u0007b\"")), err));
    CHECK(contains(err, "control characters"));
    // No manifest at all.
    auto e = goodEntries();
    e.erase(e.begin());
    CHECK(!review(e, err));
    CHECK(contains(err, "no pack.json"));
    // The channel is display only, but must be well formed.
    PackReview r;
    CHECK(review(withManifest(manifest("{\"type\":\"brush\",\"path\":\"brushes/Pack Clay.sfbrush\"},"
                                       "{\"type\":\"alpha\",\"path\":\"alphas/Dots.png\"},"
                                       "{\"type\":\"macro\",\"path\":\"macros/Prep.sfmacro\"}",
                                       ",\"channel\":\"insider\"")),
                 err, &r));
    CHECK(r.manifest.channel == "insider");
    CHECK(!review(withManifest(manifest("", ",\"channel\":\"Insider Only!\"")), err));
}

TEST(pack_rejects_wrong_file_types) {
    TempRoot root("sf_pack_types");
    std::string err;
    auto e = goodEntries();
    e.push_back({"install.sh", bytes("#!/bin/sh\nrm -rf ~\n"), {}});
    CHECK(!review(e, err));
    CHECK(contains(err, "not a file a pack may hold"));
    e = goodEntries();
    e.push_back({"brushes/plugin.so", bytes("\x7f" "ELF"), {}});
    CHECK(!review(e, err));
    CHECK(contains(err, "not listed"));
    // Listed under a wrong extension.
    e = goodEntries();
    e[0].data = bytes(manifest("{\"type\":\"brush\",\"path\":\"brushes/x.py\"}"));
    CHECK(!review(e, err));
    CHECK(contains(err, "not a file type"));
    // In the wrong folder.
    e = goodEntries();
    e[0].data = bytes(manifest("{\"type\":\"brush\",\"path\":\"alphas/x.sfbrush\"}"));
    CHECK(!review(e, err));
    CHECK(contains(err, "directly in brushes/"));
    // A "PNG" that is a script.
    e = goodEntries();
    e[3].data = bytes("#!/bin/sh\necho hi\n");
    CHECK(!review(e, err));
    CHECK(contains(err, "Dots.png"));
    // A JPEG behind a .png name is refused too (the bytes must match the name).
    // A brush that isn't one.
    e = goodEntries();
    e[2].data = bytes("{\"format\":\"something else\"}");
    CHECK(!review(e, err));
    CHECK(contains(err, "not a valid brush"));
    // A brush with a duplicated key.
    e = goodEntries();
    e[2].data = bytes("{\"name\":\"a\",\"name\":\"b\"}");
    CHECK(!review(e, err));
    // Listed but missing, and in the pack twice (case-insensitively).
    e = goodEntries();
    e.erase(e.begin() + 3);
    CHECK(!review(e, err));
    CHECK(contains(err, "doesn't hold it"));
    e = goodEntries();
    e.push_back({"BRUSHES/pack clay.sfbrush", bytes(brushJson("Y")), {}});
    CHECK(!review(e, err));
    CHECK(contains(err, "twice"));
    // Encrypted.
    e = goodEntries();
    e[2].flags = 1;
    CHECK(!review(e, err));
    CHECK(contains(err, "encrypted"));
    // A macro that reaches the network, or a file outside the project.
    e = goodEntries();
    e[4].data = bytes("brush_gallery_install name=x\n");
    CHECK(!review(e, err));
    CHECK(contains(err, "not allowed in a macro"));
    e = goodEntries();
    e[4].data = bytes("export_glb path=/home/someone/.bashrc\n");
    CHECK(!review(e, err));
    CHECK(contains(err, "not inside the project folder"));
    // Junk after the archive, and not a zip at all.
    std::vector<uint8_t> z = buildZip(goodEntries());
    z.push_back(0);
    std::vector<PackFile> files;
    CHECK(!readPackArchiveBytes(z, files, err));
    CHECK(!readPackArchiveBytes(bytes("hello"), files, err));
    CHECK(contains(err, "not a zip"));
}

TEST(pack_install_list_toggle_remove) {
    TempRoot root("sf_pack_install");
    PackReview r;
    std::string err;
    CHECK(review(goodEntries(), err, &r));
    CHECK(installPack(r, err));
    auto packs = listInstalledPacks();
    CHECK(packs.size() == 1);
    CHECK(packs.size() == 1 && packs[0].enabled && packs[0].manifest.name == "Test Pack");
    CHECK(fs::is_regular_file(root.dir / "packs" / "test-pack" / "brushes" / "Pack Clay.sfbrush"));

    // Its brush joins the library tagged with the pack; a pack brush named like a built-in gets the pack's name.
    BrushLibraryPaths paths;
    applyPacksToBrushLibrary(paths);
    CHECK(paths.packs.size() == 1);
    Document doc;
    loadBrushLibrary(doc, paths);
    int found = -1;
    for (size_t i = 0; i < doc.brushes.size(); ++i)
        if (doc.brushes[i].name == "Pack Clay") found = int(i);
    CHECK(found >= 0);
    CHECK(found >= 0 && doc.brushes[size_t(found)].origin.source == BrushSource::Pack);
    CHECK(found >= 0 && doc.brushes[size_t(found)].origin.pack == "Test Pack");
    CHECK(enabledPackItems(PackItemType::Alpha).size() == 1);
    CHECK(enabledPackItems(PackItemType::Alpha)[0].label() == "Dots (Test Pack)");

    // Reviewing it again names the installed version.
    PackReview again;
    CHECK(review(goodEntries(), err, &again));
    CHECK(again.installedVersion == "1.0");

    // Off: its items leave the lists; on again: back.
    CHECK(setPackEnabled("test-pack", false, err));
    CHECK(!listInstalledPacks()[0].enabled);
    CHECK(enabledPackItems(PackItemType::Alpha).empty());
    applyPacksToBrushLibrary(paths);
    CHECK(paths.packs.empty());
    CHECK(setPackEnabled("test-pack", true, err));
    CHECK(enabledPackItems(PackItemType::Alpha).size() == 1);
    // Safe mode: no pack at all, without changing what is enabled.
    setPacksSafeMode(true);
    CHECK(enabledPackItems(PackItemType::Alpha).empty());
    applyPacksToBrushLibrary(paths);
    CHECK(paths.packs.empty());
    setPacksSafeMode(false);

    // An update replaces the folder; a removed pack leaves nothing behind.
    CHECK(installPack(again, err));
    CHECK(listInstalledPacks().size() == 1);
    CHECK(!removePack("../packs", err));
    CHECK(removePack("test-pack", err));
    CHECK(listInstalledPacks().empty());
    CHECK(!fs::exists(root.dir / "packs" / "test-pack"));
    CHECK(!removePack("test-pack", err));
}

TEST(pack_never_replaces_builtin_brushes) {
    TempRoot root("sf_pack_builtin");
    const std::string builtin = builtinBrushRecipes().front().name;
    auto e = goodEntries();
    e[2].data = bytes(brushJson(builtin));
    PackReview r;
    std::string err;
    CHECK(review(e, err, &r));
    CHECK(installPack(r, err));
    BrushLibraryPaths paths;
    applyPacksToBrushLibrary(paths);
    Document doc;
    loadBrushLibrary(doc, paths);
    int builtinAt = -1, packAt = -1;
    for (size_t i = 0; i < doc.brushes.size(); ++i) {
        if (doc.brushes[i].name == builtin) builtinAt = int(i);
        if (doc.brushes[i].name == builtin + " (Test Pack)") packAt = int(i);
    }
    CHECK(builtinAt >= 0 && doc.brushes[size_t(builtinAt)].origin.source == BrushSource::Builtin);
    CHECK(packAt >= 0);
}

TEST(pack_write_round_trip) {
    TempRoot root("sf_pack_write");
    const fs::path src = root.dir / "src";
    fs::create_directories(src);
    {
        std::ofstream(src / "Ridge.sfbrush") << brushJson("Ridge");
        const auto png = pngBytes(8);
        std::ofstream(src / "Grain.png", std::ios::binary).write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
        std::ofstream(src / "Prep.sfmacro") << "# name: Prep\nmask_all\nexport_glb path=out/prep.glb\n";
    }
    KeymapFile k;
    k.name = "Mine";
    k.shortcuts = {{"brush.clay", "Shift+C"}};
    const std::string keymap = keymapFileText(k);
    RadialMenu menu;
    menu.name = "Tools";
    menu.slots = {{"brush.clay", "", ""}};
    const std::string pie = radialMenuFileText(menu);
    std::vector<PackSource> items = {
        {PackItemType::Brush, (src / "Ridge.sfbrush").string(), "", {}},
        {PackItemType::Alpha, (src / "Grain.png").string(), "", {}},
        {PackItemType::Macro, (src / "Prep.sfmacro").string(), "", {}},
        {PackItemType::Keymap, "", "Mine.json", bytes(keymap)},
        {PackItemType::PieMenu, "", "Tools.json", bytes(pie)},
    };
    PackManifest m;
    m.id = "my-pack";
    m.name = "My Pack";
    m.version = "1.2";
    m.author = "Me";
    m.license = "CC-BY-4.0";
    m.channel = "insider";
    std::string err;
    const std::string out = (root.dir / "My Pack.sfpack").string();
    CHECK(writePack(out, m, items, "Attribution 4.0\n", "", err));
    CHECK(err.empty());
    PackReview r;
    CHECK(reviewPack(out, r, err));
    CHECK(r.manifest.channel == "insider");
    CHECK(r.manifest.minAppVersion == appVersion());
    CHECK(r.counts[size_t(PackItemType::Keymap)] == 1 && r.counts[size_t(PackItemType::PieMenu)] == 1);
    CHECK(r.licenseText == "Attribution 4.0\n");
    KeymapFile back;
    CHECK(parseKeymapFile(keymap, back, err) && back.shortcuts.size() == 1 && back.shortcuts[0].second == "Shift+C");
    // The same item twice is refused before anything is written.
    items.push_back(items[0]);
    CHECK(!writePack((root.dir / "twice.sfpack").string(), m, items, "", "", err));
    CHECK(contains(err, "Two items"));
    // A bad keymap.
    CHECK(!parseKeymapFile("{\"format\":\"sculptforge-keymap\",\"version\":1,\"shortcuts\":{\"a b\":\"C\"}}", back, err));
    CHECK(!parseKeymapFile("{\"format\":\"sculptforge-keymap\",\"version\":1,\"shortcuts\":{\"a\":3}}", back, err));
}

TEST(pack_commands) {
    TempRoot root("sf_pack_cmds");
    const fs::path src = root.dir / "layout";
    fs::create_directories(src / "brushes");
    std::ofstream(src / "brushes" / "Cmd Brush.sfbrush") << brushJson("Cmd Brush");
    std::ofstream(src / "LICENSE.txt") << "CC0\n";
    CommandRegistry reg;
    Document doc;
    const std::string out = (root.dir / "cmd.sfpack").string();
    CommandResult r = reg.execute(doc, "pack_create",
                                  {{"folder", src.string()}, {"path", out}, {"id", std::string("cmd-pack")},
                                   {"name", std::string("Cmd Pack")}, {"author", std::string("Me")},
                                   {"license", std::string("CC0-1.0")}});
    CHECK(r.ok);
    r = reg.execute(doc, "pack_install", {{"path", out}});
    CHECK(r.ok);
    CHECK(contains(r.message, "1 brush"));
    r = reg.execute(doc, "pack_list");
    CHECK(r.ok && contains(r.message, "cmd-pack") && contains(r.message, "Cmd Pack 1.0"));
    r = reg.execute(doc, "pack_disable", {{"id", std::string("cmd-pack")}});
    CHECK(r.ok);
    CHECK(contains(reg.execute(doc, "pack_list").message, "- off"));
    r = reg.execute(doc, "pack_remove", {{"id", std::string("cmd-pack")}});
    CHECK(r.ok);
    CHECK(contains(reg.execute(doc, "pack_list").message, "No packs"));
    CHECK(!reg.execute(doc, "pack_install", {{"path", (root.dir / "missing.sfpack").string()}}).ok);
}

// --- Macros ---

TEST(macro_validation) {
    CommandRegistry reg;
    Macro m;
    std::string err;
    CHECK(parseMacro("# name: Clean\n# description: tidy up\nmask_clear\nmask_all  # all of it\n", &reg, m, err));
    CHECK(m.name == "Clean" && m.description == "tidy up" && m.statements.size() == 2);
    CHECK(!parseMacro("no_such_command\n", &reg, m, err));
    CHECK(contains(err, "unknown command"));
    CHECK(!parseMacro("mask_all bogus=1\n", &reg, m, err));
    CHECK(contains(err, "unknown parameter"));
    CHECK(!parseMacro("image_to_3d path=?\n", &reg, m, err));
    CHECK(contains(err, "not allowed in a macro"));
    CHECK(!parseMacro("macro_run name=x\n", &reg, m, err));
    CHECK(!parseMacro("pack_install path=x.sfpack\n", &reg, m, err));
    CHECK(!parseMacro("open\n", &reg, m, err));
    CHECK(contains(err, "needs path="));
    CHECK(!parseMacro("new_sphere subdivisions=lots\n", &reg, m, err));
    CHECK(contains(err, "must be a number"));
    CHECK(!parseMacro("", &reg, m, err));
    CHECK(!parseMacro("open path=\"unterminated\n", &reg, m, err));
    // File values: "?" or inside the project folder.
    CHECK(parseMacro("open path=?\nexport_glb path=out/head.glb\n", &reg, m, err));
    CHECK(macroAsks(m).size() == 1 && macroAsks(m)[0].kind == MacroPathKind::Open);
    for (const char* bad : {"open path=/etc/passwd", "open path=../../secret.obj", "open path=C:\\Windows\\x.obj",
                            "open path=~/x.obj", "export_glb path=out/../../x.glb", "open path=\"\\\\server\\share\\x.obj\"",
                            "export_glb path=$HOME/x.glb", "export_maps preset=/tmp/evil.json"}) {
        CHECK(!parseMacro(bad, &reg, m, err));
        CHECK(!err.empty());
    }
    std::string why;
    CHECK(!macroCommandAllowed("brush_ai", &why) && contains(why, "network"));
    CHECK(macroCommandAllowed("remesh"));
    // Too many commands.
    std::string many;
    for (int i = 0; i < 501; ++i) many += "mask_all\n";
    CHECK(!parseMacro(many, &reg, m, err));
    CHECK(contains(err, "too many"));
}

TEST(macro_path_confinement) {
    const fs::path root = fs::temp_directory_path() / "sf_macro_paths";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "project" / "out");
    fs::create_directories(root / "outside");
    std::string out, err;
    CHECK(resolveMacroPath((root / "project").string(), "out/a.glb", out, err));
    CHECK(fs::path(out).parent_path().filename() == "out");
    CHECK(!resolveMacroPath((root / "project").string(), "../outside/a.glb", out, err));
    CHECK(!resolveMacroPath("", "out/a.glb", out, err));
    CHECK(contains(err, "save the project first"));
    CHECK(!resolveMacroPath((root / "project").string(), "?", out, err));
#if !defined(_WIN32)
    // A symbolic link in the project folder that points out of it.
    fs::create_directory_symlink(root / "outside", root / "project" / "escape", ec);
    if (!ec) {
        CHECK(!resolveMacroPath((root / "project").string(), "escape/a.glb", out, err));
        CHECK(contains(err, "outside the project folder"));
    }
#endif
    fs::remove_all(root, ec);
}

TEST(macro_record_play_round_trip) {
    TempRoot tmp("sf_macro_round");
    const fs::path project = tmp.dir / "proj";
    fs::create_directories(project / "out");
    CommandRegistry reg;

    // Record what the user runs.
    MacroRecorder rec;
    rec.start();
    const std::vector<std::pair<std::string, Args>> commands = {
        {"mask_all", {}},
        {"mask_invert", {}},
        {"transform", {{"translate", std::string("0.25,0,0")}, {"masked", false}}},
        {"export_obj_unused", {}}, // (not a command: recorded as typed, refused by validation below)
        {"brush_gallery_install", {{"name", std::string("x")}}}, // never recorded
        {"save_obj", {{"path", (project / "out" / "rec.obj").string()}}},
        {"open", {{"path", (tmp.dir / "elsewhere.obj").string()}}},
    };
    for (const auto& [c, a] : commands) rec.record(c, a, project.string());
    rec.stop();
    CHECK(rec.skipped() == 1);
    CHECK(rec.lines().size() == 6);
    CHECK(rec.lines()[4] == "save_obj path=out/rec.obj"); // inside the project: relative
    CHECK(rec.lines()[5] == "open path=?");                // anywhere else: asked when it runs
    std::vector<std::string> lines(rec.lines().begin(), rec.lines().begin() + 3);
    lines.push_back(rec.lines()[4]);
    const std::string text = macroFileText("Shift It", "moves the sculpt", lines);

    Macro m;
    std::string err;
    CHECK(parseMacro(text, &reg, m, err));
    CHECK(m.name == "Shift It" && m.statements.size() == 4);

    // Played back: the same result as running the commands by hand.
    Document direct = makeDoc(makeIcosphere(2));
    reg.execute(direct, "mask_all");
    reg.execute(direct, "mask_invert");
    reg.execute(direct, "transform", {{"translate", std::string("0.25,0,0")}, {"masked", false}});
    Document played = makeDoc(makeIcosphere(2));
    const size_t before = played.undo.undoCount();
    MacroRunContext ctx;
    ctx.projectDir = project.string();
    const MacroRunResult r = runMacro(played, reg, m, ctx);
    CHECK(r.ok);
    CHECK(r.ran == 4);
    CHECK(played.mesh.positions == direct.mesh.positions);
    CHECK(fs::is_regular_file(project / "out" / "rec.obj"));
    // One undo step for the whole macro, and undo brings the sphere back.
    CHECK(r.oneUndoStep);
    CHECK(played.undo.undoCount() == before + 1);
    CHECK(played.undo.undoLabel() == "Macro: Shift It");
    const Document fresh = makeDoc(makeIcosphere(2));
    CHECK(reg.execute(played, "undo").ok);
    CHECK(played.mesh.positions == fresh.mesh.positions);

    // "?" without a way to ask fails; with an answer, it runs.
    Macro ask;
    CHECK(parseMacro("save_obj path=?\n", &reg, ask, err));
    Document d = makeDoc(makeIcosphere(1));
    CHECK(!runMacro(d, reg, ask, MacroRunContext{}).ok);
    MacroRunContext answer;
    answer.askPath = [&](const std::string&, const std::string&, MacroPathKind kind) {
        CHECK(kind == MacroPathKind::Save);
        return (tmp.dir / "picked.obj").string();
    };
    CHECK(runMacro(d, reg, ask, answer).ok);
    CHECK(fs::is_regular_file(tmp.dir / "picked.obj"));
    // A relative path without a saved project fails.
    Macro rel;
    CHECK(parseMacro("save_obj path=out/x.obj\n", &reg, rel, err));
    const MacroRunResult noProject = runMacro(d, reg, rel, MacroRunContext{});
    CHECK(!noProject.ok && contains(noProject.message, "save the project first"));
}

TEST(macro_library_and_confirmation) {
    TempRoot tmp("sf_macro_lib");
    fs::create_directories(tmp.dir / "macros");
    std::ofstream(tmp.dir / "macros" / "Tidy.sfmacro") << "# name: Tidy\nmask_clear\n";
    std::ofstream(tmp.dir / "macros" / "Broken.sfmacro") << "image_to_3d path=?\n";
    CommandRegistry reg;
    std::vector<std::string> messages;
    const auto macros = listMacros(&reg, &messages);
    CHECK(macros.size() == 1 && macros[0].name == "Tidy");
    CHECK(messages.size() == 1);
    Macro m;
    std::string err;
    CHECK(findMacro("tidy", &reg, m, err));
    CHECK(!findMacro("nothing", &reg, m, err));
    CHECK(!macroConfirmed(m.text));
    setMacroConfirmed(m.text);
    CHECK(macroConfirmed(m.text));
    CHECK(!macroConfirmed(m.text + "mask_all\n")); // changed: asked again
    Document doc = makeDoc(makeIcosphere(1));
    CHECK(reg.execute(doc, "macro_run", {{"name", std::string("Tidy")}}).ok);
    CHECK(reg.execute(doc, "macro_check", {{"text", std::string("mask_all")}}).ok);
    CHECK(!reg.execute(doc, "macro_check", {{"text", std::string("open path=/etc/passwd")}}).ok);
    CHECK(macroFileName("a/b:c") == "a_b_c.sfmacro");
}
