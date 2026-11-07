// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/ForgeSpheres.h"

#include "core/Commands.h"
#include "core/PathStroke.h"
#include "core/Primitives.h"
#include "core/ThreadPool.h"
#include "core/Voxel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace sf {

using glm::vec3;

namespace {

constexpr vec3 kSphereColor{0.62f, 0.60f, 0.56f};
constexpr vec3 kRootColor{0.80f, 0.55f, 0.36f};
constexpr vec3 kLinkColor{0.32f, 0.32f, 0.34f};
constexpr float kLinkThickness = 0.3f; // of each end's sphere radius
constexpr int kLinkSides = 12;

// Distance from p to a cone with rounded ends (spheres a/ra and b/rb), after Inigo Quilez's sdRoundCone.
// One sphere inside the other is just the bigger sphere.
float roundConeDistance(const vec3& p, const vec3& a, const vec3& b, float ra, float rb) {
    const vec3 ba = b - a;
    const float l2 = glm::dot(ba, ba);
    const float rr = ra - rb;
    const float a2 = l2 - rr * rr;
    if (a2 <= 1e-12f) return ra >= rb ? glm::length(p - a) - ra : glm::length(p - b) - rb;
    const float il2 = 1.0f / l2;
    const vec3 pa = p - a;
    const float y = glm::dot(pa, ba);
    const float z = y - l2;
    const vec3 xv = pa * l2 - ba * y;
    const float x2 = glm::dot(xv, xv);
    const float y2 = y * y * l2;
    const float z2 = z * z * l2;
    const float k = (rr > 0.0f ? 1.0f : rr < 0.0f ? -1.0f : 0.0f) * rr * rr * x2;
    const float sz = z > 0.0f ? 1.0f : z < 0.0f ? -1.0f : 0.0f;
    const float sy = y > 0.0f ? 1.0f : y < 0.0f ? -1.0f : 0.0f;
    if (sz * a2 * z2 > k) return std::sqrt(x2 + z2) * il2 - rb;
    if (sy * a2 * y2 < k) return std::sqrt(x2 + y2) * il2 - ra;
    return (std::sqrt(x2 * a2 * il2) + y * rr) * il2 - ra;
}

// Polynomial smooth minimum: blends over a band `k` wide.
float smoothMin(float a, float b, float k) {
    if (k <= 0.0f) return std::min(a, b);
    const float h = std::max(k - std::abs(a - b), 0.0f) / k;
    return std::min(a, b) - h * h * k * 0.25f;
}

// The pieces of the skin: one rounded cone per link, or the root alone.
struct Piece {
    vec3 a, b;
    float ra, rb;
    uint32_t group;
};

std::vector<Piece> skinPieces(const std::vector<ForgeSphere>& s) {
    std::vector<Piece> out;
    for (size_t i = 1; i < s.size(); ++i) {
        const ForgeSphere& p = s[size_t(s[i].parent)];
        out.push_back({p.center, s[i].center, p.radius, s[i].radius, kForgeSphereLinkGroup + uint32_t(i)});
    }
    if (out.empty() && !s.empty()) out.push_back({s[0].center, s[0].center, s[0].radius, s[0].radius, kForgeSphereGroup});
    return out;
}

float pieceDistance(const Piece& q, const vec3& p) { return roundConeDistance(p, q.a, q.b, q.ra, q.rb); }

// How wide the blend between two pieces is: a quarter of the thinner end, so joints fill out skin.
float blendWidth(const Piece& q) { return 0.25f * std::min(q.ra, q.rb); }

// Mirrors of `sym` other than the identity.
std::vector<vec3> otherMirrors(Symmetry sym) {
    std::vector<vec3> m = Sculptor::mirrors(sym);
    m.erase(m.begin()); // mask 0: the identity
    return m;
}

bool validIndex(const std::vector<ForgeSphere>& s, int i) { return i >= 0 && size_t(i) < s.size(); }

// Every sphere in the subtrees rooted at `roots` (the roots too).
std::vector<char> subtrees(const std::vector<ForgeSphere>& s, const std::vector<int>& roots) {
    std::vector<char> gone(s.size(), 0);
    for (int r : roots) gone[size_t(r)] = 1;
    for (bool changed = true; changed;) { // parents may come after children (an inserted sphere), so until stable
        changed = false;
        for (size_t i = 1; i < s.size(); ++i)
            if (!gone[i] && gone[size_t(s[i].parent)]) gone[i] = 1, changed = true;
    }
    return gone;
}

// Appends `src` to `dst` (positions, triangles, one group, one colour).
void appendPart(Mesh& dst, const Mesh& src, uint32_t group, const vec3& color) {
    const uint32_t base = uint32_t(dst.positions.size());
    dst.positions.insert(dst.positions.end(), src.positions.begin(), src.positions.end());
    dst.colors.insert(dst.colors.end(), src.positions.size(), color);
    for (const glm::uvec3& t : src.triangles) dst.triangles.push_back(t + glm::uvec3(base));
    dst.facegroups.insert(dst.facegroups.end(), src.triangles.size(), group);
}

Mesh unitSphere() {
    static const Mesh sphere = makeIcosphere(3, 1.0f);
    return sphere;
}

// An open tapered tube from a (radius ra) to b (rb).
Mesh tube(const vec3& a, const vec3& b, float ra, float rb) {
    Mesh m;
    const vec3 axis = glm::normalize(b - a);
    const vec3 helper = std::abs(axis.y) < 0.9f ? vec3(0, 1, 0) : vec3(1, 0, 0);
    const vec3 u = glm::normalize(glm::cross(axis, helper)), v = glm::cross(axis, u);
    for (int i = 0; i < kLinkSides; ++i) {
        const float t = 6.28318530718f * float(i) / float(kLinkSides);
        const vec3 d = u * std::cos(t) + v * std::sin(t);
        m.positions.push_back(a + d * ra);
        m.positions.push_back(b + d * rb);
    }
    for (uint32_t i = 0; i < uint32_t(kLinkSides); ++i) {
        const uint32_t j = (i + 1) % uint32_t(kLinkSides);
        m.triangles.push_back({2 * i, 2 * j, 2 * i + 1});
        m.triangles.push_back({2 * j, 2 * j + 1, 2 * i + 1});
    }
    return m;
}

std::string fmt(const char* f, double a, double b = 0, double c = 0, double d = 0) {
    char buf[160];
    std::snprintf(buf, sizeof buf, f, a, b, c, d);
    return buf;
}

} // namespace

