// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/BVH.h"

#include "core/AtomicRef.h"
#include "core/ThreadPool.h"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace sf {

namespace {

constexpr uint32_t kLeafSize = 16;
constexpr size_t kParallelRefitLeaves = 4096; // fewer: the serial walk up is cheaper than waking threads

bool rayBox(const AABB& b, const glm::vec3& o, const glm::vec3& invDir, float tMax) {
    glm::vec3 t0 = (b.min - o) * invDir;
    glm::vec3 t1 = (b.max - o) * invDir;
    glm::vec3 tmin = glm::min(t0, t1), tmax = glm::max(t0, t1);
    float enter = std::max(std::max(tmin.x, tmin.y), std::max(tmin.z, 0.0f));
    float exit = std::min(std::min(tmax.x, tmax.y), std::min(tmax.z, tMax));
    return enter <= exit;
}

bool sphereBox(const AABB& b, const glm::vec3& c, float r2) {
    glm::vec3 d = glm::max(glm::max(b.min - c, c - b.max), glm::vec3(0.0f));
    return glm::dot(d, d) <= r2;
}

// The box lies wholly within the sphere (its farthest corner does).
bool boxInSphere(const AABB& b, const glm::vec3& c, float r2) {
    const glm::vec3 d = glm::max(glm::abs(b.min - c), glm::abs(b.max - c));
    return glm::dot(d, d) <= r2;
}

// Two-sided Moller-Trumbore.
bool rayTriangle(const glm::vec3& o, const glm::vec3& d, const glm::vec3& a, const glm::vec3& b,
                 const glm::vec3& c, float& t) {
    glm::vec3 e1 = b - a, e2 = c - a;
    glm::vec3 p = glm::cross(d, e2);
    float det = glm::dot(e1, p);
    if (std::abs(det) < 1e-12f) return false;
    float inv = 1.0f / det;
    glm::vec3 s = o - a;
    float u = glm::dot(s, p) * inv;
    if (u < 0.0f || u > 1.0f) return false;
    glm::vec3 q = glm::cross(s, e1);
    float v = glm::dot(d, q) * inv;
    if (v < 0.0f || u + v > 1.0f) return false;
    t = glm::dot(e2, q) * inv;
    return t > 0.0f;
}

} // namespace

