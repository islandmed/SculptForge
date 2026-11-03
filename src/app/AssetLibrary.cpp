// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/AssetLibrary.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <system_error>

namespace sf {

namespace fs = std::filesystem;

namespace {

fs::path fsPath(const std::string& p) { return fs::path(std::u8string(p.begin(), p.end())); }
std::string utf8(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

std::string lowerExt(const std::string& path) {
    std::string ext = utf8(fsPath(path).extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return ext;
}

bool anyOf(const std::string& ext, std::initializer_list<const char*> list) {
    return std::any_of(list.begin(), list.end(), [&](const char* e) { return ext == e; });
}

int64_t toEpoch(fs::file_time_type t) {
    // file_clock -> system_clock without C++20 clock_cast (not on every standard library yet).
    const auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        t - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    return std::chrono::duration_cast<std::chrono::seconds>(sys.time_since_epoch()).count();
}

bool lessNoCase(const std::string& a, const std::string& b) {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](unsigned char x, unsigned char y) {
        return std::tolower(x) < std::tolower(y);
    });
}

} // namespace

const char* primitiveName(Primitive p) {
    switch (p) {
    case Primitive::Sphere:
        return "Sphere";
    case Primitive::Cube:
        return "Cube";
    case Primitive::Plane:
        return "Plane";
    default:
        return "";
    }
}

const char* libraryTabName(LibraryTab tab) {
    static const char* const names[] = {"Project", "Tool",   "Brush",     "Alpha",  "Material",
                                        "Image",   "Splats", "Snapshots", "Recent", "Packs"};
    return tab < LibraryTab::Count ? names[int(tab)] : "";
}

const char* libraryTabId(LibraryTab tab) {
    static const char* const ids[] = {"project", "tool",   "brush",     "alpha",  "material",
                                      "image",   "splats", "quicksave", "recent", "packs"};
    return tab < LibraryTab::Count ? ids[int(tab)] : "project";
}

LibraryTab libraryTabFromId(std::string_view id) {
    for (int i = 0; i < int(LibraryTab::Count); ++i)
        if (id == libraryTabId(LibraryTab(i))) return LibraryTab(i);
    return LibraryTab::Project;
}

const char* libraryTabFolder(LibraryTab tab) {
    switch (tab) {
    case LibraryTab::Project:
        return "Projects";
    case LibraryTab::Tool:
        return "Tools";
    case LibraryTab::Brush:
        return "Brushes";
    case LibraryTab::Alpha:
        return "Alphas";
    case LibraryTab::Material:
        return "MatCaps"; // custom MatCap images, loaded at start-up (core/Matcap.h)
    case LibraryTab::Image:
        return "Images";
    case LibraryTab::Splats:
        return "Splats";
    case LibraryTab::QuickSave:
        return "QuickSave";
    default:
        return "";
    }
}

bool isMeshFile(const std::string& path) {
    return anyOf(lowerExt(path), {".obj", ".glb", ".gltf", ".fbx", ".stl", ".ply", ".usda", ".usdz", ".usd", ".goz"});
}
bool isSplatFile(const std::string& path) { return anyOf(lowerExt(path), {".ply", ".splat"}); }
bool isImageFile(const std::string& path) {
    return anyOf(lowerExt(path), {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".psd", ".gif", ".webp"});
}
bool isBrushFile(const std::string& path) { return lowerExt(path) == ".sfbrush"; }
bool isProjectFile(const std::string& path) { return lowerExt(path) == ".sfproj"; }

std::string pathKey(const std::string& path, bool windowsRules) {
    std::string s = path;
    if (windowsRules) std::replace(s.begin(), s.end(), '\\', '/');
    const std::u8string g = fsPath(s).lexically_normal().generic_u8string();
    s.assign(g.begin(), g.end());
    if (windowsRules) {
        std::replace(s.begin(), s.end(), '\\', '/');
        for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    }
    std::string out; // doubled separators once, no trailing one (but keep a root "/")
    for (char c : s)
        if (c != '/' || out.empty() || out.back() != '/') out += c;
    while (out.size() > 1 && out.back() == '/') out.pop_back();
    return out;
}

bool samePath(const std::string& a, const std::string& b, bool windowsRules) {
    return a == b || pathKey(a, windowsRules) == pathKey(b, windowsRules);
}

AssetKind fileKindForTab(LibraryTab tab, const std::string& path) {
    switch (tab) {
    case LibraryTab::Project:
        return isProjectFile(path) ? AssetKind::Project : AssetKind::Folder;
    case LibraryTab::Tool:
        return isMeshFile(path) ? AssetKind::Mesh : AssetKind::Folder;
    case LibraryTab::Brush:
        return isBrushFile(path) ? AssetKind::BrushFile : AssetKind::Folder;
    case LibraryTab::Alpha:
        return isImageFile(path) ? AssetKind::AlphaFile : AssetKind::Folder;
    case LibraryTab::Image:
        return isImageFile(path) ? AssetKind::Image : AssetKind::Folder;
    case LibraryTab::Splats:
        return isSplatFile(path) ? AssetKind::Splat : AssetKind::Folder;
    case LibraryTab::QuickSave:
        return isProjectFile(path) ? AssetKind::Project : AssetKind::Folder;
    case LibraryTab::Recent:
        if (isProjectFile(path)) return AssetKind::Project;
        if (isMeshFile(path)) return AssetKind::Mesh;
        if (isSplatFile(path)) return AssetKind::Splat;
        if (isImageFile(path)) return AssetKind::Image;
        if (isBrushFile(path)) return AssetKind::BrushFile;
        return AssetKind::Folder;
    default:
        return AssetKind::Folder;
    }
}

std::string findBuiltinAssets(const std::string& exeDir) {
    if (exeDir.empty()) return {};
    std::error_code ec;
    fs::path dir = fsPath(exeDir);
    for (int up = 0; up < 4; ++up) {
        for (const char* sub : {"assets", "share/sculptforge/assets", "Resources/assets"}) {
            const fs::path p = dir / sub;
            if (fs::is_directory(p / "demo", ec)) return utf8(p.lexically_normal());
        }
        if (!dir.has_parent_path() || dir.parent_path() == dir) break;
        dir = dir.parent_path();
    }
    return {};
}

std::string defaultUserLibrary(const std::string& documentsDir, const std::string& prefDir) {
    if (!documentsDir.empty()) return utf8(fsPath(documentsDir) / "SculptForge");
    if (!prefDir.empty()) return utf8(fsPath(prefDir) / "library");
    return {};
}

bool ensureUserLibrary(const std::string& root, std::string& error) {
    if (root.empty()) {
        error = "No library folder";
        return false;
    }
    std::error_code ec;
    for (int i = 0; i < int(LibraryTab::Count); ++i) {
        const char* sub = libraryTabFolder(LibraryTab(i));
        if (!*sub) continue;
        fs::create_directories(fsPath(root) / sub, ec);
        if (ec) {
            error = "Cannot create " + utf8(fsPath(root) / sub) + ": " + ec.message();
            return false;
        }
    }
    return true;
}

int64_t modifiedTime(const std::string& path) {
    std::error_code ec;
    const auto t = fs::last_write_time(fsPath(path), ec);
    return ec ? 0 : toEpoch(t);
}

std::vector<AssetEntry> listFolder(const std::string& dir, const std::function<AssetKind(const std::string&)>& accept,
                                   bool newestFirst) {
    std::vector<AssetEntry> folders, files;
    std::error_code ec;
    for (fs::directory_iterator it(fsPath(dir), fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        const fs::path& p = it->path();
        const std::string name = utf8(p.filename());
        if (name.empty() || name[0] == '.') continue;
        std::error_code e2;
        AssetEntry a;
        a.path = utf8(p);
        if (it->is_directory(e2)) {
            a.kind = AssetKind::Folder;
            a.name = name;
            folders.push_back(std::move(a));
            continue;
        }
        if (!it->is_regular_file(e2)) continue;
        a.kind = accept ? accept(a.path) : AssetKind::Folder;
        if (a.kind == AssetKind::Folder) continue;
        a.name = utf8(p.stem());
        a.size = uint64_t(it->file_size(e2));
        a.mtime = toEpoch(it->last_write_time(e2));
        files.push_back(std::move(a));
    }
    auto byName = [](const AssetEntry& a, const AssetEntry& b) { return lessNoCase(a.name, b.name); };
    std::sort(folders.begin(), folders.end(), byName);
    if (newestFirst)
        std::sort(files.begin(), files.end(),
                  [](const AssetEntry& a, const AssetEntry& b) { return a.mtime > b.mtime; });
    else
        std::sort(files.begin(), files.end(), byName);
    folders.insert(folders.end(), std::make_move_iterator(files.begin()), std::make_move_iterator(files.end()));
    return folders;
}

// ---------------------------------------------------------------------------
// FolderScanner
// ---------------------------------------------------------------------------

FolderScanner::~FolderScanner() {
    for (auto& [key, l] : listings_)
        if (l.pending.valid()) l.pending.wait();
}

const std::vector<AssetEntry>* FolderScanner::get(const std::string& dir, const std::string& filterKey, Accept accept,
                                                  bool newestFirst) {
    Listing& l = listings_[dir + "|" + filterKey];
    if (!l.ready && !l.pending.valid()) {
        l.dir = dir;
        l.accept = std::move(accept);
        l.newestFirst = newestFirst;
        l.stamp = modifiedTime(dir);
        l.checkedAt = now_;
        l.pending = std::async(std::launch::async,
                               [dir, a = l.accept, newestFirst] { return listFolder(dir, a, newestFirst); });
    }
    return l.ready ? &l.entries : nullptr;
}

void FolderScanner::poll(double now, double recheckSeconds) {
    now_ = now;
    for (auto& [key, l] : listings_) {
        if (l.pending.valid()) {
            if (l.pending.wait_for(std::chrono::seconds(0)) != std::future_status::ready) continue;
            l.entries = l.pending.get();
            l.ready = true;
            continue;
        }
        if (!l.ready || now - l.checkedAt < recheckSeconds) continue;
        l.checkedAt = now;
        // A folder's time stamp changes when files are added, removed or renamed in it; files
        // rewritten in place are caught by their own time stamps in the thumbnail keys.
        const int64_t stamp = modifiedTime(l.dir);
        if (stamp == l.stamp) continue;
        l.stamp = stamp;
        l.pending = std::async(std::launch::async,
                               [dir = l.dir, a = l.accept, nf = l.newestFirst] { return listFolder(dir, a, nf); });
    }
}

void FolderScanner::invalidate() {
    for (auto& [key, l] : listings_) {
        if (l.pending.valid()) continue; // its result lands soon anyway
        l.stamp = -1;
        l.checkedAt = -1e9;
    }
}

bool FolderScanner::busy() const {
    return std::any_of(listings_.begin(), listings_.end(), [](const auto& kv) { return kv.second.pending.valid(); });
}

// ---------------------------------------------------------------------------
// Thumbnail keys
// ---------------------------------------------------------------------------

uint64_t fnv1a64(std::string_view data, uint64_t seed) {
    uint64_t h = seed;
    for (unsigned char c : data) {
        h ^= c;
        h *= 0x100000001b3ull;
    }
    return h;
}

std::string hex64(uint64_t v) {
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(v));
    return buf;
}

std::string fileThumbnailKey(const std::string& kind, const std::string& absPath, uint64_t size, int64_t mtime,
                             int pixels, int version) {
    const std::string text = kind + "|" + absPath + "|" + std::to_string(size) + "|" + std::to_string(mtime) + "|" +
                             std::to_string(pixels) + "|" + std::to_string(version);
    return hex64(fnv1a64(text));
}

std::string thumbnailCachePath(const std::string& cacheDir, const std::string& key) {
    return utf8(fsPath(cacheDir) / (key + ".png"));
}

// ---------------------------------------------------------------------------
// Brush pop-up letters
// ---------------------------------------------------------------------------

std::string typedName(const std::string& name) {
    std::string out;
    for (unsigned char c : name)
        if (std::isalnum(c)) out += char(std::tolower(c));
    return out;
}

std::string typedZBrushAlias(const std::string& typed) {
    // The brushes SculptForge names its own way, under the names ZBrush users type for them.
    static const std::pair<const char*, const char*> kAliases[] = {
        {"sharpcrease", "damstandard"}, {"clayblock", "claybuildup"}, {"tendril", "snakehook"},
        {"elastictendril", "kelvinsnake"}};
    for (const auto& [name, alias] : kAliases)
        if (typed == name) return alias;
    return {};
}

namespace {
bool typedStarts(const std::string& name, const std::string& typed) {
    const std::string n = typedName(name);
    if (n.rfind(typed, 0) == 0) return true;
    const std::string alias = typedZBrushAlias(n);
    return !alias.empty() && alias.rfind(typed, 0) == 0;
}
} // namespace

std::vector<size_t> filterByTyped(const std::vector<std::string>& names, const std::string& typed, bool* substring) {
    std::vector<size_t> out;
    if (substring) *substring = false;
    for (size_t i = 0; i < names.size(); ++i)
        if (typed.empty() || typedStarts(names[i], typed)) out.push_back(i);
    if (!out.empty() || typed.empty()) return out;
    if (substring) *substring = true;
    for (size_t i = 0; i < names.size(); ++i)
        if (typedName(names[i]).find(typed) != std::string::npos) out.push_back(i);
    return out;
}

std::vector<std::pair<char, size_t>> letterBadges(const std::vector<std::string>& names,
                                                  const std::vector<size_t>& kept, const std::string& typed) {
    std::vector<std::pair<char, size_t>> badges;
    if (typed.empty()) return badges;
    auto taken = [&](char c) {
        return std::any_of(badges.begin(), badges.end(), [c](const auto& b) { return b.first == c; });
    };
    auto has = [&](size_t i) {
        return std::any_of(badges.begin(), badges.end(), [i](const auto& b) { return b.second == i; });
    };
    // ZBrush's keys for the brushes both apps have (B S T Standard, B C B for Clay Block, ZBrush's ClayBuildup ...)
    // come first, so the hands that know ZBrush pick the same brush.
    static const std::pair<const char*, const char*> kZBrushKeys[] = {
        {"standard", "st"}, {"smooth", "sm"}, {"snakehook", "sh"}, {"claybuildup", "cb"}, {"clay", "cl"},
        {"damstandard", "ds"}, {"move", "mv"}, {"inflate", "in"}, {"pinch", "pi"}, {"flatten", "fl"},
        {"trim", "td"}, {"layer", "ly"}, {"nudge", "nu"}, {"crease", "cr"}};
    if (typed.size() == 1)
        for (size_t i : kept) {
            const std::string n = typedName(names[i]), alias = typedZBrushAlias(n);
            for (const auto& [name, keys] : kZBrushKeys)
                if ((n == name || alias == name) && keys[0] == typed[0] && !taken(keys[1]) && !has(i)) badges.emplace_back(keys[1], i);
        }
    for (size_t i : kept) {
        if (has(i)) continue;
        std::string n = typedName(names[i]);
        if (n.rfind(typed, 0) != 0) n = typedZBrushAlias(n); // typed by its ZBrush name
        if (n.rfind(typed, 0) != 0) continue;
        for (size_t k = typed.size(); k < n.size(); ++k)
            if (std::isalpha(static_cast<unsigned char>(n[k])) && !taken(n[k])) {
                badges.emplace_back(n[k], i);
                break;
            }
    }
    return badges;
}

// ---------------------------------------------------------------------------
// QuickSave
// ---------------------------------------------------------------------------

std::string quickSaveName(int64_t epochSeconds, int serial) {
    const std::time_t t = std::time_t(epochSeconds);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[64];
    std::strftime(buf, sizeof buf, "Snapshot %Y-%m-%d %H-%M-%S", &tm);
    std::string name = buf;
    if (serial > 0) name += " (" + std::to_string(serial) + ")";
    return name + ".sfproj";
}

std::string nextQuickSavePath(const std::string& folder, int64_t epochSeconds) {
    std::error_code ec;
    for (int serial = 0;; ++serial) {
        const fs::path p = fsPath(folder) / fsPath(quickSaveName(epochSeconds, serial));
        if (!fs::exists(p, ec)) return utf8(p);
    }
}

size_t pruneQuickSaves(const std::string& folder, size_t keep) {
    std::vector<AssetEntry> saves = listFolder(
        folder,
        [](const std::string& p) {
            const std::string name = utf8(fsPath(p).filename());
            return isProjectFile(p) && (name.rfind("Snapshot ", 0) == 0 || name.rfind("QuickSave ", 0) == 0) ? AssetKind::Project : AssetKind::Folder;
        },
        true);
    saves.erase(
        std::remove_if(saves.begin(), saves.end(), [](const AssetEntry& a) { return a.kind != AssetKind::Project; }),
        saves.end());
    // Same second: the name breaks the tie (later names sort after earlier ones).
    std::stable_sort(saves.begin(), saves.end(), [](const AssetEntry& a, const AssetEntry& b) {
        return a.mtime != b.mtime ? a.mtime > b.mtime : a.name > b.name;
    });
    size_t removed = 0;
    std::error_code ec;
    for (size_t i = keep; i < saves.size(); ++i)
        if (fs::remove(fsPath(saves[i].path), ec)) ++removed;
    return removed;
}

} // namespace sf
