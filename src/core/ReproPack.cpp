// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/ReproPack.h"

#include "core/Document.h"
#include "core/Json.h"
#include "core/Multires.h"
#include "core/PaintLayer.h"
#include "core/SelectionSets.h"
#include "io/Zip.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <numeric>

namespace sf::repro {

namespace {

constexpr uint64_t kTopologyMaxTriangles = 8'000'000; // above this the edge census is skipped (memory)

bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isLower(char c) { return c >= 'a' && c <= 'z'; }
bool isAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

// A plain number: -12, 3.5, 1e-3, 0x1f, 45%.
bool plainNumber(const std::string& s) {
    if (s.empty()) return false;
    size_t i = s[0] == '-' || s[0] == '+' ? 1 : 0;
    if (i >= s.size()) return false;
    if (s.compare(i, 2, "0x") == 0) {
        if (i + 2 >= s.size()) return false;
        for (size_t k = i + 2; k < s.size(); ++k)
            if (!std::isxdigit(static_cast<unsigned char>(s[k]))) return false;
        return true;
    }
    bool digit = false;
    for (; i < s.size(); ++i) {
        const char c = s[i];
        if (isDigit(c)) digit = true;
        else if (c != '.' && c != 'e' && c != 'E' && c != '-' && c != '+' && !(c == '%' && i + 1 == s.size())) return false;
    }
    return digit;
}

// A short lower-case word an option takes ("game", "front", "off").
bool plainWord(const std::string& s) {
    if (s.empty() || s.size() > 24 || !isLower(s[0])) return false;
    for (char c : s)
        if (!isLower(c) && !isDigit(c) && c != '_' && c != '-') return false;
    return true;
}

// What one whitespace-separated token (without its surrounding punctuation) becomes.
std::string redactToken(const std::string& t) {
    if (t.empty()) return t;
    if (t.find("://") != std::string::npos) return "<url>";
    if (const size_t at = t.find('@'); at != std::string::npos && at > 0 && t.find('.', at) != std::string::npos)
        return "<email>";
    if (const size_t eq = t.find('='); eq != std::string::npos && eq > 0) { // key=value
        const std::string key = t.substr(0, eq), value = t.substr(eq + 1);
        const std::string k = redactToken(key);
        if (value.empty() || plainNumber(value) || value == "true" || value == "false" || plainWord(value))
            return k + "=" + value;
        return k + "=<text>";
    }
    if (t[0] == '~' || (t.size() >= 2 && isAlpha(t[0]) && t[1] == ':' && (t.size() == 2 || t[2] == '\\' || t[2] == '/')))
        return "<path>";
    if (t.find('/') != std::string::npos || t.find('\\') != std::string::npos) {
        bool fraction = true; // 1/2, 3/4
        for (char c : t) fraction = fraction && (isDigit(c) || c == '/');
        return fraction ? t : "<path>";
    }
    // A name with an extension (troll.obj, sw.studio.internal): a dot between a letter or digit and a letter.
    for (size_t i = 1; i + 1 < t.size(); ++i)
        if (t[i] == '.' && (isAlpha(t[i - 1]) || isDigit(t[i - 1]) || t[i - 1] == '_' || t[i - 1] == '-') && isAlpha(t[i + 1]))
            return "<file>";
    return t;
}

bool leadPunct(char c) { return c == '(' || c == '[' || c == '{' || c == '<' || c == ',' || c == ';'; }
bool trailPunct(char c) {
    return c == ')' || c == ']' || c == '}' || c == '>' || c == ',' || c == ';' || c == ':' || c == '.' || c == '!' || c == '?';
}

// Quoted text goes first: "...", “...”, '...' (only a quote that opens a word: don't, it's stay).
std::string redactQuotes(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        auto closeAt = [&](const std::string& open, const std::string& close) -> size_t {
            if (s.compare(i, open.size(), open) != 0) return std::string::npos;
            const size_t end = s.find(close, i + open.size());
            return end == std::string::npos ? std::string::npos : end + close.size();
        };
        size_t end = closeAt("\"", "\"");
        if (end == std::string::npos) end = closeAt("\xE2\x80\x9C", "\xE2\x80\x9D"); // “ ”
        if (end == std::string::npos) end = closeAt("\xE2\x80\x98", "\xE2\x80\x99"); // ‘ ’
        if (end == std::string::npos && s[i] == '\'' && (i == 0 || s[i - 1] == ' ' || s[i - 1] == '=' || s[i - 1] == '(' || s[i - 1] == ':'))
            end = closeAt("'", "'");
        if (end == std::string::npos && s[i] == '\'' && i > 0 && s[i - 1] != ' ' && s[i - 1] != '=' && s[i - 1] != '(' && s[i - 1] != ':') {
            out += s[i++]; // an apostrophe
            continue;
        }
        if (end != std::string::npos) {
            out += "<text>";
            i = end;
        } else {
            out += s[i++];
        }
    }
    return out;
}

} // namespace