void BVH::build(const Mesh& mesh) {
    const size_t nt = mesh.triangleCount();
    nodes_.clear();
    std::vector<uint32_t>().swap(leafCap_);
    editedLeaves_.clear();
    leafEdited_.clear();
    unusedEntries_ = editsSinceBuild_ = 0;
    triIndices_.resize(nt);
    triLeaf_.assign(nt, 0);
    vertBits_.resize(mesh.vertexCount());
    queryLeaves_.clear();
    if (nt == 0) return;

    std::vector<glm::vec3> centroids(nt);
    poolFor(nt, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            const auto& t = mesh.triangles[i];
            centroids[i] = (mesh.positions[t.x] + mesh.positions[t.y] + mesh.positions[t.z]) / 3.0f;
            triIndices_[i] = uint32_t(i);
        }
    }, 1 << 16);

    // Splits node `ni` of `nodes` at the median centroid along its longest axis (pushing its two children), or
    // leaves it a leaf. Only touches triIndices_ within the node's range, so subtrees can be split in parallel.
    auto split = [&](std::vector<Node>& nodes, uint32_t ni) -> bool {
        const uint32_t first = nodes[ni].first, count = nodes[ni].count;
        AABB cb;
        for (uint32_t i = first; i < first + count; ++i) cb.expand(centroids[triIndices_[i]]);
        const glm::vec3 ext = cb.extent();
        const int axis = ext.x > ext.y ? (ext.x > ext.z ? 0 : 2) : (ext.y > ext.z ? 1 : 2);
        if (count <= kLeafSize || ext[axis] <= 0.0f) return false;
        const uint32_t mid = first + count / 2;
        std::nth_element(triIndices_.begin() + first, triIndices_.begin() + mid, triIndices_.begin() + first + count,
                         [&](uint32_t a, uint32_t b) { return centroids[a][axis] < centroids[b][axis]; });
        const uint32_t left = static_cast<uint32_t>(nodes.size());
        nodes.push_back(Node{AABB{}, first, mid - first, ni});
        nodes.push_back(Node{AABB{}, mid, first + count - mid, ni});
        nodes[ni].first = left;
        nodes[ni].count = 0;
        return true;
    };
    // Splits `nodes` depth first from the nodes in `tasks`, until only leaves are left.
    auto buildFrom = [&](std::vector<Node>& nodes, std::vector<uint32_t> tasks) {
        while (!tasks.empty()) {
            const uint32_t ni = tasks.back();
            tasks.pop_back();
            if (!split(nodes, ni)) continue;
            tasks.push_back(nodes[ni].first);
            tasks.push_back(nodes[ni].first + 1);
        }
    };

    nodes_.reserve(2 * (nt / kLeafSize + 1));
    nodes_.push_back(Node{AABB{}, 0, static_cast<uint32_t>(nt), 0});
    if (nt < (size_t(1) << 18) || ThreadPool::shared().threads() == 1 || ThreadPool::shared().serial()) {
        buildFrom(nodes_, {0});
    } else {
        // Split the top breadth first until there is work for every thread, then build those subtrees in
        // parallel (each into its own node list) and append them. The splits are the same as a serial build's,
        // and children still follow their parents with the right child next to the left one.
        std::vector<uint32_t> open{0};
        const size_t want = size_t(ThreadPool::shared().threads()) * 8;
        while (open.size() < want) {
            std::vector<uint32_t> next;
            for (uint32_t ni : open)
                if (split(nodes_, ni)) {
                    next.push_back(nodes_[ni].first);
                    next.push_back(nodes_[ni].first + 1);
                }
            if (next.empty()) break;
            open = std::move(next);
        }
        std::vector<std::vector<Node>> sub(open.size());
        poolFor(open.size(), [&](size_t b, size_t e) {
            for (size_t k = b; k < e; ++k) {
                const Node& root = nodes_[open[k]];
                sub[k].push_back(Node{AABB{}, root.first, root.count, 0});
                if (root.count > 0) buildFrom(sub[k], {0});
            }
        }, 1);
        for (size_t k = 0; k < open.size(); ++k) {
            // Local node i >= 1 becomes base + i - 1; local 0 is the subtree's root, nodes_[open[k]].
            const uint32_t root = open[k], base = static_cast<uint32_t>(nodes_.size());
            auto global = [&](uint32_t i) { return i == 0 ? root : base + i - 1; };
            const std::vector<Node>& local = sub[k];
            nodes_[root].first = local[0].count > 0 ? local[0].first : global(local[0].first);
            nodes_[root].count = local[0].count;
            for (size_t i = 1; i < local.size(); ++i) {
                Node n = local[i];
                n.parent = global(n.parent);
                if (n.count == 0) n.first = global(n.first);
                nodes_.push_back(n);
            }
        }
    }

    // Leaf bounds (and each triangle's leaf), then inner bounds bottom-up (children have larger indices).
    poolFor(nodes_.size(), [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i)
            if (nodes_[i].count > 0) recomputeLeaf(mesh, nodes_[i]);
    }, 1 << 12);
    for (size_t i = nodes_.size(); i-- > 0;) {
        Node& n = nodes_[i];
        if (n.count == 0) {
            n.box = nodes_[n.first].box;
            n.box.expand(nodes_[n.first + 1].box);
        }
    }
    leafBits_.resize(nodes_.size());
}

AABB BVH::leafBox(const Mesh& mesh, const Node& n) const {
    AABB box;
    for (uint32_t i = n.first; i < n.first + n.count; ++i) {
        const auto& t = mesh.triangles[triIndices_[i]];
        box.expand(mesh.positions[t.x]);
        box.expand(mesh.positions[t.y]);
        box.expand(mesh.positions[t.z]);
    }
    return box;
}

