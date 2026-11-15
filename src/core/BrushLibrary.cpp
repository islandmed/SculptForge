// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/BrushLibrary.h"
#include "core/FileReplace.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/LegacyNames.h"
#include "core/Pack.h"
#include "core/Vdm.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <set>

namespace sf {

namespace fs = std::filesystem;

namespace {

constexpr const char* kCodePrefix = "sfbrush1:";

fs::path utf8Path(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string pathString(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}
std::string fileNameOf(const std::string& path) { return pathString(utf8Path(path).filename()); }

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? v : "";
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// --- base64url (RFC 4648 section 5), no padding ---

const char kBase64Url[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string base64UrlEncode(const std::string& in) {
    std::string out;
    out.reserve((in.size() * 4 + 2) / 3);
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const uint32_t n = uint32_t(uint8_t(in[i])) << 16 | uint32_t(uint8_t(in[i + 1])) << 8 | uint8_t(in[i + 2]);
        out += kBase64Url[n >> 18 & 63];
        out += kBase64Url[n >> 12 & 63];
        out += kBase64Url[n >> 6 & 63];
        out += kBase64Url[n & 63];
    }
    if (i + 1 == in.size()) {
        const uint32_t n = uint32_t(uint8_t(in[i])) << 16;
        out += kBase64Url[n >> 18 & 63];
        out += kBase64Url[n >> 12 & 63];
    } else if (i + 2 == in.size()) {
        const uint32_t n = uint32_t(uint8_t(in[i])) << 16 | uint32_t(uint8_t(in[i + 1])) << 8;
        out += kBase64Url[n >> 18 & 63];
        out += kBase64Url[n >> 12 & 63];
        out += kBase64Url[n >> 6 & 63];
    }
    return out;
}

bool base64UrlDecode(const std::string& in, std::string& out) {
    out.clear();
    uint32_t acc = 0;
    int bits = 0;
    for (char c : in) {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '-') v = 62;
        else if (c == '_') v = 63;
        else return false;
        acc = (acc << 6) | uint32_t(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += char((acc >> bits) & 0xFF);
        }
    }
    return bits < 6; // one leftover character can't encode a byte
}

// The JSON of a pasted brush or a .sfbrush file: an object that names a brush or says it's one.
bool brushFromJsonText(const std::string& text, BrushRecipe& out, std::string& error) {
    Json j;
    std::string parseError;
    if (!Json::parse(text, j, parseError)) {
        error = "Not a brush (" + parseError + ")";
        return false;
    }
    const bool isBrush = j.isObject() && (j["format"].isNull() ? j["name"].isString() : j["format"].asString() == "sfbrush");
    if (!isBrush) {
        error = "Not a SculptForge brush";
        return false;
    }
    out = BrushRecipe::fromJson(j);
    out.origin = {};
    if (j["source"].asString() == "gallery") out.origin.source = BrushSource::Gallery;
    return true;
}

bool sameFile(const std::string& a, const std::string& b) {
    std::error_code ec;
    return fs::weakly_canonical(utf8Path(a), ec) == fs::weakly_canonical(utf8Path(b), ec);
}

// True when `path` is inside folder `dir` (the user library): studio folders are never written.
bool insideDir(const std::string& path, const std::string& dir) {
    if (path.empty() || dir.empty()) return false;
    std::error_code ec;
    const fs::path p = fs::weakly_canonical(utf8Path(path), ec);
    const fs::path d = fs::weakly_canonical(utf8Path(dir), ec);
    if (ec) return false;
    const fs::path rel = p.lexically_relative(d);
    return !rel.empty() && *rel.begin() != ".." && rel != ".";
}

// The .sfbrush files directly in `dir`, by file name (so load order doesn't depend on the file system).
std::vector<std::string> brushFiles(const std::string& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (fs::directory_iterator it(utf8Path(dir), ec), end; !ec && it != end; it.increment(ec)) {
        std::string ext = pathString(it->path().extension());
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        std::error_code fileEc;
        if (ext == ".sfbrush" && it->is_regular_file(fileEc)) out.push_back(pathString(it->path()));
    }
    std::sort(out.begin(), out.end());
    return out;
}

// Where the library keeps the last good copy of each brush file it replaces or deletes (never listed as brushes).
fs::path backupDirOf(const std::string& userDir) { return utf8Path(userDir) / "backups"; }

// Copies the brush file at `path` into the backups folder (a damaged file goes there as .damaged, never over the good copy). Best effort: a brush is never refused its save over a backup.
void backupBrushFile(const std::string& path, const char* sub = "") {
    std::error_code ec;
    BrushRecipe probe;
    std::string err;
    if (!fs::is_regular_file(utf8Path(path), ec)) return;
    const fs::path dir = backupDirOf(pathString(utf8Path(path).parent_path())) / sub;
    fs::create_directories(dir, ec);
    fs::path to = dir / utf8Path(path).filename();
    if (!readBrushFile(path, probe, err)) to += ".damaged"; // kept for salvage, apart from the good copy
    fs::copy_file(utf8Path(path), to, fs::copy_options::overwrite_existing, ec);
}

bool writeBrushFile(const std::string& path, const BrushRecipe& recipe, bool gallery, std::string& error) {
    Json j = recipe.toJson();
    j.set("format", "sfbrush");
    j.set("version", 1);
    if (gallery) j.set("source", "gallery");
    if (!recipe.id.empty()) j.set("id", recipe.id);
    // Write next to it and rename over it, so a crash never leaves half a brush behind.
    const fs::path target = utf8Path(path);
    fs::path tmp = target;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << j.dump(true) << "\n";
        f.close(); // (a full disk or a locked folder shows on flush/close, not on the write)
        if (!f) {
            error = "Cannot write " + path;
            std::error_code ec;
            fs::remove(tmp, ec);
            return false;
        }
    }
    {
        // What is on disk must read back as this brush before it replaces the good file.
        BrushRecipe check;
        std::string readError;
        if (!readBrushFile(pathString(tmp), check, readError) || check.name != recipe.name) {
            error = "Cannot write " + path + " (the saved copy didn't read back: " + readError + ")";
            std::error_code ec;
            fs::remove(tmp, ec);
            return false;
        }
    }
    backupBrushFile(path); // the file being replaced, one generation back
    return replaceFile(tmp, target, error);
}

// Where a new brush called `name` goes in the user library: its safe file name,
// numbered if that file already holds a different brush.
std::string libraryPathFor(const std::string& dir, const std::string& name) {
    const std::string file = brushFileName(name);
    const std::string stem = file.substr(0, file.size() - std::strlen(".sfbrush"));
    fs::path p;
    for (int n = 1; n <= 10000; ++n) {
        p = utf8Path(dir) / utf8Path(n == 1 ? file : stem + " " + std::to_string(n) + ".sfbrush");
        std::error_code ec;
        if (!fs::exists(p, ec)) break;
        BrushRecipe existing;
        std::string err;
        if (readBrushFile(pathString(p), existing, err) && existing.name == name) break;
    }
    return pathString(p);
}

// The studio or built-in brush called `name` that a user brush of that name hides.
bool findShadowed(const std::string& name, const BrushLibraryPaths& paths, BrushRecipe& out) {
    for (const auto& dir : paths.studioDirs) {
        for (const auto& file : brushFiles(dir)) {
            std::string err;
            if (readBrushFile(file, out, err) && out.name == name) {
                out.origin = {BrushSource::Studio, file, BrushSource::Session};
                return true;
            }
        }
    }
    for (auto& b : builtinBrushRecipes()) {
        if (b.name == name) {
            out = std::move(b);
            return true;
        }
    }
    for (const auto& pack : paths.packs) {
        if (pack.vdm) continue;
        for (const auto& file : brushFiles(pack.dir)) {
            std::string err;
            if (readBrushFile(file, out, err) && out.name == name) {
                out.origin = {BrushSource::Pack, file, BrushSource::Session, pack.pack};
                return true;
            }
        }
    }
    return false;
}

std::string lowerAscii(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// A new random identity for a brush file (16 hex digits).
std::string newBrushId() {
    static std::mt19937_64 rng{std::random_device{}() ^ uint64_t(std::chrono::steady_clock::now().time_since_epoch().count())};
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(rng()));
    return buf;
}

// The .sfbrush files in `dir` and every folder under it (a library sorted into folders), except backups/. By path.
std::vector<std::string> brushFilesDeep(const std::string& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    fs::recursive_directory_iterator it(utf8Path(dir), fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code fileEc;
        if (it->is_directory(fileEc) && it->path().filename() == "backups") {
            it.disable_recursion_pending();
            continue;
        }
        std::string ext = lowerAscii(pathString(it->path().extension()));
        if (ext == ".sfbrush" && it->is_regular_file(fileEc)) out.push_back(pathString(it->path()));
    }
    std::sort(out.begin(), out.end());
    return out;
}

// A slot's name for a brush: exactly, ASCII case aside, or as an older version's layout loader spelled it (a slot saved
// by a version that rewrote retired words inside the names of custom brushes holds today's spelling of the brush's name).
bool sameBrushName(const std::string& brushName, const std::string& slotName) {
    if (slotName.empty()) return false;
    if (brushName == slotName) return true;
    const std::string lower = lowerAscii(slotName);
    return lowerAscii(brushName) == lower || lowerAscii(legacy::modernize(brushName)) == lower;
}

int findBrush(const Document& doc, const std::string& name) {
    for (size_t i = 0; i < doc.brushes.size(); ++i)
        if (doc.brushes[i].name == name) return int(i);
    return -1;
}

} // namespace

