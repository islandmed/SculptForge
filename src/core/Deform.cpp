// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Deform.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/PathStroke.h"
#include "core/ThreadPool.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <sstream>

namespace sf {

namespace {

struct OpInfo {
    DeformOp op;
    const char* name;
    const char* command;
    float lo, hi, def;
};
// Own ranges (docs/design/deform-panel.md): degrees for Turn, Bend and Twist, percent for the rest.
const OpInfo kOps[kDeformOpCount] = {
    {DeformOp::Polish, "Polish", "deform_polish", 0, 100, 50},     {DeformOp::Relax, "Relax", "deform_relax", 0, 100, 50},
    {DeformOp::Smooth, "Smooth", "deform_smooth", 0, 100, 50},     {DeformOp::Balloon, "Balloon", "deform_balloon", -100, 100, 50},
    {DeformOp::Move, "Move", "deform_move", -100, 100, 20},        {DeformOp::Turn, "Turn", "deform_turn", -180, 180, 45},
    {DeformOp::Scale, "Scale", "deform_scale", -100, 100, 25},     {DeformOp::Squash, "Squash", "deform_squash", -100, 100, 30},
    {DeformOp::Bend, "Bend", "deform_bend", -180, 180, 45},        {DeformOp::Twist, "Twist", "deform_twist", -360, 360, 90},
    {DeformOp::Taper, "Taper", "deform_taper", -100, 100, 40},     {DeformOp::Shear, "Shear", "deform_shear", -100, 100, 30},
    {DeformOp::Round, "Round", "deform_round", -100, 100, 50},     {DeformOp::Box, "Box", "deform_box", -100, 100, 50},
    {DeformOp::Flatten, "Flatten", "deform_flatten", -100, 100, 20}, {DeformOp::Sag, "Sag", "deform_sag", -100, 100, 30},
    {DeformOp::Depth, "Depth", "deform_depth", -90, 90, 40},
};
const OpInfo& info(DeformOp op) { return kOps[size_t(op)]; }

} // namespace

const char* deformName(DeformOp op) { return info(op).name; }
const char* deformCommand(DeformOp op) { return info(op).command; }

bool deformFromName(const std::string& name, DeformOp& op) {
    std::string n;
    for (char c : name) n += char(std::tolower(static_cast<unsigned char>(c)));
    if (n.rfind("deform_", 0) == 0) n = n.substr(7);
    for (const OpInfo& i : kOps) {
        std::string m;
        for (const char* c = i.name; *c; ++c) m += char(std::tolower(static_cast<unsigned char>(*c)));
        if (m == n) {
            op = i.op;
            return true;
        }
    }
    return false;
}

void deformRange(DeformOp op, float& lo, float& hi, float& def) {
    lo = info(op).lo;
    hi = info(op).hi;
    def = info(op).def;
}

bool deformUsesDirection(DeformOp op) { return op == DeformOp::Bend || op == DeformOp::Shear; }
bool deformUsesSoft(DeformOp op) {
    return op == DeformOp::Bend || op == DeformOp::Twist || op == DeformOp::Taper || op == DeformOp::Shear ||
           op == DeformOp::Flatten;
}
bool deformUsesAxes(DeformOp op) { return int(op) >= int(DeformOp::Move); }

std::string axesLetters(uint8_t axes) {
    std::string s;
    for (int a = 0; a < 3; ++a)
        if (axes & (1 << a)) s += char('x' + a);
    return s;
}

bool parseDeformStack(const std::string& text, std::vector<DeformStep>& steps, std::string& error) {
    steps.clear();
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(';', start);
        if (end == std::string::npos) end = text.size();
        std::istringstream in(text.substr(start, end - start));
        start = end + 1;
        std::string word;
        if (!(in >> word)) continue;
        DeformStep st;
        if (!deformFromName(word, st.op)) {
            error = "Unknown deformation '" + word + "'";
            return false;
        }
        float lo, hi;
        deformRange(st.op, lo, hi, st.options.amount);
        bool haveAmount = false;
        while (in >> word) {
            char* e = nullptr;
            const float v = std::strtof(word.c_str(), &e);
            if (!haveAmount && e && *e == '\0' && e != word.c_str()) {
                st.options.amount = std::clamp(v, lo, hi);
                haveAmount = true;
            } else if (word == "soft") {
                st.options.soft = true;
            } else if (word == "volume") {
                st.options.keepVolume = true;
            } else if (word == "nomask") {
                st.options.useMask = false;
            } else if (word.rfind("dir=", 0) == 0) {
                st.options.direction = std::strtof(word.c_str() + 4, nullptr);
            } else if (word.find_first_not_of("xyzXYZ") == std::string::npos) {
                st.options.axes = 0;
                for (char c : word) st.options.axes |= uint8_t(1 << (std::tolower(static_cast<unsigned char>(c)) - 'x'));
            } else {
                error = "Bad word '" + word + "' in deformation '" + deformName(st.op) + "'";
                return false;
            }
        }
        steps.push_back(st);
    }
    if (steps.empty()) error = "No deformations";
    return !steps.empty();
}

