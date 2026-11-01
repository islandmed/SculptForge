// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Face.h"

#include "core/Document.h"
#include "core/Multires.h"
#include "core/SurfaceQuery.h"
#include "core/ThreadPool.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace sf::assets {
extern const unsigned char faceTemplateData[];
extern const unsigned long long faceTemplateData_size;
} // namespace sf::assets

namespace sf {

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------

const std::vector<std::string>& arkitBlendShapeNames() {
    static const std::vector<std::string> names = {
        "browDownLeft",      "browDownRight",     "browInnerUp",         "browOuterUpLeft",     "browOuterUpRight",
        "cheekPuff",         "cheekSquintLeft",   "cheekSquintRight",    "eyeBlinkLeft",        "eyeBlinkRight",
        "eyeLookDownLeft",   "eyeLookDownRight",  "eyeLookInLeft",       "eyeLookInRight",      "eyeLookOutLeft",
        "eyeLookOutRight",   "eyeLookUpLeft",     "eyeLookUpRight",      "eyeSquintLeft",       "eyeSquintRight",
        "eyeWideLeft",       "eyeWideRight",      "jawForward",          "jawLeft",             "jawOpen",
        "jawRight",          "mouthClose",        "mouthDimpleLeft",     "mouthDimpleRight",    "mouthFrownLeft",
        "mouthFrownRight",   "mouthFunnel",       "mouthLeft",           "mouthLowerDownLeft",  "mouthLowerDownRight",
        "mouthPressLeft",    "mouthPressRight",   "mouthPucker",         "mouthRight",          "mouthRollLower",
        "mouthRollUpper",    "mouthShrugLower",   "mouthShrugUpper",     "mouthSmileLeft",      "mouthSmileRight",
        "mouthStretchLeft",  "mouthStretchRight", "mouthUpperUpLeft",    "mouthUpperUpRight",   "noseSneerLeft",
        "noseSneerRight",    "tongueOut"};
    return names;
}

int arkitBlendShapeIndex(std::string_view name) {
    const auto& n = arkitBlendShapeNames();
    for (size_t i = 0; i < n.size(); ++i)
        if (n[i] == name) return int(i);
    return -1;
}

std::string arkitMirrorName(std::string_view name) {
    auto swapSuffix = [&](std::string_view from, std::string_view to) -> std::string {
        return std::string(name.substr(0, name.size() - from.size())) + std::string(to);
    };
    if (name.ends_with("Left")) return swapSuffix("Left", "Right");
    if (name.ends_with("Right")) return swapSuffix("Right", "Left");
    return std::string(name);
}

// ---------------------------------------------------------------------------
// Template
// ---------------------------------------------------------------------------

namespace {

struct Reader {
    const uint8_t* p;
    const uint8_t* end;
    bool ok = true;
    template <class T> T get() {
        T v{};
        if (size_t(end - p) < sizeof(T)) {
            ok = false;
            return v;
        }
        std::memcpy(&v, p, sizeof(T)); // the file is little endian, like every platform SculptForge builds for
        p += sizeof(T);
        return v;
    }
};

} // namespace

bool parseFaceTemplate(const uint8_t* data, size_t size, FaceTemplate& out, std::string& error) {
    Reader r{data, data + size};
    if (size < 8 || std::memcmp(data, "SFFACE01", 8) != 0) {
        error = "Not a SculptForge face template";
        return false;
    }
    r.p += 8;
    const uint32_t nv = r.get<uint32_t>(), nt = r.get<uint32_t>(), ns = r.get<uint32_t>();
    FaceTemplate t;
    t.faceEnd = r.get<uint32_t>();
    t.skinEnd = r.get<uint32_t>();
    t.mouthEnd = r.get<uint32_t>();
    const uint32_t eyesEnd = r.get<uint32_t>();
    if (!r.ok || nv == 0 || nv > 65536 || eyesEnd != nv || t.faceEnd > t.skinEnd || t.skinEnd > t.mouthEnd ||
        t.mouthEnd > nv || ns > 256) {
        error = "Bad face template header";
        return false;
    }
    t.neutral.resize(nv);
    for (auto& p : t.neutral) p = {r.get<float>(), r.get<float>(), r.get<float>()};
    t.triangles.resize(nt);
    for (auto& tri : t.triangles) {
        tri = {r.get<uint16_t>(), r.get<uint16_t>(), r.get<uint16_t>()};
        if (tri.x >= nv || tri.y >= nv || tri.z >= nv) r.ok = false;
    }
    t.lowerTeethCentroid = {r.get<float>(), r.get<float>(), r.get<float>()};
    t.upperTeethY = r.get<float>();
    for (uint32_t s = 0; s < ns && r.ok; ++s) {
        FaceTemplate::Shape shape;
        const uint8_t len = r.get<uint8_t>();
        if (size_t(r.end - r.p) < len) {
            r.ok = false;
            break;
        }
        shape.name.assign(reinterpret_cast<const char*>(r.p), len);
        r.p += len;
        const float step = r.get<float>();
        const float qx = r.get<float>(), qy = r.get<float>(), qz = r.get<float>(), qw = r.get<float>();
        shape.jawRotation = glm::quat(qw, qx, qy, qz);
        shape.jawTranslation = {r.get<float>(), r.get<float>(), r.get<float>()};
        const uint32_t count = r.get<uint32_t>();
        if (count > nv) {
            r.ok = false;
            break;
        }
        shape.index.resize(count);
        for (auto& i : shape.index) {
            i = r.get<uint16_t>();
            if (i >= nv) r.ok = false;
        }
        shape.offset.resize(count);
        for (auto& o : shape.offset) {
            // One read per statement: the order function arguments are evaluated in is unspecified (x86 compilers
            // go right to left), which swapped x and z there.
            const float x = r.get<int16_t>(), y = r.get<int16_t>(), z = r.get<int16_t>();
            o = step * glm::vec3(x, y, z);
        }
        t.shapes.push_back(std::move(shape));
    }
    if (!r.ok) {
        error = "Truncated or corrupt face template";
        return false;
    }
    out = std::move(t);
    return true;
}

namespace {

// The template with what the fit and the transfer need: adjacency weights (per vertex pair, the number of
// triangles sharing the edge) as CSR, and the triangles around each vertex.
struct TemplateData {
    FaceTemplate t;
    std::vector<uint32_t> rowStart, col; // neighbours
    std::vector<float> weight;
    std::vector<float> degree; // sum of the row's weights
    std::vector<uint32_t> triStart, triList; // triangles around each vertex
};

const TemplateData& templateData() {
    static const TemplateData data = [] {
        TemplateData d;
        std::string error;
        if (!parseFaceTemplate(assets::faceTemplateData, size_t(assets::faceTemplateData_size), d.t, error)) return d;
        const size_t nv = d.t.neutral.size();
        std::vector<std::vector<std::pair<uint32_t, float>>> rows(nv);
        std::vector<std::vector<uint32_t>> tris(nv);
        auto add = [&](uint32_t i, uint32_t j) {
            for (auto& [c, w] : rows[i])
                if (c == j) {
                    w += 1.0f;
                    return;
                }
            rows[i].push_back({j, 1.0f});
        };
        for (uint32_t ti = 0; ti < d.t.triangles.size(); ++ti) {
            const glm::uvec3 tri = d.t.triangles[ti];
            for (int a = 0; a < 3; ++a) {
                tris[tri[a]].push_back(ti);
                for (int b = 0; b < 3; ++b)
                    if (a != b) add(tri[a], tri[b]);
            }
        }
        d.rowStart.push_back(0);
        d.triStart.push_back(0);
        for (size_t i = 0; i < nv; ++i) {
            std::sort(rows[i].begin(), rows[i].end());
            float deg = 0.0f;
            for (auto [c, w] : rows[i]) {
                d.col.push_back(c);
                d.weight.push_back(w);
                deg += w;
            }
            d.degree.push_back(deg);
            d.rowStart.push_back(uint32_t(d.col.size()));
            d.triList.insert(d.triList.end(), tris[i].begin(), tris[i].end());
            d.triStart.push_back(uint32_t(d.triList.size()));
        }
        return d;
    }();
    return data;
}

} // namespace

const FaceTemplate& faceTemplate() { return templateData().t; }

// ---------------------------------------------------------------------------
// Landmarks
// ---------------------------------------------------------------------------

namespace {
struct LandmarkInfo {
    const char* key;
    const char* label;
    uint32_t vertex; // ICT-FaceKit vertex (its Multi-PIE 68 list: 36, 39, 42, 45, nose tip, 48, 54, 8)
};
constexpr LandmarkInfo kLandmarks[kFaceLandmarkCount] = {
    {"eye_outer_right", "Right eye, outer corner", 1507}, {"eye_inner_right", "Right eye, inner corner", 1528},
    {"eye_inner_left", "Left eye, inner corner", 3742},   {"eye_outer_left", "Left eye, outer corner", 3721},
    {"nose_tip", "Nose tip", 4841},                        {"mouth_right", "Mouth, right corner", 5708},
    {"mouth_left", "Mouth, left corner", 6213},            {"chin", "Chin (front, lowest point)", 966},
};
} // namespace

const char* faceLandmarkKey(int i) { return i >= 0 && i < kFaceLandmarkCount ? kLandmarks[i].key : ""; }
const char* faceLandmarkLabel(int i) { return i >= 0 && i < kFaceLandmarkCount ? kLandmarks[i].label : ""; }
uint32_t faceLandmarkVertex(int i) { return i >= 0 && i < kFaceLandmarkCount ? kLandmarks[i].vertex : 0; }

int FaceFitLandmarks::count() const { return int(std::count(set.begin(), set.end(), true)); }

Json FaceFitLandmarks::toJson() const {
    Json j = Json::object();
    for (int i = 0; i < kFaceLandmarkCount; ++i) {
        if (!set[size_t(i)]) continue;
        Json p = Json::array();
        for (int k = 0; k < 3; ++k) p.push(double(position[size_t(i)][k]));
        j.set(kLandmarks[i].key, std::move(p));
    }
    return j;
}

bool FaceFitLandmarks::fromJson(const Json& j, FaceFitLandmarks& out, std::string& error) {
    FaceFitLandmarks l;
    if (!j.isObject()) {
        error = "Landmarks must be a JSON object like {\"nose_tip\":[0,1.6,0.1]}";
        return false;
    }
    for (const auto& [key, value] : j.members()) {
        int index = -1;
        for (int i = 0; i < kFaceLandmarkCount; ++i)
            if (key == kLandmarks[i].key) index = i;
        if (index < 0) {
            error = "Unknown landmark '" + key + "' (eye_outer_right, eye_inner_right, eye_inner_left, eye_outer_left, "
                    "nose_tip, mouth_right, mouth_left, chin)";
            return false;
        }
        if (!value.isArray() || value.size() != 3 || !value[0].isNumber() || !value[1].isNumber() || !value[2].isNumber()) {
            error = "Landmark '" + key + "' must be [x, y, z]";
            return false;
        }
        l.position[size_t(index)] = {float(value[0].asNumber()), float(value[1].asNumber()), float(value[2].asNumber())};
        l.set[size_t(index)] = true;
    }
    out = l;
    return true;
}

// ---------------------------------------------------------------------------
// Small linear algebra
// ---------------------------------------------------------------------------

namespace {

// Eigen decomposition of a symmetric 4x4 matrix (cyclic Jacobi): the eigenvector of the largest eigenvalue.
glm::dvec4 largestEigenvector(double a[4][4]) {
    double v[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    for (int sweep = 0; sweep < 50; ++sweep) {
        double off = 0.0;
        for (int p = 0; p < 4; ++p)
            for (int q = p + 1; q < 4; ++q) off += a[p][q] * a[p][q];
        if (off < 1e-30) break;
        for (int p = 0; p < 4; ++p)
            for (int q = p + 1; q < 4; ++q) {
                if (std::abs(a[p][q]) < 1e-300) continue;
                const double theta = (a[q][q] - a[p][p]) / (2.0 * a[p][q]);
                const double t = (theta >= 0 ? 1.0 : -1.0) / (std::abs(theta) + std::sqrt(theta * theta + 1.0));
                const double c = 1.0 / std::sqrt(t * t + 1.0), s = t * c;
                for (int k = 0; k < 4; ++k) { // A = J^T A J
                    const double akp = a[k][p], akq = a[k][q];
                    a[k][p] = c * akp - s * akq;
                    a[k][q] = s * akp + c * akq;
                }
                for (int k = 0; k < 4; ++k) {
                    const double apk = a[p][k], aqk = a[q][k];
                    a[p][k] = c * apk - s * aqk;
                    a[q][k] = s * apk + c * aqk;
                }
                for (int k = 0; k < 4; ++k) {
                    const double vkp = v[k][p], vkq = v[k][q];
                    v[k][p] = c * vkp - s * vkq;
                    v[k][q] = s * vkp + c * vkq;
                }
            }
    }
    int best = 0;
    for (int i = 1; i < 4; ++i)
        if (a[i][i] > a[best][best]) best = i;
    return {v[0][best], v[1][best], v[2][best], v[3][best]};
}

// Weighted least-squares similarity (or rigid, `withScale` false) taking src to dst (Horn's quaternion method).
Similarity solveSimilarity(const std::vector<glm::vec3>& src, const std::vector<glm::vec3>& dst,
                           const std::vector<float>& w, bool withScale) {
    Similarity s;
    double wsum = 0.0;
    glm::dvec3 cs(0.0), cd(0.0);
    for (size_t i = 0; i < src.size(); ++i) {
        wsum += w[i];
        cs += double(w[i]) * glm::dvec3(src[i]);
        cd += double(w[i]) * glm::dvec3(dst[i]);
    }
    if (wsum <= 0.0) return s;
    cs /= wsum;
    cd /= wsum;
    double S[3][3] = {};
    double ss = 0.0;
    for (size_t i = 0; i < src.size(); ++i) {
        const glm::dvec3 a = glm::dvec3(src[i]) - cs, b = glm::dvec3(dst[i]) - cd;
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) S[r][c] += w[i] * a[r] * b[c];
        ss += w[i] * glm::dot(a, a);
    }
    double N[4][4] = {
        {S[0][0] + S[1][1] + S[2][2], S[1][2] - S[2][1], S[2][0] - S[0][2], S[0][1] - S[1][0]},
        {S[1][2] - S[2][1], S[0][0] - S[1][1] - S[2][2], S[0][1] + S[1][0], S[2][0] + S[0][2]},
        {S[2][0] - S[0][2], S[0][1] + S[1][0], -S[0][0] + S[1][1] - S[2][2], S[1][2] + S[2][1]},
        {S[0][1] - S[1][0], S[2][0] + S[0][2], S[1][2] + S[2][1], -S[0][0] - S[1][1] + S[2][2]}};
    const glm::dvec4 q = largestEigenvector(N); // (w, x, y, z)
    const glm::dquat dq = glm::normalize(glm::dquat(q.x, q.y, q.z, q.w));
    const glm::dmat3 R = glm::mat3_cast(dq);
    double scale = 1.0;
    if (withScale && ss > 1e-30) {
        double num = 0.0;
        for (size_t i = 0; i < src.size(); ++i)
            num += w[i] * glm::dot(glm::dvec3(dst[i]) - cd, R * (glm::dvec3(src[i]) - cs));
        scale = std::max(num / ss, 1e-9);
    }
    s.rotation = glm::mat3(R);
    s.scale = float(scale);
    s.translation = glm::vec3(cd - scale * (R * cs));
    return s;
}

// Jacobi-preconditioned conjugate gradients on 3 right-hand sides at once (each converging on its own) for a
// symmetric positive definite operator `apply(x, out)` with diagonal `diag`. `x` holds the start and receives the
// solution. Serial and in a fixed order, so the result never depends on the thread count.
template <class Apply>
int solvePcg(const Apply& apply, const std::vector<double>& diag, const std::vector<glm::dvec3>& b,
             std::vector<glm::dvec3>& x, double tolerance, int maxIterations) {
    const size_t n = b.size();
    std::vector<glm::dvec3> r(n), z(n), p(n), ap(n);
    apply(x, ap);
    glm::dvec3 bnorm(0.0), rz(0.0);
    for (size_t i = 0; i < n; ++i) {
        r[i] = b[i] - ap[i];
        z[i] = r[i] / diag[i];
        p[i] = z[i];
        rz += r[i] * z[i];
        bnorm += b[i] * b[i];
    }
    const glm::dvec3 limit = glm::max(bnorm, glm::dvec3(1e-30)) * (tolerance * tolerance);
    int it = 0;
    for (; it < maxIterations; ++it) {
        glm::dvec3 rr(0.0);
        for (size_t i = 0; i < n; ++i) rr += r[i] * r[i];
        if (rr.x <= limit.x && rr.y <= limit.y && rr.z <= limit.z) break;
        apply(p, ap);
        glm::dvec3 pap(0.0);
        for (size_t i = 0; i < n; ++i) pap += p[i] * ap[i];
        glm::dvec3 alpha(0.0);
        for (int k = 0; k < 3; ++k) alpha[k] = pap[k] > 1e-300 ? rz[k] / pap[k] : 0.0;
        glm::dvec3 rzNew(0.0);
        for (size_t i = 0; i < n; ++i) {
            x[i] += alpha * p[i];
            r[i] -= alpha * ap[i];
            z[i] = r[i] / diag[i];
            rzNew += r[i] * z[i];
        }
        glm::dvec3 beta(0.0);
        for (int k = 0; k < 3; ++k) beta[k] = rz[k] > 1e-300 ? rzNew[k] / rz[k] : 0.0;
        rz = rzNew;
        for (size_t i = 0; i < n; ++i) p[i] = z[i] + beta * p[i];
    }
    return it;
}

std::vector<glm::vec3> vertexNormals(const std::vector<glm::vec3>& pos, const std::vector<glm::uvec3>& tris) {
    std::vector<glm::vec3> n(pos.size(), glm::vec3(0.0f));
    for (const auto& t : tris) {
        const glm::vec3 f = glm::cross(pos[t.y] - pos[t.x], pos[t.z] - pos[t.x]);
        n[t.x] += f;
        n[t.y] += f;
        n[t.z] += f;
    }
    for (auto& v : n) {
        const float l = glm::length(v);
        v = l > 1e-20f ? v / l : glm::vec3(0.0f);
    }
    return n;
}

} // namespace

// ---------------------------------------------------------------------------
// Fit
// ---------------------------------------------------------------------------

namespace {

struct Correspondences {
    std::vector<glm::vec3> target;
    std::vector<float> weight; // 0 = none
};

// Closest points on the sculpt for template vertices [0, end) (every `stride`-th), accepted within `maxDist` and
// when the sculpt faces the same way (dot of normals > minDot).
void findCorrespondences(const TriangleBVH& sculpt, const std::vector<glm::vec3>& pos, const std::vector<glm::vec3>& nrm,
                         uint32_t end, uint32_t stride, float maxDist, float minDot, Correspondences& out) {
    out.target.assign(pos.size(), glm::vec3(0.0f));
    out.weight.assign(pos.size(), 0.0f);
    poolFor(end, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            if (i % stride) continue;
            SurfaceHit hit;
            if (!sculpt.closestPoint(pos[i], maxDist, hit)) continue;
            const glm::vec3 fn = sculpt.faceNormal(hit.prim);
            if (glm::dot(fn, nrm[i]) < minDot) continue;
            out.target[i] = hit.position;
            out.weight[i] = 1.0f;
        }
    }, 512);
}