std::string redactText(const std::string& text) {
    std::string s = redactQuotes(text);
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        if (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r') {
            out += s[i++];
            continue;
        }
        size_t end = i;
        while (end < s.size() && s[end] != ' ' && s[end] != '\t' && s[end] != '\n' && s[end] != '\r') ++end;
        std::string tok = s.substr(i, end - i);
        size_t a = 0, b = tok.size();
        while (a < b && leadPunct(tok[a])) ++a;
        while (b > a && trailPunct(tok[b - 1])) --b;
        out += tok.substr(0, a) + redactToken(tok.substr(a, b - a)) + tok.substr(b);
        i = end;
    }
    // Control characters other than tab and newline (an escape sequence could hide text in a terminal) go too.
    out.erase(std::remove_if(out.begin(), out.end(), [](char c) {
                  return (static_cast<unsigned char>(c) < 0x20 && c != '\n' && c != '\t') || c == 0x7f;
              }),
              out.end());
    return out;
}

// --- Actions -------------------------------------------------------------------------------------------------

namespace {
std::deque<std::string> gActions;
void push(std::string line) {
    gActions.push_back(std::move(line));
    while (gActions.size() > kActionCount) gActions.pop_front();
}
} // namespace

void noteCommand(const std::string& name, const Args& args) {
    std::string line = redactToken(name);
    for (const auto& [key, value] : args) {
        line += " " + redactToken(key) + "=";
        if (const bool* b = std::get_if<bool>(&value)) line += *b ? "true" : "false";
        else if (const double* d = std::get_if<double>(&value)) {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%g", *d);
            line += buf;
        } else {
            line += "<text>"; // names, paths, prompts, codes: never kept
        }
    }
    push(std::move(line));
}

void noteStroke(const char* brush, uint64_t dabs, uint64_t vertices) {
    push("stroke " + redactToken(brush ? brush : "?") + " dabs=" + std::to_string(dabs) + " vertices=" + std::to_string(vertices));
}

std::vector<std::string> recentActions() { return {gActions.begin(), gActions.end()}; }
void clearActions() { gActions.clear(); }

// --- Stats ---------------------------------------------------------------------------------------------------

namespace {
struct UnionFind {
    std::vector<uint32_t> parent;
    explicit UnionFind(size_t n) : parent(n) { std::iota(parent.begin(), parent.end(), 0u); }
    uint32_t find(uint32_t x) {
        while (parent[x] != x) x = parent[x] = parent[parent[x]];
        return x;
    }
    void unite(uint32_t a, uint32_t b) {
        a = find(a), b = find(b);
        if (a != b) parent[std::max(a, b)] = std::min(a, b);
    }
};
} // namespace

