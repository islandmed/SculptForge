// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Timeline.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/IndexBits.h"
#include "core/ThreadPool.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace sf {

namespace {

using Clock = std::chrono::steady_clock;

double unixNow() {
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// Shared scratch set for a seek's moved vertices (one seek at a time: the main thread).
IndexBits& batchBits() {
    static IndexBits bits;
    return bits;
}

void gather(const std::vector<glm::vec3>& from, const std::vector<uint32_t>& idx, std::vector<glm::vec3>& out) {
    out.resize(idx.size());
    poolFor(idx.size(), [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) out[i] = from[idx[i]];
    }, 1 << 14);
}

void scatter(std::vector<glm::vec3>& to, const std::vector<uint32_t>& idx, const std::vector<glm::vec3>& values) {
    poolFor(idx.size(), [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) to[idx[i]] = values[i];
    }, 1 << 14);
}

size_t vecBytes(const std::vector<glm::vec3>& v) { return v.capacity() * sizeof(glm::vec3); }

} // namespace

// ---------------------------------------------------------------------------------------------------------------
// The tree

Timeline::Kind Timeline::classify(const UndoRecord& r) {
    if (r.subdiv || r.mesh || r.topology || r.subtool) return Kind::Topology;
    if (!r.positions.empty()) return Kind::Stroke;
    if (!r.mask.empty()) return Kind::Mask;
    if (!r.colors.empty() || r.paint || r.texture || r.textureTiles) return Kind::Paint;
    if (!r.hidden.empty() || !r.polygroups.empty()) return Kind::Visibility;
    return Kind::Other;
}

bool Timeline::fastShape(const UndoRecord& r) {
    return !r.positions.empty() && r.layer == -1 && r.layerOffsets.empty() && r.mask.empty() && r.colors.empty() &&
           r.paintAlpha.empty() && !r.paint && r.hidden.empty() && r.polygroups.empty() && !r.textureTiles &&
           !r.texture && !r.mesh && !r.subdiv && !r.topology && !r.subtool && !r.uv;
}

void Timeline::reset() {
    nodes_.clear();
    checkpoints_.clear();
    nodes_.emplace_back();
    nodes_[0].kind = Kind::Start;
    nodes_[0].label = "Start";
    nodes_[0].time = unixNow();
    path_.assign(1, 0);
    cursor_ = 0;
}

uint32_t Timeline::newNode(uint32_t parent, const UndoRecord* r) {
    const uint32_t id = uint32_t(nodes_.size());
    nodes_.emplace_back();
    Node& n = nodes_.back();
    n.parent = parent;
    n.time = unixNow();
    if (r) {
        n.label = r->label.empty() ? "Edit" : r->label;
        n.kind = classify(*r);
        n.touched = uint32_t(std::max({r->positions.indices.size(), r->mask.indices.size(), r->colors.indices.size(),
                                       r->hidden.indices.size(), r->polygroups.indices.size()}));
    }
    if (parent != kNone) {
        nodes_[parent].children.push_back(id);
        nodes_[parent].activeChild = id;
    }
    return id;
}

std::shared_ptr<Timeline> Timeline::attach(Document& doc) {
    auto t = std::make_shared<Timeline>();
    t->reset();
    t->stack_ = &doc.undo;
    UndoStack& s = doc.undo;
    for (UndoRecord& r : s.undo_) t->path_.push_back(t->newNode(t->path_.back(), &r));
    t->cursor_ = s.undo_.size();
    for (auto it = s.redo_.rbegin(); it != s.redo_.rend(); ++it) t->path_.push_back(t->newNode(t->path_.back(), &*it));
    // Back to the cursor's branch: newNode made each new node its parent's active child already.
    doc.undo.listener = t;
    t->sync(doc);
    return t;
}

Timeline* Timeline::of(UndoStack& stack) { return dynamic_cast<Timeline*>(stack.listener.get()); }

bool Timeline::onPath(uint32_t id) const {
    if (id >= nodes_.size() || !nodes_[id].alive) return false;
    const int d = depth(id);
    return d >= 0 && size_t(d) < path_.size() && path_[size_t(d)] == id;
}

int Timeline::depth(uint32_t id) const {
    if (id >= nodes_.size()) return -1;
    int d = 0;
    for (uint32_t p = nodes_[id].parent; p != kNone; p = nodes_[p].parent) ++d;
    return d;
}

size_t Timeline::aliveCount() const {
    size_t n = 0;
    for (const Node& x : nodes_) n += x.alive;
    return n;
}

std::vector<Timeline::Branch> Timeline::branches() const {
    std::vector<Branch> out;
    for (size_t i = 0; i < path_.size(); ++i) {
        const Node& p = nodes_[path_[i]];
        for (uint32_t c : p.children) {
            if (!nodes_[c].alive || (i + 1 < path_.size() && path_[i + 1] == c)) continue;
            Branch b{c, i, 1, c};
            while (nodes_[b.tip].activeChild != kNone && nodes_[nodes_[b.tip].activeChild].alive) {
                b.tip = nodes_[b.tip].activeChild;
                ++b.length;
            }
            out.push_back(b);
        }
    }
    return out;
}

UndoRecord* Timeline::recordOf(uint32_t id) {
    if (id >= nodes_.size() || !nodes_[id].alive || nodes_[id].parent == kNone) return nullptr;
    if (nodes_[id].stored) return nodes_[id].stored.get();
    if (!stack_) return nullptr;
    const int d = depth(id);
    if (d <= 0 || size_t(d) >= path_.size() || path_[size_t(d)] != id) return nullptr;
    const size_t i = size_t(d);
    if (i <= cursor_) return i - 1 < stack_->undo_.size() ? &stack_->undo_[i - 1] : nullptr;
    const size_t k = i - cursor_ - 1; // 0 = the next redo
    return k < stack_->redo_.size() ? &stack_->redo_[stack_->redo_.size() - 1 - k] : nullptr;
}