void BVH::recomputeLeaf(const Mesh& mesh, Node& n) {
    n.box = leafBox(mesh, n);
    for (uint32_t i = n.first; i < n.first + n.count; ++i) triLeaf_[triIndices_[i]] = static_cast<uint32_t>(&n - nodes_.data());
}

bool BVH::raycast(const Mesh& mesh, const glm::vec3& origin, const glm::vec3& dir, RayHit& hit) const {
    if (nodes_.empty()) return false;
    const bool skipHidden = mesh.hasHidden();
    // Avoid 0 * inf = NaN in the slab test for axis-aligned rays.
    glm::vec3 safeDir = dir;
    for (int i = 0; i < 3; ++i)
        if (std::abs(safeDir[i]) < 1e-20f) safeDir[i] = std::copysign(1e-20f, safeDir[i]);
    glm::vec3 invDir = 1.0f / safeDir;
    float best = std::numeric_limits<float>::max();
    uint32_t bestTri = 0;
    bool found = false;

    // Per thread: the UI (the surface cursor, picks) casts rays while the predictive-stroke thread sculpts;
    // a shared stack mixed their traversals (strokes then differed from the serial path).
    thread_local std::vector<uint32_t> stack;
    stack.clear();
    stack.push_back(0);
    while (!stack.empty()) {
        const Node& n = nodes_[stack.back()];
        stack.pop_back();
        if (!rayBox(n.box, origin, invDir, best)) continue;
        if (n.count > 0) {
            for (uint32_t i = n.first; i < n.first + n.count; ++i) {
                if (skipHidden && mesh.triangleHidden(triIndices_[i])) continue;
                const auto& t = mesh.triangles[triIndices_[i]];
                float tt;
                if (rayTriangle(origin, dir, mesh.positions[t.x], mesh.positions[t.y], mesh.positions[t.z], tt) &&
                    tt < best) {
                    best = tt;
                    bestTri = triIndices_[i];
                    found = true;
                }
            }
        } else if (n.first != kNoLeaf) { // (kNoLeaf: a leaf dynamic edits emptied)
            stack.push_back(n.first);
            stack.push_back(n.first + 1);
        }
    }
    if (!found) return false;

    const auto& t = mesh.triangles[bestTri];
    hit.t = best;
    hit.triangle = bestTri;
    hit.position = origin + dir * best;
    glm::vec3 n = glm::cross(mesh.positions[t.y] - mesh.positions[t.x], mesh.positions[t.z] - mesh.positions[t.x]);
    float len = glm::length(n);
    hit.normal = len > 0.0f ? n / len : -dir;
    return true;
}

void BVH::queryTriangles(const Mesh& mesh, const glm::vec3& center, float radius, std::vector<uint32_t>& out) const {
    out.clear();
    if (nodes_.empty()) return;
    const float r2 = radius * radius;
    // Per thread: the UI (the surface cursor, picks) casts rays while the predictive-stroke thread sculpts;
    // a shared stack mixed their traversals (strokes then differed from the serial path).
    thread_local std::vector<uint32_t> stack;
    stack.clear();
    stack.push_back(0);
    while (!stack.empty()) {
        const Node& n = nodes_[stack.back()];
        stack.pop_back();
        if (!sphereBox(n.box, center, r2)) continue;
        if (n.count > 0) {
            for (uint32_t i = n.first; i < n.first + n.count; ++i) {
                const auto& t = mesh.triangles[triIndices_[i]];
                AABB b;
                b.expand(mesh.positions[t.x]);
                b.expand(mesh.positions[t.y]);
                b.expand(mesh.positions[t.z]);
                if (sphereBox(b, center, r2)) out.push_back(triIndices_[i]);
            }
        } else if (n.first != kNoLeaf) {
            stack.push_back(n.first);
            stack.push_back(n.first + 1);
        }
    }
}