PartStats partStats(const Mesh& m) {
    PartStats s;
    const size_t nv = m.positions.size();
    s.triangles = m.triangles.size();
    s.subdivLevels = std::max(0, subdivLevelCount(m) - 1);
    s.currentLevel = subdivCurrentLevel(m);
    s.layers = int(m.layers.size());
    s.uvs = !m.uvs.empty();
    s.colors = !m.colors.empty();
    s.mask = std::any_of(m.mask.begin(), m.mask.end(), [](float v) { return v > 0.0f; });
    s.faceGroups = std::any_of(m.facegroups.begin(), m.facegroups.end(), [&](uint32_t g) { return g != m.facegroups.front(); });
    s.hidden = std::any_of(m.hidden.begin(), m.hidden.end(), [](uint8_t h) { return h != 0; });
    std::vector<uint8_t> used(nv, 0);
    UnionFind pieces(nv);
    for (const glm::uvec3& t : m.triangles) {
        if (t.x >= nv || t.y >= nv || t.z >= nv) continue;
        used[t.x] = used[t.y] = used[t.z] = 1;
        pieces.unite(t.x, t.y);
        pieces.unite(t.y, t.z);
    }
    s.vertices = uint64_t(std::count(used.begin(), used.end(), uint8_t(1)));
    std::vector<uint32_t> pieceOf(nv, UINT32_MAX);
    std::vector<int64_t> chi; // V - E + F per piece
    for (uint32_t v = 0; v < nv; ++v) {
        if (!used[v]) continue;
        const uint32_t r = pieces.find(v);
        if (pieceOf[r] == UINT32_MAX) {
            pieceOf[r] = uint32_t(chi.size());
            chi.push_back(0);
        }
        pieceOf[v] = pieceOf[r];
        ++chi[pieceOf[v]];
    }
    s.pieces = int(chi.size());
    if (s.triangles > kTopologyMaxTriangles) {
        s.genus = -1; // not counted (mesh.json says so)
        return s;
    }
    for (const glm::uvec3& t : m.triangles)
        if (t.x < nv && t.y < nv && t.z < nv) ++chi[pieceOf[t.x]];
    std::vector<uint64_t> edges;
    edges.reserve(m.triangles.size() * 3);
    for (const glm::uvec3& t : m.triangles) {
        if (t.x >= nv || t.y >= nv || t.z >= nv) continue;
        for (int k = 0; k < 3; ++k) {
            const uint32_t a = t[k], b = t[(k + 1) % 3];
            if (a != b) edges.push_back(uint64_t(std::min(a, b)) << 32 | std::max(a, b));
        }
    }
    std::sort(edges.begin(), edges.end());
    UnionFind loops(nv);
    std::vector<uint8_t> onBoundary(nv, 0);
    for (size_t i = 0; i < edges.size();) {
        size_t j = i;
        while (j < edges.size() && edges[j] == edges[i]) ++j;
        const uint32_t a = uint32_t(edges[i] >> 32), b = uint32_t(edges[i]);
        --chi[pieceOf[a]];
        if (j - i == 1) {
            ++s.openEdges;
            loops.unite(a, b);
            onBoundary[a] = onBoundary[b] = 1;
        } else if (j - i > 2) {
            ++s.nonManifoldEdges;
        }
        i = j;
    }
    std::vector<int> loopsPerPiece(chi.size(), 0);
    for (uint32_t v = 0; v < nv; ++v)
        if (onBoundary[v] && loops.find(v) == v) ++loopsPerPiece[pieceOf[v]];
    for (size_t p = 0; p < chi.size(); ++p) {
        s.openLoops += loopsPerPiece[p];
        const int64_t g = (2 - chi[p] - loopsPerPiece[p]) / 2; // chi = 2 - 2g - b
        s.genus += int(std::max<int64_t>(0, g));
    }
    return s;
}

SceneStats sceneStats(const Document& doc) {
    SceneStats s;
    AABB box;
    for (int i = 0; i < doc.partCount(); ++i) {
        const Mesh& m = doc.partMesh(i);
        s.parts.push_back(partStats(m));
        const glm::mat4& xf = doc.parts[size_t(i)].transform;
        for (const glm::vec3& p : m.positions) box.expand(glm::vec3(xf * glm::vec4(p, 1.0f)));
    }
    if (box.max.x >= box.min.x) s.size = box.max - box.min;
    return s;
}