struct FitScore {
    float rms = 0.0f;      // template units (cm)
    float coverage = 0.0f; // of the face vertices sampled
};

// Similarity ICP over the face vertices (every `stride`-th); returns the last iteration's score.
FitScore similarityIcp(const TriangleBVH& sculpt, const FaceTemplate& t, const std::vector<glm::vec3>& templNormals,
                       const FaceFitLandmarks* landmarks, Similarity& align, int iterations, uint32_t stride) {
    FitScore score;
    std::vector<glm::vec3> pos(t.neutral.size()), nrm(t.neutral.size());
    Correspondences c;
    for (int it = 0; it < iterations; ++it) {
        for (uint32_t i = 0; i < t.faceEnd; i += stride) {
            pos[i] = align.apply(t.neutral[i]);
            nrm[i] = align.rotation * templNormals[i];
        }
        const float maxDist = align.scale * glm::mix(4.0f, 1.0f, float(it) / float(std::max(iterations - 1, 1)));
        findCorrespondences(sculpt, pos, nrm, t.faceEnd, stride, maxDist, 0.3f, c);
        std::vector<glm::vec3> src, dst;
        std::vector<float> w;
        std::vector<float> dists;
        for (uint32_t i = 0; i < t.faceEnd; i += stride)
            if (c.weight[i] > 0.0f) dists.push_back(glm::distance(pos[i], c.target[i]));
        size_t sampled = (t.faceEnd + stride - 1) / stride;
        score.coverage = float(dists.size()) / float(std::max<size_t>(sampled, 1));
        if (dists.size() < 30) {
            score.rms = 1e9f;
            return score;
        }
        // Trimmed: the closest 85% only.
        std::vector<float> sorted = dists;
        const size_t keep = std::max<size_t>(30, sorted.size() * 85 / 100);
        std::nth_element(sorted.begin(), sorted.begin() + long(keep - 1), sorted.end());
        const float cut = sorted[keep - 1];
        double sq = 0.0;
        size_t n = 0;
        for (uint32_t i = 0; i < t.faceEnd; i += stride) {
            if (c.weight[i] <= 0.0f) continue;
            const float d = glm::distance(pos[i], c.target[i]);
            if (d > cut) continue;
            src.push_back(t.neutral[i]);
            dst.push_back(c.target[i]);
            w.push_back(1.0f);
            sq += double(d) * d;
            ++n;
        }
        score.rms = float(std::sqrt(sq / double(std::max<size_t>(n, 1)))) / align.scale;
        if (landmarks) {
            const float lw = float(n) * 0.04f; // all landmarks together weigh about a third of the surface
            for (int l = 0; l < kFaceLandmarkCount; ++l)
                if (landmarks->set[size_t(l)]) {
                    src.push_back(t.neutral[kLandmarks[l].vertex]);
                    dst.push_back(landmarks->position[size_t(l)]);
                    w.push_back(lw);
                }
        }
        align = solveSimilarity(src, dst, w, true);
    }
    return score;
}

} // namespace