BrushLibraryPaths BrushLibraryPaths::fromEnvironment() {
    BrushLibraryPaths p;
    p.userDir = pathString(utf8Path(appDataDir()) / "brushes");
    p.studioDirs = splitSearchPath(env("SCULPTFORGE_BRUSH_PATH"));
    applyPacksToBrushLibrary(p); // the enabled packs' brushes and VDMs (none with --no-packs)
    return p;
}

BrushLibraryPaths& brushLibraryPaths() {
    static BrushLibraryPaths paths = BrushLibraryPaths::fromEnvironment();
    return paths;
}

std::string appDataDir() {
    // Matches SDL_GetPrefPath("SculptForge", "SculptForge"), which the app uses for its settings.
#if defined(_WIN32)
    const std::string base = env("APPDATA");
    return base.empty() ? "" : base + "\\SculptForge\\SculptForge";
#elif defined(__APPLE__)
    return env("HOME") + "/Library/Application Support/SculptForge/SculptForge";
#else
    const std::string xdg = env("XDG_DATA_HOME");
    return (xdg.empty() ? env("HOME") + "/.local/share" : xdg) + "/SculptForge/SculptForge";
#endif
}

std::vector<std::string> splitSearchPath(const std::string& value) {
#if defined(_WIN32)
    const char sep = ';';
#else
    const char sep = ':';
#endif
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= value.size()) {
        size_t end = value.find(sep, start);
        if (end == std::string::npos) end = value.size();
        std::string part = trim(value.substr(start, end - start));
        if (!part.empty()) out.push_back(std::move(part));
        start = end + 1;
    }
    return out;
}