std::string sceneStatsJson(const SceneStats& s) {
    Json j = Json::object();
    j.set("format", "sculptforge-repro-mesh");
    j.set("version", 1);
    Json size = Json::array();
    for (int k = 0; k < 3; ++k) size.push(double(std::round(s.size[k] * 1000.0f) / 1000.0f));
    j.set("size", size);
    Json parts = Json::array();
    uint64_t v = 0, t = 0;
    for (const PartStats& p : s.parts) {
        Json o = Json::object();
        o.set("vertices", double(p.vertices));
        o.set("triangles", double(p.triangles));
        o.set("pieces", p.pieces);
        if (p.genus >= 0) {
            o.set("genus", p.genus);
            o.set("openLoops", p.openLoops);
            o.set("openEdges", double(p.openEdges));
            o.set("nonManifoldEdges", double(p.nonManifoldEdges));
        } else {
            o.set("genus", "not counted (over 8 million triangles)");
        }
        o.set("subdivisionLevels", p.subdivLevels);
        if (p.subdivLevels > 0) o.set("currentLevel", p.currentLevel);
        o.set("layers", p.layers);
        o.set("uvs", p.uvs);
        o.set("vertexColors", p.colors);
        o.set("mask", p.mask);
        o.set("faceGroups", p.faceGroups);
        o.set("hiddenFaces", p.hidden);
        parts.push(std::move(o));
        v += p.vertices;
        t += p.triangles;
    }
    j.set("parts", parts);
    j.set("totalVertices", double(v));
    j.set("totalTriangles", double(t));
    return j.dump(true) + "\n";
}

// --- The proxy -----------------------------------------------------------------------------------------------

namespace {
constexpr float kPi = 3.14159265358979f;

// Shapes in a unit box centred on the origin.
void addSphere(Mesh& m, uint64_t n) {
    const uint32_t cols = std::max<uint32_t>(3, uint32_t(std::lround(std::sqrt(2.0 * double(n)))));
    const uint32_t rows = std::max<uint32_t>(1, uint32_t(std::lround(double(n > 2 ? n - 2 : 1) / cols)));
    const uint32_t base = uint32_t(m.positions.size());
    m.positions.push_back({0.0f, 0.5f, 0.0f});
    for (uint32_t r = 0; r < rows; ++r) {
        const float th = kPi * float(r + 1) / float(rows + 1);
        for (uint32_t c = 0; c < cols; ++c) {
            const float ph = 2.0f * kPi * float(c) / float(cols);
            m.positions.push_back({0.5f * std::sin(th) * std::cos(ph), 0.5f * std::cos(th), 0.5f * std::sin(th) * std::sin(ph)});
        }
    }
    m.positions.push_back({0.0f, -0.5f, 0.0f});
    const uint32_t south = uint32_t(m.positions.size() - 1);
    auto at = [&](uint32_t r, uint32_t c) { return base + 1 + r * cols + c % cols; };
    for (uint32_t c = 0; c < cols; ++c) m.triangles.push_back({base, at(0, c + 1), at(0, c)});
    for (uint32_t r = 0; r + 1 < rows; ++r)
        for (uint32_t c = 0; c < cols; ++c) {
            m.triangles.push_back({at(r, c), at(r, c + 1), at(r + 1, c)});
            m.triangles.push_back({at(r, c + 1), at(r + 1, c + 1), at(r + 1, c)});
        }
    for (uint32_t c = 0; c < cols; ++c) m.triangles.push_back({south, at(rows - 1, c), at(rows - 1, c + 1)});
}

void addTorus(Mesh& m, uint64_t n) {
    const uint32_t around = std::max<uint32_t>(3, uint32_t(std::lround(std::sqrt(2.0 * double(n)))));
    const uint32_t tube = std::max<uint32_t>(3, uint32_t(std::lround(double(n) / around)));
    const uint32_t base = uint32_t(m.positions.size());
    for (uint32_t i = 0; i < around; ++i)
        for (uint32_t k = 0; k < tube; ++k) {
            const float u = 2.0f * kPi * float(i) / float(around), v = 2.0f * kPi * float(k) / float(tube);
            const float rr = 0.35f + 0.15f * std::cos(v);
            m.positions.push_back({rr * std::cos(u), 0.15f * std::sin(v) * 3.0f, rr * std::sin(u)});
        }
    auto at = [&](uint32_t i, uint32_t k) { return base + (i % around) * tube + k % tube; };
    for (uint32_t i = 0; i < around; ++i)
        for (uint32_t k = 0; k < tube; ++k) {
            m.triangles.push_back({at(i, k), at(i + 1, k), at(i, k + 1)});
            m.triangles.push_back({at(i + 1, k), at(i + 1, k + 1), at(i, k + 1)});
        }
}

void addSheet(Mesh& m, uint64_t n) {
    const uint32_t side = std::max<uint32_t>(2, uint32_t(std::lround(std::sqrt(double(n)))));
    const uint32_t base = uint32_t(m.positions.size());
    for (uint32_t r = 0; r < side; ++r)
        for (uint32_t c = 0; c < side; ++c)
            m.positions.push_back({float(c) / float(side - 1) - 0.5f, 0.0f, float(r) / float(side - 1) - 0.5f});
    auto at = [&](uint32_t r, uint32_t c) { return base + r * side + c; };
    for (uint32_t r = 0; r + 1 < side; ++r)
        for (uint32_t c = 0; c + 1 < side; ++c) {
            m.triangles.push_back({at(r, c), at(r + 1, c), at(r, c + 1)});
            m.triangles.push_back({at(r, c + 1), at(r + 1, c), at(r + 1, c + 1)});
        }
}
} // namespace