void BVH::forEachLeafInSphere(const glm::vec3& center, float radius,
                              const std::function<void(std::span<const uint32_t>)>& fn) const {
    if (nodes_.empty()) return;
    const float r2 = radius * radius;
    std::vector<uint32_t> stack;
    stack.reserve(64);
    stack.push_back(0);
    while (!stack.empty()) {
        const Node& n = nodes_[stack.back()];
        stack.pop_back();
        if (!sphereBox(n.box, center, r2)) continue;
        if (n.count > 0) fn(std::span<const uint32_t>(triIndices_.data() + n.first, n.count));
        else if (n.first != kNoLeaf) {
            stack.push_back(n.first);
            stack.push_back(n.first + 1);
        }
    }
}

template <class Shape>
void BVH::queryVerticesIn(const Mesh& mesh, const Shape& shape, std::vector<uint32_t>& out) const {
    out.clear();
    queryLeaves_.clear();
    if (nodes_.empty()) return;
    if (vertBits_.capacity() < mesh.vertexCount()) vertBits_.resize(mesh.vertexCount());

    // The units of parallel work: the subtrees the sphere touches, split breadth first until there are enough
    // of them (the same ones for any thread count).
    constexpr size_t kTasks = 128, kSerialTriangles = 16384;
    std::vector<uint32_t>& tasks = queryTasks_;
    tasks.clear();
    if (shape.touches(nodes_[0].box)) tasks.push_back(0);
    int depth = 0; // of every inner node in `tasks` (median splits: each holds about nt >> depth triangles)
    for (bool split = true; split && tasks.size() < kTasks; ++depth) {
        split = false;
        queryNext_.clear();
        for (uint32_t ni : tasks) {
            const Node& n = nodes_[ni];
            if (n.count > 0 || n.first == kNoLeaf) {
                queryNext_.push_back(ni);
                continue;
            }
            split = true;
            for (uint32_t c : {n.first, n.first + 1})
                if (shape.touches(nodes_[c].box)) queryNext_.push_back(c);
        }
        tasks.swap(queryNext_);
    }
    // Adds the vertices inside the sphere of the triangles under node `root` that the sphere's boxes touch, and
    // lists (depth first) the leaves holding any of them.
    auto walk = [&](uint32_t root, std::vector<uint32_t>& nodeStack, std::vector<uint32_t>& leaves, IndexBits& found) {
        nodeStack.assign(1, root);
        while (!nodeStack.empty()) {
            const uint32_t ni = nodeStack.back();
            const Node& n = nodes_[ni];
            nodeStack.pop_back();
            if (!shape.touches(n.box) || (n.count == 0 && n.first == kNoLeaf)) continue;
            if (n.count == 0) {
                nodeStack.push_back(n.first);
                nodeStack.push_back(n.first + 1);
                continue;
            }
            if (shape.containsBox(n.box)) { // every corner is inside
                for (uint32_t i = n.first; i < n.first + n.count; ++i) {
                    const auto& t = mesh.triangles[triIndices_[i]];
                    found.insertLocal(t.x);
                    found.insertLocal(t.y);
                    found.insertLocal(t.z);
                }
                leaves.push_back(ni);
                continue;
            }
            bool any = false;
            for (uint32_t i = n.first; i < n.first + n.count; ++i) {
                const auto& t = mesh.triangles[triIndices_[i]];
                for (int c = 0; c < 3; ++c) {
                    const uint32_t v = t[c];
                    if (found.contains(v)) { // only vertices inside are ever added
                        any = true;
                        continue;
                    }
                    if (shape.contains(mesh.positions[v])) {
                        found.insertLocal(v);
                        any = true;
                    }
                }
            }
            if (any) leaves.push_back(ni);
        }
    };
    // The set doesn't depend on which thread found what, take() lists it in index order, and the leaves are
    // joined in task order.
    size_t work = 0; // triangles under the tasks, roughly
    for (uint32_t ni : tasks) work += nodes_[ni].count > 0 ? nodes_[ni].count : mesh.triangleCount() >> std::min(depth, 63);
    if (work < kSerialTriangles) { // waking threads would cost more
        for (uint32_t root : tasks) walk(root, stack_, queryLeaves_, vertBits_);
        vertBits_.take(out);
        return;
    }
    if (queryParts_.size() < tasks.size()) queryParts_.resize(tasks.size());
    vertBits_.insertParallel(tasks.size(), 1, [&](size_t b, size_t e, IndexBits& found) {
        std::vector<uint32_t> nodeStack;
        for (size_t k = b; k < e; ++k) {
            queryParts_[k].clear();
            walk(tasks[k], nodeStack, queryParts_[k], found);
        }
    });
    for (size_t k = 0; k < tasks.size(); ++k) queryLeaves_.insert(queryLeaves_.end(), queryParts_[k].begin(), queryParts_[k].end());
    vertBits_.take(out);
}