std::string deformStackText(const std::vector<DeformStep>& steps) {
    std::ostringstream out;
    for (size_t i = 0; i < steps.size(); ++i) {
        const DeformStep& st = steps[i];
        if (i) out << "; ";
        std::string n = deformName(st.op);
        for (char& c : n) c = char(std::tolower(static_cast<unsigned char>(c)));
        out << n << ' ' << st.options.amount;
        if (deformUsesAxes(st.op)) out << ' ' << axesLetters(st.options.axes);
        if (st.options.soft && deformUsesSoft(st.op)) out << " soft";
        if (st.options.direction != 0.0f && deformUsesDirection(st.op)) out << " dir=" << st.options.direction;
        if (st.options.keepVolume && st.op == DeformOp::Smooth) out << " volume";
        if (!st.options.useMask) out << " nomask";
    }
    return out.str();
}

namespace {

using glm::vec3;

constexpr size_t kGrain = 2048;

// How much each vertex may move: 0 when hidden, on an open border (or a non-manifold one) or unconnected;
// otherwise 1 - mask (1 with useMask off).
std::vector<float> freedom(const Mesh& m, bool useMask) {
    std::vector<float> k(m.vertexCount(), 0.0f);
    poolFor(k.size(), [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            const uint32_t v = uint32_t(i);
            const IndexRing ring = m.neighbors(v);
            if (ring.empty() || ring.size() != m.vertexTriangles(v).size()) continue; // a border: n neighbours, n-1 faces
            if (m.hasHidden() && m.vertexHidden(v)) continue;
            k[i] = useMask ? 1.0f - m.maskAt(v) : 1.0f;
        }
    }, kGrain);
    return k;
}

// Area-weighted vertex normals of the surface at positions P.
void normalsOf(const Mesh& m, const std::vector<vec3>& P, std::vector<vec3>& N) {
    N.resize(P.size());
    poolFor(P.size(), [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            vec3 n(0.0f);
            for (uint32_t t : m.vertexTriangles(uint32_t(i))) {
                const glm::uvec3& f = m.triangles[t];
                n += glm::cross(P[f.y] - P[f.x], P[f.z] - P[f.x]);
            }
            const float l = glm::length(n);
            N[i] = l > 1e-30f ? n / l : vec3(0.0f);
        }
    }, kGrain);
}

vec3 laplacian(const Mesh& m, const std::vector<vec3>& P, uint32_t v) {
    const IndexRing ring = m.polygonNeighbors(v);
    vec3 avg(0.0f);
    for (uint32_t n : ring) avg += P[n];
    return avg / float(ring.size()) - P[v];
}

// One Jacobi step: Q[v] = P[v] + k[v] * step(v) for the vertices that may move.
template <class F>
void step(const std::vector<float>& k, std::vector<vec3>& P, std::vector<vec3>& Q, F&& delta) {
    Q.resize(P.size());
    poolFor(P.size(), [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) Q[i] = k[i] > 0.0f ? P[i] + delta(uint32_t(i)) * k[i] : P[i];
    }, kGrain);
    P.swap(Q);
}

// The amount as passes: `most` passes at 100, each a fraction of a full one so the total is proportional.
void passes(float amount, int most, int& count, float& each) {
    const float t = std::clamp(std::abs(amount), 0.0f, 100.0f) / 100.0f * float(most);
    count = int(std::ceil(t - 1e-4f));
    each = count > 0 ? t / float(count) : 0.0f;
}

} // namespace

