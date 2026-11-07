// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "link/MayaLink.h"

#include "io/MayaBridge.h"
#include "io/TextureExport.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstring>
#include <random>

namespace sf::link {

namespace {

// Little-endian writing and strict reading.
void putU16(std::string& s, uint16_t v) {
    s += char(v & 0xff);
    s += char(v >> 8);
}
void putU32(std::string& s, uint32_t v) {
    for (int i = 0; i < 4; ++i) s += char((v >> (8 * i)) & 0xff);
}
void putU64(std::string& s, uint64_t v) {
    for (int i = 0; i < 8; ++i) s += char((v >> (8 * i)) & 0xff);
}
template <class T>
void putArray(std::string& s, const std::vector<T>& v) { // (little-endian hosts: every platform we build for)
    s.append(reinterpret_cast<const char*>(v.data()), v.size() * sizeof(T));
}

struct Reader {
    const std::string& s;
    size_t at = 0;
    bool ok = true;
    bool need(size_t n) {
        if (!ok || s.size() - at < n) ok = false;
        return ok;
    }
    uint16_t u16() {
        if (!need(2)) return 0;
        const auto* p = reinterpret_cast<const unsigned char*>(s.data() + at);
        at += 2;
        return uint16_t(p[0] | p[1] << 8);
    }
    uint32_t u32() {
        if (!need(4)) return 0;
        const auto* p = reinterpret_cast<const unsigned char*>(s.data() + at);
        at += 4;
        return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
    }
    uint8_t u8() {
        if (!need(1)) return 0;
        return uint8_t(s[at++]);
    }
    std::string bytes(size_t n) {
        if (!need(n)) return {};
        std::string out = s.substr(at, n);
        at += n;
        return out;
    }
    template <class T>
    bool array(std::vector<T>& v, uint64_t count) {
        if (count > (s.size() - std::min(at, s.size())) / sizeof(T) || !need(size_t(count) * sizeof(T))) return ok = false;
        v.resize(size_t(count));
        if (count) std::memcpy(v.data(), s.data() + at, size_t(count) * sizeof(T));
        at += size_t(count) * sizeof(T);
        return true;
    }
    bool done() const { return ok && at == s.size(); }
};

// A Maya name as the shelf sends it: 1..kMayaMaxNameBytes of letters, digits and '_', not starting with a digit.
bool nameOk(const std::string& n) { return !n.empty() && n.size() <= kMayaMaxNameBytes && mayaSafeName(n) == n; }

bool finite(const std::vector<float>& v) {
    return std::all_of(v.begin(), v.end(), [](float f) { return std::isfinite(f); });
}

std::string readName(Reader& r, std::string& error) {
    const uint16_t n = r.u16();
    std::string name = r.bytes(n);
    if (!r.ok || !nameOk(name)) {
        error = "a bad object name";
        return {};
    }
    return name;
}

} // namespace

std::string mayaLinkNewCode() {
    std::random_device rd; // (the OS's random source)
    std::string code;
    static const char* const kHex = "0123456789abcdef";
    for (size_t i = 0; i < kMayaLinkCodeChars / 8; ++i) {
        const uint32_t v = rd();
        for (int k = 0; k < 8; ++k) code += kHex[(v >> (4 * k)) & 15];
    }
    return code;
}

std::string mayaLinkShowCode(const std::string& code) {
    std::string out;
    for (size_t i = 0; i < code.size(); ++i) {
        if (i && i % 8 == 0) out += '-';
        out += char(std::toupper(static_cast<unsigned char>(code[i])));
    }
    return out;
}

std::string mayaLinkPairingText(const std::string& code, const std::string& endpoint) {
    return "sflink1 " + mayaLinkShowCode(code) + " " + endpoint;
}

bool mayaLinkCodeMatches(const std::string& sent, const std::string& code) {
    std::string s;
    for (unsigned char c : sent)
        if (c != '-' && s.size() <= kMayaLinkCodeChars) s += char(std::tolower(c));
    unsigned diff = s.size() == code.size() && code.size() == kMayaLinkCodeChars ? 0u : 1u;
    for (size_t i = 0; i < kMayaLinkCodeChars; ++i)
        diff |= unsigned(i < s.size() ? s[i] : 0) ^ unsigned(i < code.size() ? code[i] : 1);
    return diff == 0;
}

std::string mayaLinkFrame(MayaLinkType type, const std::string& payload) {
    std::string f = "SFML";
    putU16(f, kMayaLinkVersion);
    putU16(f, uint16_t(type));
    putU64(f, payload.size());
    return f + payload;
}

bool parseMayaLinkHeader(const char* header, MayaLinkType& type, uint64_t& length, std::string& error) {
    const std::string h(header, kMayaLinkHeaderBytes);
    if (h.compare(0, 4, "SFML") != 0) {
        error = "not the Maya link";
        return false;
    }
    Reader r{h, 4};
    const uint16_t version = r.u16();
    const uint16_t t = r.u16();
    uint64_t n = 0;
    for (int i = 0; i < 8; ++i) n |= uint64_t(uint8_t(h[8 + size_t(i)])) << (8 * i);
    if (version != kMayaLinkVersion) {
        error = "link version " + std::to_string(version) + " (this SculptForge speaks " + std::to_string(kMayaLinkVersion) +
                "): update the shelf script or SculptForge";
        return false;
    }
    if (t < uint16_t(MayaLinkType::Hello) || t > uint16_t(MayaLinkType::Bye)) {
        error = "an unknown message";
        return false;
    }
    if (n > kMayaLinkMaxPayload) {
        error = "a message over the size cap";
        return false;
    }
    type = MayaLinkType(t);
    length = n;
    return true;
}

std::string encodeMayaLinkHello(const std::string& code, const std::string& client) {
    std::string p; // the code as SculptForge shows it or bare: no dashes, lower case
    for (unsigned char c : code)
        if (c != '-' && p.size() < kMayaLinkCodeChars) p += char(std::tolower(c));
    p.resize(kMayaLinkCodeChars, '0');
    const std::string c = client.substr(0, 64);
    putU16(p, uint16_t(c.size()));
    return mayaLinkFrame(MayaLinkType::Hello, p + c);
}

bool parseMayaLinkHello(const std::string& payload, std::string& code, std::string& client, std::string& error) {
    Reader r{payload};
    code = r.bytes(kMayaLinkCodeChars);
    const uint16_t n = r.u16();
    if (n > 64) r.ok = false;
    client = r.bytes(n);
    if (!r.done()) {
        error = "a bad hello";
        return false;
    }
    for (char& c : client)
        if (static_cast<unsigned char>(c) < 32 || static_cast<unsigned char>(c) > 126) c = '?';
    return true;
}

std::string encodeMayaLinkWelcome(const std::string& version) {
    std::string p;
    const std::string v = version.substr(0, 64);
    putU16(p, uint16_t(v.size()));
    return mayaLinkFrame(MayaLinkType::Welcome, p + v);
}

std::string encodeMayaLinkMesh(const MayaLinkMesh& m) {
    std::string p;
    p.reserve(32 + m.name.size() + m.positions.size() * 4 + m.faceSizes.size() + m.indices.size() * 4 + m.uvs.size() * 4);
    putU16(p, uint16_t(m.name.size()));
    p += m.name;
    putU32(p, uint32_t(m.positions.size() / 3));
    putU32(p, uint32_t(m.faceSizes.size()));
    putU32(p, uint32_t(m.indices.size()));
    p += char(m.uvs.empty() ? 0 : 1);
    putArray(p, m.positions);
    putArray(p, m.faceSizes);
    putArray(p, m.indices);
    putArray(p, m.uvs);
    return mayaLinkFrame(MayaLinkType::Mesh, p);
}

bool parseMayaLinkMesh(const std::string& payload, MayaLinkMesh& out, std::string& error) {
    Reader r{payload};
    MayaLinkMesh m;
    m.name = readName(r, error);
    if (m.name.empty()) return false;
    const uint32_t v = r.u32(), f = r.u32(), c = r.u32();
    const uint8_t flags = r.u8();
    if (!r.ok || v == 0 || v > kMayaLinkMaxVertices || f == 0 || c < 3ull * f || c > 255ull * f || flags > 1) {
        error = "a mesh with bad counts";
        return false;
    }
    if (!r.array(m.positions, 3ull * v) || !r.array(m.faceSizes, f) || !r.array(m.indices, c) ||
        (flags && !r.array(m.uvs, 2ull * c)) || !r.done()) {
        error = "a mesh whose size doesn't add up";
        return false;
    }
    uint64_t corners = 0;
    for (uint8_t n : m.faceSizes) {
        if (n < 3) {
            error = "a mesh with a face of fewer than 3 corners";
            return false;
        }
        corners += n;
    }
    if (corners != c) {
        error = "a mesh whose faces don't add up";
        return false;
    }
    if (std::any_of(m.indices.begin(), m.indices.end(), [v](uint32_t i) { return i >= v; })) {
        error = "a mesh with a vertex index out of range";
        return false;
    }
    if (!finite(m.positions) || !finite(m.uvs)) {
        error = "a mesh with a number that isn't finite";
        return false;
    }
    out = std::move(m);
    return true;
}

std::string encodeMayaLinkPoints(const MayaLinkPoints& pts) {
    std::string p;
    putU16(p, uint16_t(pts.name.size()));
    p += pts.name;
    putU32(p, pts.vertexCount);
    putU32(p, uint32_t(pts.indices.size()));
    putArray(p, pts.indices);
    putArray(p, pts.positions);
    return mayaLinkFrame(MayaLinkType::Points, p);
}

bool parseMayaLinkPoints(const std::string& payload, MayaLinkPoints& out, std::string& error) {
    Reader r{payload};
    MayaLinkPoints p;
    p.name = readName(r, error);
    if (p.name.empty()) return false;
    p.vertexCount = r.u32();
    const uint32_t n = r.u32();
    if (!r.ok || p.vertexCount == 0 || p.vertexCount > kMayaLinkMaxVertices || n > p.vertexCount ||
        !r.array(p.indices, n) || !r.array(p.positions, 3ull * n) || !r.done()) {
        error = "points whose size doesn't add up";
        return false;
    }
    if (std::any_of(p.indices.begin(), p.indices.end(), [&](uint32_t i) { return i >= p.vertexCount; }) ||
        !finite(p.positions)) {
        error = "points out of range";
        return false;
    }
    out = std::move(p);
    return true;
}

MayaLinkMesh mayaLinkMeshFrom(const std::string& name, const Mesh& mesh) {
    MayaLinkMesh m;
    m.name = mayaSafeName(name);
    m.positions.resize(mesh.positions.size() * 3);
    std::memcpy(m.positions.data(), mesh.positions.data(), m.positions.size() * sizeof(float));
    const bool uvs = hasCornerUvs(mesh);
    m.faceSizes.reserve(mesh.triangleCount());
    m.indices.reserve(size_t(mesh.triangleCount()) * 3);
    for (uint32_t t = 0; t < mesh.triangleCount(); ++t) {
        glm::uvec4 f;
        glm::vec2 uv[4];
        const int corners = exportFaceUv(mesh, t, f, uv);
        if (corners == 0) continue;
        m.faceSizes.push_back(uint8_t(corners));
        for (int c = 0; c < corners; ++c) {
            m.indices.push_back(f[c]);
            if (uvs) {
                m.uvs.push_back(uv[c].x);
                m.uvs.push_back(uv[c].y);
            }
        }
    }
    return m;
}

bool mayaLinkMeshTo(const MayaLinkMesh& in, Mesh& out, std::string& error) {
    Mesh m;
    m.positions.resize(in.positions.size() / 3);
    std::memcpy(m.positions.data(), in.positions.data(), m.positions.size() * sizeof(glm::vec3));
    const bool uvs = !in.uvs.empty();
    std::vector<std::pair<uint32_t, uint32_t>> quads;
    size_t at = 0;
    for (uint8_t n : in.faceSizes) {
        for (uint8_t k = 1; k + 1 < n; ++k) { // a fan: (0, k, k+1)
            const size_t c[3] = {at, at + k, at + k + 1u};
            m.triangles.push_back({in.indices[c[0]], in.indices[c[1]], in.indices[c[2]]});
            if (uvs)
                for (size_t i : c) m.uvs.push_back({in.uvs[2 * i], in.uvs[2 * i + 1]});
        }
        if (n == 4) quads.push_back({uint32_t(m.triangles.size() - 2), uint32_t(m.triangles.size() - 1)});
        at += n;
    }
    // Degenerate triangles (a corner twice) can't be sculpted: dropped, as the OBJ reader does.
    if (m.triangles.empty()) {
        error = "the mesh has no faces";
        return false;
    }
    for (const auto& [a, b] : quads) m.pairQuad(a, b);
    if (m.hasQuads()) m.quadPartners.resize(m.triangles.size(), Mesh::kNoQuad);
    m.rebuildTopology();
    m.recomputeAllNormals();
    out = std::move(m);
    return true;
}

MayaLinkPoints mayaLinkPointsBetween(const std::string& name, const std::vector<glm::vec3>& before,
                                     const std::vector<glm::vec3>& now) {
    MayaLinkPoints p;
    p.name = mayaSafeName(name);
    p.vertexCount = uint32_t(now.size());
    if (before.size() != now.size()) return p;
    for (uint32_t i = 0; i < now.size(); ++i) {
        if (before[i] == now[i]) continue;
        p.indices.push_back(i);
        p.positions.insert(p.positions.end(), {now[i].x, now[i].y, now[i].z});
    }
    return p;
}

// ---------------------------------------------------------------------------
// MayaLinkServer
// ---------------------------------------------------------------------------

bool MayaLinkServer::start(const std::string& endpoint, std::string& error) {
    stop();
    if (!listener_.open(endpoint, error)) {
        if (error.find("another SculptForge") != std::string::npos)
            error = "another SculptForge window has the Maya Live link on";
        return false;
    }
    code_ = mayaLinkNewCode();
    endpoint_ = endpoint;
    refused_ = 0;
    stopping_ = false;
    running_ = true;
    thread_ = std::thread([this] { run(); });
    return true;
}

void MayaLinkServer::stop() {
    if (!running_ && !thread_.joinable()) return;
    stopping_ = true;
    if (thread_.joinable()) thread_.join();
    listener_.close();
    running_ = connected_ = false;
    std::lock_guard<std::mutex> lock(mutex_);
    outbox_.clear();
    inbox_.clear();
    client_.clear();
    code_.clear();
}

std::string MayaLinkServer::clientName() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return client_;
}