namespace {

struct SphereShape {
    glm::vec3 c;
    float r2;
    bool touches(const AABB& b) const { return sphereBox(b, c, r2); }
    bool containsBox(const AABB& b) const { return boxInSphere(b, c, r2); }
    bool contains(const glm::vec3& p) const { return glm::dot(p - c, p - c) <= r2; }
};

// An infinite cylinder: the points within sqrt(r2) of the line through c along the unit vector a.
struct CylinderShape {
    glm::vec3 c, a;
    float r, r2;
    float dist2(const glm::vec3& p) const {
        const glm::vec3 d = p - c;
        const glm::vec3 off = d - a * glm::dot(d, a);
        return glm::dot(off, off);
    }
    bool touches(const AABB& b) const { // conservative: the box's bounding sphere against the cylinder
        const glm::vec3 mid = (b.min + b.max) * 0.5f;
        const float reach = r + glm::length(b.max - mid);
        return dist2(mid) <= reach * reach;
    }
    bool containsBox(const AABB& b) const {
        for (int k = 0; k < 8; ++k) {
            const glm::vec3 p((k & 1) ? b.max.x : b.min.x, (k & 2) ? b.max.y : b.min.y, (k & 4) ? b.max.z : b.min.z);
            if (dist2(p) > r2) return false;
        }
        return true;
    }
    bool contains(const glm::vec3& p) const { return dist2(p) <= r2; }
};

} // namespace

void BVH::queryVertices(const Mesh& mesh, const glm::vec3& center, float radius, std::vector<uint32_t>& out) const {
    queryVerticesIn(mesh, SphereShape{center, radius * radius}, out);
}

void BVH::queryVerticesCylinder(const Mesh& mesh, const glm::vec3& point, const glm::vec3& axis, float radius,
                                std::vector<uint32_t>& out) const {
    const float len = glm::length(axis);
    if (!(len > 1e-12f)) {
        queryVertices(mesh, point, radius, out);
        return;
    }
    queryVerticesIn(mesh, CylinderShape{point, axis / len, radius, radius * radius}, out);
}

void BVH::refit(const Mesh& mesh, std::span<const uint32_t> movedVerts) {
    if (nodes_.empty()) return;
    if (leafBits_.capacity() < nodes_.size()) leafBits_.resize(nodes_.size());
    leafBits_.insertParallel(movedVerts.size(), 4096, [&](size_t b, size_t e, IndexBits& leaves) {
        for (size_t i = b; i < e; ++i)
            for (uint32_t ti : mesh.vertexTriangles(movedVerts[i])) leaves.insertLocal(triLeaf_[ti]);
    });
    scratchLeaves_.clear();
    leafBits_.take(scratchLeaves_);
    refitLeaves(mesh, scratchLeaves_);
}

void BVH::refitQueried(const Mesh& mesh) { refitLeaves(mesh, queryLeaves_); }

void BVH::refitLeaves(const Mesh& mesh, std::span<const uint32_t> leaves) {
    poolFor(leaves.size(), [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) nodes_[leaves[i]].box = leafBox(mesh, nodes_[leaves[i]]);
    }, 512);
    if (leaves.size() >= kParallelRefitLeaves) {
        refitAncestors(leaves);
        return;
    }
    // Grow ancestors until one already contains the new bounds (a union, so the order doesn't matter).
    for (uint32_t li : leaves) {
        AABB box = nodes_[li].box;
        uint32_t ni = li;
        while (ni != 0) {
            ni = nodes_[ni].parent;
            AABB& pb = nodes_[ni].box;
            if (glm::all(glm::lessThanEqual(pb.min, box.min)) && glm::all(glm::greaterThanEqual(pb.max, box.max)))
                break;
            pb.expand(box);
            box = pb;
        }
    }
}