Mesh proxyMesh(const SceneStats& s, uint64_t maxVertices) {
    struct Shape { int kind; uint64_t n; }; // 0 sphere, 1 torus, 2 sheet
    std::vector<Shape> shapes;
    uint64_t total = 0;
    for (const PartStats& p : s.parts) total += p.vertices;
    const double scale = total > maxVertices ? double(maxVertices) / double(total) : 1.0;
    for (const PartStats& p : s.parts) {
        if (p.vertices == 0) continue;
        const int k = std::clamp(p.pieces, 1, 16);
        const int tori = std::min(std::max(p.genus, 0), k);
        const int sheets = std::min(p.openLoops, k - tori);
        const uint64_t each = std::max<uint64_t>(8, uint64_t(double(p.vertices) * scale / k));
        for (int i = 0; i < k; ++i) shapes.push_back({i < tori ? 1 : i < tori + sheets ? 2 : 0, each});
    }
    Mesh m;
    if (shapes.empty()) return m;
    glm::vec3 size = s.size;
    for (int k = 0; k < 3; ++k)
        if (!(size[k] > 1e-6f) || !std::isfinite(size[k])) size[k] = 1.0f;
    const float cell = size.x / float(shapes.size());
    for (size_t i = 0; i < shapes.size(); ++i) {
        const size_t from = m.positions.size();
        if (shapes[i].kind == 1) addTorus(m, shapes[i].n);
        else if (shapes[i].kind == 2) addSheet(m, shapes[i].n);
        else addSphere(m, shapes[i].n);
        const glm::vec3 box{cell * 0.9f, size.y * 0.9f, size.z * 0.9f};
        const float x = -0.5f * size.x + cell * (float(i) + 0.5f);
        for (size_t v = from; v < m.positions.size(); ++v) m.positions[v] = m.positions[v] * box + glm::vec3(x, 0.0f, 0.0f);
    }
    return m;
}

