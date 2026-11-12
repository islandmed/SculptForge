// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/BlenderBridge.h"

#include "core/BrushLibrary.h"
#include "core/Json.h"
#include "io/BridgeProtocol.h"
#include "io/BridgeIO.h"
#include "io/PlyIO.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace sf {

namespace {

namespace fs = std::filesystem;

fs::path utf8Path(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string pathUtf8(const fs::path& p) {
    auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::string envValue(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, name) != 0 || !value) return {};
    std::string s = value;
    std::free(value);
    return s;
#else
    const char* v = std::getenv(name);
    return v ? v : "";
#endif
}

bool readFile(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
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

std::string number(double v) {
    char buf[40];
    std::snprintf(buf, sizeof buf, "%.17g", v);
    return buf;
}

double nowSeconds() {
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// A file name the manifest may name: no folders, nothing that leaves the folder.
bool plainFileName(const std::string& name) {
    return !name.empty() && name != "." && name != ".." && name.find_first_of("/\\:") == std::string::npos;
}

// Point (x, y, z) -> (x, z, -y) as a matrix (and its inverse, the transpose).
glm::dmat4 axesBlenderToSf() {
    glm::dmat4 a(0.0);
    a[0][0] = 1.0; // column 0: x -> x
    a[1][2] = -1.0; // column 1: y -> -z
    a[2][1] = 1.0; // column 2: z -> y
    a[3][3] = 1.0;
    return a;
}

} // namespace

std::string blenderExchangeRoot() {
    const std::string custom = envValue("SCULPTFORGE_BLENDER_EXCHANGE");
    if (!custom.empty()) return custom;
    std::string base = appDataDir();
#if !defined(_WIN32) && !defined(__APPLE__)
    if (!envValue("FLATPAK_ID").empty()) {
        const std::string host = envValue("HOST_XDG_DATA_HOME");
        const std::string home = envValue("HOME");
        if (!host.empty() || !home.empty()) base = (host.empty() ? home + "/.local/share" : host) + "/SculptForge/SculptForge";
    }
#endif
    return base.empty() ? std::string() : pathUtf8(utf8Path(base) / "blender");
}

std::string flatpakHostLauncher(const std::string& flatpakInfo) {
    // [Application] name=<id>, [Instance] app-path=<installation>/app/<id>/<arch>/<branch>/<commit>/files
    std::string section, id, appPath;
    std::istringstream in(flatpakInfo);
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() > 2 && line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        if (section == "Application" && key == "name") id = value;
        else if (section == "Instance" && key == "app-path") appPath = value;
    }
    if (id.empty() || appPath.empty()) return {};
    const size_t at = appPath.rfind("/app/" + id + "/");
    if (at == std::string::npos || at == 0) return {};
    return appPath.substr(0, at) + "/exports/bin/" + id;
}

std::string blenderInbox(const std::string& root) { return pathUtf8(utf8Path(root) / "to_sculptforge"); }
std::string blenderOutbox(const std::string& root) { return pathUtf8(utf8Path(root) / "to_blender"); }
std::string bridgeManifestPath(const std::string& folder) { return pathUtf8(utf8Path(folder) / "manifest.json"); }

glm::dvec3 blenderToSculptForge(const glm::dvec3& p) { return {p.x, p.z, -p.y}; }
glm::dvec3 sculptForgeToBlender(const glm::dvec3& p) { return {p.x, -p.z, p.y}; }
glm::dmat4 blenderMatrixToSculptForge(const glm::dmat4& m) {
    const glm::dmat4 a = axesBlenderToSf();
    return a * m * glm::transpose(a);
}

glm::mat4 splatPlacementFromBlender(const glm::dmat4& m) {
    glm::dmat4 t = axesBlenderToSf() * m;
    glm::dmat3 r(t);
    const double det = glm::determinant(r);
    const double scale = std::cbrt(std::abs(det));
    glm::dvec3 x = glm::normalize(r[0]), y = r[1], z;
    y = glm::normalize(y - x * glm::dot(x, y)); // Gram-Schmidt: a sheared or unevenly scaled object keeps its rotation
    z = glm::cross(x, y);
    glm::dmat4 out(1.0);
    if (scale > 1e-12 && std::isfinite(scale) && std::isfinite(x.x + x.y + x.z) && std::isfinite(y.x + y.y + y.z)) {
        out[0] = glm::dvec4(x * scale, 0.0);
        out[1] = glm::dvec4(y * scale, 0.0);
        out[2] = glm::dvec4(z * scale, 0.0);
    }
    out[3] = glm::dvec4(glm::dvec3(t[3]), 1.0);
    return glm::mat4(out);
}

glm::dmat4 splatMatrixToBlender(const glm::mat4& placement) {
    return glm::transpose(axesBlenderToSf()) * glm::dmat4(placement);
}

bool parseBridgeManifest(const std::string& text, BridgeManifest& out, std::string& error) {
    Json j;
    if (!Json::parse(text, j, error)) {
        error = "Bridge manifest is not valid JSON: " + error;
        return false;
    }
    if (!j.isObject() || j["format"].asString() != "sculptforge-bridge") {
        error = "Not a SculptForge bridge manifest";
        return false;
    }
    if (j["version"].asNumber(1) > 1) {
        error = "The bridge manifest is from a newer add-on or SculptForge (version " +
                std::to_string(int(j["version"].asNumber())) + "); update the older one";
        return false;
    }
    // matrix_world: rows, as Blender prints a Matrix: matrix_world[row][column].
    auto parseMatrix = [](const Json& o, const std::string& name, bool& has, glm::dmat4& matrix, std::string& err) {
        const Json& mw = o["matrix_world"];
        if (!mw.isArray()) return true;
        bool ok = mw.size() == 4;
        for (size_t r = 0; ok && r < 4; ++r) {
            ok = mw[r].isArray() && mw[r].size() == 4;
            for (size_t c = 0; ok && c < 4; ++c) {
                ok = mw[r][c].isNumber() && std::isfinite(mw[r][c].asNumber());
                if (ok) matrix[int(c)][int(r)] = mw[r][c].asNumber();
            }
        }
        if (!ok) {
            err = "Bridge manifest has a bad matrix_world for \"" + name + "\"";
            return false;
        }
        has = true;
        return true;
    };
    BridgeManifest m;
    m.revision = int64_t(j["revision"].asNumber(0));
    m.sender = j["sender"].asString();
    m.senderVersion = j["sender_version"].asString();
    for (const Json& o : j["objects"].items()) {
        BridgeObject b;
        b.name = o["name"].asString();
        b.file = o["file"].asString();
        if (!plainFileName(b.file)) {
            error = "Bridge manifest names a bad file: \"" + b.file + "\"";
            return false;
        }
        b.shapes = o["shapes"].asString();
        if (!b.shapes.empty() && !plainFileName(b.shapes)) {
            error = "Bridge manifest names a bad file: \"" + b.shapes + "\"";
            return false;
        }
        if (!parseMatrix(o, b.name, b.hasMatrix, b.matrix, error)) return false;
        m.objects.push_back(std::move(b));
    }
    for (const Json& o : j["splats"].items()) {
        BridgeSplats b;
        b.name = o["name"].asString();
        b.file = o["file"].asString();
        if (!plainFileName(b.file)) {
            error = "Bridge manifest names a bad file: \"" + b.file + "\"";
            return false;
        }
        if (!parseMatrix(o, b.name, b.hasMatrix, b.matrix, error)) return false;
        m.splats.push_back(std::move(b));
    }
    out = std::move(m);
    return true;
}

static std::string matrixRows(const glm::dmat4& m) {
    std::string s = "[";
    for (int r = 0; r < 4; ++r) {
        s += r ? ", [" : "[";
        for (int c = 0; c < 4; ++c) s += (c ? ", " : "") + number(m[c][r]);
        s += "]";
    }
    return s + "]";
}

std::string writeBridgeManifest(const BridgeManifest& m) {
    std::string s = "{\n  \"format\": \"sculptforge-bridge\",\n  \"version\": 1,\n";
    s += "  \"revision\": " + std::to_string(m.revision) + ",\n";
    s += "  \"sender\": " + jsonQuote(m.sender) + ",\n";
    s += "  \"sender_version\": " + jsonQuote(m.senderVersion) + ",\n";
    s += "  \"time\": " + number(nowSeconds()) + ",\n";
    s += "  \"objects\": [";
    for (size_t i = 0; i < m.objects.size(); ++i) {
        const BridgeObject& o = m.objects[i];
        s += i ? ",\n    {" : "\n    {";
        s += "\"name\": " + jsonQuote(o.name) + ", \"file\": " + jsonQuote(o.file);
        if (!o.shapes.empty()) s += ", \"shapes\": " + jsonQuote(o.shapes);
        if (o.hasMatrix) s += ", \"matrix_world\": " + matrixRows(o.matrix);
        s += "}";
    }
    s += m.objects.empty() ? "]" : "\n  ]";
    if (!m.splats.empty()) {
        s += ",\n  \"splats\": [";
        for (size_t i = 0; i < m.splats.size(); ++i) {
            const BridgeSplats& o = m.splats[i];
            s += i ? ",\n    {" : "\n    {";
            s += "\"name\": " + jsonQuote(o.name) + ", \"file\": " + jsonQuote(o.file);
            if (o.hasMatrix) s += ", \"matrix_world\": " + matrixRows(o.matrix);
            s += "}";
        }
        s += "\n  ]";
    }
    s += "\n}\n";
    return s;
}

bool readBridgeManifest(const std::string& path, BridgeManifest& out, std::string& error) {
    std::string text;
    if (!readFile(utf8Path(path), text)) {
        error = "Nothing has been sent yet (no " + path + ")";
        return false;
    }
    return parseBridgeManifest(text, out, error);
}

bool writeBridgeShapes(const std::string& path, const Mesh& mesh, std::string& error) {
    std::string data("SFSHAPES", 8);
    auto u32 = [&](uint32_t v) { data.append(reinterpret_cast<const char*>(&v), 4); };
    auto f32 = [&](float v) { data.append(reinterpret_cast<const char*>(&v), 4); };
    u32(1);
    u32(uint32_t(mesh.vertexCount()));
    u32(uint32_t(mesh.layers.size()));
    for (const SculptLayer& l : mesh.layers) {
        u32(uint32_t(l.name.size()));
        data += l.name;
        f32(l.weight);
        std::vector<uint32_t> idx;
        for (uint32_t v = 0; v < l.delta.size(); ++v)
            if (l.delta[v] != glm::vec3(0.0f)) idx.push_back(v);
        u32(uint32_t(idx.size()));
        for (uint32_t v : idx) u32(v);
        for (uint32_t v : idx) {
            const glm::dvec3 d = sculptForgeToBlender(glm::dvec3(l.delta[v])); // (linear: works for offsets)
            f32(float(d.x));
            f32(float(d.y));
            f32(float(d.z));
        }
    }
    if (!writeAtomically(utf8Path(path), data)) {
        error = "Cannot write " + path;
        return false;
    }
    return true;
}

bool readBridgeShapes(const std::string& path, uint32_t& vertexCount, std::vector<BridgeShape>& out, std::string& error) {
    std::string data;
    if (!readFile(utf8Path(path), data)) {
        error = "Cannot read " + path;
        return false;
    }
    size_t pos = 8;
    bool ok = data.size() >= 20 && data.compare(0, 8, "SFSHAPES") == 0;
    auto u32 = [&]() {
        uint32_t v = 0;
        if (pos + 4 > data.size()) ok = false;
        else std::memcpy(&v, data.data() + pos, 4);
        pos += 4;
        return v;
    };
    auto f32 = [&]() {
        float v = 0.0f;
        if (pos + 4 > data.size()) ok = false;
        else std::memcpy(&v, data.data() + pos, 4);
        pos += 4;
        return v;
    };
    if (ok && u32() != 1) ok = false;
    vertexCount = ok ? u32() : 0;
    const uint32_t count = ok ? u32() : 0;
    out.clear();
    for (uint32_t s = 0; ok && s < count; ++s) {
        BridgeShape shape;
        const uint32_t len = u32();
        if (!ok || pos + len > data.size()) {
            ok = false;
            break;
        }
        shape.name.assign(data, pos, len);
        pos += len;
        shape.weight = f32();
        const uint32_t n = u32();
        if (!ok || n > vertexCount || pos + size_t(n) * 16 > data.size()) {
            ok = false;
            break;
        }
        shape.index.resize(n);
        for (auto& v : shape.index) {
            v = u32();
            if (v >= vertexCount) ok = false;
        }
        shape.offset.resize(n);
        for (auto& o : shape.offset) o = {f32(), f32(), f32()};
        out.push_back(std::move(shape));
    }
    if (!ok) error = path + " is not a valid SculptForge shapes file";
    return ok;
}

bool blenderReceive(const std::string& root, std::vector<MeshObject>& out, BridgeManifest* manifest,
                    std::string& message, SplatCloud* splats, bool* splatsFound) {
    if (splatsFound) *splatsFound = false;
    if (root.empty()) {
        message = "No Blender exchange folder (no user data folder)";
        return false;
    }
    const std::string inbox = blenderInbox(root);
    BridgeManifest m;
    if (!readBridgeManifest(bridgeManifestPath(inbox), m, message)) {
        if (message.rfind("Nothing", 0) == 0) message = "Nothing has been sent from Blender yet";
        return false;
    }
    if (m.objects.empty() && (m.splats.empty() || !splats)) {
        message = m.splats.empty() ? "Blender sent no objects" : "Blender sent only Gaussian splats";
        return false;
    }
    std::vector<MeshObject> objects;
    std::string warnings;
    SplatCloud cloud;
    if (splats && !m.splats.empty()) {
        // A scene holds one capture: the first splat object (the add-on sends one).
        const BridgeSplats& b = m.splats.front();
        Mesh unused;
        PlyContent content = PlyContent::Splats;
        std::string err;
        if (!loadPly(pathUtf8(utf8Path(inbox) / utf8Path(b.file)), unused, cloud, content, err) || content != PlyContent::Splats ||
            cloud.empty()) {
            message = "Cannot load the Gaussian splats \"" + b.name + "\" from Blender: " +
                      (err.empty() ? std::string("the file holds no splats") : err);
            return false;
        }
        cloud.name = b.name.empty() ? "Splats" : b.name;
        cloud.transform = splatPlacementFromBlender(b.hasMatrix ? b.matrix : glm::dmat4(1.0));
        if (m.splats.size() > 1) warnings += "; only the first of " + std::to_string(m.splats.size()) + " splat objects was loaded";
    }
    for (const BridgeObject& b : m.objects) {
        MeshObject obj;
        obj.name = b.name.empty() ? "Object" : b.name;
        std::string err;
        if (!loadBridge(pathUtf8(utf8Path(inbox) / utf8Path(b.file)), obj.mesh, err)) {
            message = "Cannot load \"" + obj.name + "\" from Blender: " + err;
            return false;
        }
        if (!err.empty()) warnings += "; " + obj.name + ": " + err;
        if (b.hasMatrix) {
            // Into the scene: the add-on sends object-space meshes and each object's matrix_world.
            const glm::dmat4 t = blenderMatrixToSculptForge(b.matrix);
            for (glm::vec3& p : obj.mesh.positions) p = glm::vec3(t * glm::dvec4(glm::dvec3(p), 1.0));
            obj.mesh.recomputeAllNormals();
        }
        objects.push_back(std::move(obj));
    }
    size_t triangles = 0;
    for (const auto& o : objects) triangles += o.mesh.triangleCount();
    if (objects.empty()) {
        message = "Received " + std::to_string(cloud.size()) + " Gaussian splats from Blender" + warnings;
    } else {
        message = "Received " + std::to_string(objects.size()) + (objects.size() == 1 ? " object" : " objects") +
                  " from Blender (" + std::to_string(triangles) + " triangles";
        if (!cloud.empty()) message += ", " + std::to_string(cloud.size()) + " Gaussian splats";
        message += ")" + warnings;
    }
    if (!cloud.empty()) {
        if (splats) *splats = std::move(cloud);
        if (splatsFound) *splatsFound = true;
    }
    out = std::move(objects);
    if (manifest) *manifest = std::move(m);
    return true;
}

bool blenderSend(const std::vector<MeshObjectRef>& objects, const std::string& root, std::string& message,
                 std::vector<std::string>* sentNames, int64_t* revision, const SplatCloud* splats) {
    if (root.empty()) {
        message = "No Blender exchange folder (no user data folder)";
        return false;
    }
    const bool sendSplats = splats && !splats->empty();
    if (objects.empty() && !sendSplats) {
        message = "Nothing visible to send to Blender";
        return false;
    }
    const fs::path outbox = utf8Path(blenderOutbox(root));
    std::error_code ec;
    fs::create_directories(outbox, ec);
    if (ec) {
        message = "Cannot create " + pathUtf8(outbox) + ": " + ec.message();
        return false;
    }
    BridgeManifest m;
    m.revision = std::max<int64_t>(bridgeRevision(pathUtf8(outbox)), 0) + 1;
    m.sender = "sculptforge";
#ifdef SCULPTFORGE_VERSION
    m.senderVersion = SCULPTFORGE_VERSION;
#endif
    const std::string prefix = "r" + std::to_string(m.revision) + "_";
    std::vector<std::string> names;
    for (size_t k = 0; k < objects.size(); ++k) {
        // Blender's names for duplicates: Name, Name.001, Name.002...
        const std::string wanted = objects[k].name.empty() ? "Object" : objects[k].name;
        std::string name = wanted;
        for (int n = 1; std::find(names.begin(), names.end(), name) != names.end(); ++n) {
            char suffix[16];
            std::snprintf(suffix, sizeof suffix, ".%03d", n);
            name = wanted + suffix;
        }
        names.push_back(name);
        char file[48];
        std::snprintf(file, sizeof file, "%03zu%s", k, bridgefile::kExt);
        BridgeObject b;
        b.name = name;
        b.file = prefix + file;
        BridgeWriteOptions o;
        o.quads = objects[k].mesh->hasQuads(); // only kept quads: triangles of n-gons must stay triangles
        std::string err;
        const Mesh& sent = *objects[k].mesh;
        bool ok;
        if (sent.layers.empty()) {
            ok = saveBridge(pathUtf8(outbox / utf8Path(b.file)), sent, name, err, o);
        } else {
            // Sculpt layers go as shape keys: the Bridge holds the base shape, the shapes file the offsets.
            Mesh neutral = sent;
            neutral.positions = sent.base;
            neutral.layers.clear();
            neutral.base.clear();
            b.shapes = prefix + std::string(file, std::strlen(file) - 4) + ".shapes";
            ok = saveBridge(pathUtf8(outbox / utf8Path(b.file)), neutral, name, err, o) &&
                 writeBridgeShapes(pathUtf8(outbox / utf8Path(b.shapes)), sent, err);
        }
        if (!ok) {
            message = "Cannot send \"" + name + "\" to Blender: " + err;
            return false;
        }
        m.objects.push_back(std::move(b));
    }
    if (sendSplats) {
        // The splats as they are (no baking: view-dependent colour stays right); their placement goes in the manifest.
        BridgeSplats b;
        b.name = splats->name.empty() ? "Splats" : splats->name;
        b.file = prefix + "splats.ply";
        b.hasMatrix = true;
        b.matrix = splatMatrixToBlender(splats->transform);
        std::string err;
        if (!saveSplatPly(pathUtf8(outbox / utf8Path(b.file)), *splats, err, false)) {
            message = "Cannot send the Gaussian splats to Blender: " + err;
            return false;
        }
        m.splats.push_back(std::move(b));
    }
    if (!writeAtomically(outbox / "manifest.json", writeBridgeManifest(m))) {
        message = "Cannot write " + pathUtf8(outbox / "manifest.json");
        return false;
    }
    // Earlier sends' files (the add-on has read them, or never will).
    for (const auto& entry : fs::directory_iterator(outbox, ec)) {
        const std::string n = pathUtf8(entry.path().filename());
        if (n.size() > 1 && n[0] == 'r' && n.rfind(prefix, 0) != 0 && n.find('_') != std::string::npos) {
            std::error_code rm;
            fs::remove(entry.path(), rm); // may be open on Windows; the next send tries again
        }
    }
    message = "Sent " + std::to_string(objects.size()) + (objects.size() == 1 ? " object" : " objects") +
              (sendSplats ? " and " + std::to_string(splats->size()) + " Gaussian splats" : std::string()) +
              " to Blender: press Receive in Blender's SculptForge panel if it doesn't update by itself";
    if (sentNames) *sentNames = std::move(names);
    if (revision) *revision = m.revision;
    return true;
}

long long bridgeFileTime(const std::string& path) {
    if (path.empty()) return 0;
    std::error_code ec;
    auto t = fs::last_write_time(utf8Path(path), ec);
    return ec ? 0 : static_cast<long long>(t.time_since_epoch().count());
}

int64_t bridgeRevision(const std::string& folder) {
    BridgeManifest m;
    std::string err;
    return readBridgeManifest(bridgeManifestPath(folder), m, err) ? m.revision : -1;
}

void writeBridgeAppInfo(const std::string& root, const std::string& executable, const std::string& version) {
    if (root.empty() || executable.empty()) return;
    const std::string text = "{\n  \"path\": " + jsonQuote(executable) + ",\n  \"version\": " + jsonQuote(version) + "\n}\n";
    const fs::path p = utf8Path(root) / "app.json";
    std::string old;
    if (readFile(p, old) && old == text) return;
    std::error_code ec;
    fs::create_directories(utf8Path(root), ec);
    writeAtomically(p, text);
}

void writeBridgeHeartbeat(const std::string& root, bool remove) {
    if (root.empty()) return;
    const fs::path p = utf8Path(root) / "alive.json";
    std::error_code ec;
    if (remove) {
        fs::remove(p, ec);
        return;
    }
#ifdef _WIN32
    const long long pid = _getpid();
#else
    const long long pid = getpid();
#endif
    writeAtomically(p, "{\"pid\": " + std::to_string(pid) + ", \"time\": " + number(nowSeconds()) + "}\n");
}

} // namespace sf