void MayaLinkServer::event(std::string text) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (events_.size() < 16) events_.push_back(std::move(text));
}

std::string MayaLinkServer::takeEvent() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (events_.empty()) return {};
    std::string e = std::move(events_.front());
    events_.pop_front();
    return e;
}

void MayaLinkServer::send(MayaLinkMesh mesh) {
    if (!connected_) return;
    std::lock_guard<std::mutex> lock(mutex_);
    std::erase_if(outbox_, [&](const Outgoing& o) { return o.isMesh ? o.mesh.name == mesh.name : o.points.name == mesh.name; });
    outbox_.push_back({true, std::move(mesh), {}});
}

void MayaLinkServer::send(MayaLinkPoints points) {
    if (!connected_ || points.indices.empty()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = outbox_.rbegin(); it != outbox_.rend(); ++it) {
        if (it->isMesh ? it->mesh.name != points.name : it->points.name != points.name) continue;
        if (!it->isMesh && it->points.vertexCount == points.vertexCount) {
            mergeMayaLinkPoints(it->points, points);
            return;
        }
        break; // a Mesh (or another vertex count) queued last: these Points go after it
    }
    outbox_.push_back({false, {}, std::move(points)});
}

void mergeMayaLinkPoints(MayaLinkPoints& into, const MayaLinkPoints& later) {
    std::vector<uint32_t> slot(into.vertexCount, ~0u); // (vertexCount is capped: kMayaLinkMaxVertices)
    for (uint32_t k = 0; k < into.indices.size(); ++k) slot[into.indices[k]] = k;
    for (size_t k = 0; k < later.indices.size(); ++k) {
        const uint32_t v = later.indices[k];
        if (v >= into.vertexCount) continue;
        if (slot[v] == ~0u) {
            slot[v] = uint32_t(into.indices.size());
            into.indices.push_back(v);
            into.positions.insert(into.positions.end(), {0.0f, 0.0f, 0.0f});
        }
        std::memcpy(&into.positions[size_t(slot[v]) * 3], &later.positions[k * 3], 3 * sizeof(float));
    }
}

