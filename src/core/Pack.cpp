// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Pack.h"

#include "core/BrushLibrary.h"
#include "core/BrushRecipe.h"
#include "core/Commands.h"
#include "core/Image.h"
#include "core/Macro.h"
#include "core/MaterialRecipe.h"
#include "core/RadialMenu.h"
#include "core/UpdateCheck.h"
#include "core/Vdm.h"
#include "io/Zip.h"

#include <stb_image.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

#ifndef SCULPTFORGE_VERSION
#define SCULPTFORGE_VERSION "0.0.0"
#endif

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
std::string extensionOf(const std::string& name) {
    const size_t slash = name.find_last_of('/');
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return "";
    return lowerAscii(name.substr(dot));
}
std::string stemOf(const std::string& name) {
    std::string file = name.substr(name.find_last_of('/') == std::string::npos ? 0 : name.find_last_of('/') + 1);
    const size_t dot = file.find_last_of('.');
    return dot == std::string::npos || dot == 0 ? file : file.substr(0, dot);
}
std::string text(const std::vector<uint8_t>& data) { return std::string(data.begin(), data.end()); }

std::string g_packsRoot;
bool g_safeMode = false;

uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t rd32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

bool readWholeFile(const std::string& path, std::vector<uint8_t>& out, uint64_t cap, std::string& error) {
    std::error_code ec;
    const fs::path p = u8path(path);
    if (fs::is_symlink(fs::symlink_status(p, ec))) {
        error = path + " is a symbolic link";
        return false;
    }
    if (!fs::is_regular_file(p, ec)) {
        error = "Cannot read " + path;
        return false;
    }
    const uint64_t size = fs::file_size(p, ec);
    if (ec) {
        error = "Cannot read " + path;
        return false;
    }
    if (size > cap) {
        error = fs::path(p).filename().string() + " is too big (" + std::to_string(size >> 20) + " MB; at most " +
                std::to_string(cap >> 20) + " MB)";
        return false;
    }
    std::ifstream f(p, std::ios::binary);
    out.resize(size_t(size));
    if (!f || (size && !f.read(reinterpret_cast<char*>(out.data()), std::streamsize(size)))) {
        error = "Cannot read " + path;
        return false;
    }
    return true;
}

// Text a pack shows (names, author, license, description): valid UTF-8, no control characters (newlines and tabs
// allowed when MULTILINE), at most MAXBYTES.
bool cleanTextOk(const std::string& s, size_t maxBytes, bool multiline) {
    if (s.size() > maxBytes) return false;
    std::string flat = s;
    if (multiline)
        for (char& c : flat)
            if (c == '\n' || c == '\t' || c == '\r') c = ' ';
    return radialCleanText(flat, maxBytes + 1) == flat;
}

bool windowsReserved(const std::string& component) {
    std::string base = component.substr(0, component.find('.'));
    while (!base.empty() && base.back() == ' ') base.pop_back();
    for (char& c : base) c = char(std::toupper(static_cast<unsigned char>(c)));
    static const char* kReserved[] = {"CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4",
                                      "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3",
                                      "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9", "CONIN$", "CONOUT$"};
    for (const char* r : kReserved)
        if (base == r) return true;
    return false;
}

bool packIdOk(const std::string& id) {
    if (id.empty() || id.size() > 64 || id.find("..") != std::string::npos) return false;
    if (!std::isalnum(static_cast<unsigned char>(id[0])) || id.back() == '.') return false;
    for (unsigned char c : id)
        if (!(std::islower(c) || std::isdigit(c) || c == '.' || c == '_' || c == '-')) return false;
    return !windowsReserved(id);
}

