// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/MayaBridge.h"

#include "core/Json.h"
#include "io/ObjIO.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace sf {

namespace {

namespace fs = std::filesystem;

fs::path utf8Path(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string pathUtf8(const fs::path& p) {
    auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

// Writes through a temporary file and a rename, so a reader never sees half a file.
bool writeAtomically(const fs::path& p, const std::string& text) {
    fs::path tmp = p;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary);
        if (!f || !f.write(text.data(), std::streamsize(text.size()))) return false;
    }
    std::error_code ec;
    fs::rename(tmp, p, ec);
    if (ec) fs::remove(tmp, ec);
    return !ec;
}

bool isAsciiAlnum(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }

// A plain folder (not a link) inside the exchange folder.
bool plainFolder(const fs::path& p) {
    std::error_code ec;
    const fs::file_status s = fs::symlink_status(p, ec);
    return !ec && fs::is_directory(s);
}

std::string countText(size_t n) { return std::to_string(n) + (n == 1 ? " object" : " objects"); }

} // namespace

std::string mayaInbox(const std::string& folder) { return pathUtf8(utf8Path(folder) / "maya_to_sculptforge"); }
std::string mayaOutbox(const std::string& folder) { return pathUtf8(utf8Path(folder) / "sculptforge_to_maya"); }
std::string mayaManifestPath(const std::string& box) { return pathUtf8(utf8Path(box) / "manifest.json"); }

std::string mayaSafeName(const std::string& name) {
    std::string s;
    for (unsigned char c : name) {
        if (s.size() >= kMayaMaxNameBytes) break;
        s += isAsciiAlnum(c) || c == '_' ? char(c) : '_';
    }
    if (s.empty()) return "Part";
    if (s[0] >= '0' && s[0] <= '9') {
        s.insert(s.begin(), '_');
        if (s.size() > kMayaMaxNameBytes) s.pop_back();
    }
    return s;
}

bool mayaFileNameOk(const std::string& file) {
    if (file.empty() || file.size() > kMayaMaxFileNameBytes || file[0] == '.') return false;
    if (file.find("..") != std::string::npos) return false;
    for (unsigned char c : file)
        if (!isAsciiAlnum(c) && c != '_' && c != '.' && c != '-') return false; // no '/', '\\', ':', spaces...
    if (file.size() < 5) return false;
    std::string ext = file.substr(file.size() - 4);
    for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".obj";
}

bool parseMayaManifest(const std::string& text, MayaManifest& out, std::string& error) {
    if (text.size() > kMayaManifestMaxBytes) {
        error = "The Maya bridge manifest is too big";
        return false;
    }
    Json j;
    if (!Json::parse(text, j, error)) {
        error = "The Maya bridge manifest is not valid JSON: " + error;
        return false;
    }
    if (!j.isObject() || !j["format"].isString() || j["format"].asString() != "sculptforge-maya") {
        error = "Not a SculptForge Maya bridge manifest";
        return false;
    }
    const Json& version = j["version"];
    if (!version.isNumber() || version.asNumber() < 1 || version.asNumber() != std::floor(version.asNumber())) {
        error = "The Maya bridge manifest has no valid version";
        return false;
    }
    if (version.asNumber() > 1) {
        error = "The Maya bridge manifest is from a newer shelf script or SculptForge (version " +
                std::to_string(int(std::min(version.asNumber(), 1e9))) + "); update the older one";
        return false;
    }
    const Json& revision = j["revision"];
    if (!revision.isNumber() || revision.asNumber() < 0 || revision.asNumber() > 9007199254740992.0 ||
        revision.asNumber() != std::floor(revision.asNumber())) {
        error = "The Maya bridge manifest has no valid revision";
        return false;
    }
    MayaManifest m;
    m.revision = int64_t(revision.asNumber());
    auto optionalString = [&](const char* key, std::string& to) {
        const Json& v = j[key];
        if (v.isNull()) return true;
        if (!v.isString() || v.asString().size() > 64) {
            error = std::string("The Maya bridge manifest has a bad \"") + key + "\"";
            return false;
        }
        to = v.asString();
        return true;
    };
    if (!optionalString("sender", m.sender) || !optionalString("sender_version", m.senderVersion) ||
        !optionalString("unit", m.unit) || !optionalString("writer", m.writer))
        return false;
    m.live = j["live"].isBool() && j["live"].asBool();
    const Json& objects = j["objects"];
    if (!objects.isArray()) {
        error = "The Maya bridge manifest lists no objects";
        return false;
    }
    if (objects.size() > kMayaMaxObjects) {
        error = "The Maya bridge manifest lists too many objects (" + std::to_string(objects.size()) + ", at most " +
                std::to_string(kMayaMaxObjects) + ")";
        return false;
    }
    for (const Json& o : objects.items()) {
        if (!o.isObject() || !o["name"].isString() || !o["file"].isString()) {
            error = "The Maya bridge manifest has an object without a name and a file";
            return false;
        }
        MayaObject b;
        b.file = o["file"].asString();
        if (!mayaFileNameOk(b.file)) {
            error = "The Maya bridge manifest names a file SculptForge won't read: \"" +
                    b.file.substr(0, kMayaMaxFileNameBytes) + "\" (only plain .obj names in the same folder)";
            return false;
        }
        for (const MayaObject& seen : m.objects)
            if (seen.file == b.file) {
                error = "The Maya bridge manifest lists \"" + b.file + "\" twice";
                return false;
            }
        if (o["name"].asString().size() > 4 * kMayaMaxNameBytes) {
            error = "The Maya bridge manifest has a name that is too long";
            return false;
        }
        b.name = mayaSafeName(o["name"].asString());
        m.objects.push_back(std::move(b));
    }
    out = std::move(m);
    return true;
}