int forgesphereOfGroup(uint32_t group, bool* link) {
    if (link) *link = group >= kForgeSphereLinkGroup;
    if (group >= kForgeSphereLinkGroup) return int(group - kForgeSphereLinkGroup);
    if (group >= kForgeSphereGroup) return int(group - kForgeSphereGroup);
    return -1;
}

std::string forgespheresValidate(const std::vector<ForgeSphere>& s) {
    if (s.empty()) return "no spheres";
    if (s[0].parent != -1) return "sphere 0 must be the root (parent -1)";
    for (size_t i = 0; i < s.size(); ++i) {
        const ForgeSphere& z = s[i];
        if (!std::isfinite(z.center.x) || !std::isfinite(z.center.y) || !std::isfinite(z.center.z) ||
            !std::isfinite(z.radius) || !(z.radius > 0.0f))
            return "sphere " + std::to_string(i) + " has a bad position or size";
        if (i > 0 && (z.parent < 0 || size_t(z.parent) >= s.size() || size_t(z.parent) == i))
            return "sphere " + std::to_string(i) + " has a bad parent";
    }
    // Every sphere reaches the root.
    for (size_t i = 1; i < s.size(); ++i) {
        size_t at = i, steps = 0;
        while (at != 0 && steps++ <= s.size()) at = size_t(s[at].parent);
        if (at != 0) return "sphere " + std::to_string(i) + " is in a loop";
    }
    return {};
}