std::vector<uint8_t> plyBytes(const Mesh& m) {
    const std::string header = "ply\nformat binary_little_endian 1.0\ncomment SculptForge bug report pack: a generated stand-in\n"
                               "element vertex " + std::to_string(m.positions.size()) +
                               "\nproperty float x\nproperty float y\nproperty float z\nelement face " +
                               std::to_string(m.triangles.size()) + "\nproperty list uchar int vertex_indices\nend_header\n";
    std::vector<uint8_t> out(header.begin(), header.end());
    out.reserve(out.size() + m.positions.size() * 12 + m.triangles.size() * 13);
    auto put32 = [&](uint32_t v) {
        for (int k = 0; k < 4; ++k) out.push_back(uint8_t(v >> (8 * k)));
    };
    for (const glm::vec3& p : m.positions)
        for (int k = 0; k < 3; ++k) {
            uint32_t bits;
            std::memcpy(&bits, &p[k], 4);
            put32(bits);
        }
    for (const glm::uvec3& t : m.triangles) {
        out.push_back(3);
        put32(t.x), put32(t.y), put32(t.z);
    }
    return out;
}

// --- The pack ------------------------------------------------------------------------------------------------

namespace {
std::vector<uint8_t> bytes(const std::string& s) { return {s.begin(), s.end()}; }

// Settings: numbers and on/off as they are, strings only when they are a short lower-case word.
Json redactJson(const Json& j) {
    if (j.isString()) return plainWord(j.asString()) ? j : Json("<text>");
    if (j.isArray()) {
        Json a = Json::array();
        for (const Json& v : j.items()) a.push(redactJson(v));
        return a;
    }
    if (j.isObject()) {
        Json o = Json::object();
        for (const auto& [k, v] : j.members()) o.set(redactToken(k), redactJson(v));
        return o;
    }
    return j;
}

// Every NAMES entry (3 characters or more), in any case, becomes <name>; the longest first, so "Dragon Head" goes
// whole before "Dragon" would split it.
std::string replaceNames(std::string s, std::vector<std::string> names) {
    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
    auto lower = [](std::string t) {
        for (char& c : t) c = char(std::tolower(static_cast<unsigned char>(c)));
        return t;
    };
    for (const std::string& n : names) {
        if (n.size() < 3) continue;
        const std::string ln = lower(n);
        std::string ls = lower(s);
        for (size_t at = ls.find(ln); at != std::string::npos; at = ls.find(ln, at + 6)) {
            s.replace(at, n.size(), "<name>");
            ls.replace(at, n.size(), "<name>");
        }
    }
    return s;
}

std::string scrub(const std::string& s, const std::vector<std::string>& names) { return redactText(replaceNames(s, names)); }

std::string joinLines(const std::vector<std::string>& lines) {
    std::string s;
    for (const std::string& l : lines) s += l + "\n";
    return s;
}
} // namespace

std::vector<std::string> documentNames(const Document& doc) {
    std::vector<std::string> n;
    if (!doc.path.empty()) {
        const std::u8string stem = std::filesystem::path(std::u8string(doc.path.begin(), doc.path.end())).stem().u8string();
        n.emplace_back(stem.begin(), stem.end());
    }
    if (!doc.bridgeName.empty()) n.push_back(doc.bridgeName);
    for (const PartFolder& f : doc.partFolders) n.push_back(f.name);
    for (int i = 0; i < doc.partCount(); ++i) {
        n.push_back(doc.parts[size_t(i)].name);
        const Mesh& m = doc.partMesh(i);
        for (const SculptLayer& l : m.layers) n.push_back(l.name);
        for (const PaintLayer& l : m.paintLayers) n.push_back(l.name);
        if (m.selectionSets)
            for (const auto& set : m.selectionSets->sets)
                if (set) n.push_back(set->name);
    }
    for (const BrushRecipe& b : doc.brushes)
        if (b.origin.source != BrushSource::Builtin) n.push_back(b.name);
    for (const BrushRecipe& b : doc.brushes)
        if (!b.origin.pack.empty()) n.push_back(b.origin.pack);
    for (const auto& [name, data] : doc.projectFiles) {
        n.push_back(name);
        const std::u8string stem = std::filesystem::path(std::u8string(name.begin(), name.end())).stem().u8string();
        n.emplace_back(stem.begin(), stem.end());
    }
    return n;
}