std::string writeMayaManifest(const MayaManifest& m) {
    std::string s = "{\n  \"format\": \"sculptforge-maya\",\n  \"version\": 1,\n";
    s += "  \"revision\": " + std::to_string(m.revision) + ",\n";
    s += "  \"sender\": " + jsonQuote(m.sender) + ",\n";
    s += "  \"sender_version\": " + jsonQuote(m.senderVersion) + ",\n";
    if (!m.unit.empty()) s += "  \"unit\": " + jsonQuote(m.unit) + ",\n";
    if (!m.writer.empty()) s += "  \"writer\": " + jsonQuote(m.writer) + ",\n";
    if (m.live) s += "  \"live\": true,\n";
    s += "  \"objects\": [";
    for (size_t i = 0; i < m.objects.size(); ++i) {
        s += i ? ",\n    {" : "\n    {";
        s += "\"name\": " + jsonQuote(m.objects[i].name) + ", \"file\": " + jsonQuote(m.objects[i].file) + "}";
    }
    s += m.objects.empty() ? "]\n}\n" : "\n  ]\n}\n";
    return s;
}

bool readMayaManifest(const std::string& path, MayaManifest& out, std::string& error) {
    const fs::path p = utf8Path(path);
    std::error_code ec;
    const fs::file_status st = fs::symlink_status(p, ec);
    if (ec || !fs::exists(st)) {
        error = "Nothing has been sent yet (no " + path + ")";
        return false;
    }
    if (!fs::is_regular_file(st)) {
        error = path + " is not a plain file";
        return false;
    }
    const auto size = fs::file_size(p, ec);
    if (ec || size > kMayaManifestMaxBytes) {
        error = "The Maya bridge manifest is too big";
        return false;
    }
    std::ifstream f(p, std::ios::binary);
    if (!f) {
        error = "Cannot read " + path;
        return false;
    }
    std::string text(size_t(size), '\0');
    f.read(text.data(), std::streamsize(size));
    text.resize(size_t(f.gcount()));
    return parseMayaManifest(text, out, error);
}

