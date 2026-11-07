// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/MeshTransform.h"

#include "core/Document.h"
#include "core/PartOrigin.h" // carryPartOrigin

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <thread>

namespace sf {

namespace {

// Runs fn(begin, end) over [0, n) on several threads when there is enough work.
template <class Fn>
void parallelRange(size_t n, Fn fn) {
    const size_t threads = n < 100000 ? 1 : std::clamp<size_t>(std::thread::hardware_concurrency(), 1, 16);
    if (threads <= 1) {
        fn(size_t(0), n);
        return;
    }
    std::vector<std::thread> pool;
    const size_t chunk = (n + threads - 1) / threads;
    for (size_t b = 0; b < n; b += chunk) pool.emplace_back([=] { fn(b, std::min(n, b + chunk)); });
    for (auto& t : pool) t.join();
}

// Angle (radians, 0..2pi) and unit axis of a rotation, without folding angles over 180 degrees onto the short
// way round (a 270 degree ring drag at half weight turns 135 degrees, not -45).
void angleAxis(const glm::quat& q, float& angle, glm::vec3& axis) {
    const float s = std::sqrt(std::max(0.0f, 1.0f - q.w * q.w));
    angle = 2.0f * std::acos(std::clamp(q.w, -1.0f, 1.0f));
    axis = s > 1e-7f ? glm::vec3(q.x, q.y, q.z) / s : glm::vec3(1, 0, 0);
}

} // namespace

bool MeshTransform::identity() const {
    return translate == glm::vec3(0.0f) && scale == glm::vec3(1.0f) &&
           (std::abs(rotate.w) >= 1.0f || (rotate.x == 0.0f && rotate.y == 0.0f && rotate.z == 0.0f));
}

void MeshTransform::at(float w, glm::mat3& linear, glm::vec3& offset) const {
    float angle;
    glm::vec3 axis;
    angleAxis(rotate, angle, axis);
    linear = angle * w != 0.0f ? glm::mat3_cast(glm::angleAxis(angle * w, axis)) : glm::mat3(1.0f);
    const glm::vec3 s = glm::mix(glm::vec3(1.0f), scale, w);
    if (s != glm::vec3(1.0f)) {
        glm::mat3 d(1.0f);
        d[0][0] = s.x;
        d[1][1] = s.y;
        d[2][2] = s.z;
        linear = linear * frame * d * glm::transpose(frame);
    }
    offset = pivot + w * translate - linear * pivot;
}

glm::vec3 MeshTransform::apply(const glm::vec3& p) const {
    glm::mat3 l;
    glm::vec3 o;
    at(1.0f, l, o);
    return l * p + o;
}

glm::mat4 MeshTransform::matrix() const {
    glm::mat3 l;
    glm::vec3 o;
    at(1.0f, l, o);
    glm::mat4 m(l);
    m[3] = glm::vec4(o, 1.0f);
    return m;
}

MeshTransform MeshTransform::inSpaceOf(const glm::mat4& place) const {
    const float s = glm::length(glm::vec3(place[0]));
    if (!(s > 0.0f)) return *this;
    const glm::mat3 r = glm::mat3(place) / s, rt = glm::transpose(r);
    MeshTransform out = *this;
    out.pivot = glm::vec3(glm::inverse(place) * glm::vec4(pivot, 1.0f));
    out.frame = rt * frame;
    out.translate = rt * translate / s;
    out.rotate = glm::normalize(glm::quat_cast(rt) * rotate * glm::quat_cast(r));
    return out;
}

MeshTransform MeshTransform::evenScale() const {
    MeshTransform out = *this;
    out.scale = glm::vec3((scale.x + scale.y + scale.z) / 3.0f);
    return out;
}

glm::quat eulerRotation(const glm::vec3& degrees, const glm::mat3& axes) {
    const glm::quat qx = glm::angleAxis(glm::radians(degrees.x), axes[0]);
    const glm::quat qy = glm::angleAxis(glm::radians(degrees.y), axes[1]);
    const glm::quat qz = glm::angleAxis(glm::radians(degrees.z), axes[2]);
    return glm::normalize(qz * qy * qx);
}

glm::vec3 eulerDegrees(const glm::quat& q) {
    // R = Rz(c) Ry(b) Rx(a); glm matrices are m[column][row].
    const glm::mat3 m = glm::mat3_cast(q);
    const float b = std::asin(std::clamp(-m[0][2], -1.0f, 1.0f));
    float a, c;
    if (std::abs(m[0][2]) < 0.99999f) {
        a = std::atan2(m[1][2], m[2][2]);
        c = std::atan2(m[0][1], m[0][0]);
    } else { // gimbal lock: put it all in X
        a = std::atan2(-m[2][1], m[1][1]);
        c = 0.0f;
    }
    return glm::degrees(glm::vec3(a, b, c));
}

std::vector<float> transformWeights(const Mesh& mesh, bool useMask, float falloff) {
    const size_t nv = mesh.vertexCount();
    const bool masked = useMask && mesh.hasMask();
    if (!masked && !mesh.hasHidden()) return {};
    std::vector<float> w(nv, 1.0f);
    if (masked)
        for (size_t v = 0; v < nv; ++v) w[v] = std::clamp(1.0f - mesh.mask[v], 0.0f, 1.0f);
    const float reach = masked && falloff > 0.0f ? falloff * glm::length(mesh.bounds().extent()) : 0.0f;
    if (reach > 0.0f) {
        // Distance along the edges from the masked part (multi-source Dijkstra from its border, stopping at
        // `reach`), then a smoothstep ramp: only the band next to the mask is visited.
        constexpr float kFar = std::numeric_limits<float>::max();
        std::vector<float> dist(nv, kFar);
        using Item = std::pair<float, uint32_t>;
        std::priority_queue<Item, std::vector<Item>, std::greater<Item>> heap;
        for (uint32_t v = 0; v < nv; ++v) {
            if (mesh.mask[v] < 0.5f) continue;
            dist[v] = 0.0f;
            for (uint32_t n : mesh.neighbors(v))
                if (mesh.mask[n] < 0.5f) {
                    heap.push({0.0f, v});
                    break;
                }
        }
        while (!heap.empty()) {
            const auto [d, v] = heap.top();
            heap.pop();
            if (d > dist[v]) continue;
            for (uint32_t n : mesh.neighbors(v)) {
                const float nd = d + glm::length(mesh.positions[n] - mesh.positions[v]);
                if (nd < dist[n] && nd < reach) {
                    dist[n] = nd;
                    heap.push({nd, n});
                }
            }
        }
        for (size_t v = 0; v < nv; ++v) {
            if (dist[v] == kFar) continue;
            const float t = std::clamp(dist[v] / reach, 0.0f, 1.0f);
            w[v] *= t * t * (3.0f - 2.0f * t);
        }
    }
    if (mesh.hasHidden())
        for (size_t v = 0; v < nv; ++v)
            if (mesh.vertexHidden(uint32_t(v))) w[v] = 0.0f;
    if (std::all_of(w.begin(), w.end(), [](float x) { return x >= 1.0f; })) return {};
    return w;
}

AABB transformBounds(const Mesh& mesh, const std::vector<float>& weights) {
    AABB box;
    if (weights.size() != mesh.vertexCount()) return mesh.bounds();
    float most = 0.0f;
    for (float w : weights) most = std::max(most, w);
    if (most <= 0.0f) return mesh.bounds();
    const float cut = std::min(0.5f, most * 0.5f);
    for (size_t v = 0; v < weights.size(); ++v)
        if (weights[v] >= cut) box.expand(mesh.positions[v]);
    return box.valid() ? box : mesh.bounds();
}

glm::vec3 transformCenter(const Mesh& mesh, const std::vector<float>& weights) {
    const AABB box = transformBounds(mesh, weights);
    return box.valid() ? box.center() : glm::vec3(0.0f);
}

bool TransformSession::layerWeightZero(const Mesh& mesh, int layer) {
    return layer >= 0 && layer < int(mesh.layers.size()) && std::abs(mesh.layers[size_t(layer)].weight) < 1e-6f;
}

bool TransformSession::begin(const Mesh& mesh, std::vector<float> weights, Symmetry mirror, const glm::vec3& pivot,
                             int layer) {
    *this = TransformSession();
    if (layerWeightZero(mesh, layer)) return false;
    const size_t nv = mesh.vertexCount();
    const bool weighted = weights.size() == nv;
    std::vector<uint16_t> full(weighted ? nv : 0, 0); // every vertex's level (0 = still)
    for (uint32_t v = 0; v < nv; ++v) {
        const int l = weighted ? int(std::lround(std::clamp(weights[v], 0.0f, 1.0f) * kLevels)) : kLevels;
        if (l <= 0) continue;
        verts_.push_back(v);
        level_.push_back(uint16_t(l));
        if (weighted) full[v] = uint16_t(l);
    }
    if (verts_.empty()) return false;
    whole_ = verts_.size() == nv && std::all_of(level_.begin(), level_.end(), [](uint16_t l) { return l == kLevels; });

    // Symmetry only makes sense for a part of the mesh, on a side of the plane.
    Symmetry sym{false, false, false};
    if (weighted) {
        constexpr float eps = 1e-5f;
        sym.x = mirror.x && std::abs(pivot.x) > eps;
        sym.y = mirror.y && std::abs(pivot.y) > eps;
        sym.z = mirror.z && std::abs(pivot.z) > eps;
    }
    mirrors_ = Sculptor::mirrors(sym);
    side_.assign(verts_.size(), 0);
    std::vector<uint8_t> sideOf(mirrors_.size() > 1 ? nv : 0, 0);
    if (mirrors_.size() > 1) {
        const bool on[3] = {sym.x, sym.y, sym.z};
        for (size_t i = 0; i < verts_.size(); ++i) {
            const glm::vec3& p = mesh.positions[verts_[i]];
            glm::vec3 s(1.0f);
            for (int a = 0; a < 3; ++a)
                if (on[a] && p[a] * pivot[a] < 0.0f) s[a] = -1.0f;
            for (size_t m = 0; m < mirrors_.size(); ++m)
                if (mirrors_[m] == s) side_[i] = uint8_t(m);
            sideOf[verts_[i]] = side_[i];
        }
    }

    // Normals of a rigidly moved vertex just turn; where neighbours move differently they are recomputed.
    if (weighted) {
        for (size_t i = 0; i < verts_.size(); ++i) {
            const uint32_t v = verts_[i];
            bool edge = level_[i] < kLevels;
            for (uint32_t n : mesh.neighbors(v)) {
                if (edge) break;
                edge = full[n] != level_[i] || (!sideOf.empty() && sideOf[n] != side_[i]);
            }
            if (edge) boundary_.push_back(v);
        }
    }

    usedLevels_.assign(kLevels + 1, 0);
    for (uint16_t l : level_) usedLevels_[l] = 1;
    layered_ = !mesh.layers.empty();
    layer_ = layer >= 0 && layer < int(mesh.layers.size()) ? layer : -1;
    if (layer_ >= 0) invWeight_ = 1.0f / mesh.layers[size_t(layer_)].weight;
    // Recording into a layer starts from the shape as shown; otherwise from the base.
    const auto& src = layer_ >= 0 ? mesh.positions : mesh.basePositions();
    base_.resize(verts_.size());
    normals_.resize(verts_.size());
    for (size_t i = 0; i < verts_.size(); ++i) {
        base_[i] = src[verts_[i]];
        normals_[i] = mesh.normals[verts_[i]];
    }
    deltas_.resize(layer_ >= 0 ? 1 : mesh.layers.size());
    for (size_t l = 0; l < deltas_.size(); ++l) {
        const std::vector<glm::vec3>& d = mesh.layers[layer_ >= 0 ? size_t(layer_) : l].delta;
        deltas_[l].resize(verts_.size());
        for (size_t i = 0; i < verts_.size(); ++i) deltas_[l][i] = d[verts_[i]];
    }
    active_ = true;
    return true;
}

void TransformSession::apply(Mesh& mesh, const MeshTransform& t) {
    if (!active_) return;
    const size_t stride = size_t(kLevels) + 1;
    table_.resize(mirrors_.size() * stride);
    for (size_t m = 0; m < mirrors_.size(); ++m) {
        const glm::vec3 d = mirrors_[m];
        const glm::mat3 D(glm::vec3(d.x, 0, 0), glm::vec3(0, d.y, 0), glm::vec3(0, 0, d.z));
        for (int l = 0; l <= kLevels; ++l) {
            if (!usedLevels_[size_t(l)]) continue;
            const float w = l == kLevels ? 1.0f : float(l) / float(kLevels);
            Level& e = table_[m * stride + size_t(l)];
            t.at(w, e.linear, e.offset);
            e.translateOnly = e.linear == glm::mat3(1.0f);
            if (e.translateOnly) e.offset = w * t.translate; // exact: p + w * t
            if (m > 0) {
                e.linear = D * e.linear * D;
                e.offset = d * e.offset;
            }
            e.normal = e.translateOnly ? glm::mat3(1.0f) : glm::transpose(glm::inverse(e.linear));
        }
    }
    std::vector<glm::vec3>& target = layered_ ? mesh.base : mesh.positions;
    std::vector<glm::vec3>* layerDelta = layer_ >= 0 ? &mesh.layers[size_t(layer_)].delta : nullptr;
    parallelRange(verts_.size(), [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            const Level& L = table_[side_[i] * stride + level_[i]];
            const uint32_t v = verts_[i];
            if (layerDelta) { // the move goes into the layer's offsets, scaled up by its weight
                const glm::vec3 p = L.translateOnly ? base_[i] + L.offset : L.linear * base_[i] + L.offset;
                (*layerDelta)[v] = deltas_[0][i] + (p - base_[i]) * invWeight_;
                if (L.translateOnly) {
                    mesh.normals[v] = normals_[i];
                } else {
                    const glm::vec3 n = L.normal * normals_[i];
                    const float len = glm::length(n);
                    mesh.normals[v] = len > 1e-20f ? n / len : normals_[i];
                }
                continue;
            }
            if (L.translateOnly) {
                target[v] = base_[i] + L.offset;
                for (size_t k = 0; k < deltas_.size(); ++k) mesh.layers[k].delta[v] = deltas_[k][i];
                mesh.normals[v] = normals_[i];
                continue;
            }
            target[v] = L.linear * base_[i] + L.offset;
            for (size_t k = 0; k < deltas_.size(); ++k) mesh.layers[k].delta[v] = L.linear * deltas_[k][i];
            const glm::vec3 n = L.normal * normals_[i];
            const float len = glm::length(n);
            mesh.normals[v] = len > 1e-20f ? n / len : normals_[i];
        }
    });
    if (layered_) mesh.evaluate(verts_);
    // Rigidly moved normals just turn (above); recompute them only where neighbours move differently.
    if (!boundary_.empty()) mesh.updateNormals(boundary_);
    mesh.markDirtyRange(verts_.front(), verts_.back() + 1);
    moved_ = !t.identity();
}