std::vector<MayaLinkMesh> MayaLinkServer::takeReceived() {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::move(inbox_);
}

bool MayaLinkServer::readFrame(AiLinkConn& conn, MayaLinkType& type, std::string& payload, int firstByteMs,
                               uint64_t maxLength, std::string& error) {
    char header[kMayaLinkHeaderBytes];
    size_t got = 0;
    int wait = firstByteMs;
    while (got < sizeof header) {
        const long long n = conn.readBytes(header + got, sizeof header - got, wait);
        if (n < 0) {
            error = "closed";
            return false;
        }
        if (n == 0) {
            if (got == 0) {
                error.clear(); // nothing came: not an error
                return false;
            }
            error = "timed out";
            return false;
        }
        got += size_t(n);
        wait = frameMs_;
        if (stopping_) return false;
    }
    uint64_t length = 0;
    if (!parseMayaLinkHeader(header, type, length, error)) return false;
    if (length > maxLength) { // (before pairing: only a Hello's size)
        error = "a message over the size cap";
        return false;
    }
    // The payload grows as its bytes arrive, a MiB at a time: a header's claim alone never allocates.
    payload.clear();
    while (payload.size() < length) {
        const size_t at = payload.size();
        payload.resize(at + size_t(std::min<uint64_t>(length - at, kMayaLinkReadChunk)));
        largestBuffer_ = std::max<size_t>(largestBuffer_, payload.size());
        const long long n = conn.readBytes(payload.data() + at, payload.size() - at, frameMs_);
        if (n <= 0) {
            payload.clear();
            payload.shrink_to_fit();
            error = n < 0 ? "closed" : "timed out";
            return false;
        }
        payload.resize(at + size_t(n));
        if (stopping_) return false;
    }
    return true;
}