bool mayaReceive(const std::string& folder, std::vector<MeshObject>& out, MayaManifest* manifest,
                 std::string& message) {
    if (folder.empty()) {
        message = "Choose the Maya exchange folder first (File > Maya > Exchange folder...)";
        return false;
    }
    const fs::path inbox = utf8Path(mayaInbox(folder));
    if (!plainFolder(inbox)) {
        message = "Nothing has been sent from Maya yet";
        return false;
    }
    MayaManifest m;
    if (!readMayaManifest(mayaManifestPath(pathUtf8(inbox)), m, message)) {
        if (message.rfind("Nothing", 0) == 0) message = "Nothing has been sent from Maya yet";
        return false;
    }
    if (m.objects.empty()) {
        message = "Maya sent no objects";
        return false;
    }
    std::vector<MeshObject> objects;
    std::vector<std::string> names;
    for (const MayaObject& b : m.objects) {
        const fs::path file = inbox / utf8Path(b.file); // a plain name (checked): stays in the inbox
        std::error_code ec;
        const fs::file_status st = fs::symlink_status(file, ec);
        if (ec || !fs::is_regular_file(st)) {
            message = "Cannot load \"" + b.name + "\" from Maya: " + b.file + " is missing or not a plain file";
            return false;
        }
        if (fs::file_size(file, ec) > kMayaMaxObjBytes || ec) {
            message = "Cannot load \"" + b.name + "\" from Maya: " + b.file + " is too big";
            return false;
        }
        MeshObject obj;
        // Maya's own duplicates (the same short name under different parents): keep them apart.
        obj.name = b.name;
        for (int n = 1; std::find(names.begin(), names.end(), obj.name) != names.end(); ++n)
            obj.name = b.name + std::to_string(n);
        names.push_back(obj.name);
        std::string err;
        if (!loadObj(pathUtf8(file), obj.mesh, err)) {
            message = "Cannot load \"" + obj.name + "\" from Maya: " + err;
            return false;
        }
        if (obj.mesh.triangleCount() == 0) {
            message = "Cannot load \"" + obj.name + "\" from Maya: " + b.file + " has no faces";
            return false;
        }
        objects.push_back(std::move(obj));
    }
    size_t triangles = 0;
    for (const auto& o : objects) triangles += o.mesh.triangleCount();
    message = "Received " + countText(objects.size()) + " from Maya (" + std::to_string(triangles) + " triangles)";
    out = std::move(objects);
    if (manifest) *manifest = std::move(m);
    return true;
}

