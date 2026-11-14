// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/BrushSets.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace sf {

namespace fs = std::filesystem;

namespace {

fs::path u8path(const std::string& p) { return fs::path(std::u8string(p.begin(), p.end())); }
std::string u8str(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}
std::string lowerAscii(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

constexpr size_t kMaxItemId = 512;

bool readBrushSetFile(const fs::path& p, BrushSet& out, std::string& error) {
    std::error_code ec;
    const auto size = fs::file_size(p, ec);
    if (ec || size > kMaxBrushSetFileSize) {
        error = ec ? "can't read it" : "larger than 64 KB";
        return false;
    }
    std::ifstream f(p, std::ios::binary);
    if (!f) {
        error = "can't read it";
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    Json j;
    if (!Json::parseStrict(text, j, error) || !brushSetFromJson(j, out, error)) return false;
    out.path = u8str(p);
    return true;
}

} // namespace

std::string brushSetsDir(const BrushLibraryPaths& paths) {
    return paths.userDir.empty() ? std::string() : u8str(u8path(paths.userDir) / "sets");
}

Json toJson(const BrushSet& set) {
    Json j = Json::object();
    j.set("format", "sculptforge.brushset");
    j.set("version", 1);
    j.set("name", set.name);
    Json shelf = Json::array();
    for (const std::string& id : set.shelf) shelf.push(id);
    j.set("shelf", std::move(shelf));
    Json keys = Json::object();
    for (const auto& [id, chord] : set.keys) keys.set(id, chord);
    j.set("keys", std::move(keys));
    return j;
}

bool brushSetFromJson(const Json& j, BrushSet& out, std::string& error) {
    if (!j.isObject() || j["format"].asString() != "sculptforge.brushset") {
        error = "not a SculptForge brush set";
        return false;
    }
    if (j["version"].asNumber(0) > 1) {
        error = "made by a newer SculptForge";
        return false;
    }
    BrushSet s;
    s.name = j["name"].asString();
    if (s.name.empty() || s.name.size() > kMaxBrushSetName) {
        error = "the name must be 1 to 64 characters";
        return false;
    }
    if (!j["shelf"].isArray() || j["shelf"].size() > kMaxBrushSetItems) {
        error = "the shelf must be a list of up to 256 items";
        return false;
    }
    for (const Json& id : j["shelf"].items()) {
        if (!id.isString() || id.asString().empty() || id.asString().size() > kMaxItemId) {
            error = "a shelf item isn't a valid id";
            return false;
        }
        s.shelf.push_back(id.asString());
    }
    if (j.has("keys") && (!j["keys"].isObject() || j["keys"].members().size() > kMaxBrushSetItems)) {
        error = "the keys must be an object of up to 256 entries";
        return false;
    }
    for (const auto& [id, chord] : j["keys"].members()) {
        if (id.empty() || id.size() > kMaxItemId || !chord.isString() || chord.asString().size() > 64) {
            error = "a key entry isn't valid";
            return false;
        }
        s.keys.emplace_back(id, chord.asString());
    }
    out = std::move(s);
    return true;
}

std::vector<BrushSet> loadBrushSets(const std::string& dir, std::vector<std::string>* messages) {
    std::vector<BrushSet> out;
    if (dir.empty()) return out;
    std::error_code ec;
    for (fs::directory_iterator it(u8path(dir), ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2) || lowerAscii(u8str(it->path().extension())) != kBrushSetExtension) continue;
        BrushSet s;
        std::string err;
        if (!readBrushSetFile(it->path(), s, err)) {
            if (messages) messages->push_back(u8str(it->path().filename()) + ": " + err);
            continue;
        }
        // Two files with one name (copied in by hand): the first A-Z file wins.
        auto same = std::find_if(out.begin(), out.end(), [&](const BrushSet& o) { return o.name == s.name; });
        if (same == out.end()) out.push_back(std::move(s));
        else if (s.path < same->path) *same = std::move(s);
    }
    std::sort(out.begin(), out.end(), [](const BrushSet& a, const BrushSet& b) {
        const std::string x = lowerAscii(a.name), y = lowerAscii(b.name);
        return x != y ? x < y : a.name < b.name;
    });
    return out;
}

bool saveBrushSet(const std::string& dir, BrushSet& set, std::string& error) {
    if (dir.empty()) {
        error = "No brush library folder to save brush sets in";
        return false;
    }
    if (set.name.empty() || set.name.size() > kMaxBrushSetName) {
        error = "A brush set's name must be 1 to 64 characters";
        return false;
    }
    std::error_code ec;
    fs::create_directories(u8path(dir), ec);
    // The set's own file when one of that name exists; otherwise a free file name from the set's name.
    fs::path target;
    for (const BrushSet& s : loadBrushSets(dir))
        if (s.name == set.name) target = u8path(s.path);
    if (target.empty()) {
        std::string stem = brushFileName(set.name);
        stem.resize(stem.size() - std::char_traits<char>::length(".sfbrush"));
        for (int n = 1; n <= 1000; ++n) {
            target = u8path(dir) / u8path(n == 1 ? stem + kBrushSetExtension : stem + " " + std::to_string(n) + kBrushSetExtension);
            if (!fs::exists(target, ec)) break;
        }
    }
    // Next to it, then renamed over it: a crash never leaves half a set behind.
    const fs::path tmp = u8path(u8str(target) + ".tmp");
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << toJson(set).dump(true) << '\n';
        if (!f) {
            error = "Can't write " + u8str(tmp);
            return false;
        }
    }
    fs::rename(tmp, target, ec);
    if (ec) {
        fs::remove(tmp, ec);
        error = "Can't write " + u8str(target);
        return false;
    }
    set.path = u8str(target);
    return true;
}

bool deleteBrushSetFile(const BrushSet& set, std::string& error) {
    std::error_code ec;
    if (set.path.empty() || !fs::remove(u8path(set.path), ec) || ec) {
        error = "Can't delete " + (set.path.empty() ? set.name : set.path);
        return false;
    }
    return true;
}

} // namespace sf