Mesh forgesphereDisplayMesh(const std::vector<ForgeSphere>& s) {
    Mesh m;
    const Mesh& sphere = unitSphere();
    for (size_t i = 0; i < s.size(); ++i) {
        Mesh part = sphere;
        for (vec3& p : part.positions) p = s[i].center + p * s[i].radius;
        appendPart(m, part, kForgeSphereGroup + uint32_t(i), i == 0 ? kRootColor : kSphereColor);
    }
    for (size_t i = 1; i < s.size(); ++i) {
        const ForgeSphere& c = s[i];
        const ForgeSphere& p = s[size_t(c.parent)];
        if (glm::length(c.center - p.center) < 1e-6f * (c.radius + p.radius)) continue;
        appendPart(m, tube(p.center, c.center, kLinkThickness * p.radius, kLinkThickness * c.radius),
                   kForgeSphereLinkGroup + uint32_t(i), kLinkColor);
    }
    m.rebuildTopology();
    m.recomputeAllNormals();
    m.forgespheres = s;
    return m;
}

Mesh forgesphereSkin(const std::vector<ForgeSphere>& s, int resolution, int smooth) {
    if (!forgespheresValidate(s).empty()) return {};
    const std::vector<Piece> pieces = skinPieces(s);
    AABB box;
    float blend = 0.0f;
    for (const Piece& q : pieces) {
        const float pad = blendWidth(q);
        blend = std::max(blend, pad);
        box.expand(q.a - vec3(q.ra + pad)), box.expand(q.a + vec3(q.ra + pad));
        box.expand(q.b - vec3(q.rb + pad)), box.expand(q.b + vec3(q.rb + pad));
    }
    VoxelGrid grid;
    if (!makeVoxelGrid(box, std::clamp(resolution, 16, 1024), 2, grid)) return {};
    // Signed distance (negative inside), each piece only over its own box plus the blend band.
    std::vector<float> dist(grid.size(), std::numeric_limits<float>::max());
    const glm::ivec3 last = grid.dims - 1;
    for (const Piece& q : pieces) {
        const float pad = std::max(q.ra, q.rb) + blendWidth(q) + 2.0f * grid.cell;
        const vec3 lo = glm::min(q.a, q.b) - vec3(pad), hi = glm::max(q.a, q.b) + vec3(pad);
        const glm::ivec3 c0 = glm::clamp(glm::ivec3(glm::floor((lo - grid.origin) / grid.cell)), glm::ivec3(0), last);
        const glm::ivec3 c1 = glm::clamp(glm::ivec3(glm::ceil((hi - grid.origin) / grid.cell)), glm::ivec3(0), last);
        const float k = blendWidth(q);
        poolFor(size_t(c1.z - c0.z + 1), [&](size_t b, size_t e) {
            for (int z = c0.z + int(b); z < c0.z + int(e); ++z)
                for (int y = c0.y; y <= c1.y; ++y)
                    for (int x = c0.x; x <= c1.x; ++x) {
                        float& d = dist[grid.index(x, y, z)];
                        d = smoothMin(d, pieceDistance(q, grid.position(x, y, z)), k);
                    }
        }, 1);
    }
    for (float& d : dist) d = d == std::numeric_limits<float>::max() ? -1e9f : -d; // inside above iso 0
    Mesh m = meshFromField(grid, dist, 0.0f, std::max(0, smooth));
    // One face group per piece: the one each face is closest to.
    m.facegroups.resize(m.triangleCount());
    poolFor(m.triangleCount(), [&](size_t b, size_t e) {
        for (size_t t = b; t < e; ++t) {
            const glm::uvec3 tri = m.triangles[t];
            const vec3 c = (m.positions[tri.x] + m.positions[tri.y] + m.positions[tri.z]) / 3.0f;
            float best = std::numeric_limits<float>::max();
            for (const Piece& q : pieces)
                if (const float d = std::abs(pieceDistance(q, c)); d < best) best = d, m.facegroups[t] = q.group;
        }
    }, 1024);
    return m;
}

Mesh forgesphereMesh(const std::vector<ForgeSphere>& s, bool preview) {
    Mesh m = preview ? forgesphereSkin(s, kForgeSpherePreviewResolution, 2) : forgesphereDisplayMesh(s);
    if (preview && m.triangleCount() == 0) m = forgesphereDisplayMesh(s);
    m.forgespheres = s;
    m.forgespherePreview = preview && m.triangleCount() > 0;
    return m;
}