std::vector<Item> buildItems(const Inputs& in) {
    std::vector<Item> items;
    items.push_back({"system", "system.txt", "Computer", "App version, operating system, processor, memory, GPU and driver",
                     bytes(scrub(in.system, in.names)), true});
    Json settings;
    std::string err;
    if (in.settingsJson.empty() || !Json::parse(in.settingsJson, settings, err)) settings = Json::object();
    const Json clean = redactJson(settings);
    items.push_back({"settings", "settings.json", "Settings",
                     std::to_string(clean.members().size()) + " preferences that differ from the defaults (text values left out)",
                     bytes(clean.dump(true) + "\n"), true});
    std::vector<std::string> log;
    for (const std::string& l : in.log) log.push_back(scrub(l, in.names));
    items.push_back({"log", "log.txt", "Log", "The last " + std::to_string(log.size()) + " lines; paths, file names, links and known names replaced. Read it first",
                     bytes(joinLines(log)), true});
    std::vector<std::string> actions;
    for (const std::string& a : in.actions) actions.push_back(scrub(a, in.names));
    items.push_back({"actions", "actions.txt", "Recent actions",
                     "The last " + std::to_string(actions.size()) + " commands and strokes, numbers and on/off values only",
                     bytes(joinLines(actions)), true});
    if (in.doc) {
        const SceneStats stats = sceneStats(*in.doc);
        uint64_t v = 0, t = 0;
        for (const PartStats& p : stats.parts) v += p.vertices, t += p.triangles;
        items.push_back({"mesh", "mesh.json", "Mesh statistics",
                         std::to_string(stats.parts.size()) + (stats.parts.size() == 1 ? " Part, " : " Parts, ") + std::to_string(v) + " vertices, " + std::to_string(t) +
                             " triangles: counts, genus, holes, levels; no names or positions",
                         bytes(sceneStatsJson(stats)), true});
        const Mesh proxy = proxyMesh(stats);
        items.push_back({"proxy", "proxy.ply", "Stand-in mesh",
                         "Generated spheres, tori and sheets with the same counts (" + std::to_string(proxy.positions.size()) +
                             " vertices); none of your shapes",
                         plyBytes(proxy), true});
    }
    return items;
}

std::string readmeText(const std::vector<Item>& items) {
    std::string s = "SculptForge bug report pack\n\n"
                    "Made on this computer by Help > Create bug report pack. It holds no model, texture or image: the mesh is a\n"
                    "generated stand-in with the same counts. Paths, file names, links, e-mail addresses and the names of the\n"
                    "Parts, layers, project, packs and account were replaced. Other names typed into the log can remain: read\n"
                    "log.txt and actions.txt before posting. Nothing was sent anywhere.\n\n";
    for (const Item& i : items)
        if (i.include) s += i.file + ": " + i.summary + "\n";
    return s;
}

bool writePack(const std::string& path, const std::vector<Item>& items, std::string& error) {
    std::vector<ZipEntry> entries;
    entries.push_back({"README.txt", bytes(readmeText(items))});
    for (const Item& i : items)
        if (i.include) entries.push_back({i.file, i.data});
    return writeStoredZip(path, entries, 1, error);
}

std::vector<std::string> tailLines(const std::string& path, size_t lines) {
    std::ifstream f(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary | std::ios::ate);
    if (!f || lines == 0) return {};
    const std::streamoff size = f.tellg();
    const std::streamoff from = std::max<std::streamoff>(0, size - (256 << 10));
    f.seekg(from);
    std::string text(size_t(size - from), '\0');
    f.read(text.data(), std::streamsize(text.size()));
    std::vector<std::string> out;
    size_t at = 0;
    if (from > 0) at = text.find('\n') == std::string::npos ? text.size() : text.find('\n') + 1; // (a cut first line)
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string l = text.substr(at, end - at);
        if (!l.empty() && l.back() == '\r') l.pop_back();
        out.push_back(std::move(l));
        at = end + 1;
    }
    if (out.size() > lines) out.erase(out.begin(), out.end() - std::ptrdiff_t(lines));
    return out;
}

} // namespace sf::repro