std::string encodeBrushCode(const BrushRecipe& recipe) { return kCodePrefix + base64UrlEncode(recipe.toJson().dump()); }

bool decodeBrushCode(const std::string& code, BrushRecipe& out, std::string& error) {
    const std::string text = trim(code);
    if (text.size() > kMaxBrushCodeSize) {
        error = "Brush code is too long (at most 8 KB)";
        return false;
    }
    if (text.rfind(kCodePrefix, 0) != 0) {
        const bool newer = text.rfind("sfbrush", 0) == 0 && text.size() > 8 && std::isdigit(static_cast<unsigned char>(text[7]));
        error = newer ? "This brush code needs a newer version of SculptForge" : "Not a brush code (they start with sfbrush1:)";
        return false;
    }
    // Codes can get wrapped or padded on their way through chat and email.
    std::string payload;
    for (size_t i = std::strlen(kCodePrefix); i < text.size(); ++i)
        if (!std::isspace(static_cast<unsigned char>(text[i]))) payload += text[i];
    while (!payload.empty() && payload.back() == '=') payload.pop_back();
    std::string json;
    if (payload.empty() || !base64UrlDecode(payload, json)) {
        error = "Damaged brush code (incomplete or changed while copying)";
        return false;
    }
    std::string err;
    if (!brushFromJsonText(json, out, err)) {
        error = "Damaged brush code (incomplete or changed while copying)";
        return false;
    }
    out.origin = {}; // a code says nothing about where the brush lives
    return true;
}

bool parseBrushText(const std::string& text, BrushRecipe& out, std::string& error) {
    const std::string t = trim(text);
    if (t.empty()) {
        error = "Nothing to paste: copy a brush code or a .sfbrush file first";
        return false;
    }
    if (t.front() == '{') {
        if (t.size() > kMaxBrushFileSize) {
            error = "Too large to be a brush";
            return false;
        }
        if (!brushFromJsonText(t, out, error)) return false;
        out.origin = {};
        return true;
    }
    // A code on its own, or inside a message ("try my brush: sfbrush1:...").
    size_t at = t.find("sfbrush");
    if (at == std::string::npos) {
        error = "No brush found: copy a brush code (sfbrush1:...) or the contents of a .sfbrush file";
        return false;
    }
    size_t end = at;
    while (end < t.size() && !std::isspace(static_cast<unsigned char>(t[end]))) ++end;
    return decodeBrushCode(t.substr(at, end - at), out, error);
}