int forgesphereMirror(const std::vector<ForgeSphere>& s, int i, const vec3& m) {
    if (!validIndex(s, i)) return -1;
    const vec3 want = s[size_t(i)].center * m;
    const float tol = 0.02f * s[size_t(i)].radius + 1e-6f;
    int best = -1;
    float bestD = tol;
    for (size_t j = 0; j < s.size(); ++j) {
        const float d = glm::length(s[j].center - want);
        if (d <= bestD && std::abs(s[j].radius - s[size_t(i)].radius) <= tol) best = int(j), bestD = d;
    }
    return best;
}

bool forgesphereAdd(std::vector<ForgeSphere>& s, int parent, const vec3& center, float radius, Symmetry sym, int* added) {
    if (!validIndex(s, parent) || !(radius > 0.0f) || !std::isfinite(center.x + center.y + center.z)) return false;
    // Mirror parents come from the armature as it was, before the new spheres join it.
    std::vector<std::pair<int, vec3>> copies;
    for (const vec3& m : otherMirrors(sym)) {
        const vec3 c = center * m;
        if (glm::length(c - center) <= 0.02f * radius) continue; // on the plane: one sphere serves both sides
        int p = forgesphereMirror(s, parent, m);
        if (p < 0) p = parent;
        bool dup = false;
        for (const auto& [q, cc] : copies) dup |= glm::length(cc - c) <= 0.02f * radius;
        if (!dup) copies.push_back({p, c});
    }
    if (added) *added = int(s.size());
    s.push_back({center, radius, parent});
    for (const auto& [p, c] : copies) s.push_back({c, radius, p});
    return true;
}

bool forgesphereSet(std::vector<ForgeSphere>& s, int i, const vec3& center, float radius, Symmetry sym) {
    if (!validIndex(s, i) || !(radius > 0.0f) || !std::isfinite(center.x + center.y + center.z)) return false;
    std::vector<std::pair<int, vec3>> targets{{i, vec3(1.0f)}};
    for (const vec3& m : otherMirrors(sym))
        if (const int j = forgesphereMirror(s, i, m); j >= 0) targets.push_back({j, m});
    vec3 c = center;
    for (const auto& [j, m] : targets) // a sphere that mirrors onto itself stays on that plane
        if (j == i)
            for (int a = 0; a < 3; ++a)
                if (m[a] < 0.0f) c[a] = 0.0f;
    for (const auto& [j, m] : targets) {
        if (j == i && m != vec3(1.0f)) continue;
        s[size_t(j)].center = c * m;
        s[size_t(j)].radius = radius;
    }
    return true;
}

bool forgesphereDelete(std::vector<ForgeSphere>& s, int i, Symmetry sym) {
    if (!validIndex(s, i) || i == 0) return false;
    std::vector<int> roots{i};
    for (const vec3& m : otherMirrors(sym))
        if (const int j = forgesphereMirror(s, i, m); j > 0) roots.push_back(j);
    const std::vector<char> gone = subtrees(s, roots);
    std::vector<int> remap(s.size(), -1);
    std::vector<ForgeSphere> kept;
    for (size_t j = 0; j < s.size(); ++j)
        if (!gone[j]) remap[j] = int(kept.size()), kept.push_back(s[j]);
    for (size_t j = 1; j < kept.size(); ++j) kept[j].parent = remap[size_t(kept[j].parent)];
    s = std::move(kept);
    return true;
}

bool forgesphereInsert(std::vector<ForgeSphere>& s, int child, Symmetry sym, int* added) {
    if (!validIndex(s, child) || child == 0) return false;
    std::vector<int> children{child};
    for (const vec3& m : otherMirrors(sym))
        if (const int j = forgesphereMirror(s, child, m); j > 0 && std::find(children.begin(), children.end(), j) == children.end())
            children.push_back(j);
    if (added) *added = int(s.size());
    for (int c : children) {
        const ForgeSphere& z = s[size_t(c)];
        const ForgeSphere& p = s[size_t(z.parent)];
        const ForgeSphere mid{(z.center + p.center) * 0.5f, (z.radius + p.radius) * 0.5f, z.parent};
        s[size_t(c)].parent = int(s.size());
        s.push_back(mid);
    }
    return true;
}