namespace {

// Polish, Relax, Smooth and Balloon on P (open borders stay).
void surfaceOp(const Mesh& m, std::vector<vec3>& P, DeformOp op, const DeformOptions& o) {
    std::vector<vec3> Q, N;
    const std::vector<float> k = freedom(m, o.useMask);
    int count = 0;
    float each = 0.0f;
    switch (op) {
    case DeformOp::Smooth: {
        passes(o.amount, 20, count, each);
        const float lambda = 0.5f * each;
        // Taubin: mu from lambda and the pass-band frequency 0.1 (1/lambda + 1/mu = 0.1), a little stronger back.
        const float mu = 1.0f / (0.1f - 1.0f / std::max(lambda, 1e-3f));
        for (int i = 0; i < count; ++i) {
            step(k, P, Q, [&](uint32_t v) { return laplacian(m, P, v) * lambda; });
            if (o.keepVolume) step(k, P, Q, [&](uint32_t v) { return laplacian(m, P, v) * mu; });
        }
        break;
    }
    case DeformOp::Relax: {
        passes(o.amount, 20, count, each);
        const float lambda = 0.5f * each;
        for (int i = 0; i < count; ++i) {
            normalsOf(m, P, N);
            step(k, P, Q, [&](uint32_t v) {
                const vec3 l = laplacian(m, P, v);
                return (l - N[v] * glm::dot(N[v], l)) * lambda; // along the surface only
            });
        }
        break;
    }
    case DeformOp::Polish: {
        // Bilateral normal filtering (Zheng et al. 2011): each face's normal becomes a weighted average of the
        // normals of the faces around it, weighted down by distance and, steeply, by how different they are, so
        // the small tilts of noise average out while the faces across an edge or corner don't count. Then the
        // vertices move to fit the filtered normals (Sun et al. 2007), which keeps edges where they are.
        passes(o.amount, 6, count, each);
        const size_t nt = m.triangleCount();
        std::vector<vec3> fn(nt), fc(nt), filtered(nt);
        std::vector<float> fa(nt);
        auto faces = [&] {
            poolFor(nt, [&](size_t b, size_t e) {
                for (size_t t = b; t < e; ++t) {
                    const glm::uvec3& f = m.triangles[t];
                    const vec3 c = glm::cross(P[f.y] - P[f.x], P[f.z] - P[f.x]);
                    const float l = glm::length(c);
                    fa[t] = 0.5f * l;
                    fn[t] = l > 1e-30f ? c / l : vec3(0.0f);
                    fc[t] = (P[f.x] + P[f.y] + P[f.z]) / 3.0f;
                }
            }, kGrain);
        };
        constexpr float kSigmaNormal = 0.35f; // |n_f - n_g| of a 20 degree tilt; a 90 degree edge is 1.41
        for (int i = 0; i < count; ++i) {
            faces();
            poolFor(nt, [&](size_t b, size_t e) {
                for (size_t t = b; t < e; ++t) {
                    const glm::uvec3& f = m.triangles[t];
                    // The faces sharing a corner with t (some twice, which only weighs them a little more).
                    float spacing = 0.0f;
                    for (int c = 0; c < 3; ++c) spacing += glm::distance(P[f[c]], fc[t]);
                    spacing = std::max(spacing / 3.0f * 2.0f, 1e-20f);
                    vec3 sum(0.0f);
                    for (int c = 0; c < 3; ++c)
                        for (uint32_t g : m.vertexTriangles(f[c])) {
                            const vec3 dn = fn[g] - fn[t];
                            const vec3 dc = fc[g] - fc[t];
                            const float w = fa[g] * std::exp(-glm::dot(dc, dc) / (2.0f * spacing * spacing)) *
                                            std::exp(-glm::dot(dn, dn) / (2.0f * kSigmaNormal * kSigmaNormal));
                            sum += fn[g] * w;
                        }
                    const float l = glm::length(sum);
                    filtered[t] = l > 1e-30f ? sum / l : fn[t];
                }
            }, kGrain);
            for (int fit = 0; fit < 3; ++fit) {
                if (fit > 0) faces();
                step(k, P, Q, [&](uint32_t v) {
                    vec3 d(0.0f);
                    const IndexRing tris = m.vertexTriangles(v);
                    for (uint32_t t : tris) d += filtered[t] * glm::dot(filtered[t], fc[t] - P[v]);
                    return d * (each / float(tris.size()));
                });
            }
        }
        break;
    }
    case DeformOp::Balloon: {
        passes(o.amount, 10, count, each);
        const float diagonal = glm::length(m.bounds().extent());
        const float push = (o.amount < 0.0f ? -1.0f : 1.0f) * 0.01f * diagonal * each;
        for (int i = 0; i < count; ++i) {
            normalsOf(m, P, N);
            // Out along the normal, then the skin pulls taut (a Laplacian step): flat sides bulge, corners round.
            step(k, P, Q, [&](uint32_t v) { return N[v] * push; });
            step(k, P, Q, [&](uint32_t v) { return laplacian(m, P, v) * (0.5f * each); });
        }
        break;
    }
    default: break;
    }
}

// The eased profile: u (3 - u^2) / 2, flat at the ends; plain u otherwise.
inline float profile(float u, bool soft) {
    if (!soft) return u;
    u = std::clamp(u, -1.0f, 1.0f);
    return u * (3.0f - u * u) * 0.5f;
}

// Unit axis a (0-2) and two directions across it: e1 (+Y for X and Z, +X for Y) and e2 = a x e1.
void frameOf(int a, vec3& axis, vec3& e1, vec3& e2) {
    axis = vec3(0.0f);
    axis[a] = 1.0f;
    e1 = a == 1 ? vec3(1, 0, 0) : vec3(0, 1, 0);
    e2 = glm::cross(axis, e1);
}

// The largest |q . dir| over Q (never 0).
float extentAlong(const std::vector<vec3>& Q, const vec3& c, const vec3& dir) {
    float e = 0.0f;
    for (const vec3& p : Q) e = std::max(e, std::abs(glm::dot(p - c, dir)));
    return std::max(e, 1e-12f);
}

// One pass of an operation on Q (every vertex, unweighted) about c.
template <class F>
void mapAll(std::vector<vec3>& Q, F&& f) {
    poolFor(Q.size(), [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) Q[i] = f(Q[i]);
    }, kGrain);
}