// A big brush refits hundreds of thousands of leaves, and walking each one's ancestors on one thread cost more
// than the leaves themselves (13 ms a dab at 21M triangles). Here the ancestors are flagged in parallel (each once:
// the thread that flags a node first goes on up, the others stop there), then recomputed from their children:
// bottom-up within subtrees taken in parallel, then the few nodes above those subtrees. Every box is the union
// of its children's (tighter than growing the old one), so the result doesn't depend on the thread count.
void BVH::refitAncestors(std::span<const uint32_t> leaves) {
    if (nodeDirty_.size() < nodes_.size()) nodeDirty_.resize(nodes_.size(), 0); // (all 0 between calls)
    poolFor(leaves.size(), [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i)
            for (uint32_t ni = leaves[i]; ni != 0;) {
                ni = nodes_[ni].parent;
                if (AtomicRef<uint8_t>(nodeDirty_[ni]).exchange(1, std::memory_order_relaxed)) break;
            }
    }, 1024);
    auto inner = [&](uint32_t ni) { return nodes_[ni].count == 0 && nodes_[ni].first != kNoLeaf; };
    auto dirtyInner = [&](uint32_t ni) { return inner(ni) && nodeDirty_[ni]; };
    auto recompute = [&](uint32_t ni) {
        Node& n = nodes_[ni];
        AABB box = nodes_[n.first].box;
        box.expand(nodes_[n.first + 1].box);
        n.box = box;
        nodeDirty_[ni] = 0;
    };
    // The subtrees: dirty nodes split breadth first until there are enough; the split ones are done last.
    constexpr size_t kTasks = 128;
    std::vector<uint32_t>& tasks = queryTasks_;
    std::vector<uint32_t>& next = queryNext_;
    std::vector<uint32_t> aboveNodes;
    tasks.clear();
    if (dirtyInner(0)) tasks.push_back(0);
    for (bool split = true; split && tasks.size() < kTasks;) {
        split = false;
        next.clear();
        for (uint32_t ni : tasks) {
            const uint32_t c = nodes_[ni].first;
            if (!dirtyInner(c) && !dirtyInner(c + 1)) {
                next.push_back(ni);
                continue;
            }
            split = true;
            aboveNodes.push_back(ni);
            for (uint32_t k : {c, c + 1})
                if (dirtyInner(k)) next.push_back(k);
        }
        tasks.swap(next);
    }
    // Each subtree post-order: a node once both its dirty children are done.
    poolFor(tasks.size(), [&](size_t b, size_t e) {
        std::vector<std::pair<uint32_t, bool>> stack;
        for (size_t t = b; t < e; ++t) {
            stack.assign(1, {tasks[t], false});
            while (!stack.empty()) {
                auto [ni, childrenDone] = stack.back();
                stack.pop_back();
                if (childrenDone) {
                    recompute(ni);
                    continue;
                }
                stack.push_back({ni, true});
                const uint32_t c = nodes_[ni].first;
                for (uint32_t k : {c, c + 1})
                    if (dirtyInner(k)) stack.push_back({k, false});
            }
        }
    }, 1, 2);
    // Then the nodes above them, deepest first (they were split in breadth-first order).
    for (auto it = aboveNodes.rbegin(); it != aboveNodes.rend(); ++it) recompute(*it);
}

void BVH::editLeaf(uint32_t leaf) {
    if (leafCap_.size() < nodes_.size()) { // the first edit: every leaf full
        const size_t from = leafCap_.size();
        leafCap_.resize(nodes_.size());
        for (size_t i = from; i < nodes_.size(); ++i) leafCap_[i] = nodes_[i].count;
    }
    if (leafEdited_.size() < nodes_.size()) leafEdited_.resize(nodes_.size(), 0);
    if (!leafEdited_[leaf]) {
        leafEdited_[leaf] = 1;
        editedLeaves_.push_back(leaf);
    }
    ++editsSinceBuild_;
}