bool forgesphereSetRoot(std::vector<ForgeSphere>& s, int i) {
    if (!validIndex(s, i) || i == 0) return false;
    // Turn the path from i up to the root around.
    int at = i, below = -1;
    while (at >= 0) {
        const int up = s[size_t(at)].parent;
        s[size_t(at)].parent = below;
        below = at;
        at = up;
    }
    // Swap indices 0 and i.
    std::swap(s[0], s[size_t(i)]);
    for (ForgeSphere& z : s) {
        if (z.parent == 0) z.parent = i;
        else if (z.parent == i) z.parent = 0;
    }
    return true;
}

Json forgespheresToJson(const std::vector<ForgeSphere>& s) {
    Json out = Json::array();
    for (const ForgeSphere& z : s) {
        for (int k = 0; k < 3; ++k) out.push(double(z.center[k]));
        out.push(double(z.radius));
        out.push(double(z.parent));
    }
    return out;
}

std::vector<ForgeSphere> forgespheresFromJson(const Json& json) {
    std::vector<ForgeSphere> s;
    if (!json.isArray() || json.size() % 5 != 0) return s;
    for (size_t i = 0; i < json.size(); i += 5)
        s.push_back({vec3(float(json[i].asNumber(NAN)), float(json[i + 1].asNumber(NAN)), float(json[i + 2].asNumber(NAN))),
                     float(json[i + 3].asNumber(NAN)), int(json[i + 4].asNumber(-2))});
    if (!forgespheresValidate(s).empty()) s.clear();
    return s;
}

namespace {

constexpr const char* kNotForgeSphere = "The active Part is not a Forge Sphere Part (forgesphere_new makes one)";

Symmetry symmetryArg(const Args& a) {
    Symmetry s{false, false, false};
    for (char c : argString(a, "symmetry")) {
        s.x |= c == 'x' || c == 'X';
        s.y |= c == 'y' || c == 'Y';
        s.z |= c == 'z' || c == 'Z';
    }
    return s;
}

CommandResult applyEdit(Document& doc, std::vector<ForgeSphere> s, const std::string& label, std::string message) {
    if (const std::string err = forgespheresValidate(s); !err.empty()) return {false, "Forge Spheres: " + err};
    doc.replaceMeshUndoable(forgesphereMesh(s, doc.mesh.forgespherePreview), label);
    return {true, message + "; " + std::to_string(s.size()) + " spheres"};
}

std::string describe(const std::vector<ForgeSphere>& s, size_t i) {
    const ForgeSphere& z = s[i];
    return std::to_string(i) + ": " + fmt("(%.4g, %.4g, %.4g) radius %.4g", z.center.x, z.center.y, z.center.z, z.radius) +
           (i == 0 ? " root" : ", from " + std::to_string(z.parent));
}

} // namespace