bool readBrushFile(const std::string& path, BrushRecipe& out, std::string& error) {
    std::error_code ec;
    const auto size = fs::file_size(utf8Path(path), ec);
    if (ec) {
        error = "Cannot open " + path;
        return false;
    }
    if (size > kMaxBrushFileSize) {
        error = "Too large to be a brush (" + std::to_string(size / 1024) + " KB)";
        return false;
    }
    std::ifstream f(utf8Path(path), std::ios::binary);
    if (!f) {
        error = "Cannot open " + path;
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (!brushFromJsonText(text, out, error)) return false;
    Json j;
    std::string ignored;
    if (Json::parse(text, j, ignored) && j["id"].isString() && j["id"].asString().size() <= 64) out.id = j["id"].asString();
    return true;
}

std::string brushFileName(const std::string& name) {
    std::string out;
    for (unsigned char c : name) out += c < 0x20 || std::strchr("<>:\"/\\|?*", c) ? '_' : char(c);
    // No leading dots (hidden files, "..") or trailing dots and spaces (Windows drops them).
    const size_t a = out.find_first_not_of(". ");
    out = a == std::string::npos ? "" : out.substr(a);
    while (!out.empty() && (out.back() == '.' || out.back() == ' ')) out.pop_back();
    if (out.empty()) out = "Brush";
    // Reserved device names on Windows, with or without an extension.
    std::string base = out.substr(0, out.find('.'));
    std::transform(base.begin(), base.end(), base.begin(), [](unsigned char c) { return char(std::toupper(c)); });
    static const char* kReserved[] = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7",
                                      "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    for (const char* r : kReserved)
        if (base == r) out = "_" + out;
    return out + ".sfbrush";
}

bool isUserLibraryBrush(const BrushRecipe& recipe) {
    return recipe.origin.source == BrushSource::User || recipe.origin.source == BrushSource::Gallery;
}

std::vector<std::string> loadBrushLibrary(Document& doc, const BrushLibraryPaths& paths) {
    std::vector<std::string> messages;
    const std::string active =
        doc.activeBrush >= 0 && doc.activeBrush < int(doc.brushes.size()) ? doc.brushes[size_t(doc.activeBrush)].name : "";
    // Brushes that live only in this session or the open project (not in any folder) survive a reload.
    std::vector<BrushRecipe> keep;
    for (auto& b : doc.brushes)
        if (b.origin.source == BrushSource::Session || b.origin.source == BrushSource::Project) keep.push_back(std::move(b));
    doc.brushes = builtinBrushRecipes();

    for (const auto& dir : paths.studioDirs) {
        std::error_code ec;
        if (!fs::is_directory(utf8Path(dir), ec)) {
            messages.push_back("Studio brush folder not found: " + dir);
            continue;
        }
        for (const auto& file : brushFiles(dir)) {
            BrushRecipe r;
            std::string err;
            if (!readBrushFile(file, r, err)) {
                messages.push_back("Skipped studio brush " + file + ": " + err);
                continue;
            }
            r.origin = {BrushSource::Studio, file, BrushSource::Session};
            const int i = findBrush(doc, r.name);
            if (i < 0) doc.brushes.push_back(std::move(r));
            else if (doc.brushes[size_t(i)].origin.source == BrushSource::Builtin) {
                r.origin.overrides = BrushSource::Builtin;
                doc.brushes[size_t(i)] = std::move(r);
            } // else an earlier studio folder has it, and wins
        }
    }

    for (const auto& pack : paths.packs) {
        if (pack.vdm || packsSafeMode()) continue;
        for (const auto& file : brushFiles(pack.dir)) {
            BrushRecipe r;
            std::string err;
            if (!readBrushFile(file, r, err)) {
                messages.push_back("Skipped " + file + " (pack " + pack.pack + "): " + err);
                continue;
            }
            r.origin = {BrushSource::Pack, file, BrushSource::Session, pack.pack};
            if (findBrush(doc, r.name) >= 0) r.name += " (" + pack.pack + ")"; // never replaces another brush
            if (findBrush(doc, r.name) >= 0) continue;                          // (the same pack twice)
            doc.brushes.push_back(std::move(r));
        }
    }

    if (!paths.userDir.empty()) {
        std::set<std::string> userIds;
        for (const auto& file : brushFiles(paths.userDir)) {
            BrushRecipe r;
            std::string err;
            bool readOk = true;
            if (!readBrushFile(file, r, err)) {
                readOk = false;
                // The file is kept as it is. If its last good copy is in backups/, the brush comes back from that.
                const std::string backup = pathString(backupDirOf(paths.userDir) / utf8Path(file).filename());
                if (!readBrushFile(backup, r, err)) {
                    messages.push_back("Kept " + file + " but can't read it: " + err);
                    continue;
                }
                messages.push_back("Restored '" + r.name + "' from its backup (" + fileNameOf(file) + " couldn't be read; it is kept)");
            }
            r.origin.source = r.origin.source == BrushSource::Gallery ? BrushSource::Gallery : BrushSource::User;
            r.origin.path = file;
            if (readOk && (r.id.empty() || !userIds.insert(r.id).second)) { // no identity yet, or a copied file's: give it its own
                r.id = newBrushId();
                userIds.insert(r.id);
                std::string idError;
                writeBrushFile(file, r, r.origin.source == BrushSource::Gallery, idError); // (best effort: a slot still finds it by name)
            }
            const int i = findBrush(doc, r.name);
            if (i < 0) {
                doc.brushes.push_back(std::move(r));
            } else if (isUserLibraryBrush(doc.brushes[size_t(i)])) {
                messages.push_back("Skipped " + file + ": " + fileNameOf(doc.brushes[size_t(i)].origin.path) +
                                   " already has a brush called '" + r.name + "'");
            } else {
                r.origin.overrides = doc.brushes[size_t(i)].origin.source;
                r.origin.pack = doc.brushes[size_t(i)].origin.pack;
                doc.brushes[size_t(i)] = std::move(r);
            }
        }
    }
    for (auto& b : keep)
        if (findBrush(doc, b.name) < 0) doc.brushes.push_back(std::move(b));
    doc.activeBrush = active.empty() ? -1 : findBrush(doc, active);
    loadVdmLibrary(doc, paths, messages); // the VDM brush's maps (.sfvdm) live in the same folders (Vdm.cpp)
    return messages;
}

bool saveBrushToLibrary(Document& doc, int index, std::string& error, bool gallery, const BrushLibraryPaths& paths) {
    if (index < 0 || index >= int(doc.brushes.size())) {
        error = "No such brush";
        return false;
    }
    if (paths.userDir.empty()) {
        error = "Brushes aren't saved in this session";
        return false;
    }
    std::error_code ec;
    fs::create_directories(utf8Path(paths.userDir), ec);
    if (ec) {
        error = "Cannot create the brush library " + paths.userDir + ": " + ec.message();
        return false;
    }
    BrushRecipe& r = doc.brushes[size_t(index)];
    const bool inLibrary = isUserLibraryBrush(r) && insideDir(r.origin.path, paths.userDir);
    const std::string target = inLibrary ? r.origin.path : libraryPathFor(paths.userDir, r.name);
    gallery = gallery || r.origin.source == BrushSource::Gallery;
    if (r.id.empty()) r.id = newBrushId();
    if (!writeBrushFile(target, r, gallery, error)) return false;
    if (!inLibrary) {
        BrushRecipe hidden;
        r.origin.overrides = findShadowed(r.name, paths, hidden) ? hidden.origin.source : BrushSource::Session;
    }
    r.origin.source = gallery ? BrushSource::Gallery : BrushSource::User;
    r.origin.path = target;
    return true;
}

int recoverBrush(Document& doc, const std::string& name, std::string& message, const BrushLibraryPaths& paths) {
    return relinkBrush(doc, name, "", message, paths);
}

int findBrushById(const Document& doc, const std::string& id) {
    if (id.empty()) return -1;
    for (size_t i = 0; i < doc.brushes.size(); ++i)
        if (doc.brushes[i].id == id) return int(i);
    return -1;
}

int findBrushForSlot(const Document& doc, const std::string& name, const std::string& id) {
    if (const int i = findBrushById(doc, id); i >= 0) return i;
    if (const int i = findBrush(doc, name); i >= 0) return i;
    for (size_t i = 0; i < doc.brushes.size(); ++i)
        if (sameBrushName(doc.brushes[i].name, name)) return int(i);
    return -1;
}

std::string describeMissingSlot(const Document& doc, const std::string& name, const std::string& id) {
    auto squashed = [](const std::string& s) {
        std::string r;
        for (unsigned char c : s)
            if (std::isalnum(c) || c >= 0x80) r += char(std::tolower(c));
        return r;
    };
    const std::string want = squashed(name);
    std::string close;
    int shown = 0;
    for (const BrushRecipe& b : doc.brushes) {
        if (!isUserLibraryBrush(b) || shown >= 3) continue;
        const std::string have = squashed(b.name);
        if (have == want || (want.size() >= 4 && have.compare(0, 4, want, 0, 4) == 0)) {
            close += (shown++ ? ", '" : " '") + b.name + "'";
        }
    }
    return "Shelf slot '" + name + "' (id " + (id.empty() ? "none" : id) + ") finds no brush in the library; closest:" +
           (shown ? close : " none");
}

int relinkBrush(Document& doc, const std::string& name, const std::string& id, std::string& message, const BrushLibraryPaths& paths) {
    message.clear();
    if (const int i = findBrushForSlot(doc, name, id); i >= 0) return i;
    if (paths.userDir.empty() || (name.empty() && id.empty())) return -1;
    // Not loaded: its file, wherever it lives now (user library with its folders, studio folders), then the last good copy.
    struct Source {
        std::string dir;
        bool backup, deep;
    };
    std::vector<Source> sources = {{paths.userDir, false, true}};
    for (const auto& dir : paths.studioDirs) sources.push_back({dir, false, true});
    sources.push_back({pathString(backupDirOf(paths.userDir)), true, false});
    for (const bool byId : {true, false}) {
        if (byId && id.empty()) continue;
        for (const auto& src : sources) {
            for (const auto& file : src.deep ? brushFilesDeep(src.dir) : brushFiles(src.dir)) {
                BrushRecipe r;
                std::string err;
                if (!readBrushFile(file, r, err)) continue;
                if (byId ? r.id != id : !sameBrushName(r.name, name)) continue;
                if (findBrush(doc, r.name) >= 0) continue; // another brush has that name now: never replaces it
                const bool user = src.backup || insideDir(file, paths.userDir);
                r.origin.source = !user ? BrushSource::Studio : r.origin.source == BrushSource::Gallery ? BrushSource::Gallery : BrushSource::User;
                r.origin.path = src.backup ? "" : file;
                doc.brushes.push_back(std::move(r));
                const int index = int(doc.brushes.size()) - 1;
                const std::string& found = doc.brushes[size_t(index)].name;
                if (src.backup) {
                    std::string saveError;
                    if (!saveBrushToLibrary(doc, index, saveError, false, paths))
                        message = "Found '" + found + "' in backups/ but couldn't save it again: " + saveError;
                    else message = "Brought '" + found + "' back from its backup";
                } else {
                    message = "Found '" + found + "' in your brush library again";
                }
                return index;
            }
        }
    }
    return -1;
}

int installBrush(Document& doc, BrushRecipe recipe, std::string& error, bool gallery, const BrushLibraryPaths& paths) {
    recipe.origin = {}; // an install of a brush already in the library updates it (see Document::addBrush)
    const int index = doc.addBrush(std::move(recipe));
    doc.activeBrush = index;
    error.clear();
    saveBrushToLibrary(doc, index, error, gallery, paths);
    return index;
}

bool deleteBrush(Document& doc, int index, std::string& error, const BrushLibraryPaths& paths) {
    if (index < 0 || index >= int(doc.brushes.size())) {
        error = "No such brush";
        return false;
    }
    const BrushRecipe& r = doc.brushes[size_t(index)];
    if (!isUserLibraryBrush(r)) {
        error = "'" + r.name + "' is a " + brushSourceName(r.origin.source) +
                " brush: only brushes in your own library can be deleted";
        return false;
    }
    if (!r.origin.path.empty()) {
        if (!insideDir(r.origin.path, paths.userDir)) {
            error = "'" + r.name + "' is not in your brush library (" + r.origin.path + ")";
            return false;
        }
        backupBrushFile(r.origin.path, "deleted"); // Delete keeps a copy in backups/deleted (a shelf slot doesn't bring it back)
        std::error_code ec;
        fs::remove(utf8Path(r.origin.path), ec);
        if (ec) {
            error = "Cannot delete " + r.origin.path + ": " + ec.message();
            return false;
        }
        fs::remove(backupDirOf(paths.userDir) / utf8Path(r.origin.path).filename(), ec); // (moved to backups/deleted)
    }
    BrushRecipe hidden;
    if (findShadowed(r.name, paths, hidden)) {
        doc.brushes[size_t(index)] = std::move(hidden); // the studio or built-in version is back
        return true;
    }
    doc.brushes.erase(doc.brushes.begin() + index);
    if (doc.activeBrush == index) doc.activeBrush = -1;
    else if (doc.activeBrush > index) --doc.activeBrush;
    return true;
}

int renameBrush(Document& doc, int index, const std::string& newName, std::string& error, const BrushLibraryPaths& paths) {
    if (index < 0 || index >= int(doc.brushes.size())) {
        error = "No such brush";
        return -1;
    }
    BrushRecipe probe;
    probe.name = trim(newName);
    if (probe.name.empty()) {
        error = "A brush needs a name";
        return -1;
    }
    probe.clamp(); // same length limit as every recipe
    const std::string name = probe.name, oldName = doc.brushes[size_t(index)].name;
    if (name == oldName) return index;
    if (findBrush(doc, name) >= 0) {
        error = "There is already a brush called '" + name + "'";
        return -1;
    }
    auto insertAt = [&](int at, BrushRecipe r) {
        doc.brushes.insert(doc.brushes.begin() + at, std::move(r));
        if (doc.activeBrush >= at) ++doc.activeBrush;
    };

    BrushRecipe& r = doc.brushes[size_t(index)];
    const BrushSource source = r.origin.source;
    if (source == BrushSource::Builtin || source == BrushSource::Studio || source == BrushSource::Pack) {
        // Shared brushes stay as they are; the renamed one is the artist's copy.
        BrushRecipe copy = r;
        copy.name = name;
        copy.origin = {};
        insertAt(index + 1, std::move(copy));
        doc.activeBrush = index + 1;
        if (!paths.userDir.empty()) saveBrushToLibrary(doc, index + 1, error, false, paths);
        return index + 1;
    }
    if (!isUserLibraryBrush(r) || paths.userDir.empty()) {
        r.name = name; // session and project brushes aren't in the library
        doc.activeBrush = index;
        return index;
    }
    const std::string oldPath = r.origin.path;
    r.name = name;
    if (r.id.empty()) r.id = newBrushId(); // the identity stays through the rename: a shelf slot finds the brush by it
    const std::string target = libraryPathFor(paths.userDir, name);
    std::string err;
    if (!writeBrushFile(target, r, r.origin.source == BrushSource::Gallery, err)) {
        r.name = oldName;
        error = err;
        return -1;
    }
    if (!oldPath.empty() && !sameFile(oldPath, target) && insideDir(oldPath, paths.userDir)) {
        std::error_code ec;
        fs::remove(utf8Path(oldPath), ec);
        fs::remove(backupDirOf(paths.userDir) / utf8Path(oldPath).filename(), ec); // (the brush goes on under its new name)
    }
    r.origin.path = target;
    BrushRecipe hidden;
    r.origin.overrides = findShadowed(name, paths, hidden) ? hidden.origin.source : BrushSource::Session;
    doc.activeBrush = index;
    if (findShadowed(oldName, paths, hidden)) { // the brush the old name overrode is back
        insertAt(index, std::move(hidden));
        doc.activeBrush = index + 1;
        return index + 1;
    }
    return index;
}

void registerBrushLibraryCommands(CommandRegistry& registry) {
    using T = ParamSpec::Type;
    // Brush by name, or the active brush when no name is given.
    auto pick = [](const Document& doc, const Args& a, std::string& error) {
        const std::string name = argString(a, "name");
        const int i = name.empty() ? doc.activeBrush : findBrush(doc, name);
        if (i < 0 || i >= int(doc.brushes.size())) error = name.empty() ? "No custom brush is active" : "No such brush: " + name;
        return i < 0 || i >= int(doc.brushes.size()) ? -1 : i;
    };

    registry.add(
        {"brush_install",
         "Install a brush into your brush library (saved, loaded at every start) and make it active: from a .sfbrush "
         "file, a brush code (sfbrush1:...) or recipe JSON. Installing a brush with the same name again updates it.",
         {{"path", T::String, ".sfbrush file to install.", false},
          {"code", T::String, "Brush code (sfbrush1:...).", false},
          {"json", T::String, "Brush recipe JSON (the contents of a .sfbrush file).", false},
          {"source", T::String, "'gallery' marks a brush installed from the community gallery.", false}},
         [](Document& doc, const Args& a) -> CommandResult {
             const int given = int(a.contains("path")) + int(a.contains("code")) + int(a.contains("json"));
             if (given != 1) return {false, "Give one of path, code or json"};
             BrushRecipe r;
             std::string err;
             const bool ok = a.contains("path")   ? readBrushFile(argString(a, "path"), r, err)
                             : a.contains("code") ? decodeBrushCode(argString(a, "code"), r, err)
                                                  : parseBrushText(argString(a, "json"), r, err);
             if (!ok) return {false, err};
             const std::string source = argString(a, "source");
             if (!source.empty() && source != "gallery" && source != "user")
                 return {false, "source must be 'gallery' or 'user'"};
             const bool gallery = source == "gallery" || (source.empty() && r.origin.source == BrushSource::Gallery);
             const int existing = findBrush(doc, r.name);
             const bool update = existing >= 0 && isUserLibraryBrush(doc.brushes[size_t(existing)]);
             const BrushLibraryPaths& paths = brushLibraryPaths();
             const int i = installBrush(doc, std::move(r), err, gallery, paths);
             const BrushRecipe& b = doc.brushes[size_t(i)];
             if (paths.userDir.empty()) return {true, "Brush '" + b.name + "' ready (not saved: no brush library this session)"};
             if (!err.empty()) return {false, "Brush '" + b.name + "' is ready this session but not saved: " + err};
             return {true, std::string(update ? "Updated" : "Installed") + " brush '" + b.name + "' in your library (" +
                               b.origin.path + ")"};
         }});

    registry.add({"brush_code",
                  "Print a brush as a brush code (sfbrush1:...): one line of text to share in chat, email or a forum. "
                  "Paste it into SculptForge (Ctrl+V) or run brush_install code=... to add the brush.",
                  {{"name", T::String, "Brush name (defaults to the active brush).", false}},
                  [pick](Document& doc, const Args& a) -> CommandResult {
                      std::string err;
                      const int i = pick(doc, a, err);
                      if (i < 0) return {false, err};
                      return {true, encodeBrushCode(doc.brushes[size_t(i)])};
                  }});

    registry.add({"brush_delete",
                  "Delete a brush from your brush library (its file too). Built-in and studio brushes can't be "
                  "deleted; if your brush replaced one of them, that one is used again.",
                  {{"name", T::String, "Brush name.", true}},
                  [pick](Document& doc, const Args& a) -> CommandResult {
                      std::string err;
                      const int i = pick(doc, a, err);
                      if (i < 0) return {false, err};
                      const std::string name = doc.brushes[size_t(i)].name;
                      if (!deleteBrush(doc, i, err)) return {false, err};
                      const int back = findBrush(doc, name);
                      return {true, "Deleted brush '" + name + "'" +
                                        (back >= 0 ? std::string(" (the ") + brushSourceName(doc.brushes[size_t(back)].origin.source) +
                                                         " brush of that name is back)"
                                                   : "")};
                  }});

    registry.add({"brush_rename",
                  "Rename a brush. Your own brushes are renamed in the library; renaming a built-in or studio brush "
                  "saves a renamed copy and keeps the original.",
                  {{"name", T::String, "Current name (defaults to the active brush).", false},
                   {"to", T::String, "New name.", true}},
                  [pick](Document& doc, const Args& a) -> CommandResult {
                      std::string err;
                      const int i = pick(doc, a, err);
                      if (i < 0) return {false, err};
                      const std::string from = doc.brushes[size_t(i)].name;
                      const BrushSource source = doc.brushes[size_t(i)].origin.source;
                      const int j = renameBrush(doc, i, argString(a, "to"), err);
                      if (j < 0) return {false, err};
                      const std::string to = doc.brushes[size_t(j)].name;
                      const std::string note = err.empty() ? "" : " (not saved: " + err + ")";
                      if (source == BrushSource::Builtin || source == BrushSource::Studio || source == BrushSource::Pack)
                          return {true, "Saved a copy of the " + std::string(brushSourceName(source)) + " brush '" + from +
                                            "' as '" + to + "'" + note};
                      return {true, "Renamed '" + from + "' to '" + to + "'" + note};
                  }});

    registry.add({"brush_library",
                  "List every custom brush with where it comes from (built-in, user, studio, gallery, project, "
                  "session) and its file, plus the library folders.",
                  {},
                  [](Document& doc, const Args&) -> CommandResult {
                      const BrushLibraryPaths& paths = brushLibraryPaths();
                      std::string out = "User library: " + (paths.userDir.empty() ? std::string("(none)") : paths.userDir);
                      for (const auto& dir : paths.studioDirs) out += "\nStudio folder: " + dir;
                      for (size_t i = 0; i < doc.brushes.size(); ++i) {
                          const BrushRecipe& b = doc.brushes[i];
                          out += "\n" + std::to_string(i) + ": " + b.name + " [" + brushSourceName(b.origin.source) + "]";
                          if (b.origin.overrides != BrushSource::Session)
                              out += std::string(" overrides ") + brushSourceName(b.origin.overrides);
                          if (!b.origin.path.empty()) out += " " + b.origin.path;
                      }
                      return {true, out};
                  }});
}

} // namespace sf