// Move ... Depth on P: the unweighted result of every pass, then blended in by each vertex's weight.
void shapeOp(const Mesh& m, std::vector<vec3>& P, DeformOp op, const DeformOptions& o) {
    if (P.empty() || o.axes == 0) return;
    vec3 c = o.pivot;
    if (!o.hasPivot) {
        AABB box;
        for (const vec3& p : P) box.expand(p);
        c = box.center();
    }
    const float t = o.amount / 100.0f;
    const float radians = glm::radians(o.amount);
    std::vector<vec3> Q = P;
    const bool vectorOp = op == DeformOp::Move || op == DeformOp::Scale || op == DeformOp::Round || op == DeformOp::Box;
    if (vectorOp) {
        vec3 sel(0.0f);
        for (int a = 0; a < 3; ++a)
            if (o.axes & (1 << a)) sel[a] = 1.0f;
        switch (op) {
        case DeformOp::Move: {
            vec3 d(0.0f);
            for (int a = 0; a < 3; ++a)
                if (sel[a] > 0.0f) {
                    vec3 dir(0.0f);
                    dir[a] = 1.0f;
                    d[a] = t * extentAlong(Q, c, dir);
                }
            mapAll(Q, [&](const vec3& p) { return p + d; });
            break;
        }
        case DeformOp::Scale: {
            const vec3 f = glm::mix(vec3(1.0f), vec3(std::exp2(t)), sel);
            mapAll(Q, [&](const vec3& p) { return c + (p - c) * f; });
            break;
        }
        default: { // Round, Box: the chosen components toward the sphere (cube) of their mean radius
            const bool box = op == DeformOp::Box;
            auto radius = [&](const vec3& q) {
                const vec3 s = q * sel;
                return box ? std::max(std::abs(s.x), std::max(std::abs(s.y), std::abs(s.z))) : glm::length(s);
            };
            double sum = 0.0;
            for (const vec3& p : Q) sum += double(radius(p - c));
            const float r = float(sum / double(Q.size()));
            mapAll(Q, [&](const vec3& p) {
                const vec3 q = p - c;
                const float l = radius(q);
                if (l < 1e-20f) return p;
                const vec3 s = q * sel;
                return p + (s * (r / l) - s) * t;
            });
            break;
        }
        }
    } else {
        for (int a = 0; a < 3; ++a) {
            if (!(o.axes & (1 << a))) continue;
            vec3 axis, e1, e2;
            frameOf(a, axis, e1, e2);
            const float th = glm::radians(o.direction);
            const vec3 b = std::cos(th) * e1 + std::sin(th) * e2; // Bend, Shear: toward b
            const vec3 n3 = glm::cross(axis, b);
            const float ext = extentAlong(Q, c, axis);
            switch (op) {
            case DeformOp::Turn: {
                const glm::mat3 R = glm::mat3_cast(glm::angleAxis(radians, axis));
                mapAll(Q, [&](const vec3& p) { return c + R * (p - c); });
                break;
            }
            case DeformOp::Squash: {
                const float f = std::exp2(-t), g = 1.0f / std::sqrt(f);
                mapAll(Q, [&](const vec3& p) {
                    const vec3 q = p - c;
                    const float y = q[a];
                    vec3 r = q * g;
                    r[a] = y * f;
                    return c + r;
                });
                break;
            }
            case DeformOp::Twist: {
                const float half = 0.5f * radians;
                mapAll(Q, [&](const vec3& p) {
                    const vec3 q = p - c;
                    const float phi = half * profile(q[a] / ext, o.soft);
                    const float x = glm::dot(q, e1), z = glm::dot(q, e2), cs = std::cos(phi), sn = std::sin(phi);
                    return c + axis * q[a] + e1 * (x * cs - z * sn) + e2 * (x * sn + z * cs);
                });
                break;
            }
            case DeformOp::Taper: {
                mapAll(Q, [&](const vec3& p) {
                    const vec3 q = p - c;
                    const float f = std::max(0.0f, 1.0f + t * profile(q[a] / ext, o.soft));
                    return c + axis * q[a] + (q - axis * q[a]) * f;
                });
                break;
            }
            case DeformOp::Shear: {
                mapAll(Q, [&](const vec3& p) { return p + b * (t * ext * profile((p - c)[a] / ext, o.soft)); });
                break;
            }
            case DeformOp::Depth: {
                const float tt = std::clamp(t, -0.9f, 0.9f);
                mapAll(Q, [&](const vec3& p) {
                    const vec3 q = p - c;
                    const float f = 1.0f / (1.0f - 0.5f * tt * std::clamp(q[a] / ext, -1.0f, 1.0f));
                    return c + axis * q[a] + (q - axis * q[a]) * f;
                });
                break;
            }
            case DeformOp::Sag: {
                float R = 0.0f;
                for (const vec3& p : Q) {
                    const vec3 q = p - c;
                    R = std::max(R, glm::length(q - axis * q[a]));
                }
                R = std::max(R, 1e-12f);
                mapAll(Q, [&](const vec3& p) {
                    const vec3 q = p - c;
                    const float r = glm::length(q - axis * q[a]) / R;
                    return p - axis * (t * 0.5f * R * r * r);
                });
                break;
            }
            case DeformOp::Flatten: {
                float lo = std::numeric_limits<float>::max(), hi = std::numeric_limits<float>::lowest();
                for (const vec3& p : Q) {
                    lo = std::min(lo, p[a]);
                    hi = std::max(hi, p[a]);
                }
                const float depth = hi - lo, k = o.soft ? 0.08f * depth : 0.0f;
                const bool top = t >= 0.0f;
                const float level = top ? hi - std::abs(t) * depth : lo + std::abs(t) * depth;
                mapAll(Q, [&](vec3 p) {
                    // The + side cut at level (the - side for a negative amount); Soft: a polynomial smooth min.
                    const float x = top ? p[a] : -p[a], L = top ? level : -level;
                    float y = std::min(x, L);
                    if (k > 0.0f) {
                        const float h = std::clamp(0.5f + 0.5f * (L - x) / k, 0.0f, 1.0f);
                        y = glm::mix(L, x, h) - k * h * (1.0f - h);
                    }
                    p[a] = top ? y : -y;
                    return p;
                });
                break;
            }
            case DeformOp::Bend: {
                // The axis through c bends into a curve C(y) whose direction turns by phi(y) toward b; each point
                // keeps its distance x across it (toward b) and z (along n3). Uniform: an arc of radius
                // 2 ext / Phi; Soft: C tabulated by the midpoint rule.
                const float half = 0.5f * radians;
                if (std::abs(half) < 1e-7f) break;
                constexpr int kSamples = 2048;
                std::vector<glm::vec2> table;
                if (o.soft) {
                    table.resize(kSamples + 1);
                    const float dy = 2.0f * ext / kSamples;
                    const int mid = kSamples / 2;
                    table[size_t(mid)] = glm::vec2(0.0f);
                    for (int i = mid; i < kSamples; ++i) { // outward from the middle, both ways
                        const float ym = (float(i - mid) + 0.5f) * dy, phi = half * profile(ym / ext, true);
                        table[size_t(i + 1)] = table[size_t(i)] + glm::vec2(std::cos(phi), std::sin(phi)) * dy;
                        const float yn = -ym, phn = half * profile(yn / ext, true);
                        table[size_t(2 * mid - i - 1)] =
                            table[size_t(2 * mid - i)] - glm::vec2(std::cos(phn), std::sin(phn)) * dy;
                    }
                }
                const float kc = half / ext; // curvature of the uniform bend
                mapAll(Q, [&](const vec3& p) {
                    const vec3 q = p - c;
                    const float y = q[a], x = glm::dot(q, b), z = glm::dot(q, n3);
                    const float phi = half * profile(y / ext, o.soft);
                    glm::vec2 C; // (along the axis, toward b)
                    if (o.soft) {
                        const float f = std::clamp((y / ext + 1.0f) * 0.5f * kSamples, 0.0f, float(kSamples));
                        const int i = std::min(int(f), kSamples - 1);
                        C = glm::mix(table[size_t(i)], table[size_t(i + 1)], f - float(i));
                    } else {
                        C = glm::vec2(std::sin(kc * y), 1.0f - std::cos(kc * y)) / kc;
                    }
                    const float cs = std::cos(phi), sn = std::sin(phi);
                    return c + axis * (C.x - x * sn) + b * (C.y + x * cs) + n3 * z;
                });
                break;
            }
            default: break;
            }
        }
    }
    // Weighted in: 1 - mask (useMask), 0 when hidden.
    const bool hidden = m.hasHidden(), masked = o.useMask && m.hasMask();
    poolFor(P.size(), [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            const uint32_t v = uint32_t(i);
            float w = masked ? 1.0f - m.maskAt(v) : 1.0f;
            if (hidden && m.vertexHidden(v)) w = 0.0f;
            P[i] = w >= 1.0f ? Q[i] : P[i] + (Q[i] - P[i]) * w;
        }
    }, kGrain);
}