void registerForgeSphereCommands(CommandRegistry& reg) {
    using T = ParamSpec::Type;
    const ParamSpec symmetryParam{"symmetry", T::String,
                                  "Mirror the edit across these planes through the centre, e.g. \"x\". Default: none.",
                                  false};
    reg.add({"forgesphere_new",
             "Forge Spheres: add a Forge Sphere Part holding one root sphere at the centre and make it "
             "active. Grow limbs from it with forgesphere_add (or the Forge Sphere tool), see the skin with forgesphere_preview, "
             "then forgesphere_make_mesh turns it into a sculptable Part.",
             {{"name", T::String, "Part name (default \"Forge Sphere\").", false},
              {"radius", T::Number, "Root sphere radius (default 0.5).", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 const float r = float(argNumber(a, "radius", 0.5));
                 if (!(r > 0.0f) || !std::isfinite(r)) return {false, "radius must be above 0"};
                 const std::string name = doc.uniquePartName(argString(a, "name", "Forge Sphere"));
                 doc.addPart(forgesphereMesh({{vec3(0.0f), r, -1}}, false), name);
                 return {true, "Added Forge Sphere Part \"" + name + "\" with a root sphere (index 0)"};
             }});
    reg.add({"forgesphere_list", "List the active Forge Sphere Part's spheres: index, centre, radius and parent.", {},
             [](Document& doc, const Args&) -> CommandResult {
                 const auto& s = doc.mesh.forgespheres;
                 if (s.empty()) return {false, kNotForgeSphere};
                 std::string out = std::to_string(s.size()) + " spheres" + (doc.mesh.forgespherePreview ? " (skin preview on)" : "");
                 for (size_t i = 0; i < s.size(); ++i) out += "\n" + describe(s, i);
                 return {true, out};
             }});
    reg.add({"forgesphere_add",
             "Grow a new sphere from sphere `parent` of the active Forge Sphere Part (a limb, finger or tail joint). "
             "Its index is reported; mirror copies (with symmetry) follow it.",
             {{"parent", T::Number, "Index of the sphere it grows from (0 = the root).", true},
              {"position", T::String, "Centre \"x,y,z\".", true},
              {"radius", T::Number, "Radius (default: the parent's).", false},
              symmetryParam},
             [](Document& doc, const Args& a) -> CommandResult {
                 auto s = doc.mesh.forgespheres;
                 if (s.empty()) return {false, kNotForgeSphere};
                 const int parent = int(argNumber(a, "parent", -1));
                 if (!validIndex(s, parent)) return {false, "parent must be a sphere index from 0 to " + std::to_string(s.size() - 1)};
                 vec3 c;
                 if (!parseVec3(argString(a, "position"), c)) return {false, "position must be \"x,y,z\""};
                 const float r = float(argNumber(a, "radius", s[size_t(parent)].radius));
                 int added = -1;
                 if (!forgesphereAdd(s, parent, c, r, symmetryArg(a), &added)) return {false, "radius must be above 0"};
                 return applyEdit(doc, std::move(s), "Forge Sphere add", "Added sphere " + std::to_string(added));
             }});
    reg.add({"forgesphere_set", "Move and/or resize a sphere of the active Forge Sphere Part.",
             {{"index", T::Number, "Sphere index.", true},
              {"position", T::String, "New centre \"x,y,z\" (default: unchanged).", false},
              {"radius", T::Number, "New radius (default: unchanged).", false},
              symmetryParam},
             [](Document& doc, const Args& a) -> CommandResult {
                 auto s = doc.mesh.forgespheres;
                 if (s.empty()) return {false, kNotForgeSphere};
                 const int i = int(argNumber(a, "index", -1));
                 if (!validIndex(s, i)) return {false, "index must be from 0 to " + std::to_string(s.size() - 1)};
                 vec3 c = s[size_t(i)].center;
                 if (a.contains("position") && !parseVec3(argString(a, "position"), c))
                     return {false, "position must be \"x,y,z\""};
                 if (!forgesphereSet(s, i, c, float(argNumber(a, "radius", s[size_t(i)].radius)), symmetryArg(a)))
                     return {false, "radius must be above 0"};
                 return applyEdit(doc, std::move(s), "Forge Sphere edit", "Changed sphere " + std::to_string(i));
             }});
    reg.add({"forgesphere_delete", "Delete a sphere of the active Forge Sphere Part and every sphere growing from it.",
             {{"index", T::Number, "Sphere index (not 0, the root).", true}, symmetryParam},
             [](Document& doc, const Args& a) -> CommandResult {
                 auto s = doc.mesh.forgespheres;
                 if (s.empty()) return {false, kNotForgeSphere};
                 const int i = int(argNumber(a, "index", -1));
                 if (!forgesphereDelete(s, i, symmetryArg(a)))
                     return {false, "index must be from 1 to " + std::to_string(s.size() - 1) + " (the root stays)"};
                 return applyEdit(doc, std::move(s), "Forge Sphere delete", "Deleted sphere " + std::to_string(i));
             }});
    reg.add({"forgesphere_insert",
             "Insert a sphere halfway along the link from sphere `index` to its parent (a new joint, like clicking "
             "a link in Draw mode).",
             {{"index", T::Number, "The child sphere whose link gets the new joint.", true}, symmetryParam},
             [](Document& doc, const Args& a) -> CommandResult {
                 auto s = doc.mesh.forgespheres;
                 if (s.empty()) return {false, kNotForgeSphere};
                 int added = -1;
                 if (!forgesphereInsert(s, int(argNumber(a, "index", -1)), symmetryArg(a), &added))
                     return {false, "index must be from 1 to " + std::to_string(s.size() - 1)};
                 return applyEdit(doc, std::move(s), "Forge Sphere insert", "Inserted sphere " + std::to_string(added));
             }});
    reg.add({"forgesphere_set_root",
             "Make sphere `index` of the active Forge Sphere Part the root (index 0): the armature keeps its shape and "
             "links, and grows from there. The old root takes the sphere's index.",
             {{"index", T::Number, "Sphere index (1 or more).", true}},
             [](Document& doc, const Args& a) -> CommandResult {
                 auto s = doc.mesh.forgespheres;
                 if (s.empty()) return {false, kNotForgeSphere};
                 const int i = int(argNumber(a, "index", -1));
                 if (!forgesphereSetRoot(s, i))
                     return {false, i == 0 ? "Sphere 0 is the root already"
                                           : "index must be from 1 to " + std::to_string(s.size() - 1)};
                 return applyEdit(doc, std::move(s), "Forge Sphere set root",
                                  "Sphere " + std::to_string(i) + " is the root now (index 0)");
             }});
    reg.add({"forgesphere_preview",
             "Show the skin over the active Forge Sphere Part's spheres instead of the spheres.",
             {{"on", T::Bool, "Default: toggle.", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 if (doc.mesh.forgespheres.empty()) return {false, kNotForgeSphere};
                 const bool on = argBool(a, "on", !doc.mesh.forgespherePreview);
                 if (on == doc.mesh.forgespherePreview) return {true, on ? "Skin preview is on" : "Skin preview is off"};
                 doc.replaceMeshUndoable(forgesphereMesh(doc.mesh.forgespheres, on), on ? "Forge Sphere preview" : "Forge Sphere spheres");
                 return {true, on ? "Skin preview on" : "Skin preview off"};
             }});
    reg.add({"forgesphere_make_mesh",
             "Make Mesh: build the skin over the active Forge Sphere Part as a new, "
             "sculptable Part right below it, watertight quads with one face group per link. The Forge Sphere Part "
             "is hidden and kept so the proportions can still be changed and a new mesh made.",
             {{"resolution", T::Number, "Cells across the longest side (16-1024, default 128): higher is denser.", false},
              {"smooth", T::Number, "Smoothing passes (default 2).", false},
              {"keep", T::Bool, "Keep the Forge Sphere Part (hidden). Default true; false deletes it.", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 if (doc.mesh.forgespheres.empty()) return {false, kNotForgeSphere};
                 const int res = int(argNumber(a, "resolution", 128));
                 if (res < 16 || res > 1024) return {false, "resolution must be from 16 to 1024"};
                 Mesh skin = forgesphereSkin(doc.mesh.forgespheres, res, int(argNumber(a, "smooth", 2)));
                 if (skin.triangleCount() == 0) return {false, "Forge Spheres: the skin came out empty (resolution too high?)"};
                 const size_t faces = skin.triangleCount() - skin.quadCount();
                 const int from = doc.activePart;
                 const std::string name = doc.uniquePartName(doc.parts[size_t(from)].name + " Mesh");
                 const glm::mat4 place = doc.parts[size_t(from)].transform;
                 doc.addPart(std::move(skin), name, true, from + 1);
                 doc.activePartInfo().transform = place;
                 if (argBool(a, "keep", true)) doc.parts[size_t(from)].visible = false;
                 else doc.removePart(from);
                 return {true, "Made \"" + name + "\" from the Forge Spheres: " + std::to_string(faces) + " faces"};
             }});
}

} // namespace sf