bool versionOk(const std::string& v) {
    if (v.empty() || v.size() > 32 || !std::isdigit(static_cast<unsigned char>(v[0]))) return false;
    for (unsigned char c : v)
        if (!(std::isalnum(c) || c == '.' || c == '-' || c == '+')) return false;
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Item types
// ---------------------------------------------------------------------------

const char* packItemTypeId(PackItemType t) {
    switch (t) {
    case PackItemType::Brush: return "brush";
    case PackItemType::Alpha: return "alpha";
    case PackItemType::Matcap: return "matcap";
    case PackItemType::Material: return "material";
    case PackItemType::Vdm: return "vdm";
    case PackItemType::Keymap: return "keymap";
    case PackItemType::PieMenu: return "piemenu";
    case PackItemType::Macro: return "macro";
    case PackItemType::Count: break;
    }
    return "";
}

const char* packItemFolder(PackItemType t) {
    switch (t) {
    case PackItemType::Brush: return "brushes";
    case PackItemType::Alpha: return "alphas";
    case PackItemType::Matcap: return "matcaps";
    case PackItemType::Material: return "materials";
    case PackItemType::Vdm: return "vdms";
    case PackItemType::Keymap: return "keymaps";
    case PackItemType::PieMenu: return "piemenus";
    case PackItemType::Macro: return "macros";
    case PackItemType::Count: break;
    }
    return "";
}

const char* packItemLabel(PackItemType t, int count) {
    const bool one = count == 1;
    switch (t) {
    case PackItemType::Brush: return one ? "brush" : "brushes";
    case PackItemType::Alpha: return one ? "alpha" : "alphas";
    case PackItemType::Matcap: return one ? "Lit Sphere" : "Lit Spheres";
    case PackItemType::Material: return one ? "Material Recipe" : "Material Recipes";
    case PackItemType::Vdm: return one ? "VDM" : "VDMs";
    case PackItemType::Keymap: return one ? "keymap" : "keymaps";
    case PackItemType::PieMenu: return one ? "pie menu" : "pie menus";
    case PackItemType::Macro: return one ? "macro" : "macros";
    case PackItemType::Count: break;
    }
    return "";
}

bool packItemTypeFromId(const std::string& id, PackItemType& out) {
    for (int i = 0; i < kPackItemTypes; ++i)
        if (id == packItemTypeId(PackItemType(i))) {
            out = PackItemType(i);
            return true;
        }
    return false;
}

bool packItemExtensionOk(PackItemType t, const std::string& fileName) {
    const std::string ext = extensionOf(fileName);
    switch (t) {
    case PackItemType::Brush: return ext == ".sfbrush";
    case PackItemType::Alpha:
    case PackItemType::Matcap: return ext == ".png" || ext == ".jpg" || ext == ".jpeg";
    case PackItemType::Material: return ext == kMaterialRecipeExtension;
    case PackItemType::Vdm: return ext == ".sfvdm";
    case PackItemType::Keymap:
    case PackItemType::PieMenu: return ext == ".json";
    case PackItemType::Macro: return ext == kMacroExtension;
    case PackItemType::Count: break;
    }
    return false;
}

std::string packCountsText(const std::array<int, kPackItemTypes>& counts) {
    std::string out;
    for (int i = 0; i < kPackItemTypes; ++i) {
        if (!counts[size_t(i)]) continue;
        if (!out.empty()) out += ", ";
        out += std::to_string(counts[size_t(i)]) + " " + packItemLabel(PackItemType(i), counts[size_t(i)]);
    }
    return out.empty() ? "nothing" : out;
}

// ---------------------------------------------------------------------------
// Versions and the manifest
// ---------------------------------------------------------------------------

std::string appVersion() { return SCULPTFORGE_VERSION; }

bool parsePackManifest(const std::string& textIn, PackManifest& out, std::string& error) {
    out = {};
    if (textIn.size() > 64 * 1024) {
        error = "pack.json is too big (at most 64 KB)";
        return false;
    }
    Json j;
    std::string err;
    if (!Json::parseStrict(textIn, j, err)) {
        error = "pack.json is not valid JSON: " + err;
        return false;
    }
    if (!j.isObject()) {
        error = "pack.json must hold a JSON object";
        return false;
    }
    static const std::set<std::string> kKeys = {"format", "formatVersion", "id",          "name",    "version",
                                                "author", "license",       "description", "minAppVersion",
                                                "preview", "channel",      "contents"};
    for (const auto& [key, v] : j.members())
        if (!kKeys.count(key)) {
            error = "pack.json has an unknown key \"" + radialCleanText(key, 40) + "\"";
            return false;
        }
    auto str = [&](const char* key, bool required, size_t maxBytes, bool multiline, std::string& to) -> bool {
        const Json& v = j[key];
        if (v.isNull() && !j.has(key)) {
            if (required) error = std::string("pack.json is missing \"") + key + "\"";
            return !required;
        }
        if (!v.isString()) {
            error = std::string("pack.json: \"") + key + "\" must be a string";
            return false;
        }
        if (!cleanTextOk(v.asString(), maxBytes, multiline)) {
            error = std::string("pack.json: \"") + key + "\" is too long or holds control characters or invalid text";
            return false;
        }
        to = v.asString();
        if (required && to.empty()) {
            error = std::string("pack.json: \"") + key + "\" is empty";
            return false;
        }
        return true;
    };
    std::string format;
    if (!str("format", true, 32, false, format)) return false;
    if (format != "sculptforge-pack") {
        error = "pack.json: \"format\" must be \"sculptforge-pack\"";
        return false;
    }
    if (!j["formatVersion"].isNumber() || j["formatVersion"].asNumber() != double(kPackFormatVersion)) {
        error = "pack.json: \"formatVersion\" must be " + std::to_string(kPackFormatVersion) +
                " (a newer SculptForge may read this pack)";
        return false;
    }
    if (!str("id", true, 64, false, out.id) || !str("name", true, 64, false, out.name) ||
        !str("version", true, 32, false, out.version) || !str("author", true, 64, false, out.author) ||
        !str("license", true, 64, false, out.license) || !str("description", false, 1000, true, out.description) ||
        !str("minAppVersion", false, 32, false, out.minAppVersion) || !str("preview", false, 32, false, out.preview) ||
        !str("channel", false, 32, false, out.channel))
        return false;
    if (!packIdOk(out.id)) {
        error = "pack.json: \"id\" must be 1-64 lower-case letters, digits, '.', '_' or '-', starting with a letter or digit";
        return false;
    }
    if (!versionOk(out.version)) {
        error = "pack.json: \"version\" must look like 1.0 or 1.2.3";
        return false;
    }
    if (!out.minAppVersion.empty() && !versionOk(out.minAppVersion)) {
        error = "pack.json: \"minAppVersion\" must look like 0.10.6";
        return false;
    }
    if (!out.preview.empty() && out.preview != "preview.png" && out.preview != "preview.jpg") {
        error = "pack.json: \"preview\" must be preview.png or preview.jpg";
        return false;
    }
    for (unsigned char c : out.channel)
        if (!(std::islower(c) || std::isdigit(c) || c == '-')) {
            error = "pack.json: \"channel\" must be lower-case letters, digits or '-' (for example \"insider\")";
            return false;
        }
    const Json& contents = j["contents"];
    if (!contents.isArray()) {
        error = "pack.json: \"contents\" must be a list";
        return false;
    }
    if (contents.size() > PackLimits{}.entries) {
        error = "pack.json lists too many items";
        return false;
    }
    for (size_t i = 0; i < contents.size(); ++i) {
        const Json& it = contents[i];
        const std::string where = "pack.json: contents item " + std::to_string(i + 1);
        if (!it.isObject() || it.members().size() != 2 || !it["type"].isString() || !it["path"].isString()) {
            error = where + " must be {\"type\": ..., \"path\": ...}";
            return false;
        }
        PackItem item;
        if (!packItemTypeFromId(it["type"].asString(), item.type)) {
            error = where + " has an unknown type \"" + radialCleanText(it["type"].asString(), 40) + "\"";
            return false;
        }
        item.path = it["path"].asString();
        std::string nameErr;
        if (!packEntryNameOk(item.path, nameErr)) {
            error = where + ": " + nameErr;
            return false;
        }
        const std::string folder = std::string(packItemFolder(item.type)) + "/";
        if (item.path.compare(0, folder.size(), folder) != 0 || item.path.find('/', folder.size()) != std::string::npos ||
            item.path.size() == folder.size()) {
            error = where + ": a " + packItemTypeId(item.type) + " must be directly in " + folder;
            return false;
        }
        if (!packItemExtensionOk(item.type, item.path)) {
            error = where + ": " + radialCleanText(item.path, 80) + " is not a file type a " + packItemTypeId(item.type) +
                    " may be";
            return false;
        }
        out.contents.push_back(std::move(item));
    }
    return true;
}

std::string packManifestText(const PackManifest& m) {
    Json j = Json::object();
    j.set("format", "sculptforge-pack");
    j.set("formatVersion", kPackFormatVersion);
    j.set("id", m.id);
    j.set("name", m.name);
    j.set("version", m.version);
    j.set("author", m.author);
    j.set("license", m.license);
    if (!m.description.empty()) j.set("description", m.description);
    if (!m.minAppVersion.empty()) j.set("minAppVersion", m.minAppVersion);
    if (!m.preview.empty()) j.set("preview", m.preview);
    if (!m.channel.empty()) j.set("channel", m.channel);
    Json contents = Json::array();
    for (const PackItem& it : m.contents) {
        Json e = Json::object();
        e.set("type", packItemTypeId(it.type));
        e.set("path", it.path);
        contents.push(std::move(e));
    }
    j.set("contents", std::move(contents));
    return j.dump(true) + "\n";
}

// ---------------------------------------------------------------------------
// The strict zip reader
// ---------------------------------------------------------------------------

bool packEntryNameOk(const std::string& name, std::string& error, size_t maxBytes) {
    const std::string shown = radialCleanText(name, 80);
    if (name.empty()) {
        error = "A file in the pack has no name";
        return false;
    }
    if (name.size() > maxBytes) {
        error = "\"" + shown + "...\" has too long a name";
        return false;
    }
    if (radialCleanText(name, name.size() + 1) != name) {
        error = "\"" + shown + "\" has control characters or invalid text in its name";
        return false;
    }
    if (name[0] == '/' || name.find('\\') != std::string::npos) {
        error = "\"" + shown + "\" is an absolute path or uses backslashes";
        return false;
    }
    if (name.find(':') != std::string::npos) {
        error = "\"" + shown + "\" names a drive or a stream (':')";
        return false;
    }
    if (name[0] == '~') {
        error = "\"" + shown + "\" points into a home folder";
        return false;
    }
    std::string body = name;
    if (body.back() == '/') body.pop_back(); // a folder entry
    size_t depth = 0;
    for (size_t start = 0; start <= body.size();) {
        const size_t end = std::min(body.find('/', start), body.size());
        const std::string part = body.substr(start, end - start);
        ++depth;
        if (part.empty()) {
            error = "\"" + shown + "\" has an empty path part";
            return false;
        }
        if (part == "." || part == "..") {
            error = "\"" + shown + "\" leaves its folder ('..')";
            return false;
        }
        if (part[0] == '.' || part[0] == ' ' || part.back() == '.' || part.back() == ' ') {
            error = "\"" + shown + "\" is a hidden name or starts or ends with a dot or space";
            return false;
        }
        for (char c : part)
            if (std::strchr("<>\"|?*", c)) {
                error = "\"" + shown + "\" has a character Windows can't hold in a file name";
                return false;
            }
        if (windowsReserved(part)) {
            error = "\"" + shown + "\" uses a name Windows reserves for devices";
            return false;
        }
        start = end + 1;
    }
    if (depth > 2) {
        error = "\"" + shown + "\" is nested too deep (packs hold folder/file at most)";
        return false;
    }
    return true;
}

bool readPackArchiveBytes(const std::vector<uint8_t>& b, std::vector<PackFile>& files, std::string& error,
                          const PackLimits& limits) {
    files.clear();
    const size_t n = b.size();
    if (n > limits.archiveBytes) {
        error = "The pack is too big (at most " + std::to_string(limits.archiveBytes >> 20) + " MB)";
        return false;
    }
    // End of central directory: the last 22 bytes, or before a comment of up to 64 KB.
    if (n < 22) {
        error = "Not a pack: the file is not a zip archive";
        return false;
    }
    size_t eocd = std::string::npos;
    for (size_t back = 0; back <= std::min<size_t>(n - 22, 0xFFFF); ++back) {
        const size_t at = n - 22 - back;
        if (rd32(&b[at]) == 0x06054B50 && at + 22 + rd16(&b[at + 20]) == n) {
            eocd = at;
            break;
        }
    }
    if (eocd == std::string::npos) {
        error = "Not a pack: the file is not a zip archive (or has data after its end)";
        return false;
    }
    const uint8_t* e = &b[eocd];
    const uint16_t disk = rd16(e + 4), cdDisk = rd16(e + 6), here = rd16(e + 8), total = rd16(e + 10);
    const uint32_t cdSize = rd32(e + 12), cdOffset = rd32(e + 16);
    if (eocd >= 20 && rd32(&b[eocd - 20]) == 0x07064B50) {
        error = "The pack is a ZIP64 archive, which packs never need: refused";
        return false;
    }
    if (disk != 0 || cdDisk != 0 || here != total) {
        error = "The pack is split over several disks: refused";
        return false;
    }
    if (total == 0xFFFF || cdSize == 0xFFFFFFFFu || cdOffset == 0xFFFFFFFFu) {
        error = "The pack is a ZIP64 archive, which packs never need: refused";
        return false;
    }
    if (total > limits.entries) {
        error = "The pack holds too many files (" + std::to_string(total) + "; at most " + std::to_string(limits.entries) + ")";
        return false;
    }
    if (uint64_t(cdOffset) + cdSize != eocd) {
        error = "The pack's zip directory is damaged";
        return false;
    }

    struct Span {
        uint64_t begin, end;
        std::string name;
    };
    std::vector<Span> spans;
    std::set<std::string> seen; // lower-cased: names that differ only in case collide on Windows and macOS
    uint64_t totalUnpacked = 0, totalPacked = 0;
    size_t at = cdOffset;
    for (uint32_t k = 0; k < total; ++k) {
        if (at + 46 > eocd || rd32(&b[at]) != 0x02014B50) {
            error = "The pack's zip directory is damaged";
            return false;
        }
        const uint8_t* c = &b[at];
        const uint16_t madeBy = rd16(c + 4), flags = rd16(c + 8), method = rd16(c + 10);
        const uint32_t crc = rd32(c + 16), packed = rd32(c + 20), unpacked = rd32(c + 24);
        const uint16_t nameLen = rd16(c + 28), extraLen = rd16(c + 30), commentLen = rd16(c + 32), startDisk = rd16(c + 34);
        const uint32_t external = rd32(c + 38), local = rd32(c + 42);
        const size_t next = at + 46 + size_t(nameLen) + extraLen + commentLen;
        if (next > eocd) {
            error = "The pack's zip directory is damaged";
            return false;
        }
        std::string name(reinterpret_cast<const char*>(c + 46), nameLen);
        if (name.find('\0') != std::string::npos) {
            error = "A file name in the pack holds a NUL character: refused";
            return false;
        }
        std::string nameErr;
        if (!packEntryNameOk(name, nameErr, limits.nameBytes)) {
            error = nameErr;
            return false;
        }
        const std::string shown = radialCleanText(name, 80);
        const unsigned mode = external >> 16;
        const unsigned host = madeBy >> 8;
        if ((mode & 0170000) == 0120000) {
            error = "\"" + shown + "\" is a symbolic link: packs may hold plain files only";
            return false;
        }
        if (host == 3 && (mode & 0170000) != 0 && (mode & 0170000) != 0100000 && (mode & 0170000) != 0040000) {
            error = "\"" + shown + "\" is a device or other special file: refused";
            return false;
        }
        if (flags & 0x0041) {
            error = "\"" + shown + "\" is encrypted: packs are never encrypted";
            return false;
        }
        if (method != 0 && method != 8) {
            error = "\"" + shown + "\" uses a compression SculptForge doesn't read (only stored and deflate)";
            return false;
        }
        if (startDisk != 0 || packed == 0xFFFFFFFFu || unpacked == 0xFFFFFFFFu || local == 0xFFFFFFFFu) {
            error = "\"" + shown + "\" uses ZIP64 or several disks: refused";
            return false;
        }
        if (!seen.insert(lowerAscii(name.back() == '/' ? name.substr(0, name.size() - 1) : name)).second) {
            error = "\"" + shown + "\" is in the pack twice (names that differ only in case count as the same)";
            return false;
        }
        if (unpacked > limits.fileBytes) {
            error = "\"" + shown + "\" is too big unpacked (" + std::to_string(unpacked >> 20) + " MB; at most " +
                    std::to_string(limits.fileBytes >> 20) + " MB)";
            return false;
        }
        totalUnpacked += unpacked;
        totalPacked += packed;
        if (totalUnpacked > limits.totalBytes) {
            error = "The pack is too big unpacked (at most " + std::to_string(limits.totalBytes >> 20) + " MB)";
            return false;
        }
        if (method == 0 && packed != unpacked) {
            error = "\"" + shown + "\" is damaged (its sizes disagree)";
            return false;
        }
        if (unpacked > 64 * 1024 && uint64_t(unpacked) > uint64_t(std::max<uint32_t>(packed, 1)) * limits.ratio) {
            error = "\"" + shown + "\" unpacks to more than " + std::to_string(limits.ratio) +
                    " times its packed size (a zip bomb?): refused";
            return false;
        }
        // The local header must agree with the directory.
        if (uint64_t(local) + 30 > cdOffset || rd32(&b[local]) != 0x04034B50) {
            error = "\"" + shown + "\" is damaged (no local header)";
            return false;
        }
        const uint8_t* l = &b[local];
        const uint16_t localFlags = rd16(l + 6), localMethod = rd16(l + 8), localNameLen = rd16(l + 26),
                       localExtra = rd16(l + 28);
        const uint64_t dataStart = uint64_t(local) + 30 + localNameLen + localExtra;
        const uint64_t dataEnd = dataStart + packed;
        if (dataEnd > cdOffset || localMethod != method || (localFlags & 0x0041) ||
            localNameLen != nameLen || std::memcmp(l + 30, name.data(), nameLen) != 0) {
            error = "\"" + shown + "\" is damaged (its local header disagrees with the directory)";
            return false;
        }
        spans.push_back({local, dataEnd + ((localFlags & 0x0008) ? 12 : 0), name}); // (a data descriptor: 12 bytes at least)
        if (name.back() == '/') {
            if (unpacked != 0) {
                error = "\"" + shown + "\" is a folder with data: refused";
                return false;
            }
        } else {
            PackFile f;
            f.name = name;
            f.data.resize(unpacked);
            const uint8_t* src = &b[size_t(dataStart)];
            if (method == 0) {
                if (unpacked) std::memcpy(f.data.data(), src, unpacked);
            } else if (unpacked) {
                // The output buffer is exactly the declared size: data that inflates to more fails here.
                const int got = stbi_zlib_decode_noheader_buffer(reinterpret_cast<char*>(f.data.data()), int(unpacked),
                                                                 reinterpret_cast<const char*>(src), int(packed));
                if (got != int(unpacked)) {
                    error = "\"" + shown + "\" doesn't unpack to its stated size (damaged, or a zip bomb): refused";
                    return false;
                }
            }
            if (crc32(f.data.data(), f.data.size()) != crc) {
                error = "\"" + shown + "\" is damaged (checksum mismatch)";
                return false;
            }
            files.push_back(std::move(f));
        }
        at = next;
    }
    if (at != eocd) {
        error = "The pack's zip directory is damaged";
        return false;
    }
    if (totalUnpacked > 1024 * 1024 && totalUnpacked > std::max<uint64_t>(totalPacked, 1) * limits.ratio) {
        error = "The pack unpacks to more than " + std::to_string(limits.ratio) + " times its size (a zip bomb?): refused";
        return false;
    }
    // No two entries may share bytes (overlapping entries are how some zip bombs multiply their data).
    std::sort(spans.begin(), spans.end(), [](const Span& x, const Span& y) { return x.begin < y.begin; });
    for (size_t i = 1; i < spans.size(); ++i)
        if (spans[i].begin < spans[i - 1].end) {
            error = "\"" + radialCleanText(spans[i].name, 80) + "\" overlaps another file in the archive: refused";
            return false;
        }
    return true;
}

bool readPackArchive(const std::string& path, std::vector<PackFile>& files, std::string& error, const PackLimits& limits) {
    std::vector<uint8_t> bytes;
    if (!readWholeFile(path, bytes, limits.archiveBytes, error)) return false;
    return readPackArchiveBytes(bytes, files, error, limits);
}

// ---------------------------------------------------------------------------
// Checking what a pack holds
// ---------------------------------------------------------------------------

namespace {

bool imageOk(const std::vector<uint8_t>& data, int maxSide, std::string& error, const std::string& shown) {
    int w = 0, h = 0, comp = 0;
    if (data.size() > size_t(INT32_MAX) ||
        !stbi_info_from_memory(data.data(), int(data.size()), &w, &h, &comp)) {
        error = "\"" + shown + "\" is not a readable PNG or JPEG image";
        return false;
    }
    if (w <= 0 || h <= 0 || w > maxSide || h > maxSide) {
        error = "\"" + shown + "\" is " + std::to_string(w) + " x " + std::to_string(h) + " pixels (at most " +
                std::to_string(maxSide) + " a side)";
        return false;
    }
    // The magic bytes must match the extension: no other format hides behind .png.
    const std::string ext = extensionOf(shown);
    const bool png = data.size() > 8 && std::memcmp(data.data(), "\x89PNG\r\n\x1a\n", 8) == 0;
    const bool jpg = data.size() > 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF;
    if ((ext == ".png" && !png) || ((ext == ".jpg" || ext == ".jpeg") && !jpg)) {
        error = "\"" + shown + "\" is not the image type its name says";
        return false;
    }
    Image img;
    std::string err;
    if (!Image::decode(data.data(), data.size(), img, err)) {
        error = "\"" + shown + "\" can't be decoded: " + err;
        return false;
    }
    return true;
}

bool vdmOk(const std::vector<uint8_t>& b, std::string& error, const std::string& shown) {
    auto u32 = [&](size_t at) { return rd32(&b[at]); };
    if (b.size() < 20 || std::memcmp(b.data(), "SFVDM1\n\0", 8) != 0) {
        error = "\"" + shown + "\" is not a SculptForge VDM (.sfvdm)";
        return false;
    }
    const uint32_t w = u32(8), h = u32(12), nameLen = u32(16);
    if (w == 0 || h == 0 || w > 2048 || h > 2048 || nameLen > 1024 || b.size() != 20 + size_t(nameLen) + size_t(w) * h * 12) {
        error = "\"" + shown + "\" is a damaged VDM (bad size)";
        return false;
    }
    const std::string name(reinterpret_cast<const char*>(b.data() + 20), nameLen);
    if (!cleanTextOk(name, 1024, false)) {
        error = "\"" + shown + "\" has an invalid VDM name";
        return false;
    }
    return true;
}

bool checkPackFile(PackItemType type, const PackFile& f, const CommandRegistry* registry, std::string& error) {
    const std::string shown = radialCleanText(f.name, 80);
    switch (type) {
    case PackItemType::Brush: {
        if (f.data.size() > kMaxBrushFileSize) {
            error = "\"" + shown + "\" is too big for a brush";
            return false;
        }
        BrushRecipe r;
        std::string err;
        Json j;
        if (!Json::parseStrict(text(f.data), j, err) || !parseBrushText(text(f.data), r, err)) {
            error = "\"" + shown + "\" is not a valid brush: " + err;
            return false;
        }
        return true;
    }
    case PackItemType::Alpha:
    case PackItemType::Matcap: return imageOk(f.data, 4096, error, f.name);
    case PackItemType::Material: {
        if (f.data.size() > 256 * 1024) {
            error = "\"" + shown + "\" is too big for a Material Recipe";
            return false;
        }
        MaterialRecipe m;
        Json j;
        std::string err;
        if (!Json::parseStrict(text(f.data), j, err) || !parseMaterialRecipe(text(f.data), m, err)) {
            error = "\"" + shown + "\" is not a valid Material Recipe: " + err;
            return false;
        }
        return true;
    }
    case PackItemType::Vdm: return vdmOk(f.data, error, f.name);
    case PackItemType::Keymap: {
        KeymapFile k;
        std::string err;
        if (!parseKeymapFile(text(f.data), k, err)) {
            error = "\"" + shown + "\" is not a valid keymap: " + err;
            return false;
        }
        return true;
    }
    case PackItemType::PieMenu: {
        if (f.data.size() > kRadialFileMax) {
            error = "\"" + shown + "\" is too big for a pie menu";
            return false;
        }
        // The app checks the actions against its own when the menu is added; here: well formed, ids that could be.
        RadialVocabulary any;
        auto idChars = [](std::string_view id) {
            if (id.empty() || id.size() > kRadialIdMax) return false;
            for (unsigned char c : id)
                if (!(std::isalnum(c) || c == '.' || c == '_' || c == '-')) return false;
            return true;
        };
        any.action = idChars;
        any.icon = idChars;
        RadialMenu menu;
        Json j;
        std::string err;
        if (!Json::parseStrict(text(f.data), j, err) || !parseRadialMenuFile(text(f.data), any, menu, err)) {
            error = "\"" + shown + "\" is not a valid pie menu: " + err;
            return false;
        }
        return true;
    }
    case PackItemType::Macro: {
        Macro m;
        std::string err;
        if (f.data.size() > kMaxMacroFileSize) {
            error = "\"" + shown + "\" is too big for a macro";
            return false;
        }
        if (!parseMacro(text(f.data), registry, m, err, stemOf(f.name))) {
            error = "\"" + shown + "\": " + err;
            return false;
        }
        return true;
    }
    case PackItemType::Count: break;
    }
    error = "\"" + shown + "\" has an unknown type";
    return false;
}

bool isLicenseName(const std::string& name) {
    return name == "LICENSE" || name == "LICENSE.txt" || name == "LICENSE.md";
}

} // namespace

bool reviewPackFiles(std::vector<PackFile> files, PackReview& out, std::string& error, const CommandRegistry* registry) {
    const std::string source = out.source;
    out = {};
    out.source = source;
    const PackFile* manifestFile = nullptr;
    for (const PackFile& f : files)
        if (f.name == "pack.json") manifestFile = &f;
    if (!manifestFile) {
        error = "Not a SculptForge pack: there is no pack.json";
        return false;
    }
    if (!parsePackManifest(text(manifestFile->data), out.manifest, error)) return false;
    const PackManifest& m = out.manifest;
    if (!m.minAppVersion.empty() && compareVersions(m.minAppVersion, appVersion()) > 0) {
        out.needsNewerApp = true;
        error = "\"" + m.name + "\" needs SculptForge " + m.minAppVersion + " or newer (this is " + appVersion() + ")";
        return false;
    }
    std::map<std::string, PackItemType> listed;
    for (const PackItem& it : m.contents)
        if (!listed.emplace(it.path, it.type).second) {
            error = "pack.json lists " + radialCleanText(it.path, 80) + " twice";
            return false;
        }
    std::set<std::string> present;
    bool haveLicense = false;
    for (const PackFile& f : files) {
        const std::string shown = radialCleanText(f.name, 80);
        out.bytes += f.data.size();
        present.insert(f.name);
        if (f.name == "pack.json") continue;
        if (f.name.find('/') == std::string::npos) {
            if (isLicenseName(f.name)) {
                if (haveLicense) {
                    error = "The pack holds more than one license file";
                    return false;
                }
                haveLicense = true;
                if (f.data.size() > 64 * 1024 || !cleanTextOk(text(f.data), 64 * 1024, true)) {
                    error = "\"" + shown + "\" is not a plain text license (or is over 64 KB)";
                    return false;
                }
                out.licenseText = text(f.data);
                continue;
            }
            if (f.name == "preview.png" || f.name == "preview.jpg") {
                if (f.name != m.preview) {
                    error = "\"" + shown + "\" is in the pack but pack.json doesn't name it as the preview";
                    return false;
                }
                if (!imageOk(f.data, 2048, error, f.name)) return false;
                continue;
            }
            error = "\"" + shown + "\" is not a file a pack may hold (allowed: pack.json, LICENSE, a preview image and "
                                   "the item folders)";
            return false;
        }
        const auto it = listed.find(f.name);
        if (it == listed.end()) {
            error = "\"" + shown + "\" is in the pack but not listed in pack.json";
            return false;
        }
        if (!checkPackFile(it->second, f, registry, error)) return false;
        ++out.counts[size_t(it->second)];
        out.itemNames.push_back(std::string(packItemTypeId(it->second)) + ": " + stemOf(f.name));
    }
    for (const PackItem& it : m.contents)
        if (!present.count(it.path)) {
            error = "pack.json lists " + radialCleanText(it.path, 80) + " but the pack doesn't hold it";
            return false;
        }
    if (!m.preview.empty() && !present.count(m.preview)) {
        error = "pack.json names " + m.preview + " as the preview but the pack doesn't hold it";
        return false;
    }
    for (const InstalledPack& p : listInstalledPacks())
        if (p.manifest.id == m.id) out.installedVersion = p.manifest.version;
    out.files = std::move(files);
    return true;
}

bool reviewPack(const std::string& path, PackReview& out, std::string& error, const CommandRegistry* registry,
                const PackLimits& limits) {
    std::vector<PackFile> files;
    if (!readPackArchive(path, files, error, limits)) return false;
    out.source = path;
    return reviewPackFiles(std::move(files), out, error, registry);
}

// ---------------------------------------------------------------------------
// Installed packs
// ---------------------------------------------------------------------------

std::string packsRoot() {
    if (!g_packsRoot.empty()) return g_packsRoot;
    const std::string data = appDataDir();
    return data.empty() ? std::string() : u8str(u8path(data) / "packs");
}
void setPacksRoot(const std::string& dir) { g_packsRoot = dir; }
bool packsSafeMode() { return g_safeMode; }
void setPacksSafeMode(bool on) { g_safeMode = on; }

namespace {

fs::path statePath() { return u8path(packsRoot()) / "packs.json"; }

std::set<std::string> disabledPacks() {
    std::set<std::string> out;
    std::vector<uint8_t> bytes;
    std::string err;
    if (!readWholeFile(u8str(statePath()), bytes, 256 * 1024, err)) return out;
    Json j;
    if (!Json::parseStrict(text(bytes), j, err) || !j.isObject() || !j["disabled"].isArray()) return out;
    for (const Json& id : j["disabled"].items())
        if (id.isString() && packIdOk(id.asString())) out.insert(id.asString());
    return out;
}

bool writeDisabledPacks(const std::set<std::string>& ids, std::string& error) {
    Json list = Json::array();
    for (const std::string& id : ids) list.push(id);
    Json j = Json::object();
    j.set("disabled", std::move(list));
    std::error_code ec;
    fs::create_directories(u8path(packsRoot()), ec);
    const fs::path tmp = u8path(packsRoot()) / "packs.json.tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << j.dump(true) << "\n";
        if (!f) {
            error = "Cannot write " + u8str(tmp);
            return false;
        }
    }
    fs::rename(tmp, statePath(), ec);
    if (ec) {
        error = "Cannot write " + u8str(statePath()) + ": " + ec.message();
        return false;
    }
    return true;
}

// The installed pack folder for ID, checked to be a real folder directly in the packs root.
bool packDir(const std::string& id, fs::path& out, std::string& error) {
    if (!packIdOk(id)) {
        error = "\"" + radialCleanText(id, 64) + "\" is not a pack id";
        return false;
    }
    if (packsRoot().empty()) {
        error = "There is no folder for packs on this system";
        return false;
    }
    out = u8path(packsRoot()) / u8path(id);
    return true;
}

bool readInstalledManifest(const fs::path& dir, PackManifest& m, std::string& error) {
    std::vector<uint8_t> bytes;
    if (!readWholeFile(u8str(dir / "pack.json"), bytes, 64 * 1024, error)) return false;
    return parsePackManifest(text(bytes), m, error);
}

} // namespace