bool mayaSend(const std::vector<MeshObjectRef>& objects, const std::string& folder, std::string& message,
              std::vector<std::string>* sentNames, int64_t* revision, const std::string& writer, bool live) {
    if (folder.empty()) {
        message = "Choose the Maya exchange folder first (File > Maya > Exchange folder...)";
        return false;
    }
    if (objects.empty()) {
        message = "Nothing visible to send to Maya";
        return false;
    }
    if (objects.size() > kMayaMaxObjects) {
        message = "Too many Parts to send to Maya at once (at most " + std::to_string(kMayaMaxObjects) + ")";
        return false;
    }
    std::error_code ec;
    if (!fs::is_directory(utf8Path(folder), ec)) {
        message = "The Maya exchange folder is gone: " + folder;
        return false;
    }
    const fs::path outbox = utf8Path(mayaOutbox(folder));
    if (fs::exists(fs::symlink_status(outbox, ec)) && !plainFolder(outbox)) {
        message = pathUtf8(outbox) + " is not a plain folder";
        return false;
    }
    fs::create_directories(outbox, ec);
    if (ec) {
        message = "Cannot create " + pathUtf8(outbox) + ": " + ec.message();
        return false;
    }
    MayaManifest m;
    m.revision = std::max<int64_t>(mayaRevision(pathUtf8(outbox)), 0) + 1;
    m.sender = "sculptforge";
    m.writer = writer;
    m.live = live;
#ifdef SCULPTFORGE_VERSION
    m.senderVersion = SCULPTFORGE_VERSION;
#endif
    const std::string prefix = "r" + std::to_string(m.revision) + "_";
    // Nothing already in the way of this send's files may redirect them: a planted link named like one of them
    // (or the manifest's temporary file) is removed, not written through.
    for (const auto& entry : fs::directory_iterator(outbox, ec)) {
        const std::string n = pathUtf8(entry.path().filename());
        if (n.rfind(prefix, 0) == 0 || n == "manifest.json.tmp") {
            std::error_code rm;
            fs::remove(entry.path(), rm);
        }
    }
    std::vector<std::string> names;
    for (size_t k = 0; k < objects.size(); ++k) {
        // Maya's names for duplicates: Name, Name1, Name2...
        const std::string wanted = mayaSafeName(objects[k].name);
        std::string name = wanted;
        for (int n = 1; std::find(names.begin(), names.end(), name) != names.end(); ++n) name = wanted + std::to_string(n);
        names.push_back(name);
        char file[32];
        std::snprintf(file, sizeof file, "%03zu.obj", k);
        MayaObject b{name, prefix + file};
        std::string err;
        const Mesh& mesh = *objects[k].mesh;
        // Written as <file>.tmp, then renamed (a reader polling the folder never sees half of it; a leftover
        // .tmp starts with this send's prefix, so the next send removes it).
        const fs::path final = outbox / utf8Path(b.file);
        fs::path tmp = final;
        tmp += ".tmp";
        bool ok;
        if (mesh.hasFaceGroups()) {
            // Face groups would be OBJ groups, which Maya's importer turns into separate objects or sets.
            Mesh plain = mesh;
            plain.facegroups.clear();
            ok = saveObj(pathUtf8(tmp), plain, err);
        } else {
            ok = saveObj(pathUtf8(tmp), mesh, err);
        }
        if (ok) {
            std::error_code rn;
            fs::rename(tmp, final, rn);
            if (rn) {
                fs::remove(tmp, rn);
                ok = false;
                err = "cannot rename " + pathUtf8(tmp);
            }
        }
        if (!ok) {
            message = "Cannot send \"" + name + "\" to Maya: " + err;
            return false;
        }
        m.objects.push_back(std::move(b));
    }
    if (!writeAtomically(outbox / "manifest.json", writeMayaManifest(m))) {
        message = "Cannot write " + pathUtf8(outbox / "manifest.json");
        return false;
    }
    // Earlier sends' files (r<n>_...): the shelf script has read them, or never will.
    for (const auto& entry : fs::directory_iterator(outbox, ec)) {
        const std::string n = pathUtf8(entry.path().filename());
        size_t digits = 1;
        while (digits < n.size() && n[digits] >= '0' && n[digits] <= '9') ++digits;
        const bool ours = n.size() > 2 && n[0] == 'r' && digits > 1 && digits < n.size() && n[digits] == '_';
        if (ours && n.rfind(prefix, 0) != 0) {
            std::error_code rm;
            fs::remove(entry.path(), rm); // may be open on Windows; the next send tries again
        }
    }
    message = live ? "Live: sent " + countText(objects.size()) + " to Maya"
                   : "Sent " + countText(objects.size()) + " to Maya: click Get from SculptForge on Maya's SculptForge shelf";
    if (sentNames) *sentNames = std::move(names);
    if (revision) *revision = m.revision;
    return true;
}

int64_t mayaRevision(const std::string& box) {
    MayaManifest m;
    std::string err;
    return readMayaManifest(mayaManifestPath(box), m, err) ? m.revision : -1;
}

long long mayaManifestStamp(const std::string& box) {
    if (box.empty()) return 0;
    std::error_code ec;
    const fs::path p = utf8Path(mayaManifestPath(box));
    const auto t = fs::last_write_time(p, ec);
    if (ec) return 0;
    const auto size = fs::file_size(p, ec);
    if (ec) return 0;
    const unsigned long long stamp = static_cast<unsigned long long>(t.time_since_epoch().count()) * 1000003ull + size;
    return static_cast<long long>(stamp | 1); // never 0 for a file that is there
}

std::string mayaNewWriterId() {
    std::random_device rd;
    const unsigned long long v = (static_cast<unsigned long long>(rd()) << 32) ^ rd() ^
                                 static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count());
    char text[17];
    std::snprintf(text, sizeof text, "%016llx", v);
    return text;
}

