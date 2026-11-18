// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/BrushGalleryClient.h"

#include "ai/BrushAssistant.h"
#include "ai/Http.h"
#include "core/Json.h"
#include "core/Sha256.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace sf::ai {

namespace {

namespace fs = std::filesystem;

constexpr size_t kMaxIndexBytes = 8u << 20;
constexpr size_t kMaxPreviewBytes = 2u << 20;
constexpr size_t kMaxBrushBytes = 64u << 10;

fs::path utf8Path(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string pathUtf8(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

// <app data>, the folder that holds ai.json.
fs::path appDataDir() { return utf8Path(defaultAiSettingsPath()).parent_path(); }

bool readFile(const fs::path& path, std::string& out, size_t limit) {
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    if (ec || size > limit) return false;
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

bool writeFile(const fs::path& path, const std::string& data) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    fs::path tmp = path;
    tmp += ".part";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f || !(f << data)) return false;
    }
    fs::rename(tmp, path, ec);
    return !ec;
}

// Reads a gallery file from disk (file:// or a path) or over HTTP(S).
GalleryFetch fetch(const std::string& location, size_t limit) {
    GalleryFetch r;
    const std::string local = galleryLocalPath(location);
    if (!local.empty()) {
        r.ok = readFile(utf8Path(local), r.data, limit);
        if (!r.ok) r.error = "Cannot read " + local;
        return r;
    }
    const HttpResponse resp = httpRequest("GET", location, {"Accept: */*"}, {}, 30);
    if (resp.status == 0) {
        r.error = resp.error.empty() ? "No connection" : resp.error;
    } else if (resp.status != 200) {
        r.error = "The gallery answered HTTP " + std::to_string(resp.status);
    } else if (resp.body.size() > limit) {
        r.error = "The download is too large";
    } else {
        r.ok = true;
        r.data = resp.body;
    }
    return r;
}

std::string fileName(const std::string& relative) {
    const size_t slash = relative.find_last_of('/');
    return slash == std::string::npos ? relative : relative.substr(slash + 1);
}

} // namespace

std::string brushGallerySetting() {
    std::string text;
    if (!readFile(appDataDir() / "brush-gallery.json", text, 64 << 10)) return {};
    Json j;
    std::string err;
    return Json::parse(text, j, err) ? j["url"].asString() : std::string();
}

bool saveBrushGallerySetting(const std::string& location, std::string& error) {
    Json j = Json::object();
    j.set("url", location);
    const fs::path path = appDataDir() / "brush-gallery.json";
    if (!writeFile(path, j.dump(true) + "\n")) {
        error = "Cannot write " + pathUtf8(path);
        return false;
    }
    return true;
}

bool brushGalleryFromEnvironment() {
    const char* env = std::getenv("SCULPTFORGE_BRUSH_GALLERY");
    return env && *env;
}

std::string brushGalleryLocation() {
    if (brushGalleryFromEnvironment()) return galleryIndexLocation(std::getenv("SCULPTFORGE_BRUSH_GALLERY"));
    return galleryIndexLocation(brushGallerySetting());
}

std::string brushGalleryCacheDir(const std::string& indexLocation) {
    return pathUtf8(appDataDir() / "brush-gallery-cache" / sha256Hex(indexLocation).substr(0, 16));
}

GalleryFetch fetchGalleryIndex(const std::string& indexLocation) {
    const fs::path cached = utf8Path(brushGalleryCacheDir(indexLocation)) / "index.json";
    GalleryFetch r = fetch(indexLocation, kMaxIndexBytes);
    GalleryIndex parsed;
    std::string parseError;
    if (r.ok && !parseGalleryIndex(r.data, parsed, parseError)) {
        r.ok = false;
        r.error = parseError;
        return r; // a broken index is reported, never replaced by an old copy silently
    }
    if (r.ok) {
        writeFile(cached, r.data);
        return r;
    }
    GalleryFetch old;
    if (readFile(cached, old.data, kMaxIndexBytes)) {
        old.ok = true;
        old.fromCache = true;
        old.error = r.error;
        return old;
    }
    return r;
}

GalleryFetch fetchGalleryPreview(const std::string& indexLocation, const GalleryBrush& brush) {
    GalleryFetch r;
    if (brush.preview.empty()) {
        r.error = "No preview";
        return r;
    }
    const fs::path cached = utf8Path(brushGalleryCacheDir(indexLocation)) / "previews" /
                            (brush.sha256.substr(0, 16) + "-" + fileName(brush.preview));
    if (readFile(cached, r.data, kMaxPreviewBytes)) {
        r.ok = true;
        return r;
    }
    r = fetch(resolveGalleryPath(indexLocation, brush.preview), kMaxPreviewBytes);
    if (r.ok) writeFile(cached, r.data);
    return r;
}