std::vector<InstalledPack> listInstalledPacks(std::vector<std::string>* messages) {
    std::vector<InstalledPack> out;
    const std::string root = packsRoot();
    if (root.empty()) return out;
    const std::set<std::string> disabled = disabledPacks();
    std::error_code ec;
    for (fs::directory_iterator it(u8path(root), ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (it->is_symlink(e2) || !it->is_directory(e2)) continue;
        const std::string id = u8str(it->path().filename());
        if (!packIdOk(id)) continue; // ".staging-..." and anything else that isn't a pack
        InstalledPack p;
        std::string err;
        if (!readInstalledManifest(it->path(), p.manifest, err) || p.manifest.id != id) {
            if (messages) messages->push_back("Skipped pack folder " + id + ": " + (err.empty() ? "its id doesn't match" : err));
            continue;
        }
        p.dir = u8str(it->path());
        p.enabled = !disabled.count(id);
        out.push_back(std::move(p));
    }
    std::sort(out.begin(), out.end(), [](const InstalledPack& a, const InstalledPack& b) {
        return lowerAscii(a.manifest.name) < lowerAscii(b.manifest.name) ||
               (lowerAscii(a.manifest.name) == lowerAscii(b.manifest.name) && a.manifest.id < b.manifest.id);
    });
    return out;
}

bool installPack(const PackReview& review, std::string& error) {
    fs::path dest;
    if (!packDir(review.manifest.id, dest, error)) return false;
    if (review.files.empty()) {
        error = "Nothing to install (review the pack first)";
        return false;
    }
    const fs::path root = u8path(packsRoot());
    std::error_code ec;
    fs::create_directories(root, ec);
    if (ec) {
        error = "Cannot create " + u8str(root) + ": " + ec.message();
        return false;
    }
    // Unpack into a fresh staging folder, then swap it in: a failed install leaves the old version as it was.
    const fs::path staging = root / u8path(".staging-" + review.manifest.id);
    fs::remove_all(staging, ec);
    if (!fs::create_directory(staging, ec) || ec) {
        error = "Cannot create " + u8str(staging);
        return false;
    }
    for (const PackFile& f : review.files) {
        std::string nameErr;
        if (!packEntryNameOk(f.name, nameErr)) { // (checked when read; checked again before anything is written)
            fs::remove_all(staging, ec);
            error = nameErr;
            return false;
        }
        const fs::path target = (staging / u8path(f.name)).lexically_normal();
        const std::string rel = u8str(target.lexically_relative(staging));
        if (rel.empty() || rel.rfind("..", 0) == 0 || target.is_absolute() != staging.is_absolute()) {
            fs::remove_all(staging, ec);
            error = "\"" + radialCleanText(f.name, 80) + "\" would land outside the pack's folder: refused";
            return false;
        }
        fs::create_directories(target.parent_path(), ec);
        if (fs::exists(fs::symlink_status(target, ec))) {
            fs::remove_all(staging, ec);
            error = "\"" + radialCleanText(f.name, 80) + "\" is in the pack twice";
            return false;
        }
        std::ofstream o(target, std::ios::binary | std::ios::trunc);
        if (!f.data.empty()) o.write(reinterpret_cast<const char*>(f.data.data()), std::streamsize(f.data.size()));
        if (!o) {
            fs::remove_all(staging, ec);
            error = "Cannot write " + u8str(target);
            return false;
        }
    }
    const fs::path old = root / u8path(".old-" + review.manifest.id);
    fs::remove_all(old, ec);
    if (fs::exists(fs::symlink_status(dest, ec))) {
        fs::rename(dest, old, ec);
        if (ec) {
            fs::remove_all(staging, ec);
            error = "Cannot replace the installed " + review.manifest.name + ": " + ec.message();
            return false;
        }
    }
    fs::rename(staging, dest, ec);
    if (ec) {
        std::error_code ignore;
        if (fs::exists(old, ignore)) fs::rename(old, dest, ignore);
        fs::remove_all(staging, ignore);
        error = "Cannot install " + review.manifest.name + ": " + ec.message();
        return false;
    }
    fs::remove_all(old, ec);
    std::set<std::string> disabled = disabledPacks();
    if (disabled.erase(review.manifest.id)) return writeDisabledPacks(disabled, error);
    return true;
}

bool removePack(const std::string& id, std::string& error) {
    fs::path dir;
    if (!packDir(id, dir, error)) return false;
    std::error_code ec;
    const auto st = fs::symlink_status(dir, ec);
    if (fs::is_symlink(st)) { // never follow a link out of the packs folder
        fs::remove(dir, ec);
        return true;
    }
    if (!fs::is_directory(st)) {
        error = "No pack \"" + id + "\" is installed";
        return false;
    }
    fs::remove_all(dir, ec);
    if (ec) {
        error = "Cannot remove " + u8str(dir) + ": " + ec.message();
        return false;
    }
    std::set<std::string> disabled = disabledPacks();
    if (disabled.erase(id)) return writeDisabledPacks(disabled, error);
    return true;
}

bool setPackEnabled(const std::string& id, bool enabled, std::string& error) {
    fs::path dir;
    if (!packDir(id, dir, error)) return false;
    std::error_code ec;
    if (!fs::is_directory(fs::symlink_status(dir, ec))) {
        error = "No pack \"" + id + "\" is installed";
        return false;
    }
    std::set<std::string> disabled = disabledPacks();
    if (enabled) disabled.erase(id);
    else disabled.insert(id);
    return writeDisabledPacks(disabled, error);
}

std::vector<PackContentItem> enabledPackItems(PackItemType type) {
    std::vector<PackContentItem> out;
    if (packsSafeMode()) return out;
    for (const InstalledPack& p : listInstalledPacks()) {
        if (!p.enabled) continue;
        for (const PackItem& it : p.manifest.contents) {
            if (it.type != type) continue;
            const fs::path file = u8path(p.dir) / u8path(it.path);
            std::error_code ec;
            if (!fs::is_regular_file(fs::symlink_status(file, ec))) continue; // (changed on disk since)
            PackContentItem c;
            c.type = type;
            c.file = u8str(file);
            c.name = stemOf(it.path);
            c.packId = p.manifest.id;
            c.packName = p.manifest.name;
            out.push_back(std::move(c));
        }
    }
    return out;
}

void applyPacksToBrushLibrary(BrushLibraryPaths& paths) {
    paths.packs.clear();
    if (packsSafeMode()) return;
    for (const InstalledPack& p : listInstalledPacks()) {
        if (!p.enabled) continue;
        bool brushes = false, vdms = false;
        for (const PackItem& it : p.manifest.contents) {
            brushes = brushes || it.type == PackItemType::Brush;
            vdms = vdms || it.type == PackItemType::Vdm;
        }
        if (brushes) paths.packs.push_back({u8str(u8path(p.dir) / "brushes"), p.manifest.name, false});
        if (vdms) paths.packs.push_back({u8str(u8path(p.dir) / "vdms"), p.manifest.name, true});
    }
}

// ---------------------------------------------------------------------------
// Keymaps
// ---------------------------------------------------------------------------

bool parseKeymapFile(const std::string& t, KeymapFile& out, std::string& error) {
    out = {};
    if (t.size() > 128 * 1024) {
        error = "too big (at most 128 KB)";
        return false;
    }
    Json j;
    std::string err;
    if (!Json::parseStrict(t, j, err)) {
        error = "not valid JSON: " + err;
        return false;
    }
    if (!j.isObject() || j["format"].asString() != "sculptforge-keymap" || !j["version"].isNumber() ||
        j["version"].asNumber() != 1.0) {
        error = "not a SculptForge keymap (format \"sculptforge-keymap\", version 1)";
        return false;
    }
    for (const auto& [key, v] : j.members())
        if (key != "format" && key != "version" && key != "name" && key != "shortcuts") {
            error = "unknown key \"" + radialCleanText(key, 40) + "\"";
            return false;
        }
    if (j.has("name")) {
        if (!j["name"].isString() || !cleanTextOk(j["name"].asString(), 64, false)) {
            error = "\"name\" must be a short string";
            return false;
        }
        out.name = j["name"].asString();
    }
    const Json& s = j["shortcuts"];
    if (!s.isObject()) {
        error = "\"shortcuts\" must be an object of action id to key";
        return false;
    }
    if (s.members().size() > kMaxKeymapShortcuts) {
        error = "too many shortcuts (at most " + std::to_string(kMaxKeymapShortcuts) + ")";
        return false;
    }
    for (const auto& [id, chord] : s.members()) {
        bool idOk = !id.empty() && id.size() <= kRadialIdMax;
        for (unsigned char c : id) idOk = idOk && (std::isalnum(c) || c == '.' || c == '_' || c == '-');
        if (!idOk) {
            error = "\"" + radialCleanText(id, 40) + "\" is not an action id";
            return false;
        }
        if (!chord.isString() || !cleanTextOk(chord.asString(), 48, false)) {
            error = "the key of " + id + " must be a short string such as \"Ctrl+Shift+K\"";
            return false;
        }
        out.shortcuts.emplace_back(id, chord.asString());
    }
    return true;
}

std::string keymapFileText(const KeymapFile& k) {
    Json j = Json::object();
    j.set("format", "sculptforge-keymap");
    j.set("version", 1);
    if (!k.name.empty()) j.set("name", k.name);
    Json s = Json::object();
    for (const auto& [id, chord] : k.shortcuts) s.set(id, chord);
    j.set("shortcuts", std::move(s));
    return j.dump(true) + "\n";
}

// ---------------------------------------------------------------------------
// Making a pack
// ---------------------------------------------------------------------------

bool writePack(const std::string& outPath, PackManifest manifest, const std::vector<PackSource>& items,
               const std::string& licenseText, const std::string& previewFile, std::string& error,
               const CommandRegistry* registry) {
    std::vector<ZipEntry> entries;
    manifest.contents.clear();
    std::set<std::string> names;
    for (const PackSource& s : items) {
        ZipEntry e;
        std::string file = s.name;
        if (file.empty()) file = u8str(u8path(s.file).filename());
        e.name = std::string(packItemFolder(s.type)) + "/" + file;
        if (!s.data.empty() || s.file.empty()) e.data = s.data;
        else if (!readWholeFile(s.file, e.data, PackLimits{}.fileBytes, error)) return false;
        if (!names.insert(lowerAscii(e.name)).second) {
            error = "Two items would both be " + e.name + " in the pack: rename one";
            return false;
        }
        manifest.contents.push_back({s.type, e.name});
        entries.push_back(std::move(e));
    }
    if (!licenseText.empty()) {
        ZipEntry e;
        e.name = "LICENSE.txt";
        e.data.assign(licenseText.begin(), licenseText.end());
        entries.push_back(std::move(e));
    }
    manifest.preview.clear();
    if (!previewFile.empty()) {
        ZipEntry e;
        const std::string ext = extensionOf(previewFile);
        e.name = ext == ".png" ? "preview.png" : "preview.jpg";
        if (!readWholeFile(previewFile, e.data, 16ull << 20, error)) return false;
        manifest.preview = e.name;
        entries.push_back(std::move(e));
    }
    if (manifest.minAppVersion.empty()) manifest.minAppVersion = appVersion();
    ZipEntry m;
    m.name = "pack.json";
    const std::string mt = packManifestText(manifest);
    m.data.assign(mt.begin(), mt.end());
    entries.insert(entries.begin(), std::move(m));

    // Check it exactly as an install would before writing anything.
    std::vector<PackFile> files;
    for (const ZipEntry& e : entries) files.push_back({e.name, e.data});
    PackReview review;
    if (!reviewPackFiles(std::move(files), review, error, registry)) return false;
    if (!writeStoredZip(outPath, entries, 1, error)) return false;
    // And read back what was written.
    PackReview again;
    if (!reviewPack(outPath, again, error, registry)) return false;
    return true;
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

void registerPackCommands(CommandRegistry& registry) {
    using T = ParamSpec::Type;
    const CommandRegistry* reg = &registry;
    registry.add({"pack_install",
                  "Install a SculptForge pack (.sfpack): brushes, alphas, Lit Spheres, Material Recipes, VDMs, keymaps, "
                  "pie menus and macros. Packs hold data only and are checked completely first (in the app, a review "
                  "shows what's inside before anything is installed).",
                  {{"path", T::String, "The .sfpack file.", true}},
                  [reg](Document& doc, const Args& a) -> CommandResult {
                      PackReview review;
                      std::string err;
                      if (!reviewPack(argString(a, "path"), review, err, reg)) return {false, err};
                      if (!installPack(review, err)) return {false, err};
                      applyPacksToBrushLibrary(brushLibraryPaths());
                      loadBrushLibrary(doc);
                      return {true, "Installed " + review.manifest.name + " " + review.manifest.version + " by " +
                                        review.manifest.author + ": " + packCountsText(review.counts) +
                                        (packsSafeMode() ? " (not loaded: packs are off this session)" : "")};
                  }});
    registry.add({"pack_list", "List the installed packs: id, name, version, author, license, what they hold and whether "
                               "they are on.",
                  {},
                  [](Document&, const Args&) -> CommandResult {
                      std::vector<std::string> messages;
                      const auto packs = listInstalledPacks(&messages);
                      std::string out = packs.empty() ? "No packs installed"
                                                      : std::to_string(packs.size()) + (packs.size() == 1 ? " pack" : " packs");
                      if (packsSafeMode()) out += " (packs are off this session: --no-packs)";
                      for (const InstalledPack& p : packs) {
                          std::array<int, kPackItemTypes> counts{};
                          for (const PackItem& it : p.manifest.contents) ++counts[size_t(it.type)];
                          out += "\n" + p.manifest.id + "  " + p.manifest.name + " " + p.manifest.version + " by " +
                                 p.manifest.author + " (" + p.manifest.license + ")" +
                                 (p.manifest.channel.empty() ? "" : " [" + p.manifest.channel + "]") + ": " +
                                 packCountsText(counts) + (p.enabled ? "" : " - off");
                      }
                      for (const std::string& m : messages) out += "\n" + m;
                      return {true, out};
                  }});
    registry.add({"pack_remove", "Remove an installed pack and everything it brought (your own files are never touched).",
                  {{"id", T::String, "The pack's id (pack_list shows it).", true}},
                  [](Document& doc, const Args& a) -> CommandResult {
                      std::string err;
                      if (!removePack(argString(a, "id"), err)) return {false, err};
                      applyPacksToBrushLibrary(brushLibraryPaths());
                      loadBrushLibrary(doc);
                      return {true, "Removed pack " + argString(a, "id")};
                  }});
    registry.add({"pack_create",
                  "Make a pack (.sfpack) from a folder laid out like one: brushes/, alphas/, matcaps/, materials/, vdms/, "
                  "keymaps/, piemenus/ and macros/ (each with its own file types), plus LICENSE.txt and preview.png if "
                  "there. Everything is checked exactly as an install would check it.",
                  {{"folder", T::String, "The folder holding the items.", true},
                   {"path", T::String, "The .sfpack to write.", true},
                   {"id", T::String, "Pack id: lower-case letters, digits, '.', '_', '-'.", true},
                   {"name", T::String, "Pack name.", true},
                   {"version", T::String, "Version, e.g. 1.0.", false},
                   {"author", T::String, "Who made it.", true},
                   {"license", T::String, "License, e.g. CC-BY-4.0.", true},
                   {"description", T::String, "What it holds.", false},
                   {"channel", T::String, "insider marks the monthly pack or an Insider drop (display only).", false}},
                  [reg](Document&, const Args& a) -> CommandResult {
                      PackManifest m;
                      m.id = argString(a, "id");
                      m.name = argString(a, "name");
                      m.version = argString(a, "version", "1.0");
                      m.author = argString(a, "author");
                      m.license = argString(a, "license");
                      m.description = argString(a, "description");
                      m.channel = argString(a, "channel");
                      const fs::path dir = u8path(argString(a, "folder"));
                      std::vector<PackSource> items;
                      for (int t = 0; t < kPackItemTypes; ++t) {
                          std::vector<std::string> files;
                          std::error_code ec;
                          for (fs::directory_iterator it(dir / packItemFolder(PackItemType(t)), ec), end; !ec && it != end;
                               it.increment(ec)) {
                              std::error_code e2;
                              if (!it->is_symlink(e2) && it->is_regular_file(e2) &&
                                  packItemExtensionOk(PackItemType(t), u8str(it->path().filename())))
                                  files.push_back(u8str(it->path()));
                          }
                          std::sort(files.begin(), files.end());
                          for (const std::string& f : files) items.push_back({PackItemType(t), f, "", {}});
                      }
                      std::string license, preview, err;
                      for (const char* n : {"LICENSE.txt", "LICENSE", "LICENSE.md"}) {
                          std::vector<uint8_t> b;
                          std::error_code ec;
                          if (license.empty() && fs::is_regular_file(dir / n, ec) && readWholeFile(u8str(dir / n), b, 64 * 1024, err))
                              license = text(b);
                      }
                      for (const char* n : {"preview.png", "preview.jpg"}) {
                          std::error_code ec;
                          if (preview.empty() && fs::is_regular_file(dir / n, ec)) preview = u8str(dir / n);
                      }
                      err.clear();
                      if (!writePack(argString(a, "path"), m, items, license, preview, err, reg)) return {false, err};
                      std::array<int, kPackItemTypes> counts{};
                      for (const PackSource& s : items) ++counts[size_t(s.type)];
                      return {true, "Wrote " + argString(a, "path") + ": " + packCountsText(counts)};
                  }});
    for (const bool on : {true, false})
        registry.add({on ? "pack_enable" : "pack_disable",
                      on ? "Turn an installed pack on: its items show in the lists again."
                         : "Turn an installed pack off without removing it: its items leave the lists.",
                      {{"id", T::String, "The pack's id (pack_list shows it).", true}},
                      [on](Document& doc, const Args& a) -> CommandResult {
                          std::string err;
                          if (!setPackEnabled(argString(a, "id"), on, err)) return {false, err};
                          applyPacksToBrushLibrary(brushLibraryPaths());
                          loadBrushLibrary(doc);
                          return {true, std::string(on ? "Turned on" : "Turned off") + " pack " + argString(a, "id")};
                      }});
}

} // namespace sf