void runOp(const Mesh& m, std::vector<vec3>& P, DeformOp op, const DeformOptions& o) {
    if (deformUsesAxes(op)) shapeOp(m, P, op, o);
    else surfaceOp(m, P, op, o);
}

} // namespace

std::vector<vec3> deformPositions(const Mesh& m, DeformOp op, const DeformOptions& o) {
    std::vector<vec3> P = m.positions;
    if (m.vertexCount() == 0 || m.triangleCount() == 0) return P;
    runOp(m, P, op, o);
    return P;
}

std::vector<vec3> deformStackPositions(const Mesh& m, const std::vector<DeformStep>& steps) {
    std::vector<vec3> P = m.positions;
    if (m.vertexCount() == 0 || m.triangleCount() == 0) return P;
    for (const DeformStep& st : steps) runOp(m, P, st.op, st.options);
    return P;
}

size_t deformActivePart(Document& doc, DeformOp op, const DeformOptions& o, std::string& error) {
    return deformActivePart(doc, {DeformStep{op, o}}, error, deformName(op));
}

namespace {

// Why the active Part can't be deformed now ("" when it can).
bool canDeform(Document& doc, const std::string& name, std::string& error) {
    const Mesh& mesh = doc.mesh;
    if (doc.sculptor.strokeActive()) error = name + ": finish the stroke first";
    else if (mesh.vertexCount() == 0 || mesh.triangleCount() == 0) error = name + ": the active Part is empty";
    else if (!mesh.forgespheres.empty()) error = name + ": the active Part is a Forge Sphere Part (Make Mesh first)";
    else if (mesh.micro) error = name + ": the active Part has a micro level (Bake Micro or Delete Micro first)";
    else return true;
    return false;
}

// Moves the active Part's vertices to P through the Sculptor as one undo step labelled `name`.
size_t commitPositions(Document& doc, const std::vector<vec3>& P, const std::string& name, bool masked, std::string& error) {
    Mesh& mesh = doc.mesh;
    std::vector<uint32_t> verts;
    std::vector<vec3> d;
    for (uint32_t v = 0; v < mesh.vertexCount(); ++v) {
        const vec3 dv = P[v] - mesh.positions[v];
        if (dv != vec3(0.0f) && std::isfinite(dv.x + dv.y + dv.z)) {
            verts.push_back(v);
            d.push_back(dv);
        }
    }
    if (verts.empty()) {
        error = name + ": nothing moved" + (masked && mesh.hasMask() ? " (is everything masked?)" : "");
        return 0;
    }
    if (!doc.sculptor.beginStroke(mesh, name, doc.activeLayer)) {
        error = name + ": the active layer's weight is 0";
        return 0;
    }
    doc.sculptor.moveVertices(mesh, doc.bvh, verts, d);
    UndoRecord r = doc.sculptor.endStroke();
    if (!r.empty()) doc.undo.push(std::move(r));
    return verts.size();
}

} // namespace

