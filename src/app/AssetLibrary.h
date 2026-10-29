// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The asset library behind the LightBox: the built-in and user folders, what
// each LightBox tab lists, folder scanning on a worker thread, QuickSave files
// and the thumbnail cache keys. No GL or ImGui here, so the unit tests link it.
#pragma once

#include <cstdint>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sf {

enum class AssetKind {
    Folder,    // a directory (double-click enters it)
    Up,        // the ".." tile at the start of a sub-folder
    Project,   // .sfproj
    Mesh,      // .obj .glb .gltf .fbx .stl .ply .usda .usdz .GoZ
    Primitive, // built-in Sphere / Cube / Plane (index = Primitive)
    BrushType, // built-in brush (index = BrushType, kPaintBrush = the Paint brush)
    Recipe,    // custom brush in the document (index into doc.brushes)
    BrushFile, // .sfbrush not loaded yet
    Alpha,     // built-in alpha (name)
    AlphaFile, // image used as an alpha
    Material,  // matcap (index; -1 = Studio)
    Image,     // reference image
    Splat,     // Gaussian splat capture (.ply .splat, splat .glb)
};

enum class Primitive { Sphere, Cube, Plane, Count };
const char* primitiveName(Primitive p);
constexpr int kPaintBrush = 1000; // AssetKind::BrushType index of the Paint brush

struct AssetEntry {
    AssetKind kind = AssetKind::Folder;
    std::string name; // shown under the tile
    std::string path; // absolute UTF-8 path (files and folders); empty for built-in items
    int index = -1;
    uint64_t size = 0; // bytes (files)
    int64_t mtime = 0; // seconds since the epoch (files)
};

// The LightBox tabs, in the order they are shown.
enum class LibraryTab { Project, Tool, Brush, Alpha, Material, Image, Splats, QuickSave, Recent, Count };
const char* libraryTabName(LibraryTab tab);       // "Project"
const char* libraryTabId(LibraryTab tab);         // "project" (UiLayout::lightboxTab)
LibraryTab libraryTabFromId(std::string_view id); // unknown -> Project
// User-library sub-folder a tab browses ("Projects", "Tools", ...); "" for tabs without one.
const char* libraryTabFolder(LibraryTab tab);

// File types (case-insensitive extension checks).
bool isMeshFile(const std::string& path);
bool isSplatFile(const std::string& path); // .ply/.splat: the Splats tab (.ply also opens as a mesh)
bool isImageFile(const std::string& path);
bool isBrushFile(const std::string& path);
bool isProjectFile(const std::string& path);
// Kind of a file for a tab (Recent and QuickSave mix kinds); Folder for files the tab doesn't show.
AssetKind fileKindForTab(LibraryTab tab, const std::string& path);

struct LibraryRoots {
    std::string builtin; // bundled assets (contains demo/); may be empty
    std::string user;    // Documents/SculptForge (created on first use)
};
// The bundled assets folder: "assets" next to the executable or up to three levels above it.
std::string findBuiltinAssets(const std::string& exeDir);
// SDL's Documents folder + "SculptForge"; falls back to `prefDir` + "library".
std::string defaultUserLibrary(const std::string& documentsDir, const std::string& prefDir);
// Creates the root and Projects/ Tools/ Brushes/ Alphas/ Images/ Splats/ QuickSave/. False on failure.
bool ensureUserLibrary(const std::string& root, std::string& error);

// Directory listing: sub-folders (sorted by name), then the files `accept` keeps, sorted by name or
// newest first. Hidden files (".x") are skipped. Never throws.
std::vector<AssetEntry> listFolder(const std::string& dir, const std::function<AssetKind(const std::string&)>& accept,
                                   bool newestFirst = false);
// Seconds since the epoch of the last change of `path` (0 if missing).
int64_t modifiedTime(const std::string& path);

// Lists folders on a worker thread. get() never blocks: it returns the last finished listing (or
// nullptr before the first one) and starts a scan when there is none or the folder changed. Call
// poll() once per frame; it collects finished scans and re-checks each folder's time stamp every
// `recheckSeconds` (a cheap stat, as ZBrush's LightBox refreshes while open).
class FolderScanner {
public:
    using Accept = std::function<AssetKind(const std::string&)>;
    ~FolderScanner();
    const std::vector<AssetEntry>* get(const std::string& dir, const std::string& filterKey, Accept accept,
                                       bool newestFirst = false);
    void poll(double now, double recheckSeconds = 5.0);
    void invalidate(); // re-scan everything on the next get()
    bool busy() const;

private:
    struct Listing {
        std::vector<AssetEntry> entries;
        bool ready = false;
        int64_t stamp = 0;    // folder mtime the listing was made at
        double checkedAt = 0; // last stat
        Accept accept;
        bool newestFirst = false;
        std::string dir;
        std::future<std::vector<AssetEntry>> pending;
    };
    std::map<std::string, Listing> listings_; // by dir + "|" + filterKey
    double now_ = 0;
};

// --- Thumbnail cache keys (spec: fnv1a64(kind|absPath|size|mtime|version)) ---

constexpr int kThumbnailVersion = 2; // bump when thumbnails are rendered differently
uint64_t fnv1a64(std::string_view data, uint64_t seed = 0xcbf29ce484222325ull);
std::string hex64(uint64_t v); // 16 lower-case hex digits
// Key of a file's thumbnail: changes when the file is rewritten (size or time) or the renderer changes.
std::string fileThumbnailKey(const std::string& kind, const std::string& absPath, uint64_t size, int64_t mtime,
                             int pixels, int version = kThumbnailVersion);
// <cacheDir>/<key>.png
std::string thumbnailCachePath(const std::string& cacheDir, const std::string& key);

// --- Brush pop-up letters (ZBrush: B, then S, then T picks Standard) ---

// Lower-case letters and digits only ("Clay Buildup" -> "claybuildup"): what a name is typed as.
std::string typedName(const std::string& name);
// Indices of `names` the typed letters keep: names starting with `typed`, or (when none does) names
// containing it (`substring` says which). Empty `typed` keeps all.
std::vector<size_t> filterByTyped(const std::vector<std::string>& names, const std::string& typed,
                                  bool* substring = nullptr);
// The key badge of each kept name: ZBrush's own second letter for the brushes it shares with SculptForge
// (B C B ClayBuildup, B D S DamStandard, B M V Move ...), otherwise the first letter after `typed` in the name
// that no earlier name took. Returns (letter, index into names) pairs; substring matches get none.
std::vector<std::pair<char, size_t>> letterBadges(const std::vector<std::string>& names,
                                                  const std::vector<size_t>& kept, const std::string& typed);

// --- QuickSave (title strip, file.quicksave): timestamped projects in <user library>/QuickSave ---

constexpr size_t kQuickSaveKeep = 10;
// "QuickSave 2026-10-21 23-15-04.sfproj" for a local time; `serial` > 0 appends " (n)".
std::string quickSaveName(int64_t epochSeconds, int serial = 0);
// A path in `folder` that doesn't exist yet.
std::string nextQuickSavePath(const std::string& folder, int64_t epochSeconds);
// Deletes the oldest QuickSave projects beyond `keep`; returns how many were removed.
size_t pruneQuickSaves(const std::string& folder, size_t keep = kQuickSaveKeep);

} // namespace sf