namespace {

// 64-bit words through a multiply-rotate mix: a few GB/s, enough for a 2M-triangle Part in a few ms.
template <class T>
void hashArray(uint64_t& h, const std::vector<T>& v) {
    const size_t bytes = v.size() * sizeof(T);
    const unsigned char* p = reinterpret_cast<const unsigned char*>(v.data());
    h = (h ^ bytes) * 0x9E3779B97F4A7C15ull;
    size_t i = 0;
    for (; i + 8 <= bytes; i += 8) {
        uint64_t w;
        std::memcpy(&w, p + i, 8);
        h = ((h ^ w) * 0xFF51AFD7ED558CCDull);
        h ^= h >> 29;
    }
    uint64_t tail = 0;
    if (i < bytes) std::memcpy(&tail, p + i, bytes - i);
    h = (h ^ tail) * 0xC4CEB9FE1A85EC53ull;
}

} // namespace

uint64_t mayaMeshHash(const Mesh& mesh) {
    uint64_t h = 0x5346u; // "SF"
    hashArray(h, mesh.positions);
    hashArray(h, mesh.triangles);
    hashArray(h, mesh.quadPartners);
    hashArray(h, mesh.uvs);
    hashArray(h, mesh.colors);
    return h ^ (h >> 31);
}

Mesh mayaSendCopy(const Mesh& mesh) {
    Mesh m;
    m.positions = mesh.positions;
    m.triangles = mesh.triangles;
    m.uvs = mesh.uvs;
    m.colors = mesh.colors;
    // Quads as the exporter sees them (quadPartnerAt: a pair split between face groups is two triangles), since
    // the copy has no face groups.
    if (mesh.hasQuads()) {
        m.quadPartners.resize(mesh.triangleCount());
        for (uint32_t t = 0; t < mesh.triangleCount(); ++t) m.quadPartners[t] = mesh.quadPartnerAt(t);
    }
    return m;
}

namespace {

// The faces an OBJ carries (quads as quads), each starting at its lowest vertex: what survives a round trip
// through another app, which may split a quad along its other diagonal.
std::vector<uint32_t> writtenFaces(const Mesh& m) {
    std::vector<uint32_t> out;
    out.reserve(size_t(m.triangleCount()) * 3);
    for (uint32_t t = 0; t < m.triangleCount(); ++t) {
        glm::uvec4 f;
        const int n = m.exportFace(t, f);
        if (n == 0) continue;
        int first = 0;
        for (int c = 1; c < n; ++c)
            if (f[c] < f[first]) first = c;
        out.push_back(uint32_t(n));
        for (int c = 0; c < n; ++c) out.push_back(f[(first + c) % n]);
    }
    return out;
}

} // namespace

bool mayaMeshesMatch(const Mesh& a, const Mesh& b) {
    if (a.positions.size() != b.positions.size() || a.triangleCount() != b.triangleCount() || a.uvs.size() != b.uvs.size())
        return false;
    const bool sameTriangles = a.triangles == b.triangles;
    if (!sameTriangles && writtenFaces(a) != writtenFaces(b)) return false;
    // OBJ text rounds (7 significant digits from SculptForge, Maya's own from Maya): allow a millionth of each
    // coordinate and a hundred-thousandth of the mesh's size, far below anything a stroke does.
    glm::vec3 lo(0.0f), hi(0.0f);
    if (!a.positions.empty()) lo = hi = a.positions[0];
    for (const glm::vec3& p : a.positions) {
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
    }
    const float size = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, 1e-6f});
    for (size_t i = 0; i < a.positions.size(); ++i) {
        const glm::vec3 d = glm::abs(a.positions[i] - b.positions[i]);
        const float tol = 1e-5f * size + 1e-6f * std::max({std::abs(a.positions[i].x), std::abs(a.positions[i].y),
                                                            std::abs(a.positions[i].z)});
        if (d.x > tol || d.y > tol || d.z > tol) return false;
    }
    for (size_t i = 0; sameTriangles && i < a.uvs.size(); ++i) // (other triangles: their corners' UVs differ in order)
        if (glm::any(glm::greaterThan(glm::abs(a.uvs[i] - b.uvs[i]), glm::vec2(1e-6f)))) return false;
    return true;
}

} // namespace sf