void BVH::insertTriangle(const Mesh& mesh, uint32_t t, uint32_t nearby) {
    if (nodes_.empty()) {
        nodes_.push_back(Node{AABB{}, kNoLeaf, 0, 0});
        leafBits_.resize(nodes_.size());
    }
    uint32_t leaf = nearby < triLeaf_.size() ? triLeaf_[nearby] : kNoLeaf;
    if (leaf == kNoLeaf) { // down the tree towards the triangle's centroid
        const glm::uvec3& tri = mesh.triangles[t];
        const glm::vec3 c = (mesh.positions[tri.x] + mesh.positions[tri.y] + mesh.positions[tri.z]) / 3.0f;
        leaf = 0;
        while (nodes_[leaf].count == 0 && nodes_[leaf].first != kNoLeaf) {
            const Node& n = nodes_[leaf];
            auto dist = [&](const AABB& b) {
                if (!b.valid()) return std::numeric_limits<float>::max();
                const glm::vec3 d = glm::max(glm::max(b.min - c, c - b.max), glm::vec3(0.0f));
                return glm::dot(d, d);
            };
            leaf = dist(nodes_[n.first].box) <= dist(nodes_[n.first + 1].box) ? n.first : n.first + 1;
        }
    }
    editLeaf(leaf);
    Node& n = nodes_[leaf];
    if (n.count == leafCap_[leaf]) { // full: move the leaf's entries to the end, with room to grow
        const uint32_t cap = std::max(2 * n.count, kLeafSize);
        const uint32_t first = uint32_t(triIndices_.size());
        triIndices_.resize(triIndices_.size() + cap);
        if (n.count > 0) std::copy_n(triIndices_.begin() + n.first, n.count, triIndices_.begin() + first);
        unusedEntries_ += leafCap_[leaf];
        n.first = first;
        leafCap_[leaf] = cap;
    }
    triIndices_[n.first + n.count++] = t;
    if (t >= triLeaf_.size()) triLeaf_.resize(size_t(t) + 1, kNoLeaf);
    triLeaf_[t] = leaf;
}

void BVH::removeTriangle(uint32_t t) {
    const uint32_t leaf = t < triLeaf_.size() ? triLeaf_[t] : kNoLeaf;
    if (leaf == kNoLeaf) return;
    editLeaf(leaf);
    Node& n = nodes_[leaf];
    for (uint32_t i = n.first; i < n.first + n.count; ++i)
        if (triIndices_[i] == t) {
            triIndices_[i] = triIndices_[n.first + n.count - 1];
            --n.count;
            break;
        }
    if (n.count == 0) { // an empty leaf is marked so traversals don't take it for an inner node
        unusedEntries_ += leafCap_[leaf];
        n.first = kNoLeaf;
        leafCap_[leaf] = 0;
    }
    triLeaf_[t] = kNoLeaf;
}

void BVH::renumberTriangle(uint32_t from, uint32_t to) {
    const uint32_t leaf = from < triLeaf_.size() ? triLeaf_[from] : kNoLeaf;
    if (leaf == kNoLeaf) return;
    const Node& n = nodes_[leaf];
    for (uint32_t i = n.first; i < n.first + n.count; ++i)
        if (triIndices_[i] == from) triIndices_[i] = to;
    if (to >= triLeaf_.size()) triLeaf_.resize(size_t(to) + 1, kNoLeaf);
    triLeaf_[to] = leaf;
    triLeaf_[from] = kNoLeaf;
}

void BVH::resizeTriangles(size_t count) { triLeaf_.resize(count, kNoLeaf); }

void BVH::touchTriangle(uint32_t t) {
    if (t < triLeaf_.size() && triLeaf_[t] != kNoLeaf) editLeaf(triLeaf_[t]);
}