void Timeline::freeSubtree(uint32_t id) {
    std::vector<uint32_t> todo{id};
    while (!todo.empty()) {
        const uint32_t x = todo.back();
        todo.pop_back();
        Node& n = nodes_[x];
        for (uint32_t c : n.children) todo.push_back(c);
        n.alive = false;
        n.children.clear();
        n.activeChild = kNone;
        n.stored.reset();
        std::vector<glm::vec3>().swap(n.other);
        std::vector<glm::vec3>().swap(n.retuneDelta);
        n.fast = false;
        checkpoints_.erase(x);
    }
    const uint32_t p = nodes_[id].parent;
    if (p != kNone) {
        auto& ch = nodes_[p].children;
        ch.erase(std::remove(ch.begin(), ch.end(), id), ch.end());
        if (nodes_[p].activeChild == id) nodes_[p].activeChild = ch.empty() ? kNone : ch.back();
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Following the undo stack

void Timeline::undoPushed(UndoRecord& pushed, std::deque<UndoRecord>& discardedRedo) {
    // The redo steps become an inactive branch: redo.back() is path_[cursor_ + 1].
    const size_t n = discardedRedo.size();
    for (size_t k = 0; k < n && cursor_ + 1 + k < path_.size(); ++k)
        nodes_[path_[cursor_ + 1 + k]].stored = std::make_unique<UndoRecord>(std::move(discardedRedo[n - 1 - k]));
    path_.resize(cursor_ + 1);
    path_.push_back(newNode(path_.back(), &pushed));
    ++cursor_;
    enforceBudgets();
}

void Timeline::captureFrom(Node& n, const UndoRecord& r, Mesh& mesh) {
    n.captured = true;
    const auto& idx = r.positions.indices;
    uint32_t maxIndex = 0;
    for (uint32_t v : idx) maxIndex = std::max(maxIndex, v);
    if (idx.empty() || maxIndex >= mesh.vertexCount()) return;
    glm::vec3 lo(1e30f), hi(-1e30f); // bounds of the stroke after it
    for (uint32_t v : idx) {
        lo = glm::min(lo, mesh.positions[v]);
        hi = glm::max(hi, mesh.positions[v]);
    }
    n.center = (lo + hi) * 0.5f;
    n.radius = glm::length(hi - lo) * 0.5f;
    if (fastShape(r) && mesh.layers.empty()) {
        gather(mesh.positions, idx, n.other);
        n.fast = true;
    }
}

void Timeline::capture(Mesh& mesh) {
    if (cursor_ == 0 || cursor_ >= path_.size() || !stack_ || cursor_ - 1 >= stack_->undo_.size()) return;
    captureFrom(nodes_[path_[cursor_]], stack_->undo_[cursor_ - 1], mesh);
}

void Timeline::undoApplying(UndoRecord& r, Mesh& mesh, bool isUndo) {
    // A step about to be undone: bring its "after" values in while the mesh still shows them.
    if (isUndo && cursor_ > 0 && !nodes_[path_[cursor_]].captured) captureFrom(nodes_[path_[cursor_]], r, mesh);
}

void Timeline::undoApplied(UndoRecord& r, Mesh& mesh, bool isUndo) {
    if (isUndo) {
        if (cursor_ == 0) return;
        Node& n = nodes_[path_[cursor_]];
        if (n.fast) {
            if (mesh.layers.empty() && fastShape(r)) gather(mesh.positions, r.positions.indices, n.other); // now "before"
            else n.fast = false, std::vector<glm::vec3>().swap(n.other);
        }
        --cursor_;
    } else {
        if (cursor_ + 1 >= path_.size()) return;
        ++cursor_;
        Node& n = nodes_[path_[cursor_]];
        if (!n.captured) {
            captureFrom(n, r, mesh);
        } else if (mesh.layers.empty() && fastShape(r)) {
            gather(mesh.positions, r.positions.indices, n.other); // "after"
            n.fast = true;
        } else {
            n.fast = false;
            std::vector<glm::vec3>().swap(n.other);
        }
    }
}

void Timeline::undoDroppedOldest() {
    if (path_.size() < 2) return;
    const uint32_t oldRoot = path_[0], newRoot = path_[1];
    for (uint32_t c : std::vector<uint32_t>(nodes_[oldRoot].children))
        if (c != newRoot) freeSubtree(c);
    Node& o = nodes_[oldRoot];
    o.alive = false;
    o.children.clear();
    checkpoints_.erase(oldRoot);
    Node& n = nodes_[newRoot];
    n.parent = kNone;
    n.stored.reset();
    n.fast = false;
    std::vector<glm::vec3>().swap(n.other);
    std::vector<glm::vec3>().swap(n.retuneDelta);
    path_.erase(path_.begin());
    if (cursor_ > 0) --cursor_;
}

void Timeline::undoCleared() { reset(); }

void Timeline::sync(Document& doc) {
    stack_ = &doc.undo;
    if (doc.undo.listener.get() != this) return;
    // The stack and the tree should always agree; if something replaced the stack's steps behind our back,
    // start over from what the stack holds.
    if (path_.size() != doc.undo.undo_.size() + doc.undo.redo_.size() + 1 || cursor_ != doc.undo.undo_.size()) {
        reset();
        for (UndoRecord& r : doc.undo.undo_) path_.push_back(newNode(path_.back(), &r));
        cursor_ = doc.undo.undo_.size();
        for (auto it = doc.undo.redo_.rbegin(); it != doc.undo.redo_.rend(); ++it)
            path_.push_back(newNode(path_.back(), &*it));
    }
    if (cursor_ > 0) {
        Node& n = nodes_[path_[cursor_]];
        if (!n.captured) capture(doc.mesh);
    }
    maybeCheckpoint(doc.mesh);
}

// ---------------------------------------------------------------------------------------------------------------
// Checkpoints and budgets

size_t Timeline::checkpointBytes() const {
    size_t b = 0;
    for (const auto& [id, c] : checkpoints_) b += vecBytes(c);
    return b;
}

size_t Timeline::extraBytes() const {
    size_t b = 0;
    for (const Node& n : nodes_) {
        if (!n.alive) continue;
        b += vecBytes(n.other) + vecBytes(n.retuneDelta);
        if (n.stored) b += n.stored->approxBytes();
    }
    return b;
}

void Timeline::maybeCheckpoint(const Mesh& mesh) {
    const uint32_t id = path_[cursor_];
    if (checkpoints_.count(id) || cursor_ % std::max<size_t>(checkpointEvery, 1) != 0 || !mesh.layers.empty() ||
        mesh.vertexCount() == 0)
        return;
    if (cursor_ > 0 && !nodes_[id].fast) return; // a checkpoint helps runs of fast steps
    const size_t bytes = mesh.vertexCount() * sizeof(glm::vec3);
    if (bytes > checkpointBudget) return;
    std::vector<glm::vec3> copy(mesh.vertexCount());
    poolFor(copy.size(), [&](size_t b, size_t e) { std::memcpy(&copy[b], &mesh.positions[b], (e - b) * sizeof(glm::vec3)); },
            1 << 18);
    checkpoints_[id] = std::move(copy);
    enforceBudgets();
}

void Timeline::enforceBudgets() {
    // Checkpoints: thin them out by doubling the interval, then drop the deepest ones off the active branch.
    while (checkpointBytes() > checkpointBudget && !checkpoints_.empty()) {
        checkpointEvery = std::max<size_t>(checkpointEvery, 1) * 2;
        bool dropped = false;
        for (auto it = checkpoints_.begin(); it != checkpoints_.end();) {
            const int d = depth(it->first);
            if (d > 0 && size_t(d) % checkpointEvery != 0) {
                it = checkpoints_.erase(it);
                dropped = true;
            } else {
                ++it;
            }
        }
        if (!dropped && checkpointBytes() > checkpointBudget) {
            // Even the multiples don't fit: keep the shallowest.
            uint32_t deepest = checkpoints_.begin()->first;
            for (const auto& [id, c] : checkpoints_)
                if (depth(id) > depth(deepest)) deepest = id;
            checkpoints_.erase(deepest);
        }
    }
    // Inactive branches and `other` copies: the oldest branches go first, then the oldest steps' copies (those
    // steps are then walked by the undo stack instead).
    size_t extra = extraBytes();
    if (extra <= branchBudget) return;
    std::vector<Branch> bs = branches();
    std::sort(bs.begin(), bs.end(), [&](const Branch& a, const Branch& b) { return nodes_[a.first].time < nodes_[b.first].time; });
    for (const Branch& b : bs) {
        if (extra <= branchBudget) break;
        freeSubtree(b.first);
        extra = extraBytes();
    }
    for (size_t i = 1; i < path_.size() && extra > branchBudget; ++i) {
        Node& n = nodes_[path_[i]];
        if (!n.fast) continue;
        extra -= std::min(extra, vecBytes(n.other));
        n.fast = false;
        std::vector<glm::vec3>().swap(n.other);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Travel

void Timeline::flushBatch(Document& doc) {
    if (batch_.empty()) return;
    const auto t0 = Clock::now();
    struct Done {
        Clock::time_point t0;
        double& ms;
        ~Done() { ms += std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); }
    } done{t0, flushMs_};
    Mesh& mesh = doc.mesh;
    IndexBits& bits = batchBits();
    if (bits.capacity() < mesh.vertexCount()) bits.resize(mesh.vertexCount());
    std::vector<uint32_t> verts;
    for (uint32_t v : batch_) bits.insertLocal(v);
    bits.take(verts);
    batch_.clear();
    // Normals: the parallel update unless nearly everything moved (Mesh::recomputeAllNormals sums serially).
    if (verts.size() * 10 > mesh.vertexCount() * 9) mesh.recomputeAllNormals();
    else mesh.updateNormals(verts);
    // Refits only grow boxes: after moving much of the mesh, tighten the whole tree (as UndoStack::apply does).
    if (verts.size() * 4 > mesh.vertexCount()) doc.bvh.refitAll(mesh);
    else doc.bvh.refit(mesh, verts);
}

namespace {
bool fastUsable(const Timeline::Node& n, const UndoRecord* r, const Mesh& mesh) {
    if (!n.fast || !r || !Timeline::fastShape(*r) || !mesh.layers.empty()) return false;
    if (n.other.size() != r->positions.indices.size()) return false;
    for (uint32_t v : r->positions.indices) // (cheap next to the copy below)
        if (v >= mesh.vertexCount()) return false;
    return true;
}
} // namespace

void Timeline::walkTo(Document& doc, size_t index) {
    UndoStack& s = doc.undo;
    Mesh& mesh = doc.mesh;
    while (cursor_ > index) {
        Node& n = nodes_[path_[cursor_]];
        UndoRecord* r = s.undo_.empty() ? nullptr : &s.undo_.back();
        if (fastUsable(n, r, mesh)) {
            scatter(mesh.positions, r->positions.indices, r->positions.values); // "before"
            std::swap(r->positions.values, n.other);                           // the record now holds "after"
            batch_.insert(batch_.end(), r->positions.indices.begin(), r->positions.indices.end());
            s.redo_.push_back(std::move(*r));
            s.undo_.pop_back();
            ++s.revision_;
            --cursor_;
            ++lastSeek_.batched;
        } else {
            flushBatch(doc);
            if (!s.undo(mesh, doc.bvh)) break;
            if (UndoRecord* u = s.lastRedo(); u && u->subtool) doc.applySubtoolStep(*u->subtool);
            ++lastSeek_.walked;
        }
    }
    while (cursor_ < index) {
        Node& n = nodes_[path_[cursor_ + 1]];
        UndoRecord* r = s.redo_.empty() ? nullptr : &s.redo_.back();
        if (fastUsable(n, r, mesh)) {
            scatter(mesh.positions, r->positions.indices, r->positions.values); // "after"
            std::swap(r->positions.values, n.other);
            batch_.insert(batch_.end(), r->positions.indices.begin(), r->positions.indices.end());
            s.undo_.push_back(std::move(*r));
            s.redo_.pop_back();
            ++s.revision_;
            ++cursor_;
            ++lastSeek_.batched;
        } else {
            flushBatch(doc);
            if (!s.redo(mesh, doc.bvh)) break;
            if (UndoRecord* u = s.lastUndo(); u && u->subtool) doc.applySubtoolStep(*u->subtool);
            ++lastSeek_.walked;
        }
    }
    flushBatch(doc);
}

bool Timeline::jumpWithCheckpoint(Document& doc, size_t index) {
    Mesh& mesh = doc.mesh;
    const size_t V = mesh.vertexCount();
    if (checkpoints_.empty() || index == cursor_ || !mesh.layers.empty()) return false;
    const size_t lo = std::min(index, cursor_), hi = std::max(index, cursor_);
    auto fastAt = [&](size_t i) { return fastUsable(nodes_[path_[i]], recordOf(path_[i]), mesh); };
    size_t walkCost = 0;
    for (size_t i = lo + 1; i <= hi; ++i) {
        if (!fastAt(i)) return false; // a slow step in between: walk
        walkCost += nodes_[path_[i]].other.size();
    }
    // The nearest usable checkpoint at or before the target.
    size_t from = std::string::npos, replayCost = 0;
    for (size_t p = index + 1; p-- > 0;) {
        if (p < index && !fastAt(p + 1)) break;
        if (p < index) replayCost += nodes_[path_[p + 1]].other.size();
        auto it = checkpoints_.find(path_[p]);
        if (it != checkpoints_.end() && it->second.size() == V) {
            from = p;
            break;
        }
        if (replayCost + V >= walkCost) return false;
    }
    if (from == std::string::npos || V + replayCost >= walkCost) return false;
    const std::vector<glm::vec3>& ck = checkpoints_[path_[from]];
    poolFor(V, [&](size_t b, size_t e) { std::memcpy(&mesh.positions[b], &ck[b], (e - b) * sizeof(glm::vec3)); }, 1 << 18);
    for (size_t j = from + 1; j <= index; ++j) {
        Node& n = nodes_[path_[j]];
        const UndoRecord* r = recordOf(path_[j]);
        scatter(mesh.positions, r->positions.indices, j <= cursor_ ? n.other : r->positions.values); // "after" values
        ++lastSeek_.replayed;
    }
    // The steps between move sides without touching the mesh: swap which vector the record holds.
    UndoStack& s = *stack_;
    while (cursor_ > index) {
        UndoRecord& r = s.undo_.back();
        std::swap(r.positions.values, nodes_[path_[cursor_]].other);
        s.redo_.push_back(std::move(r));
        s.undo_.pop_back();
        --cursor_;
    }
    while (cursor_ < index) {
        UndoRecord& r = s.redo_.back();
        std::swap(r.positions.values, nodes_[path_[cursor_ + 1]].other);
        s.undo_.push_back(std::move(r));
        s.redo_.pop_back();
        ++cursor_;
    }
    ++s.revision_;
    // Normals, the BVH and the GPU: only where the shape changed (the steps between the old and the new cursor).
    for (size_t j = lo + 1; j <= hi; ++j) {
        const UndoRecord* r = recordOf(path_[j]);
        batch_.insert(batch_.end(), r->positions.indices.begin(), r->positions.indices.end());
    }
    flushBatch(doc);
    lastSeek_.usedCheckpoint = true;
    return true;
}

bool Timeline::seek(Document& doc, size_t index, std::string* error) {
    if (doc.undo.listener.get() != this) {
        if (error) *error = "This timeline doesn't watch the document's history";
        return false;
    }
    sync(doc);
    if (index >= path_.size()) {
        if (error) *error = "No step " + std::to_string(index) + " on this branch (it has " + std::to_string(path_.size() - 1) + ")";
        return false;
    }
    const auto t0 = Clock::now();
    lastSeek_ = {};
    if (!jumpWithCheckpoint(doc, index)) walkTo(doc, index);
    lastSeek_.ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    sync(doc);
    if (std::getenv("SF_TIMELINE_LOG"))
        std::fprintf(stderr, "timeline seek to %zu: %.1f ms (flush %.1f ms), then sync %.1f ms\n", index, lastSeek_.ms,
                     flushMs_, std::chrono::duration<double, std::milli>(Clock::now() - t0).count() - lastSeek_.ms);
    flushMs_ = 0.0;
    if (cursor_ != index) {
        if (error) *error = "Stopped at step " + std::to_string(cursor_);
        return false;
    }
    return true;
}

bool Timeline::select(Document& doc, uint32_t id, std::string* error) {
    if (doc.undo.listener.get() != this) {
        if (error) *error = "This timeline doesn't watch the document's history";
        return false;
    }
    sync(doc);
    if (id >= nodes_.size() || !nodes_[id].alive) {
        if (error) *error = "No such step";
        return false;
    }
    if (onPath(id)) return seek(doc, size_t(depth(id)), error);
    std::vector<uint32_t> chain; // from the branch's first node to `id`
    uint32_t a = id;
    while (a != kNone && !onPath(a)) {
        chain.push_back(a);
        a = nodes_[a].parent;
    }
    if (a == kNone) {
        if (error) *error = "That step isn't connected to the history any more";
        return false;
    }
    std::reverse(chain.begin(), chain.end());
    const size_t ap = size_t(depth(a));
    if (!seek(doc, ap, error)) return false;
    UndoStack& s = doc.undo;
    // The current continuation becomes an inactive branch (all of it is redo steps now).
    const size_t n = path_.size() - 1 - ap;
    for (size_t k = 0; k < n && k < s.redo_.size(); ++k)
        nodes_[path_[ap + 1 + k]].stored = std::make_unique<UndoRecord>(std::move(s.redo_[s.redo_.size() - 1 - k]));
    s.redo_.clear();
    path_.resize(ap + 1);
    for (uint32_t c : chain) {
        nodes_[path_.back()].activeChild = c;
        path_.push_back(c);
    }
    for (uint32_t x = nodes_[id].activeChild; x != kNone && nodes_[x].alive; x = nodes_[x].activeChild) path_.push_back(x);
    for (size_t k = path_.size() - 1; k > ap; --k) {
        Node& node = nodes_[path_[k]];
        if (!node.stored) { // (shouldn't happen) cut the branch here
            path_.resize(k);
            s.redo_.clear();
            for (size_t j = path_.size() - 1; j > ap; --j) {
                s.redo_.push_back(std::move(*nodes_[path_[j]].stored));
                nodes_[path_[j]].stored.reset();
            }
            break;
        }
        s.redo_.push_back(std::move(*node.stored));
        node.stored.reset();
    }
    ++s.revision_;
    const int d = depth(id);
    if (d < 0 || size_t(d) >= path_.size()) {
        if (error) *error = "That branch couldn't be restored";
        return false;
    }
    return seek(doc, size_t(d), error);
}

bool Timeline::canRetune(uint32_t id, std::string* why) const {
    auto no = [&](const std::string& w) {
        if (why) *why = w;
        return false;
    };
    if (id >= nodes_.size() || !nodes_[id].alive || nodes_[id].parent == kNone) return no("Pick a stroke");
    const Node& n = nodes_[id];
    if (!onPath(id)) return no("Select the stroke's branch first");
    if (n.kind != Kind::Stroke) return no("Only sculpt strokes can be re-tuned");
    if (!n.fast) return no("This stroke can't be re-tuned (it was made on a sculpt layer, or its history was trimmed)");
    std::vector<uint32_t> todo(n.children.begin(), n.children.end());
    while (!todo.empty()) {
        const uint32_t x = todo.back();
        todo.pop_back();
        const UndoRecord* r = recordOf(x);
        if (!r) continue;
        if (r->mesh || r->topology || r->subdiv || r->subtool || !r->layerOffsets.empty() || (!r->positions.empty() && r->layer != -1))
            return no("A later step (\"" + nodes_[x].label + "\") changes the mesh as a whole; re-tuning would undo it");
        for (uint32_t c : nodes_[x].children) todo.push_back(c);
    }
    return true;
}

bool Timeline::retune(Document& doc, uint32_t id, float strength, std::string* error) {
    if (doc.undo.listener.get() != this) {
        if (error) *error = "This timeline doesn't watch the document's history";
        return false;
    }
    sync(doc);
    if (!canRetune(id, error)) return false;
    strength = std::clamp(strength, 0.0f, 4.0f);
    Node& n = nodes_[id];
    const float old = n.strength;
    if (std::abs(strength - old) < 1e-6f) return true;
    UndoRecord* r = recordOf(id);
    const size_t pos = size_t(depth(id));
    const bool applied = pos <= cursor_; // the mesh shows this stroke
    std::vector<glm::vec3>& before = applied ? r->positions.values : n.other;
    std::vector<glm::vec3>& after = applied ? n.other : r->positions.values;
    const auto& idx = r->positions.indices;
    if (n.retuneDelta.empty()) { // first re-tune: the stroke's own delta at strength 1 (old is 1 then)
        n.retuneDelta.resize(idx.size());
        for (size_t i = 0; i < idx.size(); ++i) n.retuneDelta[i] = after[i] - before[i];
    }
    std::vector<glm::vec3> d(idx.size());
    for (size_t i = 0; i < idx.size(); ++i) d[i] = (strength - old) * n.retuneDelta[i];
    for (size_t i = 0; i < idx.size(); ++i) after[i] += d[i];
    // Where later steps overlap the stroke, their stored positions move with it (both sides).
    std::vector<std::pair<uint32_t, uint32_t>> sorted(idx.size());
    for (size_t i = 0; i < idx.size(); ++i) sorted[i] = {idx[i], uint32_t(i)};
    std::sort(sorted.begin(), sorted.end());
    auto lookup = [&](uint32_t v) -> int64_t {
        auto it = std::lower_bound(sorted.begin(), sorted.end(), std::make_pair(v, uint32_t(0)));
        return it != sorted.end() && it->first == v ? int64_t(it->second) : -1;
    };
    std::vector<uint32_t> todo(n.children.begin(), n.children.end());
    while (!todo.empty()) {
        const uint32_t x = todo.back();
        todo.pop_back();
        for (uint32_t c : nodes_[x].children) todo.push_back(c);
        UndoRecord* rx = recordOf(x);
        if (!rx || rx->positions.empty()) continue;
        Node& nx = nodes_[x];
        const auto& ix = rx->positions.indices;
        const bool hasOther = nx.fast && nx.other.size() == ix.size();
        poolFor(ix.size(), [&](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) {
                const int64_t k = lookup(ix[i]);
                if (k < 0) continue;
                rx->positions.values[i] += d[size_t(k)];
                if (hasOther) nx.other[i] += d[size_t(k)];
            }
        }, 1 << 14);
    }
    // Checkpoints at or after the stroke.
    for (auto& [cid, ck] : checkpoints_) {
        bool below = false;
        for (uint32_t p = cid; p != kNone; p = nodes_[p].parent)
            if (p == id) {
                below = true;
                break;
            }
        if (!below) continue;
        for (size_t i = 0; i < idx.size(); ++i)
            if (idx[i] < ck.size()) ck[idx[i]] += d[i];
    }
    if (applied) {
        Mesh& mesh = doc.mesh;
        for (size_t i = 0; i < idx.size(); ++i) mesh.positions[idx[i]] += d[i];
        mesh.updateNormals(idx);
        doc.bvh.refit(mesh, idx);
    }
    n.strength = strength;
    ++doc.undo.revision_;
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Saving

namespace {

constexpr char kMagic[8] = {'S', 'F', 'H', 'I', 'S', 'T', '1', '\0'};
constexpr char kSidecarMagic[8] = {'S', 'F', 'H', 'I', 'S', 'T', 'S', '\0'};
constexpr uint32_t kVersion = 1;

struct Writer {
    std::vector<uint8_t>& out;
    template <class T>
    void pod(const T& v) {
        const auto* p = reinterpret_cast<const uint8_t*>(&v);
        out.insert(out.end(), p, p + sizeof(T));
    }
    template <class T>
    void vec(const std::vector<T>& v) {
        pod(uint64_t(v.size()));
        const auto* p = reinterpret_cast<const uint8_t*>(v.data());
        out.insert(out.end(), p, p + v.size() * sizeof(T));
    }
    void str(const std::string& s) {
        pod(uint32_t(s.size()));
        out.insert(out.end(), s.begin(), s.end());
    }
};

struct Reader {
    const uint8_t* p;
    const uint8_t* end;
    bool ok = true;
    template <class T>
    T pod() {
        T v{};
        if (size_t(end - p) < sizeof(T)) {
            ok = false;
            return v;
        }
        std::memcpy(&v, p, sizeof(T));
        p += sizeof(T);
        return v;
    }
    template <class T>
    void vec(std::vector<T>& v) {
        const uint64_t n = pod<uint64_t>();
        if (!ok || n > uint64_t(end - p) / sizeof(T)) {
            ok = false;
            return;
        }
        v.resize(size_t(n));
        if (n) std::memcpy(v.data(), p, size_t(n) * sizeof(T));
        p += n * sizeof(T);
    }
    std::string str() {
        const uint32_t n = pod<uint32_t>();
        if (!ok || n > uint64_t(end - p)) {
            ok = false;
            return {};
        }
        std::string s(reinterpret_cast<const char*>(p), n);
        p += n;
        return s;
    }
};

bool savable(const UndoRecord& r) {
    return !r.mesh && !r.subdiv && !r.topology && !r.subtool && !r.texture && !r.textureTiles && !r.paint && !r.uv &&
           r.layerOffsets.empty() && r.layer == -1 && r.paintLayer == -1 && r.paintAlpha.empty();
}

template <class T>
void writeChannel(Writer& w, const UndoChannel<T>& c) {
    w.vec(c.indices);
    w.vec(c.values);
}
template <class T>
bool readChannel(Reader& rd, UndoChannel<T>& c) {
    rd.vec(c.indices);
    rd.vec(c.values);
    return rd.ok && c.indices.size() == c.values.size();
}

template <class T>
bool channelInRange(const UndoChannel<T>& c, size_t n) {
    for (uint32_t i : c.indices)
        if (i >= n) return false;
    return true;
}

} // namespace

// A fingerprint of the mesh a history belongs to.
static uint64_t meshFingerprint(const Mesh& mesh) {
    const size_t n = mesh.vertexCount();
    const size_t blocks = (n + (1 << 16) - 1) >> 16;
    std::vector<uint64_t> part(blocks, 0);
    poolFor(n, [&](size_t b, size_t e) {
        uint64_t h = 1469598103934665603ull;
        const auto* p = reinterpret_cast<const uint32_t*>(&mesh.positions[b]);
        for (size_t i = 0; i < (e - b) * 3; ++i) h = (h ^ p[i]) * 1099511628211ull;
        part[b >> 16] = h;
    }, 1 << 16);
    uint64_t h = 1469598103934665603ull ^ n ^ (uint64_t(mesh.triangleCount()) << 32);
    for (uint64_t x : part) h = (h ^ x) * 1099511628211ull;
    return h;
}

bool Timeline::save(std::vector<uint8_t>& out, const Mesh& mesh, std::string* error) const {
    // Which nodes can go in: the path from the newest unsavable step at or before the cursor, up to the first
    // unsavable one after it, and the branches off it that are savable all the way.
    auto ok = [&](uint32_t id) {
        const UndoRecord* r = recordOf(id);
        return r && savable(*r);
    };
    size_t first = 0;
    for (size_t i = 1; i <= cursor_; ++i)
        if (!ok(path_[i])) first = i;
    size_t last = cursor_;
    while (last + 1 < path_.size() && ok(path_[last + 1])) ++last;
    std::vector<uint32_t> order; // parents before children
    std::map<uint32_t, uint32_t> local;
    for (size_t i = first; i <= last; ++i) {
        local[path_[i]] = uint32_t(order.size());
        order.push_back(path_[i]);
    }
    for (size_t i = first; i <= last; ++i) { // inactive branches
        std::vector<uint32_t> todo;
        for (uint32_t c : nodes_[path_[i]].children)
            if (!local.count(c)) todo.push_back(c);
        while (!todo.empty()) {
            const uint32_t x = todo.back();
            todo.pop_back();
            if (!nodes_[x].alive || !ok(x)) continue;
            local[x] = uint32_t(order.size());
            order.push_back(x);
            for (uint32_t c : nodes_[x].children) todo.push_back(c);
        }
    }
    if (order.size() < 2) {
        if (error) *error = "Nothing in the history can be saved";
        return false;
    }
    out.clear();
    Writer w{out};
    out.insert(out.end(), kMagic, kMagic + 8);
    w.pod(kVersion);
    w.pod(uint64_t(mesh.vertexCount()));
    w.pod(uint64_t(mesh.triangleCount()));
    w.pod(meshFingerprint(mesh));
    w.pod(uint32_t(order.size()));
    w.pod(uint32_t(cursor_ - first)); // the cursor's local index (path nodes come first, in order)
    w.pod(uint32_t(last - first + 1)); // how many of the first nodes are the active branch
    for (size_t k = 0; k < order.size(); ++k) {
        const Node& n = nodes_[order[k]];
        const uint32_t parent = k == 0 ? kNone : local.at(n.parent);
        const uint32_t active = n.activeChild != kNone && local.count(n.activeChild) ? local.at(n.activeChild) : kNone;
        w.pod(parent);
        w.pod(active);
        w.pod(uint8_t(k == 0 ? Kind::Start : n.kind));
        w.pod(n.time);
        w.pod(n.strength);
        w.pod(n.touched);
        w.pod(n.center);
        w.pod(n.radius);
        w.str(k == 0 ? std::string("Start") : n.label);
        if (k == 0) continue;
        const UndoRecord& r = *recordOf(order[k]);
        writeChannel(w, r.positions);
        writeChannel(w, r.mask);
        writeChannel(w, r.colors);
        writeChannel(w, r.hidden);
        writeChannel(w, r.polygroups);
        w.pod(uint8_t(n.fast));
        if (n.fast) w.vec(n.other);
        w.vec(n.retuneDelta);
    }
    return true;
}

bool Timeline::load(Document& doc, const std::vector<uint8_t>& in, std::string* error) {
    auto fail = [&](const std::string& e) {
        if (error) *error = e;
        return false;
    };
    if (in.size() < 8 || std::memcmp(in.data(), kMagic, 8) != 0) return fail("Not a SculptForge history");
    Reader rd{in.data() + 8, in.data() + in.size()};
    if (rd.pod<uint32_t>() != kVersion) return fail("This history was saved by a newer SculptForge");
    const Mesh& mesh = doc.mesh;
    const uint64_t verts = rd.pod<uint64_t>(), tris = rd.pod<uint64_t>(), print = rd.pod<uint64_t>();
    if (!rd.ok || verts != mesh.vertexCount() || tris != mesh.triangleCount() || print != meshFingerprint(mesh))
        return fail("The history belongs to a different version of this mesh");
    const uint32_t count = rd.pod<uint32_t>(), cursor = rd.pod<uint32_t>(), pathLen = rd.pod<uint32_t>();
    if (!rd.ok || count < 2 || pathLen < 1 || pathLen > count || cursor >= pathLen) return fail("The history file is damaged");
    auto t = std::make_shared<Timeline>();
    t->nodes_.resize(count);
    std::vector<std::unique_ptr<UndoRecord>> records(count);
    for (uint32_t k = 0; k < count; ++k) {
        Node& n = t->nodes_[k];
        n.parent = rd.pod<uint32_t>();
        n.activeChild = rd.pod<uint32_t>();
        n.kind = Kind(rd.pod<uint8_t>());
        n.time = rd.pod<double>();
        n.strength = rd.pod<float>();
        n.touched = rd.pod<uint32_t>();
        n.center = rd.pod<glm::vec3>();
        n.radius = rd.pod<float>();
        n.label = rd.str();
        if (!rd.ok || (k == 0) != (n.parent == kNone) || (k > 0 && n.parent >= k) ||
            (n.activeChild != kNone && n.activeChild >= count))
            return fail("The history file is damaged");
        if (k > 0) t->nodes_[n.parent].children.push_back(k);
        if (k == 0) continue;
        auto r = std::make_unique<UndoRecord>();
        r->label = n.label;
        bool good = readChannel(rd, r->positions) && readChannel(rd, r->mask) && readChannel(rd, r->colors) &&
                    readChannel(rd, r->hidden) && readChannel(rd, r->polygroups);
        n.fast = rd.pod<uint8_t>() != 0;
        if (n.fast) rd.vec(n.other);
        rd.vec(n.retuneDelta);
        good = good && rd.ok && channelInRange(r->positions, verts) && channelInRange(r->mask, verts) &&
               channelInRange(r->colors, verts) && channelInRange(r->hidden, tris) && channelInRange(r->polygroups, tris) &&
               (!n.fast || n.other.size() == r->positions.indices.size()) &&
               (n.retuneDelta.empty() || n.retuneDelta.size() == r->positions.indices.size());
        if (!good || r->empty()) return fail("The history file is damaged");
        records[k] = std::move(r);
    }
    // The active branch is nodes 0 .. pathLen - 1, each the parent of the next.
    for (uint32_t k = 1; k < pathLen; ++k)
        if (t->nodes_[k].parent != k - 1) return fail("The history file is damaged");
    UndoStack& s = doc.undo;
    s.listener.reset();
    s.undo_.clear();
    s.redo_.clear();
    t->path_.resize(pathLen);
    for (uint32_t k = 0; k < pathLen; ++k) t->path_[k] = k;
    t->cursor_ = cursor;
    for (uint32_t k = 1; k <= cursor; ++k) s.undo_.push_back(std::move(*records[k]));
    for (uint32_t k = pathLen - 1; k > cursor; --k) s.redo_.push_back(std::move(*records[k]));
    for (uint32_t k = pathLen; k < count; ++k) t->nodes_[k].stored = std::move(records[k]);
    t->stack_ = &s;
    s.listener = t;
    t->sync(doc);
    return true;
}

std::string historySidecarPath(const std::string& projectPath) {
    std::filesystem::path p(projectPath);
    p.replace_extension(".sfhist");
    return p.string();
}

bool saveHistorySidecar(Document& doc, const std::string& projectPath, std::string* error) {
    std::vector<uint8_t> file(kSidecarMagic, kSidecarMagic + 8);
    Writer w{file};
    uint32_t saved = 0;
    std::vector<uint8_t> blob;
    std::vector<uint8_t> body;
    Writer wb{body};
    for (int i = 0; i < doc.subtoolCount(); ++i) {
        UndoStack& stack = i == doc.activeSubtool ? doc.undo : doc.subtools[size_t(i)].undo;
        Timeline* t = Timeline::of(stack);
        if (!t) continue;
        t->bindStack(stack);
        if (!t->save(blob, doc.subtoolMesh(i))) continue;
        wb.pod(uint32_t(i));
        wb.str(doc.subtools[size_t(i)].name);
        wb.vec(blob);
        ++saved;
    }
    const std::string path = historySidecarPath(projectPath);
    std::error_code ec;
    if (saved == 0) {
        std::filesystem::remove(path, ec); // a history from before would no longer match
        return true;
    }
    w.pod(kVersion);
    w.pod(saved);
    file.insert(file.end(), body.begin(), body.end());
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(std::filesystem::path(tmp), std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(file.data()), std::streamsize(file.size()));
        if (!f) {
            if (error) *error = "Couldn't write " + tmp;
            return false;
        }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        if (error) *error = "Couldn't write " + path + ": " + ec.message();
        return false;
    }
    return true;
}

int loadHistorySidecar(Document& doc, const std::string& projectPath, std::string* error) {
    const std::string path = historySidecarPath(projectPath);
    std::ifstream f(std::filesystem::path(path), std::ios::binary);
    if (!f) return 0;
    std::vector<uint8_t> file((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (file.size() < 16 || std::memcmp(file.data(), kSidecarMagic, 8) != 0) {
        if (error) *error = path + " is not a SculptForge history";
        return 0;
    }
    Reader rd{file.data() + 8, file.data() + file.size()};
    if (rd.pod<uint32_t>() != kVersion) {
        if (error) *error = path + " was saved by a newer SculptForge";
        return 0;
    }
    const uint32_t count = rd.pod<uint32_t>();
    int loaded = 0;
    const int active = doc.activeSubtool;
    for (uint32_t k = 0; k < count && rd.ok; ++k) {
        const uint32_t index = rd.pod<uint32_t>();
        const std::string name = rd.str();
        std::vector<uint8_t> blob;
        rd.vec(blob);
        if (!rd.ok || int(index) >= doc.subtoolCount() || doc.subtools[index].name != name) continue;
        doc.selectSubtool(int(index));
        std::string e;
        if (Timeline::load(doc, blob, &e)) ++loaded;
        else if (error) *error = e;
    }
    doc.selectSubtool(active);
    return loaded;
}

// ---------------------------------------------------------------------------------------------------------------
// Commands

namespace {

Timeline& ensureTimeline(Document& doc) {
    if (Timeline* t = Timeline::of(doc.undo)) {
        t->sync(doc);
        return *t;
    }
    return *Timeline::attach(doc);
}

const char* kindName(Timeline::Kind k) {
    switch (k) {
    case Timeline::Kind::Start: return "start";
    case Timeline::Kind::Stroke: return "stroke";
    case Timeline::Kind::Mask: return "mask";
    case Timeline::Kind::Paint: return "paint";
    case Timeline::Kind::Visibility: return "visibility";
    case Timeline::Kind::Topology: return "topology";
    default: return "other";
    }
}

} // namespace

void registerTimelineCommands(CommandRegistry& registry) {
    registry.add({"timeline_info", "Describe the Sculpt Timeline: steps on the active branch, the cursor, branches and checkpoints.",
                  {{"list", ParamSpec::Type::Bool, "List every step of the active branch", false}},
                  [](Document& doc, const Args& args) -> CommandResult {
                      Timeline& t = ensureTimeline(doc);
                      std::ostringstream s;
                      s << "Timeline: " << t.path().size() - 1 << " steps on the active branch, at step " << t.cursor()
                        << ", " << t.branches().size() << " other branches, " << t.checkpointCount() << " checkpoints ("
                        << t.checkpointBytes() / (1024 * 1024) << " MiB), history extras "
                        << t.extraBytes() / (1024 * 1024) << " MiB";
                      if (argBool(args, "list", false)) {
                          for (size_t i = 0; i < t.path().size(); ++i) {
                              const Timeline::Node& n = t.node(t.path()[i]);
                              s << "\n" << (i == t.cursor() ? "> " : "  ") << i << " #" << t.path()[i] << " " << kindName(n.kind)
                                << " \"" << n.label << "\" " << n.touched << (n.fast ? " fast" : "")
                                << (t.hasCheckpoint(t.path()[i]) ? " checkpoint" : "");
                              if (n.strength != 1.0f) s << " strength " << n.strength;
                          }
                          for (const Timeline::Branch& b : t.branches())
                              s << "\n  branch from step " << b.from << ": #" << b.first << " .. #" << b.tip << " (" << b.length << " steps)";
                      }
                      return {true, s.str()};
                  }});
    registry.add({"timeline_seek", "Show the model as it was after step `index` of the active branch (0 = the start).",
                  {{"index", ParamSpec::Type::Number, "Step on the active branch", true}},
                  [](Document& doc, const Args& args) -> CommandResult {
                      Timeline& t = ensureTimeline(doc);
                      const double v = argNumber(args, "index", -1.0);
                      if (v < 0) return {false, "index must be 0 or more"};
                      std::string e;
                      if (!t.seek(doc, size_t(v), &e)) return {false, e};
                      const auto& st = t.lastSeek();
                      std::ostringstream s;
                      s << "At step " << t.cursor() << " (" << st.ms << " ms; " << st.batched << " batched, " << st.walked
                        << " walked, " << st.replayed << " replayed" << (st.usedCheckpoint ? ", from a checkpoint" : "") << ")";
                      return {true, s.str()};
                  }});
    registry.add({"timeline_select", "Make the branch through step `node` (an id from timeline_info list) active and show it.",
                  {{"node", ParamSpec::Type::Number, "Step id (#)", true}},
                  [](Document& doc, const Args& args) -> CommandResult {
                      Timeline& t = ensureTimeline(doc);
                      std::string e;
                      if (!t.select(doc, uint32_t(argNumber(args, "node", -1.0)), &e)) return {false, e};
                      return {true, "At step " + std::to_string(t.cursor()) + " of the selected branch"};
                  }});
    registry.add({"timeline_retune", "Change how strongly a past stroke shows (1 = as sculpted, 0 = gone) without redoing the strokes after it.",
                  {{"node", ParamSpec::Type::Number, "Step id (#) of the stroke", true},
                   {"strength", ParamSpec::Type::Number, "0 .. 4", true}},
                  [](Document& doc, const Args& args) -> CommandResult {
                      Timeline& t = ensureTimeline(doc);
                      std::string e;
                      if (!t.retune(doc, uint32_t(argNumber(args, "node", -1.0)), float(argNumber(args, "strength", 1.0)), &e))
                          return {false, e};
                      doc.modified = true;
                      return {true, "Stroke re-tuned"};
                  }});
    registry.add({"timeline_save", "Save the history beside the project (<name>.sfhist).",
                  {{"path", ParamSpec::Type::String, "Project path (default: the open project)", false}},
                  [](Document& doc, const Args& args) -> CommandResult {
                      ensureTimeline(doc);
                      const std::string path = argString(args, "path", doc.path);
                      if (path.empty()) return {false, "Save the project first (or give a path)"};
                      std::string e;
                      if (!saveHistorySidecar(doc, path, &e)) return {false, e};
                      return {true, "History saved to " + historySidecarPath(path)};
                  }});
    registry.add({"timeline_load", "Load the history saved beside the project (<name>.sfhist), if it matches the mesh.",
                  {{"path", ParamSpec::Type::String, "Project path (default: the open project)", false}},
                  [](Document& doc, const Args& args) -> CommandResult {
                      const std::string path = argString(args, "path", doc.path);
                      if (path.empty()) return {false, "No project path"};
                      std::string e;
                      const int n = loadHistorySidecar(doc, path, &e);
                      if (n == 0) return {false, e.empty() ? "No history beside " + path : e};
                      return {true, "History loaded for " + std::to_string(n) + " Part(s)"};
                  }});
}

} // namespace sf