size_t deformActivePart(Document& doc, const std::vector<DeformStep>& steps, std::string& error, const std::string& label) {
    if (!canDeform(doc, label, error)) return 0;
    doc.syncBvh();
    bool masked = false;
    for (const DeformStep& st : steps) masked |= st.options.useMask;
    return commitPositions(doc, deformStackPositions(doc.mesh, steps), label, masked, error);
}

size_t resizeActivePart(Document& doc, const glm::vec3& size, bool useMask, std::string& error) {
    if (!canDeform(doc, "Size", error)) return 0;
    const Mesh& m = doc.mesh;
    const AABB box = m.bounds();
    const vec3 now = box.max - box.min, c = box.center();
    vec3 f(1.0f);
    for (int a = 0; a < 3; ++a)
        if (size[a] > 0.0f) {
            if (now[a] < 1e-12f) {
                error = std::string("Size: the Part is flat along ") + char('X' + a) + ", it can't be stretched to a size";
                return 0;
            }
            f[a] = size[a] / now[a];
        }
    doc.syncBvh();
    std::vector<vec3> P = m.positions;
    const bool hidden = m.hasHidden(), masked = useMask && m.hasMask();
    poolFor(P.size(), [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            const uint32_t v = uint32_t(i);
            float w = masked ? 1.0f - m.maskAt(v) : 1.0f;
            if (hidden && m.vertexHidden(v)) w = 0.0f;
            const vec3 q = c + (P[i] - c) * f;
            P[i] = w >= 1.0f ? q : P[i] + (q - P[i]) * w;
        }
    }, kGrain);
    return commitPositions(doc, P, "Size", useMask, error);
}