bool fitFaceTemplate(const std::vector<glm::vec3>& positions, const std::vector<glm::uvec3>& triangles,
                     const FaceFitLandmarks* landmarksIn, FaceFit& out, std::string& error) {
    const TemplateData& td = templateData();
    const FaceTemplate& t = td.t;
    if (t.neutral.empty()) {
        error = "The face template is missing from this build";
        return false;
    }
    if (triangles.empty()) {
        error = "The mesh is empty";
        return false;
    }
    const FaceFitLandmarks* landmarks = landmarksIn && landmarksIn->count() >= 3 ? landmarksIn : nullptr;
    if (landmarksIn && landmarksIn->count() > 0 && !landmarks) {
        error = "Place at least 3 landmarks (or none, for the automatic fit)";
        return false;
    }
    TriangleBVH sculpt;
    sculpt.build(positions, triangles);
    const std::vector<glm::vec3> templNormals = vertexNormals(t.neutral, t.triangles);

    // --- 1. The similarity transform ---
    Similarity align;
    if (landmarks) {
        std::vector<glm::vec3> src, dst;
        std::vector<float> w;
        for (int l = 0; l < kFaceLandmarkCount; ++l)
            if (landmarks->set[size_t(l)]) {
                src.push_back(t.neutral[kLandmarks[l].vertex]);
                dst.push_back(landmarks->position[size_t(l)]);
                w.push_back(1.0f);
            }
        align = solveSimilarity(src, dst, w, true);
        similarityIcp(sculpt, t, templNormals, landmarks, align, 12, 3);
    } else {
        // Automatic: the head looks down +Z with Y up (SculptForge's front view). The nose tip is the most forward
        // point near the middle, above the lower quarter; the scale comes from the nose tip to the top of the head.
        AABB b;
        for (const auto& tri : triangles)
            for (int k = 0; k < 3; ++k) b.expand(positions[tri[k]]);
        const glm::vec3 ext = b.extent();
        const float cx = b.center().x;
        int nose = -1;
        for (const auto& tri : triangles)
            for (int k = 0; k < 3; ++k) {
                const glm::vec3& p = positions[tri[k]];
                if (std::abs(p.x - cx) > 0.12f * ext.x || p.y < b.min.y + 0.25f * ext.y) continue;
                if (nose < 0 || p.z > positions[size_t(nose)].z) nose = int(tri[k]);
            }
        if (nose < 0) {
            error = "Couldn't find a nose: place landmarks";
            return false;
        }
        const glm::vec3 noseTip = positions[size_t(nose)];
        float templTop = -1e9f;
        for (uint32_t i = 0; i < t.skinEnd; ++i) templTop = std::max(templTop, t.neutral[i].y);
        const glm::vec3 templNose = t.neutral[kLandmarks[kNoseTip].vertex];
        const float s0 = std::max(b.max.y - noseTip.y, 1e-6f) / (templTop - templNose.y);
        // Hair, horns or a hat make the head look taller: try a few scales and keep the best fit.
        float best = 1e30f;
        for (float f : {0.6f, 0.75f, 0.9f, 1.0f}) {
            Similarity s;
            s.scale = s0 * f;
            s.translation = noseTip - s.scale * templNose;
            const FitScore score = similarityIcp(sculpt, t, templNormals, nullptr, s, 10, 4);
            const float value = score.rms * (1.0f + 3.0f * (1.0f - score.coverage));
            if (value < best) {
                best = value;
                align = s;
            }
        }
        similarityIcp(sculpt, t, templNormals, nullptr, align, 10, 2);
    }

    // --- 2. Non-rigid ICP: displacement d of every template vertex, data term on the skin ---
    const size_t nv = t.neutral.size();
    std::vector<glm::vec3> start(nv), pos(nv);
    for (size_t i = 0; i < nv; ++i) start[i] = align.apply(t.neutral[i]);
    std::vector<glm::dvec3> d(nv, glm::dvec3(0.0));
    Correspondences c;
    const float s = align.scale;
    struct Stage {
        float alpha, maxDist;
    };
    // Stiffness from nearly rigid to loose; the search radius (template cm) shrinks with it.
    const Stage stages[] = {{60.0f, 3.0f}, {25.0f, 2.5f}, {10.0f, 2.0f}, {5.0f, 1.5f},
                            {2.5f, 1.2f},  {1.2f, 1.0f},  {0.6f, 0.8f}, {0.35f, 0.7f}};
    const double eps = 1e-6;
    for (const Stage& stage : stages)
        for (int inner = 0; inner < 2; ++inner) {
            for (size_t i = 0; i < nv; ++i) pos[i] = start[i] + glm::vec3(d[i]);
            const std::vector<glm::vec3> nrm = vertexNormals(pos, t.triangles);
            findCorrespondences(sculpt, pos, nrm, t.skinEnd, 1, stage.maxDist * s, 0.5f, c);
            std::vector<double> w(nv, 0.0), diag(nv);
            std::vector<glm::dvec3> rhs(nv, glm::dvec3(0.0));
            for (size_t i = 0; i < t.skinEnd; ++i)
                if (c.weight[i] > 0.0f) {
                    w[i] = 1.0;
                    rhs[i] = glm::dvec3(c.target[i] - start[i]);
                }
            if (landmarks) {
                const double lw = 20.0;
                for (int l = 0; l < kFaceLandmarkCount; ++l)
                    if (landmarks->set[size_t(l)]) {
                        const uint32_t v = kLandmarks[l].vertex;
                        w[v] += lw;
                        rhs[v] += lw * glm::dvec3(landmarks->position[size_t(l)] - start[v]);
                    }
            }
            for (size_t i = 0; i < nv; ++i) {
                rhs[i] += eps * d[i];
                diag[i] = w[i] + double(stage.alpha) * td.degree[i] + eps;
            }
            const double alpha = stage.alpha;
            auto apply = [&](const std::vector<glm::dvec3>& x, std::vector<glm::dvec3>& y) {
                for (size_t i = 0; i < nv; ++i) {
                    glm::dvec3 acc = diag[i] * x[i];
                    for (uint32_t k = td.rowStart[i]; k < td.rowStart[i + 1]; ++k) acc -= alpha * td.weight[k] * x[td.col[k]];
                    y[i] = acc;
                }
            };
            solvePcg(apply, diag, rhs, d, 1e-4, 400);
        }
    for (size_t i = 0; i < nv; ++i) pos[i] = start[i] + glm::vec3(d[i]);

    // --- Quality: how close the face came, and how much of it found the sculpt ---
    {
        const std::vector<glm::vec3> nrm = vertexNormals(pos, t.triangles);
        findCorrespondences(sculpt, pos, nrm, t.faceEnd, 1, 1.0f * s, 0.3f, c);
        double sq = 0.0;
        size_t n = 0;
        for (uint32_t i = 0; i < t.faceEnd; ++i)
            if (c.weight[i] > 0.0f) {
                const double dd = glm::distance(pos[i], c.target[i]);
                sq += dd * dd;
                ++n;
            }
        out.rmsError = float(std::sqrt(sq / double(std::max<size_t>(n, 1))));
        out.coverage = float(n) / float(std::max<uint32_t>(t.faceEnd, 1));
    }
    out.align = align;
    out.positions = std::move(pos);
    for (int l = 0; l < kFaceLandmarkCount; ++l) {
        out.landmarks.position[size_t(l)] = out.positions[kLandmarks[l].vertex];
        out.landmarks.set[size_t(l)] = true;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Deformation transfer
// ---------------------------------------------------------------------------

namespace {

glm::dmat3 triangleFrame(const glm::dvec3& a, const glm::dvec3& b, const glm::dvec3& c) {
    const glm::dvec3 e1 = b - a, e2 = c - a, n = glm::cross(e1, e2);
    const double len = glm::length(n);
    return glm::dmat3(e1, e2, len > 1e-300 ? n / std::sqrt(len) : glm::dvec3(0.0));
}

} // namespace

void transferFaceShapes(const FaceFit& fit, std::vector<std::vector<glm::vec3>>& offsets) {
    const TemplateData& td = templateData();
    const FaceTemplate& t = td.t;
    const size_t nv = t.neutral.size(), ns = t.shapes.size();
    offsets.assign(ns, std::vector<glm::vec3>(nv, glm::vec3(0.0f)));
    if (fit.positions.size() != nv) return;
    // The fitted template back in the template's frame (the similarity's rotation and scale undone).
    std::vector<glm::dvec3> x(nv);
    for (size_t i = 0; i < nv; ++i) x[i] = glm::dvec3(fit.align.inverse(fit.positions[i]));

    poolFor(ns, [&](size_t sb, size_t se) {
        for (size_t si = sb; si < se; ++si) {
            const FaceTemplate::Shape& shape = t.shapes[si];
            if (shape.index.empty()) continue;
            std::vector<glm::dvec3> dense(nv, glm::dvec3(0.0));
            std::vector<uint8_t> inRegion(nv, 0);
            for (size_t k = 0; k < shape.index.size(); ++k) {
                dense[shape.index[k]] = glm::dvec3(shape.offset[k]);
                inRegion[shape.index[k]] = 1;
            }
            // The region solved for: the moved vertices and two rings around them; the rest stays put.
            for (int ring = 0; ring < 2; ++ring) {
                std::vector<uint8_t> grown = inRegion;
                for (size_t i = 0; i < nv; ++i)
                    if (inRegion[i])
                        for (uint32_t k = td.rowStart[i]; k < td.rowStart[i + 1]; ++k) grown[td.col[k]] = 1;
                inRegion.swap(grown);
            }
            std::vector<uint32_t> region, local(nv, ~0u);
            for (uint32_t i = 0; i < nv; ++i)
                if (inRegion[i]) {
                    local[i] = uint32_t(region.size());
                    region.push_back(i);
                }
            const size_t n = region.size();
            // Right-hand side: per triangle, the source deformation gradient minus identity applied to the
            // fitted template's edges.
            std::vector<glm::dvec3> rhs(n, glm::dvec3(0.0)), u(n);
            std::vector<uint8_t> triDone(t.triangles.size(), 0);
            for (uint32_t i : region)
                for (uint32_t k = td.triStart[i]; k < td.triStart[i + 1]; ++k) {
                    const uint32_t ti = td.triList[k];
                    if (triDone[ti]) continue;
                    triDone[ti] = 1;
                    const glm::uvec3 tri = t.triangles[ti];
                    const glm::dvec3 n0(t.neutral[tri.x]), n1(t.neutral[tri.y]), n2(t.neutral[tri.z]);
                    const glm::dmat3 vn = triangleFrame(n0, n1, n2);
                    const glm::dmat3 ve = triangleFrame(n0 + dense[tri.x], n1 + dense[tri.y], n2 + dense[tri.z]);
                    if (std::abs(glm::determinant(vn)) < 1e-18) continue;
                    const glm::dmat3 m = ve * glm::inverse(vn) - glm::dmat3(1.0);
                    for (int a = 0; a < 3; ++a) {
                        const uint32_t li = local[tri[a]];
                        if (li == ~0u) continue;
                        for (int bb = 0; bb < 3; ++bb)
                            if (a != bb) rhs[li] += m * (x[tri[a]] - x[tri[bb]]);
                    }
                }
            std::vector<double> diag(n);
            for (size_t li = 0; li < n; ++li) {
                diag[li] = td.degree[region[li]] + 1e-9;
                u[li] = dense[region[li]]; // start from the template's own offsets
            }
            auto apply = [&](const std::vector<glm::dvec3>& xin, std::vector<glm::dvec3>& y) {
                for (size_t li = 0; li < n; ++li) {
                    const uint32_t i = region[li];
                    glm::dvec3 acc = diag[li] * xin[li];
                    for (uint32_t k = td.rowStart[i]; k < td.rowStart[i + 1]; ++k) {
                        const uint32_t lj = local[td.col[k]];
                        if (lj != ~0u) acc -= double(td.weight[k]) * xin[lj];
                    }
                    y[li] = acc;
                }
            };
            solvePcg(apply, diag, rhs, u, 1e-7, 3000);
            for (size_t li = 0; li < n; ++li) offsets[si][region[li]] = fit.align.applyVector(glm::vec3(u[li]));
        }
    }, 1, 2);
}

// ---------------------------------------------------------------------------
// Generate
// ---------------------------------------------------------------------------

namespace {

std::string lower(std::string s) {
    for (auto& ch : s) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

enum class PartKind { Skin, Eyes, Teeth, Tongue, Other };

PartKind classifySubtool(const std::string& name) {
    const std::string n = lower(name);
    auto has = [&](const char* w) { return n.find(w) != std::string::npos; };
    if (has("tongue")) return PartKind::Tongue;
    if (has("teeth") || has("tooth") || has("gum") || has("jaw")) return PartKind::Teeth;
    if (has("eye") && !has("lash") && !has("brow") && !has("lid")) return PartKind::Eyes;
    return PartKind::Other;
}

// Where each vertex sits on the fitted template: its closest triangle, barycentric weights and a falloff weight
// (1 near the template, 0 beyond `r1`).
struct Binding {
    std::vector<uint32_t> tri;
    std::vector<glm::vec3> bary;
    std::vector<float> weight;
};

Binding bindToTemplate(const std::vector<glm::vec3>& positions, const TriangleBVH& fitted, float r0, float r1) {
    Binding b;
    const size_t n = positions.size();
    b.tri.assign(n, 0);
    b.bary.assign(n, glm::vec3(0.0f));
    b.weight.assign(n, 0.0f);
    poolFor(n, [&](size_t lo, size_t hi) {
        for (size_t v = lo; v < hi; ++v) {
            SurfaceHit hit;
            if (!fitted.closestPoint(positions[v], r1, hit)) continue;
            b.tri[v] = hit.prim;
            b.bary[v] = hit.bary;
            b.weight[v] = hit.distance <= r0 ? 1.0f : std::clamp((r1 - hit.distance) / (r1 - r0), 0.0f, 1.0f);
            const float w = b.weight[v];
            b.weight[v] = w * w * (3.0f - 2.0f * w); // smoothstep
        }
    }, 4096);
    return b;
}

std::vector<glm::vec3> boundOffsets(const Binding& b, const std::vector<glm::uvec3>& tris, const std::vector<glm::vec3>& off) {
    std::vector<glm::vec3> out(b.weight.size(), glm::vec3(0.0f));
    poolFor(out.size(), [&](size_t lo, size_t hi) {
        for (size_t v = lo; v < hi; ++v) {
            if (b.weight[v] <= 0.0f) continue;
            const glm::uvec3 t = tris[b.tri[v]];
            out[v] = b.weight[v] * (b.bary[v].x * off[t.x] + b.bary[v].y * off[t.y] + b.bary[v].z * off[t.z]);
        }
    }, 8192);
    return out;
}

// The vertex nearest to each vertex's mirror image across X = 0, or ~0u when there is none within `tol`.
std::vector<uint32_t> mirrorMap(const std::vector<glm::vec3>& pos, const std::vector<glm::uvec3>& tris, float tol) {
    TriangleBVH bvh;
    bvh.build(pos, tris);
    std::vector<uint32_t> m(pos.size(), ~0u);
    poolFor(pos.size(), [&](size_t lo, size_t hi) {
        for (size_t v = lo; v < hi; ++v) {
            const glm::vec3 q(-pos[v].x, pos[v].y, pos[v].z);
            SurfaceHit hit;
            if (!bvh.closestPoint(q, tol, hit)) continue;
            const glm::uvec3 t = tris[hit.prim];
            uint32_t best = t.x;
            for (int k = 1; k < 3; ++k)
                if (glm::distance(pos[t[k]], q) < glm::distance(pos[best], q)) best = t[k];
            if (glm::distance(pos[best], q) <= tol) m[v] = best;
        }
    }, 4096);
    return m;
}

void setNamedLayer(Mesh& m, const std::string& name, std::vector<glm::vec3> delta) {
    for (auto& l : m.layers)
        if (l.name == name) {
            l.delta = std::move(delta);
            l.weight = 0.0f;
            return;
        }
    const int i = m.addLayer(name);
    m.layers[size_t(i)].delta = std::move(delta);
    m.layers[size_t(i)].weight = 0.0f;
}

bool nonZero(const std::vector<glm::vec3>& d) {
    return std::any_of(d.begin(), d.end(), [](const glm::vec3& v) { return v != glm::vec3(0.0f); });
}

// The visible triangles of `m`.
std::vector<glm::uvec3> visibleTriangles(const Mesh& m) {
    std::vector<glm::uvec3> out;
    out.reserve(m.triangleCount());
    for (uint32_t t = 0; t < m.triangleCount(); ++t)
        if (!m.triangleHidden(t)) out.push_back(m.triangles[t]);
    return out;
}

// The subdivision level to make the shapes on: the lowest with at least kFaceMinVertices vertices (else the top).
int faceLevel(const Mesh& m, int wanted) {
    const int levels = subdivLevelCount(m);
    if (levels == 0) return 0;
    if (wanted >= 0) return std::min(wanted, levels - 1);
    for (int l = 0; l < levels; ++l)
        if (subdivLevelData(m, l).positions.size() >= kFaceMinVertices) return l;
    return levels - 1;
}

// Rigid motions in the template frame, applied to sculpt-space points through the fit's similarity.
glm::vec3 moveRigid(const Similarity& align, const glm::mat3& R, const glm::vec3& tr, const glm::vec3& shift, const glm::vec3& p) {
    const glm::vec3 x = align.inverse(p);
    return align.apply(R * (x - shift) + tr + shift);
}

// Undoable mesh replacement for SubTool `i` (the active one through Document::replaceMeshUndoable).
void replaceSubtoolMesh(Document& doc, int i, Mesh m, const std::string& label) {
    if (i == doc.activeSubtool) {
        doc.replaceMeshUndoable(std::move(m), label);
        return;
    }
    SubTool& s = doc.subtools[size_t(i)];
    UndoRecord r;
    r.label = label;
    r.mesh = std::make_unique<Mesh>(std::move(s.mesh));
    s.mesh = std::move(m);
    s.mesh.topologyDirty = true;
    s.bvhDirty = true;
    s.undo.push(std::move(r));
    doc.modified = true;
}

} // namespace

bool generateFaceShapes(Document& doc, const FaceShapesOptions& options, FaceShapesReport& report, std::string& error) {
    const TemplateData& td = templateData();
    const FaceTemplate& t = td.t;
    const auto& names = arkitBlendShapeNames();
    if (t.shapes.size() != names.size()) {
        error = "The face template is missing from this build";
        return false;
    }
    if (doc.mesh.triangleCount() == 0) {
        error = "The active Part is empty";
        return false;
    }
    subdivCommit(doc.mesh);
    Mesh m = doc.mesh;
    const int originalLevel = subdivCurrentLevel(m);
    const int level = faceLevel(m, options.level);
    if (level != originalLevel && !subdivSetLevel(m, level, error)) return false;
    report.level = level;

    // --- Fit and transfer ---
    const std::vector<glm::vec3> base = m.basePositions();
    const std::vector<glm::uvec3> tris = visibleTriangles(m);
    FaceFit& fit = report.fit;
    if (!fitFaceTemplate(base, tris, options.landmarks.count() ? &options.landmarks : nullptr, fit, error)) return false;
    if (fit.coverage < 0.35f) {
        error = "Couldn't fit the face (only " + std::to_string(int(fit.coverage * 100.0f)) +
                "% of the template found the sculpt): face the head down +Z (front view), Y up, or place landmarks";
        return false;
    }
    std::vector<std::vector<glm::vec3>> offsets;
    transferFaceShapes(fit, offsets);

    // --- Bind the sculpt to the fitted template ---
    TriangleBVH fitted;
    fitted.build(fit.positions, t.triangles);
    const float s = fit.align.scale;
    const Binding bind = bindToTemplate(base, fitted, 0.6f * s, 2.0f * s);
    std::vector<std::vector<glm::vec3>> deltas(names.size());
    poolFor(names.size(), [&](size_t b, size_t e) {
        for (size_t k = b; k < e; ++k) deltas[k] = boundOffsets(bind, t.triangles, offsets[k]);
    }, 1, 2);

    // --- Symmetry: mirror Left / Right pairs and make centre shapes symmetric ---
    AABB box;
    for (const auto& p : base) box.expand(p);
    const float tol = 2e-3f * glm::length(box.extent());
    bool symmetric = options.symmetry == 1;
    std::vector<uint32_t> mirror;
    if (options.symmetry != 0) {
        mirror = mirrorMap(base, tris, tol);
        size_t bound = 0, paired = 0;
        for (size_t v = 0; v < base.size(); ++v)
            if (bind.weight[v] > 0.0f) {
                ++bound;
                paired += mirror[v] != ~0u;
            }
        if (options.symmetry < 0) symmetric = bound > 0 && paired >= bound * 97 / 100;
    }
    if (symmetric) {
        auto flip = [](const glm::vec3& d) { return glm::vec3(-d.x, d.y, d.z); };
        std::vector<std::vector<glm::vec3>> sym(names.size());
        for (size_t k = 0; k < names.size(); ++k) {
            const int other = arkitBlendShapeIndex(arkitMirrorName(names[k]));
            const auto& mine = deltas[k];
            const auto& theirs = deltas[size_t(other)];
            sym[k].resize(mine.size());
            for (size_t v = 0; v < mine.size(); ++v)
                sym[k][v] = mirror[v] == ~0u ? mine[v] : 0.5f * (mine[v] + flip(theirs[mirror[v]]));
        }
        deltas.swap(sym);
    }
    report.symmetric = symmetric;
    // Offsets under 0.1 mm on a life-size head are dropped: they don't show, and live updates (face_weights) only
    // visit the vertices a shape moves.
    const float cutoff = 0.01f * s;
    for (auto& d : deltas)
        for (auto& v : d)
            if (glm::length(v) < cutoff) v = glm::vec3(0.0f);

    // --- Into the active SubTool's layers ---
    for (size_t k = 0; k < names.size(); ++k) {
        report.movedVertices = std::max(report.movedVertices,
                                        size_t(std::count_if(deltas[k].begin(), deltas[k].end(),
                                                             [](const glm::vec3& v) { return v != glm::vec3(0.0f); })));
        setNamedLayer(m, names[k], std::move(deltas[k]));
    }
    m.evaluateAll();
    m.recomputeAllNormals();
    if (level != originalLevel && !subdivSetLevel(m, originalLevel, error)) return false;
    replaceSubtoolMesh(doc, doc.activeSubtool, std::move(m), "Generate Facial Shapes");

    if (!options.subtools) return true;

    // --- The other SubTools ---
    const glm::vec3 mouthCenter = 0.5f * (fit.align.inverse(fit.landmarks.position[kMouthLeft]) +
                                          fit.align.inverse(fit.landmarks.position[kMouthRight]));
    const glm::vec3 templMouth = 0.5f * (t.neutral[kLandmarks[kMouthLeft].vertex] + t.neutral[kLandmarks[kMouthRight].vertex]);
    const glm::vec3 shift = mouthCenter - templMouth; // where the sculpt's mouth sits relative to the template's
    const float biteY = 0.5f * (t.upperTeethY + t.lowerTeethCentroid.y) + shift.y;
    for (int i = 0; i < doc.subtoolCount(); ++i) {
        if (i == doc.activeSubtool || !doc.subtools[size_t(i)].visible) continue;
        const std::string& name = doc.subtools[size_t(i)].name;
        const PartKind kind = classifySubtool(name);
        subdivCommit(doc.subtoolMesh(i));
        Mesh sm = doc.subtoolMesh(i);
        if (sm.triangleCount() == 0) continue;
        const int orig = subdivCurrentLevel(sm), lvl = faceLevel(sm, options.level);
        if (lvl != orig && !subdivSetLevel(sm, lvl, error)) return false;
        const std::vector<glm::vec3> pos = sm.basePositions();
        std::vector<std::pair<std::string, std::vector<glm::vec3>>> made;
        std::string what;
        if (kind == PartKind::Eyes) {
            // Each eye turns about its own centre: the vertices on the character's left (+X in the template
            // frame) are the left eye.
            AABB eyeBox[2];
            for (const auto& p : pos) eyeBox[fit.align.inverse(p).x > 0.0f ? 1 : 0].expand(p);
            const glm::mat3 R = fit.align.rotation;
            auto gaze = [&](float yawDeg, float pitchDeg) {
                glm::mat4 rot(1.0f);
                rot = glm::rotate(rot, glm::radians(yawDeg), glm::vec3(0, 1, 0));
                rot = glm::rotate(rot, glm::radians(pitchDeg), glm::vec3(1, 0, 0));
                return R * glm::mat3(rot) * glm::transpose(R);
            };
            struct EyeShape {
                const char* name;
                int side; // 0 right, 1 left
                float yaw, pitch;
            };
            // Looking out / in turns about Y, up / down about X (ARKit's eyes reach about 30 degrees sideways and
            // 25 up or down).
            const EyeShape eyeShapes[] = {
                {"eyeLookDownLeft", 1, 0, 25},  {"eyeLookDownRight", 0, 0, 25}, {"eyeLookInLeft", 1, -30, 0},
                {"eyeLookInRight", 0, 30, 0},   {"eyeLookOutLeft", 1, 30, 0},   {"eyeLookOutRight", 0, -30, 0},
                {"eyeLookUpLeft", 1, 0, -25},   {"eyeLookUpRight", 0, 0, -25}};
            for (const EyeShape& es : eyeShapes) {
                if (!eyeBox[es.side].valid()) continue;
                const glm::vec3 pivot = eyeBox[es.side].center();
                const glm::mat3 rot = gaze(es.yaw, es.pitch);
                std::vector<glm::vec3> d(pos.size(), glm::vec3(0.0f));
                for (size_t v = 0; v < pos.size(); ++v)
                    if ((fit.align.inverse(pos[v]).x > 0.0f ? 1 : 0) == es.side) d[v] = pivot + rot * (pos[v] - pivot) - pos[v];
                made.push_back({es.name, std::move(d)});
            }
            what = "eye rotations";
        } else if (kind == PartKind::Teeth || kind == PartKind::Tongue) {
            for (size_t k = 0; k < names.size(); ++k) {
                const FaceTemplate::Shape& sh = t.shapes[k];
                const glm::mat3 R = glm::mat3_cast(sh.jawRotation);
                const bool jaw = sh.jawTranslation != glm::vec3(0.0f) || std::abs(sh.jawRotation.w) < 0.999999f;
                const bool tongueOut = kind == PartKind::Tongue && names[k] == "tongueOut";
                if (!jaw && !tongueOut) continue;
                std::vector<glm::vec3> d(pos.size(), glm::vec3(0.0f));
                for (size_t v = 0; v < pos.size(); ++v) {
                    const bool lowerJaw = kind == PartKind::Tongue || fit.align.inverse(pos[v]).y < biteY;
                    if (!lowerJaw) continue;
                    glm::vec3 p = moveRigid(fit.align, R, sh.jawTranslation, shift, pos[v]);
                    if (tongueOut) p += fit.align.applyVector(glm::vec3(0.0f, -0.3f, 2.2f)); // out past the lips
                    d[v] = p - pos[v];
                }
                made.push_back({names[k], std::move(d)});
            }
            what = kind == PartKind::Tongue ? "jaw and tongueOut" : "jaw (teeth below the bite)";
        } else {
            // Brows, lashes, a beard: follow the skin, if they sit on the face.
            const Binding b = bindToTemplate(pos, fitted, 0.6f * s, 2.0f * s);
            const size_t onFace = size_t(std::count_if(b.weight.begin(), b.weight.end(), [](float w) { return w > 0.5f; }));
            if (onFace * 5 < pos.size()) continue; // not on the face
            for (size_t k = 0; k < names.size(); ++k) {
                std::vector<glm::vec3> d = boundOffsets(b, t.triangles, offsets[k]);
                if (nonZero(d)) made.push_back({names[k], std::move(d)});
            }
            what = "follows the skin";
        }
        if (made.empty()) continue;
        for (auto& [n, d] : made) setNamedLayer(sm, n, std::move(d));
        sm.evaluateAll();
        sm.recomputeAllNormals();
        if (lvl != orig && !subdivSetLevel(sm, orig, error)) return false;
        replaceSubtoolMesh(doc, i, std::move(sm), "Generate Facial Shapes");
        report.subtools.push_back(name + ": " + what);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Live weights
// ---------------------------------------------------------------------------

bool parseFaceWeights(const std::string& text, std::vector<std::pair<std::string, float>>& out, std::string& error) {
    Json j;
    if (!Json::parse(text, j, error)) {
        error = "face_weights: not JSON (" + error + ")";
        return false;
    }
    // The whole face message is fine too: {"type":"face","weights":{...}}.
    const Json& w = j.isObject() && j["weights"].isObject() ? j["weights"] : j;
    if (!w.isObject()) {
        error = "face_weights: expected an object like {\"jawOpen\":0.3}";
        return false;
    }
    out.clear();
    for (const auto& [name, value] : w.members()) {
        if (!value.isNumber()) {
            error = "face_weights: '" + name + "' is not a number";
            return false;
        }
        out.push_back({name, std::clamp(float(value.asNumber()), -1.0f, 2.0f)});
    }
    return true;
}

namespace {

// The vertices each layer moves, found once per layer and kept while its offsets stay the same: keyed by the
// offsets' address and checked against their size, the SubTool's undo revision (strokes into a layer push an
// undo step) and a few sampled values.
struct SupportEntry {
    size_t size = 0;
    uint64_t revision = 0, fingerprint = 0;
    std::vector<uint32_t> verts;
};

uint64_t fingerprint(const std::vector<glm::vec3>& d) {
    uint64_t h = 1469598103934665603ull ^ d.size();
    const size_t step = std::max<size_t>(d.size() / 61, 1);
    for (size_t i = 0; i < d.size(); i += step) {
        uint32_t bits[3];
        std::memcpy(bits, &d[i], sizeof bits);
        for (uint32_t b : bits) h = (h ^ b) * 1099511628211ull;
    }
    return h;
}

std::mutex gSupportMutex;
std::unordered_map<const void*, SupportEntry> gSupport;

const std::vector<uint32_t>& layerSupport(const SculptLayer& l, uint64_t revision) {
    const uint64_t fp = fingerprint(l.delta);
    SupportEntry& e = gSupport[l.delta.data()];
    if (e.size == l.delta.size() && e.revision == revision && e.fingerprint == fp && !e.verts.empty()) return e.verts;
    e.size = l.delta.size();
    e.revision = revision;
    e.fingerprint = fp;
    e.verts.clear();
    for (uint32_t v = 0; v < l.delta.size(); ++v)
        if (l.delta[v] != glm::vec3(0.0f)) e.verts.push_back(v);
    if (e.verts.empty()) e.verts.push_back(~0u); // (marks "computed, empty")
    return e.verts;
}

} // namespace

FaceWeightsStats applyFaceWeights(Document& doc, const std::vector<std::pair<std::string, float>>& weights) {
    FaceWeightsStats stats;
    std::vector<uint8_t> matched(weights.size(), 0);
    std::lock_guard lock(gSupportMutex);
    if (gSupport.size() > 8192) gSupport.clear();
    for (int si = 0; si < doc.subtoolCount(); ++si) {
        Mesh& m = doc.subtoolMesh(si);
        if (m.layers.empty() || m.base.size() != m.positions.size()) continue;
        const uint64_t revision = si == doc.activeSubtool ? doc.undo.revision() : doc.subtools[size_t(si)].undo.revision();
        std::vector<uint32_t> changed;
        for (size_t w = 0; w < weights.size(); ++w)
            for (size_t li = 0; li < m.layers.size(); ++li)
                if (m.layers[li].name == weights[w].first) {
                    matched[w] = 1;
                    if (m.layers[li].weight != weights[w].second) {
                        m.layers[li].weight = weights[w].second;
                        changed.push_back(uint32_t(li));
                    }
                }
        if (changed.empty()) continue;
        stats.layers += int(changed.size());
        // The vertices the changed layers move, found and re-evaluated in blocks of the vertex index range, in
        // parallel: each vertex from its base plus every layer with a weight, in layer order, walking each layer's
        // own (sorted) vertex list. Those are the same sums as Mesh::evaluate (the layers left out would only add
        // zeros), at the cost of the offsets that exist rather than vertices x layers.
        std::vector<const std::vector<uint32_t>*> changedSupport;
        for (uint32_t li : changed) changedSupport.push_back(&layerSupport(m.layers[li], revision));
        std::vector<std::pair<const SculptLayer*, const std::vector<uint32_t>*>> live;
        for (const auto& l : m.layers)
            if (l.weight != 0.0f) live.push_back({&l, &layerSupport(l, revision)});
        constexpr size_t kBlock = 16384;
        const size_t nv = m.positions.size(), blocks = (nv + kBlock - 1) / kBlock;
        static std::vector<uint8_t> mark; // (guarded by gSupportMutex; all zero between calls)
        if (mark.size() < nv) mark.assign(nv, 0);
        std::vector<std::vector<uint32_t>> blockVerts(blocks);
        poolFor(blocks, [&](size_t bb, size_t be) {
            for (size_t blk = bb; blk < be; ++blk) {
                const uint32_t lo = uint32_t(blk * kBlock), hi = uint32_t(std::min(nv, (blk + 1) * kBlock));
                for (const auto* support : changedSupport)
                    for (auto it = std::lower_bound(support->begin(), support->end(), lo); it != support->end() && *it < hi; ++it)
                        mark[*it] = 1;
                std::vector<uint32_t>& vs = blockVerts[blk];
                for (uint32_t v = lo; v < hi; ++v)
                    if (mark[v]) {
                        vs.push_back(v);
                        m.positions[v] = m.base[v];
                    }
                if (vs.empty()) continue;
                for (const auto& [l, support] : live) {
                    const float w = l->weight;
                    for (auto it = std::lower_bound(support->begin(), support->end(), lo); it != support->end() && *it < hi; ++it)
                        if (mark[*it]) m.positions[*it] += w * l->delta[*it];
                }
                for (uint32_t v : vs) mark[v] = 0;
            }
        }, 1, 2);
        std::vector<uint32_t> verts;
        for (const auto& vs : blockVerts) verts.insert(verts.end(), vs.begin(), vs.end());
        if (verts.empty()) continue;
        m.updateNormals(verts); // (also marks them, and their neighbours, for the GPU)
        stats.vertices += verts.size();
        if (si == doc.activeSubtool) doc.bvhDirty = true;
        else doc.subtools[size_t(si)].bvhDirty = true;
    }
    for (size_t w = 0; w < weights.size(); ++w) {
        if (matched[w]) ++stats.matched;
        else stats.unknown.push_back(weights[w].first);
    }
    return stats;
}

// ---------------------------------------------------------------------------
// Takes
// ---------------------------------------------------------------------------

bool parseFaceTake(const std::string& text, MorphAnimation& out, std::string& error) {
    std::vector<Json> frames;
    Json j;
    std::string err;
    if (Json::parse(text, j, err)) {
        if (j.isArray()) frames = j.items();
        else if (j.isObject() && j["frames"].isArray()) frames = j["frames"].items();
        else if (j.isObject()) frames.push_back(j);
    } else {
        // JSON Lines: one face message per line.
        std::istringstream in(text);
        std::string line;
        int n = 0;
        while (std::getline(in, line)) {
            ++n;
            if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
            Json f;
            if (!Json::parse(line, f, err)) {
                error = "Take line " + std::to_string(n) + ": " + err;
                return false;
            }
            frames.push_back(std::move(f));
        }
    }
    const auto& names = arkitBlendShapeNames();
    MorphAnimation a;
    a.channels = names;
    const double fps = j.isObject() && j["fps"].isNumber() ? j["fps"].asNumber() : 30.0;
    double t0 = 0.0;
    bool first = true;
    for (size_t i = 0; i < frames.size(); ++i) {
        const Json& f = frames[i];
        if (!f.isObject()) continue;
        if (f["type"].isString() && f["type"].asString() != "face") continue;
        const double t = f["t"].isNumber() ? f["t"].asNumber() / 1000.0 : double(a.times.size()) / std::max(fps, 1.0);
        if (first) t0 = t;
        first = false;
        const float time = float(t - t0);
        if (!a.times.empty() && time <= a.times.back()) continue; // (out of order or duplicate)
        std::vector<float> row(names.size(), 0.0f);
        const Json& w = f["weights"];
        if (w.isObject()) {
            for (const auto& [name, value] : w.members()) {
                const int k = arkitBlendShapeIndex(name);
                if (k >= 0 && value.isNumber()) row[size_t(k)] = float(value.asNumber());
            }
        } else if (f["blendshapes"].isArray()) { // MediaPipe: [{"categoryName":..,"score":..}]
            for (const Json& c : f["blendshapes"].items()) {
                const int k = arkitBlendShapeIndex(c["categoryName"].asString());
                if (k >= 0) row[size_t(k)] = float(c["score"].asNumber());
            }
        } else {
            continue;
        }
        a.times.push_back(time);
        a.values.insert(a.values.end(), row.begin(), row.end());
    }
    if (a.times.empty()) {
        error = "The take has no face frames ({\"type\":\"face\",\"t\":ms,\"weights\":{...}})";
        return false;
    }
    a.fps = float(fps);
    out = std::move(a);
    return true;
}

bool loadFaceTake(const std::string& path, MorphAnimation& out, std::string& error) {
    std::ifstream f(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
    if (!f) {
        error = "Cannot open " + path;
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    if (!parseFaceTake(ss.str(), out, error)) return false;
    out.name = std::filesystem::path(std::u8string(path.begin(), path.end())).stem().string();
    return true;
}

void sampleMorphAnimation(const MorphAnimation& a, float time, std::vector<std::pair<std::string, float>>& out) {
    out.clear();
    if (a.empty()) return;
    const size_t nc = a.channels.size();
    size_t k = size_t(std::upper_bound(a.times.begin(), a.times.end(), time) - a.times.begin());
    size_t k0 = k == 0 ? 0 : k - 1, k1 = std::min(k, a.times.size() - 1);
    float f = 0.0f;
    if (k1 != k0 && a.times[k1] > a.times[k0]) f = std::clamp((time - a.times[k0]) / (a.times[k1] - a.times[k0]), 0.0f, 1.0f);
    for (size_t c = 0; c < nc; ++c) out.push_back({a.channels[c], glm::mix(a.value(k0, c), a.value(k1, c), f)});
}

MorphAnimation resampleMorphAnimation(const MorphAnimation& a, float fps) {
    MorphAnimation r;
    r.name = a.name;
    r.channels = a.channels;
    r.fps = fps;
    if (a.empty() || fps <= 0.0f) return r;
    const int frames = int(std::floor(a.duration() * fps + 1e-4f)) + 1;
    std::vector<std::pair<std::string, float>> w;
    for (int i = 0; i < frames; ++i) {
        const float t = float(i) / fps;
        sampleMorphAnimation(a, t, w);
        r.times.push_back(t);
        for (const auto& [n, v] : w) r.values.push_back(v);
    }
    return r;
}

void setBakedFaceAnimation(Document& doc, const MorphAnimation* a) {
    if (!doc.projectSettings.isObject()) doc.projectSettings = Json::object();
    if (!a || a->empty()) {
        if (doc.projectSettings.has("face_animation")) {
            Json copy = Json::object();
            for (const auto& [k, v] : doc.projectSettings.members())
                if (k != "face_animation") copy.set(k, v);
            doc.projectSettings = std::move(copy);
            doc.modified = true;
        }
        return;
    }
    Json j = Json::object();
    j.set("name", a->name);
    j.set("fps", double(a->fps));
    Json ch = Json::array(), times = Json::array(), values = Json::array();
    for (const auto& c : a->channels) ch.push(c);
    for (float t : a->times) times.push(double(t));
    for (float v : a->values) values.push(double(std::round(v * 10000.0f) / 10000.0f));
    j.set("channels", std::move(ch));
    j.set("times", std::move(times));
    j.set("values", std::move(values));
    doc.projectSettings.set("face_animation", std::move(j));
    doc.modified = true;
}

bool bakedFaceAnimation(const Document& doc, MorphAnimation& out) {
    if (!doc.projectSettings.isObject()) return false;
    const Json& j = doc.projectSettings["face_animation"];
    if (!j.isObject() || !j["channels"].isArray() || !j["times"].isArray() || !j["values"].isArray()) return false;
    MorphAnimation a;
    a.name = j["name"].isString() ? j["name"].asString() : "Take";
    a.fps = float(j["fps"].asNumber(30.0));
    for (const Json& c : j["channels"].items()) a.channels.push_back(c.asString());
    for (const Json& t : j["times"].items()) a.times.push_back(float(t.asNumber()));
    for (const Json& v : j["values"].items()) a.values.push_back(float(v.asNumber()));
    if (a.empty() || a.values.size() != a.times.size() * a.channels.size()) return false;
    out = std::move(a);
    return true;
}

// ---------------------------------------------------------------------------
// Presets
// ---------------------------------------------------------------------------

const std::vector<FacePreset>& facePresets() {
    static const std::vector<FacePreset> presets = {
        {"Neutral", "Every facial shape at 0.", {}},
        {"Smile", "Closed smile: mouth corners up, cheeks raised.",
         {{"mouthSmileLeft", 0.8f}, {"mouthSmileRight", 0.8f}, {"cheekSquintLeft", 0.4f}, {"cheekSquintRight", 0.4f},
          {"eyeSquintLeft", 0.2f}, {"eyeSquintRight", 0.2f}}},
        {"Grin", "Big open smile.",
         {{"mouthSmileLeft", 1.0f}, {"mouthSmileRight", 1.0f}, {"jawOpen", 0.25f}, {"mouthUpperUpLeft", 0.3f},
          {"mouthUpperUpRight", 0.3f}, {"mouthLowerDownLeft", 0.3f}, {"mouthLowerDownRight", 0.3f},
          {"cheekSquintLeft", 0.6f}, {"cheekSquintRight", 0.6f}, {"eyeSquintLeft", 0.3f}, {"eyeSquintRight", 0.3f}}},
        {"Surprise", "Brows up, eyes wide, jaw dropped.",
         {{"browInnerUp", 0.9f}, {"browOuterUpLeft", 0.8f}, {"browOuterUpRight", 0.8f}, {"eyeWideLeft", 0.8f},
          {"eyeWideRight", 0.8f}, {"jawOpen", 0.45f}}},
        {"Anger", "Brows down, nose sneer, lips pressed.",
         {{"browDownLeft", 0.9f}, {"browDownRight", 0.9f}, {"eyeSquintLeft", 0.5f}, {"eyeSquintRight", 0.5f},
          {"noseSneerLeft", 0.5f}, {"noseSneerRight", 0.5f}, {"mouthPressLeft", 0.4f}, {"mouthPressRight", 0.4f},
          {"mouthFrownLeft", 0.3f}, {"mouthFrownRight", 0.3f}}},
        {"Sad", "Inner brows up, mouth corners down.",
         {{"browInnerUp", 0.7f}, {"mouthFrownLeft", 0.7f}, {"mouthFrownRight", 0.7f}, {"mouthShrugLower", 0.4f},
          {"eyeLookDownLeft", 0.2f}, {"eyeLookDownRight", 0.2f}}},
        {"Disgust", "Nose wrinkled, upper lip raised.",
         {{"noseSneerLeft", 0.8f}, {"noseSneerRight", 0.8f}, {"mouthUpperUpLeft", 0.6f}, {"mouthUpperUpRight", 0.6f},
          {"browDownLeft", 0.4f}, {"browDownRight", 0.4f}, {"cheekSquintLeft", 0.3f}, {"cheekSquintRight", 0.3f}}},
        {"Fear", "Brows up and together, eyes wide, lips stretched.",
         {{"browInnerUp", 0.8f}, {"browOuterUpLeft", 0.4f}, {"browOuterUpRight", 0.4f}, {"eyeWideLeft", 0.9f},
          {"eyeWideRight", 0.9f}, {"mouthStretchLeft", 0.6f}, {"mouthStretchRight", 0.6f}, {"jawOpen", 0.15f}}},
        {"Blink", "Both eyes closed.", {{"eyeBlinkLeft", 1.0f}, {"eyeBlinkRight", 1.0f}}},
        {"Wink", "Left eye closed with a half smile.",
         {{"eyeBlinkLeft", 1.0f}, {"cheekSquintLeft", 0.4f}, {"mouthSmileLeft", 0.4f}}},
        {"Kiss", "Lips puckered.", {{"mouthPucker", 0.9f}, {"mouthFunnel", 0.2f}}},
        {"Aah", "Open vowel.", {{"jawOpen", 0.6f}, {"mouthLowerDownLeft", 0.2f}, {"mouthLowerDownRight", 0.2f}}},
        {"Ooh", "Rounded vowel.", {{"mouthFunnel", 0.7f}, {"mouthPucker", 0.4f}, {"jawOpen", 0.2f}}},
        {"Puff", "Cheeks puffed.", {{"cheekPuff", 1.0f}, {"mouthPucker", 0.2f}}},
        {"Look left", "Both eyes turned to the character's left.", {{"eyeLookOutLeft", 0.8f}, {"eyeLookInRight", 0.8f}}},
        {"Tongue out", "Tongue out, jaw a little open.", {{"tongueOut", 1.0f}, {"jawOpen", 0.2f}}},
    };
    return presets;
}

const FacePreset* findFacePreset(std::string_view name) {
    for (const auto& p : facePresets()) {
        std::string a(p.name), b(name);
        if (lower(a) == lower(b)) return &p;
    }
    return nullptr;
}

} // namespace sf