bool downloadGalleryBrush(const std::string& indexLocation, const GalleryBrush& brush, std::string& localPath,
                          std::string& error) {
    const GalleryFetch r = fetch(resolveGalleryPath(indexLocation, brush.file), kMaxBrushBytes);
    if (!r.ok) {
        error = r.error;
        return false;
    }
    if (!galleryHashMatches(r.data, brush.sha256)) {
        error = "'" + brush.name + "' does not match the gallery's checksum - not installed";
        return false;
    }
    const fs::path path = utf8Path(brushGalleryCacheDir(indexLocation)) / "brushes" / fileName(brush.file);
    if (!writeFile(path, r.data)) {
        error = "Cannot write " + pathUtf8(path);
        return false;
    }
    localPath = pathUtf8(path);
    return true;
}

CommandResult installGalleryBrushFile(const CommandRegistry& registry, Document& doc, const std::string& path) {
    if (registry.find("brush_install")) // the library tags it so the brush list shows where it came from
        return registry.execute(doc, "brush_install", {{"path", path}, {"source", std::string("gallery")}});
    return registry.execute(doc, "brush_load", {{"path", path}});
}

void registerBrushGalleryCommands(CommandRegistry& registry) {
    using T = ParamSpec::Type;
    // Reads and parses the configured gallery's index (or `gallery`, if given).
    auto loadIndex = [](const Args& a, std::string& location, GalleryIndex& index, std::string& error) {
        const std::string given = argString(a, "gallery");
        location = given.empty() ? brushGalleryLocation() : galleryIndexLocation(given);
        const GalleryFetch r = fetchGalleryIndex(location);
        if (!r.ok) {
            error = "Cannot reach the brush gallery (" + r.error + ")";
            return false;
        }
        return parseGalleryIndex(r.data, index, error);
    };
    const ParamSpec galleryParam{"gallery", T::String,
                                 "Gallery index URL or folder (default: $SCULPTFORGE_BRUSH_GALLERY or the community "
                                 "gallery).",
                                 false};

    registry.add({"brush_gallery_search",
                  "Search the community brush gallery (shared, CC0 sculpting brushes). Lists name, tags and "
                  "description; install one with brush_gallery_install.",
                  {{"query", T::String, "Words to look for in names, descriptions and tags (default: all).", false},
                   {"tag", T::String, "Only brushes with this tag, e.g. skin, stone, creature.", false},
                   galleryParam},
                  [loadIndex](Document&, const Args& a) -> CommandResult {
                      std::string location, error;
                      GalleryIndex index;
                      if (!loadIndex(a, location, index, error)) return {false, error};
                      std::string out;
                      int n = 0;
                      for (const auto& b : index.brushes) {
                          if (!galleryBrushMatches(b, argString(a, "query"), argString(a, "tag"))) continue;
                          std::string tags;
                          for (const auto& t : b.tags) tags += (tags.empty() ? "" : ", ") + t;
                          out += (n++ ? "\n" : "") + b.name + " [" + tags + "] by " + b.author + " - " + b.description;
                      }
                      if (!n) return {true, "No gallery brushes match"};
                      return {true, out};
                  }});

    registry.add({"brush_gallery_install",
                  "Download a brush from the community gallery (checked against its SHA-256), add it to the "
                  "library and make it the active brush.",
                  {{"name", T::String, "Brush name as listed by brush_gallery_search.", true}, galleryParam},
                  // The registry owns this command, so it outlives the reference.
                  [loadIndex, &registry](Document& doc, const Args& a) -> CommandResult {
                      std::string location, error;
                      GalleryIndex index;
                      if (!loadIndex(a, location, index, error)) return {false, error};
                      const std::string name = argString(a, "name");
                      const GalleryBrush* found = nullptr;
                      for (const auto& b : index.brushes)
                          if (b.name.size() == name.size() &&
                              std::equal(b.name.begin(), b.name.end(), name.begin(), [](char x, char y) {
                                  return std::tolower(static_cast<unsigned char>(x)) ==
                                         std::tolower(static_cast<unsigned char>(y));
                              }))
                              found = &b;
                      if (!found) return {false, "The gallery has no brush called '" + name + "'"};
                      std::string path;
                      if (!downloadGalleryBrush(location, *found, path, error)) return {false, error};
                      return installGalleryBrushFile(registry, doc, path);
                  }});
}

} // namespace sf::ai