bool MayaLinkServer::flush(AiLinkConn& conn) {
    for (;;) {
        Outgoing o;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (outbox_.empty()) return true;
            o = std::move(outbox_.front());
            outbox_.pop_front();
        }
        const std::string frame = o.isMesh ? encodeMayaLinkMesh(o.mesh) : encodeMayaLinkPoints(o.points);
        if (!conn.writeAll(frame.data(), frame.size())) return false;
    }
}

bool MayaLinkServer::serve(AiLinkConn& conn) {
    std::string error, payload;
    MayaLinkType type{};
    // Pairing first: a Hello with the code within kMayaLinkHelloMs, or the connection closes.
    if (!readFrame(conn, type, payload, kMayaLinkHelloMs, kMayaLinkMaxHello, error) || type != MayaLinkType::Hello) {
        if (error == "closed") return false; // (a probe: another window checking whether the link is on)
        ++refused_;
        event("refused: " + std::string(error.empty() ? "no pairing code" : error));
        return false;
    }
    std::string code, client;
    if (!parseMayaLinkHello(payload, code, client, error) || !mayaLinkCodeMatches(code, code_)) {
        ++refused_;
        event("refused: wrong pairing code");
        return false;
    }
#ifdef SCULPTFORGE_VERSION
    const std::string version = SCULPTFORGE_VERSION;
#else
    const std::string version = "dev";
#endif
    const std::string welcome = encodeMayaLinkWelcome(version);
    if (!conn.writeAll(welcome.data(), welcome.size())) return false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        client_ = client.empty() ? "Maya" : client;
        outbox_.clear();
    }
    connected_ = true;
    event("connected");
    while (!stopping_) {
        if (!flush(conn)) {
            error = "closed";
            break;
        }
        if (!readFrame(conn, type, payload, 10, kMayaLinkMaxPayload, error)) {
            if (error.empty()) continue; // nothing this time
            break;
        }
        if (type == MayaLinkType::Bye) {
            error = "Maya closed the link";
            break;
        }
        if (type != MayaLinkType::Mesh) {
            error = "an unexpected message";
            ++refused_;
            break;
        }
        MayaLinkMesh mesh;
        if (!parseMayaLinkMesh(payload, mesh, error)) {
            ++refused_;
            break;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        // The newest mesh per object wins (Maya sends whole meshes).
        std::erase_if(inbox_, [&](const MayaLinkMesh& m) { return m.name == mesh.name; });
        inbox_.push_back(std::move(mesh));
    }
    connected_ = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        client_.clear();
    }
    event("disconnected: " + (error.empty() ? std::string("link off") : error));
    return true;
}

void MayaLinkServer::run() {
    while (!stopping_) {
        AiLinkConn conn;
        bool refusedPeer = false;
        if (!listener_.accept(conn, 100, &refusedPeer)) {
            if (refusedPeer) {
                ++refused_;
                event("refused: another user");
            }
            continue;
        }
        serve(conn);
        if (conn.valid()) {
            const std::string bye = mayaLinkFrame(MayaLinkType::Bye, {});
            conn.writeAll(bye.data(), bye.size());
        }
        conn.close();
    }
    running_ = false;
}

} // namespace sf::link