void TransformSession::writeBack(Mesh& mesh) {
    if (layer_ >= 0) {
        std::vector<glm::vec3>& d = mesh.layers[size_t(layer_)].delta;
        for (size_t i = 0; i < verts_.size(); ++i) d[verts_[i]] = deltas_[0][i];
        mesh.evaluate(verts_);
        mesh.updateNormals(verts_);
        return;
    }
    std::vector<glm::vec3>& target = layered_ ? mesh.base : mesh.positions;
    for (size_t i = 0; i < verts_.size(); ++i) {
        target[verts_[i]] = base_[i];
        for (size_t k = 0; k < deltas_.size(); ++k) mesh.layers[k].delta[verts_[i]] = deltas_[k][i];
    }
    if (layered_) mesh.evaluate(verts_);
    mesh.updateNormals(verts_);
}

void TransformSession::restore(Mesh& mesh) {
    if (!active_) return;
    writeBack(mesh);
    moved_ = false;
}

UndoRecord TransformSession::finish(Mesh& mesh, BVH& bvh, std::string label) {
    UndoRecord r;
    if (!active_) return r;
    active_ = false;
    if (!moved_) {
        writeBack(mesh); // an identity transform: leave the mesh exactly as it was
        return r;
    }
    // Recomputed once from the final positions: keeps the mesh's face-normal cache right for later strokes.
    mesh.updateNormals(verts_);
    if (verts_.size() * 4 > mesh.vertexCount()) bvh.refitAll(mesh);
    else bvh.refit(mesh, verts_);
    r.label = std::move(label);
    r.positions.indices = verts_;
    if (layer_ >= 0) { // like a stroke into the layer: its old offsets
        r.layer = layer_;
        r.positions.values = std::move(deltas_[0]);
        *this = TransformSession();
        return r;
    }
    r.layer = -1;
    r.positions.values = std::move(base_);
    for (auto& d : deltas_) {
        UndoChannel<glm::vec3> ch;
        ch.indices = verts_;
        ch.values = std::move(d);
        r.layerOffsets.push_back(std::move(ch));
    }
    *this = TransformSession();
    return r;
}

