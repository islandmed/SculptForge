// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/UndoBatch.h"

#include "core/FigurePose.h"
#include "core/PaintLayer.h"
#include "core/Reference.h"
#include "core/SelectionSets.h"
#include "core/Splats.h"

#include <algorithm>
#include <cstring>

namespace sf {

namespace {

// The Parts that have a history of their own: one index per PartData (linked copies share theirs).
std::vector<int> historyParts(const Document& doc) {
    std::vector<int> out;
    std::vector<const PartData*> seen;
    for (int i = 0; i < doc.partCount(); ++i) {
        const PartData* d = doc.parts[size_t(i)].data.get();
        if (std::find(seen.begin(), seen.end(), d) != seen.end()) continue;
        seen.push_back(d);
        out.push_back(i);
    }
    return out;
}

// The Part that was active, made active again when it still exists (a walk selects the Parts it needs).
struct KeepActive {
    Document& doc;
    uint32_t id;
    explicit KeepActive(Document& d) : doc(d), id(d.parts[size_t(d.activePart)].id) {}
    ~KeepActive() {
        const int i = doc.partIndex(id);
        if (i >= 0 && i != doc.activePart) doc.selectPart(i);
    }
};

// Takes the rest of batch `batch`: the newest of its steps (undo) or the oldest (redo) on any Part, until none is left.
void followBatch(Document& doc, uint64_t batch, bool isUndo) {
    for (int guard = 0; guard < (1 << 20); ++guard) {
        int best = -1;
        uint64_t bestSeq = 0;
        for (int i : historyParts(doc)) {
            const UndoStack& s = doc.partUndo(i);
            const UndoRecord* r = isUndo ? (s.canUndo() ? s.undoStep(0) : nullptr) : (s.canRedo() ? s.redoStep(0) : nullptr);
            if (!r || r->batch != batch) continue;
            if (best < 0 || (isUndo ? r->seq > bestSeq : r->seq < bestSeq)) best = i, bestSeq = r->seq;
        }
        if (best < 0) return;
        if (!doc.checkedOut(best)) doc.selectPart(best);
        if (!(isUndo ? undoOnce(doc) : redoOnce(doc))) return;
    }
}

std::map<uint32_t, uint64_t> topsOf(const Document& doc) {
    std::map<uint32_t, uint64_t> tops;
    for (int i : historyParts(doc)) tops[doc.meshKey(i)] = doc.partUndo(i).topSeq();
    return tops;
}

uint64_t targetOf(const HistoryMark& m, uint32_t key) {
    const auto it = m.tops.find(key);
    return it == m.tops.end() ? 0 : it->second;
}

// 64-bit hash of raw bytes, eight at a time.
struct Hasher {
    uint64_t h = 0x9E3779B97F4A7C15ull;
    void word(uint64_t w) {
        h = (h ^ w) * 0xFF51AFD7ED558CCDull;
        h ^= h >> 31;
    }
    void bytes(const void* p, size_t n) {
        word(n);
        const auto* b = static_cast<const unsigned char*>(p);
        size_t i = 0;
        if (n >= 256) { // four independent chains, so the multiplies overlap (a mesh is hundreds of MB): ~3x the one-chain speed
            constexpr uint64_t kMul = 0xFF51AFD7ED558CCDull;
            uint64_t lane[4] = {h, h + 1, h + 2, h + 3};
            for (; i + 32 <= n; i += 32) {
                uint64_t w[4];
                std::memcpy(w, b + i, 32);
                for (int k = 0; k < 4; ++k) {
                    lane[k] = (lane[k] ^ w[k]) * kMul;
                    lane[k] ^= lane[k] >> 31;
                }
            }
            for (uint64_t l : lane) word(l);
        }
        for (; i + 8 <= n; i += 8) {
            uint64_t w;
            std::memcpy(&w, b + i, 8);
            word(w);
        }
        uint64_t tail = 0;
        if (i < n) std::memcpy(&tail, b + i, n - i);
        word(tail);
    }
    template <class T> void vec(const std::vector<T>& v) { bytes(v.data(), v.size() * sizeof(T)); }
    // An attribute that is empty, or holds only its default (undo allocates a missing one to restore it): the same hash.
    template <class T> void attribute(const std::vector<T>& v, const T& fill) {
        const bool plain = std::all_of(v.begin(), v.end(), [&](const T& x) { return std::memcmp(&x, &fill, sizeof(T)) == 0; });
        plain ? word(0xA77) : vec(v);
    }
    void str(const std::string& s) { bytes(s.data(), s.size()); }
    template <class T> void pod(const T& v) { bytes(&v, sizeof v); }
};

void hashSelectionSetContent(Hasher& h, const SelectionSetList* list) {
    h.word(list->sets.size());
    h.str(list->morphTarget);
    for (const auto& s : list->sets) {
        h.str(s->name);
        h.word(uint64_t(s->channels) | uint64_t(uint32_t(s->level)) << 32);
        h.word(uint64_t(s->vertexCount) | uint64_t(s->triangleCount) << 32);
        h.vec(s->maskRuns);
        h.vec(s->maskValues);
        h.vec(s->groupRuns);
        h.vec(s->paintRuns);
        h.vec(s->paintValues);
        h.vec(s->hiddenRuns);
        h.vec(s->uvs);
        h.vec(s->positions);
    }
}

void hashSelectionSets(Hasher& out, const SelectionSetList* list) {
    if (!list) return out.word(0x5E7);
    if (!list->hashCache.value) {
        Hasher h;
        hashSelectionSetContent(h, list);
        list->hashCache.value = h.h | 1; // (never 0)
    }
    out.word(list->hashCache.value);
}

} // namespace

bool undoOnce(Document& doc) {
    doc.syncBvh(); // (a pending rebuild first: a whole-mesh step keeps the tree it swaps out)
    if (!doc.undo.undo(doc.mesh, doc.bvh)) return false;
    if (UndoRecord* r = doc.undo.lastRedo(); r && r->part) doc.applyPartStep(*r->part);
    if (UndoRecord* r = doc.undo.lastRedo(); r && r->group) undoGroupOnOtherParts(doc, r->group, true);
    doc.modified = true;
    return true;
}

bool redoOnce(Document& doc) {
    doc.syncBvh();
    if (!doc.undo.redo(doc.mesh, doc.bvh)) return false;
    if (UndoRecord* r = doc.undo.lastUndo(); r && r->part) doc.applyPartStep(*r->part);
    if (UndoRecord* r = doc.undo.lastUndo(); r && r->group) undoGroupOnOtherParts(doc, r->group, false);
    doc.modified = true;
    return true;
}

// A step that belongs to a batch is taken with the rest of it, newest (undo) or oldest (redo) first across every Part.
bool undoStepOrBatch(Document& doc) {
    const uint64_t batch = doc.undo.canUndo() ? doc.undo.undoStep(0)->batch : 0;
    KeepActive keep(doc);
    if (!batch) return undoOnce(doc);
    followBatch(doc, batch, true);
    return true;
}

bool redoStepOrBatch(Document& doc) {
    const uint64_t batch = doc.undo.canRedo() ? doc.undo.redoStep(0)->batch : 0;
    KeepActive keep(doc);
    if (!batch) return redoOnce(doc);
    followBatch(doc, batch, false);
    return true;
}

uint64_t sceneHash(const Document& doc, HashScope scope) {
    const bool all = scope == HashScope::Everything;
    Hasher h;
    h.word(uint64_t(doc.partCount()));
    for (int i = 0; i < doc.partCount(); ++i) {
        const Part& p = doc.parts[size_t(i)];
        h.word(p.id);
        h.str(p.name);
        h.word(uint64_t(p.visible) | uint64_t(p.boolean) << 8 | uint64_t(p.shading) << 16 | uint64_t(p.folder) << 24);
        h.bytes(&p.transform, sizeof p.transform);
        const Mesh& m = doc.partMesh(i);
        h.vec(m.positions);
        h.vec(m.triangles);
        h.attribute(m.mask, 0.0f);
        h.attribute(m.colors, glm::vec3(1.0f));
        h.attribute(m.facegroups, uint32_t(0));
        h.attribute(m.hidden, uint8_t(0));
        h.attribute(m.uvs, glm::vec2(0.0f));
        h.attribute(m.creases, 0.0f);
        h.vec(m.base);
        h.vec(m.paintBase);
        if (all) {
            h.vec(m.uvFlags);
            h.vec(m.quadPartners);
            h.word(uint64_t(uint32_t(m.activePaintLayer)));
            h.word(m.paintLayers.size());
            for (const PaintLayer& l : m.paintLayers) {
                h.str(l.name);
                h.vec(l.color);
                h.vec(l.alpha);
                h.word(uint64_t(l.blend) | uint64_t(l.visible) << 8 | uint64_t(l.locked) << 16);
                h.pod(l.opacity);
            }
            hashSelectionSets(h, m.selectionSets.get());
        }
        for (const SculptLayer& l : m.layers) {
            h.str(l.name);
            h.bytes(&l.weight, sizeof l.weight);
            h.vec(l.delta);
        }
    }
    if (all) {
        h.word(doc.partFolders.size());
        for (const PartFolder& f : doc.partFolders) {
            h.word(uint64_t(f.id) | uint64_t(f.collapsed) << 32);
            h.str(f.name);
        }
        h.word(doc.references.size());
        for (const ReferenceImage& r : doc.references) {
            h.str(r.name);
            h.str(r.path);
            h.word(uint64_t(r.mode) | uint64_t(r.plane) << 8 | uint64_t(r.flipX) << 16 | uint64_t(r.flipY) << 17 |
                   uint64_t(r.locked) << 18 | uint64_t(r.visible) << 19 | uint64_t(r.inRenders) << 20 | uint64_t(r.embed) << 21);
            h.pod(r.center);
            h.pod(r.origin);
            h.pod(r.height);
            h.pod(r.sceneHeight);
            h.pod(r.stretch);
            h.pod(r.opacity);
            if (r.image) {
                h.word(uint64_t(uint32_t(r.image->width)) | uint64_t(uint32_t(r.image->height)) << 32);
                h.vec(r.image->rgba);
            } else {
                h.word(0);
            }
        }
    }
    // Splats are not in the undo history at all: any change to them is a change outside it.
    h.word(doc.splats.size());
    if (!all) return h.h;
    const SplatCloud& sp = doc.splats;
    h.word(doc.splatsVersion);
    h.word(uint64_t(uint32_t(sp.shDegree)));
    h.vec(sp.positions);
    h.vec(sp.rotations);
    h.vec(sp.scales);
    h.vec(sp.opacities);
    h.vec(sp.sh);
    h.pod(sp.transform);
    h.str(sp.name);
    return h.h;
}

HistoryMark markHistory(const Document& doc, HashScope scope) {
    HistoryMark m;
    m.epoch = undoClearEpoch();
    m.everything = scope == HashScope::Everything;
    m.hash = sceneHash(doc, scope);
    m.tops = topsOf(doc);
    return m;
}

HistoryWalk walkToMark(Document& doc, const HistoryMark& mark, uint64_t groupId) {
    HistoryWalk w;
    if (mark.epoch != undoClearEpoch()) {
        w.problem = "the history was cleared since (a new scene, an open or a step that can't be undone)";
        return w;
    }
    KeepActive keep(doc);
    for (int guard = 0; guard < (1 << 20); ++guard) {
        int undoPart = -1, redoPart = -1;
        uint64_t undoSeq = 0, redoSeq = 0;
        for (int i : historyParts(doc)) {
            const UndoStack& s = doc.partUndo(i);
            const uint64_t target = targetOf(mark, doc.meshKey(i));
            if (s.canUndo() && s.undoStep(0)->seq > target && s.undoStep(0)->seq > undoSeq)
                undoPart = i, undoSeq = s.undoStep(0)->seq;
            if (s.canRedo() && s.redoStep(0)->seq <= target && (redoPart < 0 || s.redoStep(0)->seq < redoSeq))
                redoPart = i, redoSeq = s.redoStep(0)->seq;
        }
        const bool isUndo = undoPart >= 0;
        const int part = isUndo ? undoPart : redoPart;
        if (part < 0) break;
        if (!doc.checkedOut(part)) doc.selectPart(part);
        if (!(isUndo ? undoOnce(doc) : redoOnce(doc))) {
            w.problem = "a step could not be applied";
            return w;
        }
        isUndo ? ++w.undone : ++w.redone;
        if (groupId) {
            UndoRecord* r = isUndo ? doc.undo.lastRedo() : doc.undo.lastUndo();
            if (r) r->batch = groupId;
        }
    }
    if (topsOf(doc) != mark.tops) {
        const auto now = topsOf(doc);
        for (const auto& [key, top] : mark.tops)
            if (!now.contains(key)) w.problem = "a Part that was there is gone (removed by a command that can't be undone)";
        for (const auto& [key, top] : now)
            if (w.problem.empty() && !mark.tops.contains(key))
                w.problem = "a Part added since can't be taken away (made by a command that can't be undone)";
        if (w.problem.empty()) w.problem = "some steps were dropped by the undo memory budget";
        return w;
    }
    w.exact = true;
    return w;
}

std::string markUnreachable(const Document& doc, const HistoryMark& mark) {
    if (mark.epoch != undoClearEpoch()) return "the history was cleared since (a new scene, an open or a step that can't be undone)";
    for (int i : historyParts(doc)) {
        const uint64_t target = targetOf(mark, doc.meshKey(i));
        if (!doc.partUndo(i).reaches(target))
            return "its steps were replaced by newer edits or dropped by the undo memory budget";
    }
    return {};
}

HistoryRestore restoreToMark(Document& doc, const HistoryMark& mark, uint64_t groupId) {
    HistoryRestore out;
    if (std::string why = markUnreachable(doc, mark); !why.empty()) {
        out.problem = std::move(why);
        out.untouched = true;
        return out;
    }
    const HashScope scope = mark.everything ? HashScope::Everything : HashScope::Mesh;
    const HistoryMark before = markHistory(doc, scope);
    std::map<uint64_t, uint64_t> batchOf; // step seq -> batch id, as the walk restamps the steps it moves
    for (int i : historyParts(doc)) doc.partUndo(i).forEachStep([&](UndoRecord& r) { batchOf[r.seq] = r.batch; });

    static_cast<HistoryWalk&>(out) = walkToMark(doc, mark, groupId);
    out.exact = out.exact && sceneHash(doc, scope) == mark.hash;
    if (out.exact) return out;

    out.offHistory = out.problem.empty();
    // Not there (steps the budget dropped, a Part a command that can't be undone made or removed, a scene changed outside
    // the history): go back to where we started, and stamp the steps with their own batch ids again.
    std::string problem = out.problem.empty() ? "the scene differs from the checkpoint (something in it was changed outside "
                                                "the undo history)"
                                              : out.problem;
    const HistoryWalk back = walkToMark(doc, before);
    for (int i : historyParts(doc))
        doc.partUndo(i).forEachStep([&](UndoRecord& r) {
            if (const auto it = batchOf.find(r.seq); it != batchOf.end()) r.batch = it->second;
        });
    out.untouched = back.exact && sceneHash(doc, scope) == before.hash;
    if (!out.untouched) problem += "; going back to where it was did not work out exactly (" + back.problem + ")";
    out.problem = std::move(problem);
    out.exact = false;
    out.undone = out.redone = 0;
    return out;
}

HistoryDistance distanceToMark(const Document& doc, const HistoryMark& mark) {
    HistoryDistance d;
    if (mark.epoch != undoClearEpoch()) return {-1, -1, false};
    for (int i : historyParts(doc)) {
        const UndoStack& s = doc.partUndo(i);
        const uint64_t target = targetOf(mark, doc.meshKey(i));
        for (size_t k = 0; k < s.undoCount(); ++k)
            if (s.undoStep(k)->seq > target) ++d.back;
        for (size_t k = 0; k < s.redoCount(); ++k)
            if (s.redoStep(k)->seq <= target) ++d.ahead;
    }
    return d;
}

} // namespace sf