// Splits an overfull leaf at the median centroid along its longest axis: its two halves become new leaves
// (appended, so children still follow their parents), each keeping its part of the leaf's room.
void BVH::splitLeaf(const Mesh& mesh, uint32_t leaf) {
    const uint32_t first = nodes_[leaf].first, count = nodes_[leaf].count;
    auto centroid = [&](uint32_t t) {
        const glm::uvec3& tri = mesh.triangles[t];
        return mesh.positions[tri.x] + mesh.positions[tri.y] + mesh.positions[tri.z];
    };
    AABB cb;
    for (uint32_t i = first; i < first + count; ++i) cb.expand(centroid(triIndices_[i]));
    const glm::vec3 ext = cb.extent();
    const int axis = ext.x > ext.y ? (ext.x > ext.z ? 0 : 2) : (ext.y > ext.z ? 1 : 2);
    if (ext[axis] <= 0.0f) return;
    const uint32_t mid = first + count / 2;
    std::nth_element(triIndices_.begin() + first, triIndices_.begin() + mid, triIndices_.begin() + first + count,
                     [&](uint32_t a, uint32_t b) { return centroid(a)[axis] < centroid(b)[axis]; });
    const uint32_t left = uint32_t(nodes_.size()), cap = leafCap_[leaf];
    nodes_.push_back(Node{AABB{}, first, mid - first, leaf});
    nodes_.push_back(Node{AABB{}, mid, first + count - mid, leaf});
    leafCap_.push_back(mid - first);
    leafCap_.push_back(cap - (mid - first));
    leafEdited_.resize(nodes_.size(), 0);
    nodes_[leaf].first = left;
    nodes_[leaf].count = 0;
    leafCap_[leaf] = 0;
    for (uint32_t c : {left, left + 1}) {
        recomputeLeaf(mesh, nodes_[c]);
        AABB box = nodes_[c].box;
        for (uint32_t ni = c; ni != 0;) { // the ancestors hold it (as they held the leaf), but grow them anyway
            ni = nodes_[ni].parent;
            nodes_[ni].box.expand(box);
            box = nodes_[ni].box;
        }
    }
    leafBits_.resize(nodes_.size());
}

void BVH::refitChanged(const Mesh& mesh) {
    constexpr uint32_t kMaxLeaf = 3 * kLeafSize;
    scratchLeaves_.clear();
    for (uint32_t leaf : editedLeaves_) {
        leafEdited_[leaf] = 0;
        if (nodes_[leaf].count > 0) recomputeLeaf(mesh, nodes_[leaf]);
        else nodes_[leaf].box = AABB();
        scratchLeaves_.push_back(leaf);
    }
    editedLeaves_.clear();
    // (refitLeaves recomputes the boxes again: cheap for a few leaves, and it grows the ancestors.)
    refitLeaves(mesh, scratchLeaves_);
    for (uint32_t leaf : scratchLeaves_)
        if (nodes_[leaf].count > kMaxLeaf) splitLeaf(mesh, leaf);
}

bool BVH::degraded() const {
    // Rebuild once the edits rival the tree: boxes only grow, and moved leaves leave entries unused.
    const size_t live = triLeaf_.size();
    return unusedEntries_ > live / 2 + 1024 || editsSinceBuild_ > live + 4096;
}

size_t BVH::memoryBytes() const {
    auto bytes = [](const auto& v) { return v.capacity() * sizeof(v[0]); };
    return bytes(nodes_) + bytes(triIndices_) + bytes(triLeaf_) + bytes(leafCap_) + bytes(editedLeaves_) + bytes(leafEdited_) + vertBits_.memoryBytes() + leafBits_.memoryBytes() +
           bytes(queryLeaves_) + bytes(scratchLeaves_) + bytes(stack_) + bytes(nodeDirty_);
}

void BVH::refitAll(const Mesh& mesh) {
    for (size_t i = nodes_.size(); i-- > 0;) { // children always have larger indices
        Node& n = nodes_[i];
        if (n.count > 0) {
            recomputeLeaf(mesh, n);
        } else if (n.first == kNoLeaf) {
            n.box = AABB();
        } else {
            n.box = nodes_[n.first].box;
            n.box.expand(nodes_[n.first + 1].box);
        }
    }
}

} // namespace sf