namespace {

// axes=, direction=, soft=, pivot= into o (shape operations).
bool shapeArgs(const Args& a, DeformOptions& o, std::string& error) {
    const std::string axes = argString(a, "axes", "y");
    o.axes = 0;
    for (char ch : axes) {
        const int k = std::tolower(static_cast<unsigned char>(ch)) - 'x';
        if (k < 0 || k > 2) {
            error = "Bad axes '" + axes + "' (any of x, y, z)";
            return false;
        }
        o.axes |= uint8_t(1 << k);
    }
    if (o.axes == 0) o.axes = DeformY;
    o.direction = float(argNumber(a, "direction", 0.0));
    o.soft = argBool(a, "soft", false);
    const std::string pivot = argString(a, "pivot");
    if (!pivot.empty()) {
        if (!parseVec3(pivot, o.pivot)) {
            error = "Bad pivot '" + pivot + "' (expected x,y,z)";
            return false;
        }
        o.hasPivot = true;
    }
    return true;
}

} // namespace

void registerDeformCommands(CommandRegistry& reg) {
    using T = ParamSpec::Type;
    static const char* const kWhat[kDeformOpCount] = {
        "Polish the active Part: flatten fine noise and small bumps while keeping flat areas and sharp edges.",
        "Relax the active Part: even out the spacing of its vertices along the surface without changing its shape.",
        "Smooth the whole active Part (Laplacian; keep_volume=true for Taubin smoothing, which doesn't shrink it).",
        "Balloon: fill the active Part out toward a rounder volume; a negative amount lets the air out.",
        "Move the active Part along the axes by amount percent of its half-size.",
        "Turn the active Part by amount degrees about each axis in turn (X, then Y, then Z) through the pivot.",
        "Scale the active Part along the axes: 100 doubles, -100 halves.",
        "Squash the active Part along each axis, keeping its volume (it widens across): 100 halves the length, "
        "-100 doubles it.",
        "Bend the active Part along each axis by amount degrees end to end, toward direction (degrees about the "
        "axis); soft=true bends the middle most.",
        "Twist the active Part about each axis by amount degrees end to end; soft=true eases toward the ends.",
        "Taper the active Part along each axis: the + end grows by amount percent, the - end shrinks by it.",
        "Shear the active Part along each axis toward direction: 100 leans it 45 degrees.",
        "Round the active Part: its axes' components move toward a sphere (one axis left out: a cylinder about it); "
        "negative moves away.",
        "Box the active Part: its axes' components move toward a cube (one axis left out: a square prism).",
        "Flatten the active Part: cut its + side flat along each axis by amount percent of its depth (negative: the "
        "- side); soft=true rounds the edge of the cut.",
        "Sag the active Part along -axis: parts far from the axis droop (negative lifts them).",
        "Depth: a perspective along each axis, the + end wider for a positive amount.",
    };
    for (int i = 0; i < kDeformOpCount; ++i) {
        const DeformOp op = DeformOp(i);
        float lo, hi, def;
        deformRange(op, lo, hi, def);
        char range[96];
        std::snprintf(range, sizeof range, "How much, %g to %g (default %g).", double(lo), double(hi), double(def));
        std::vector<ParamSpec> params = {{"amount", T::Number, range, false},
                                         {"masked", T::Bool, "Masked parts stay (default true; false ignores the mask).", false}};
        if (op == DeformOp::Smooth) params.push_back({"keep_volume", T::Bool, "Taubin smoothing (default false).", false});
        if (deformUsesAxes(op)) {
            params.push_back({"axes", T::String, "Any of x, y, z (default y).", false});
            params.push_back({"pivot", T::String, "x,y,z (default: the Part's bounds centre).", false});
        }
        if (deformUsesDirection(op))
            params.push_back({"direction", T::Number, "Degrees about the axis (0: toward +Y, +X for the Y axis).", false});
        if (deformUsesSoft(op)) params.push_back({"soft", T::Bool, "Eased profile (default false).", false});
        const std::string tail = deformUsesAxes(op) ? " Mask-aware, hidden faces stay; one undo step."
                                                    : " Mask-aware (partly masked vertices move partly), hidden faces and "
                                                      "open borders stay; one undo step.";
        reg.add({deformCommand(op), std::string(kWhat[i]) + tail, params,
                 [op, lo, hi, def](Document& doc, const Args& a) -> CommandResult {
                     DeformOptions o;
                     o.amount = float(std::clamp(argNumber(a, "amount", double(def)), double(lo), double(hi)));
                     o.useMask = argBool(a, "masked", true);
                     o.keepVolume = argBool(a, "keep_volume", false);
                     std::string err;
                     if (deformUsesAxes(op) && !shapeArgs(a, o, err)) return {false, err};
                     if (o.amount == 0.0f) return {true, std::string(deformName(op)) + ": amount 0, nothing to do"};
                     const size_t moved = deformActivePart(doc, op, o, err);
                     if (moved == 0) return {false, err};
                     return {true, std::string(deformName(op)) + " " + std::to_string(int(std::lround(o.amount))) + ": moved " +
                                       std::to_string(moved) + " vertices"};
                 }});
    }
    reg.add({"deform_size",
             "Resize the active Part to exact dimensions in scene units about its bounds centre (x, y, z; an axis "
             "left out keeps its size; uniform=N scales evenly so the largest side is N). Mask-aware; one undo step.",
             {{"x", T::Number, "Width (X), scene units.", false},
              {"y", T::Number, "Height (Y), scene units.", false},
              {"z", T::Number, "Depth (Z), scene units.", false},
              {"uniform", T::Number, "The largest side, scene units, the others in proportion.", false},
              {"masked", T::Bool, "Masked parts stay (default true).", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 glm::vec3 size(argNumber(a, "x", 0.0), argNumber(a, "y", 0.0), argNumber(a, "z", 0.0));
                 if (const double u = argNumber(a, "uniform", 0.0); u > 0.0 && doc.mesh.vertexCount() > 0) {
                     const AABB box = doc.mesh.bounds();
                     const vec3 now = box.max - box.min;
                     const float big = std::max(now.x, std::max(now.y, now.z));
                     if (big > 1e-12f) size = now * float(u / big);
                 }
                 for (int k = 0; k < 3; ++k)
                     if (!std::isfinite(size[k]) || size[k] < 0.0f) return {false, "Size: give positive sizes"};
                 if (size == glm::vec3(0.0f)) return {false, "Size: give x, y, z or uniform"};
                 std::string err;
                 const size_t moved = resizeActivePart(doc, size, argBool(a, "masked", true), err);
                 if (moved == 0) return {false, err};
                 const AABB box = doc.mesh.bounds();
                 const vec3 now = box.max - box.min;
                 char msg[160];
                 std::snprintf(msg, sizeof msg, "Size %.4g x %.4g x %.4g: moved %zu vertices", double(now.x), double(now.y),
                               double(now.z), moved);
                 return {true, msg};
             }});
    reg.add({"deform_stack",
             "Run several whole-Part deformations in order as one undo step, e.g. ops=\"bend 40 y soft dir=90; twist "
             "90 y; taper -30 xz\" (name, amount, axes letters, soft, dir=degrees, volume for Smooth, nomask; "
             "names: polish relax smooth balloon move turn scale squash bend twist taper shear round box flatten sag "
             "depth).",
             {{"ops", T::String, "The steps, separated by ';'.", true},
              {"pivot", T::String, "x,y,z for every shape step (default: the Part's bounds centre at each step).", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 std::vector<DeformStep> steps;
                 std::string err;
                 if (!parseDeformStack(argString(a, "ops"), steps, err)) return {false, err};
                 const std::string pivot = argString(a, "pivot");
                 if (!pivot.empty()) {
                     glm::vec3 p;
                     if (!parseVec3(pivot, p)) return {false, "Bad pivot '" + pivot + "' (expected x,y,z)"};
                     for (DeformStep& st : steps) {
                         st.options.pivot = p;
                         st.options.hasPivot = true;
                     }
                 }
                 const size_t moved = deformActivePart(doc, steps, err);
                 if (moved == 0) return {false, err};
                 return {true, "Deform stack (" + std::to_string(steps.size()) + (steps.size() == 1 ? " step" : " steps") +
                                   "): moved " + std::to_string(moved) + " vertices"};
             }});
}

} // namespace sf