void TransformSession::cancel(Mesh& mesh, BVH& bvh) {
    if (!active_) return;
    writeBack(mesh);
    if (verts_.size() * 4 > mesh.vertexCount()) bvh.refitAll(mesh);
    else bvh.refit(mesh, verts_);
    *this = TransformSession();
}

std::string vertexCountText(size_t n) {
    std::string digits = std::to_string(n);
    for (int i = int(digits.size()) - 3; i > 0; i -= 3) digits.insert(size_t(i), 1, ',');
    return digits + (n == 1 ? " vertex" : " vertices");
}

bool partMovesWhole(const Document& doc, int index, bool useMask) {
    const bool instance = doc.linkCount(index) > 1 || doc.parts[size_t(index)].moved();
    return instance && !(useMask && doc.partMesh(index).hasMask());
}

size_t transformPart(Document& doc, int index, const MeshTransform& t, bool useMask, float falloff, Symmetry mirror,
                        const std::string& label) {
    if (index < 0 || index >= doc.partCount()) return 0;
    Mesh& mesh = doc.partMesh(index);
    const Part& part = doc.parts[size_t(index)];
    // A linked copy (or one moved as a whole before) moves as a whole: its transform, one undo step in its
    // group's history. With a mask, what moves is the shared mesh, in the Part's own space.
    if (partMovesWhole(doc, index, useMask)) {
        if (!part.visible || mesh.vertexCount() == 0) return 0;
        doc.setPartTransform(index, t.evenScale().matrix() * part.transform, label);
        return mesh.vertexCount();
    }
    const MeshTransform local = part.moved() ? t.inSpaceOf(part.transform) : t;
    BVH& bvh = doc.partBvh(index);
    UndoStack& undo = doc.partUndo(index);
    TransformSession s;
    const int layer = part.data == doc.activePartInfo().data ? doc.activeLayer : -1; // (the checked-out mesh)
    if (!s.begin(mesh, transformWeights(mesh, useMask, falloff), mirror, local.pivot, layer)) return 0;
    const size_t n = s.movingVertices();
    s.apply(mesh, local);
    const bool whole = s.whole();
    UndoRecord r = s.finish(mesh, bvh, label);
    if (r.empty()) return 0;
    undo.push(std::move(r));
    if (whole) carryPartOrigin(doc, index, local, undo); // the origin (in the mesh's space) moves with it, same step
    return n;
}

} // namespace sf
