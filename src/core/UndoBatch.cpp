// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/UndoBatch.h"

#include "core/FigurePose.h"

#include <algorithm>
#include <cstring>

namespace sf {

namespace {

constexpr int kMaxWalkSteps = 1 << 20;

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

uint64_t topSeq(const UndoStack& s) { return s.canUndo() ? s.undoStep(0)->seq : s.retainedBase(); }

std::map<uint32_t, uint64_t> topsOf(const Document& doc) {
    std::map<uint32_t, uint64_t> tops;
    for (int i : historyParts(doc)) tops[doc.meshKey(i)] = topSeq(doc.partUndo(i));
    return tops;
}

uint64_t targetOf(const HistoryMark& m, uint32_t key) {
    const auto it = m.tops.find(key);
    return it == m.tops.end() ? 0 : it->second;
}

// Sequence numbers identify positions. Depth alone aliases discarded branches and the root left by eviction.
struct StackRange {
    size_t back = 0, ahead = 0;
    uint64_t destination = 0;
};

StackRange rangeTo(const UndoStack& stack, uint64_t target) {
    StackRange range;
    while (const UndoRecord* r = stack.undoStep(range.back)) {
        if (r->seq <= target) break;
        ++range.back;
    }
    const UndoRecord* top = stack.undoStep(range.back);
    range.destination = top ? top->seq : stack.retainedBase();
    while (const UndoRecord* r = stack.redoStep(range.ahead)) {
        if (r->seq > target) break;
        range.destination = r->seq;
        ++range.ahead;
    }
    return range;
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
};

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

// A whole-mesh record supplies its own destination. Sparse records carry the destination values of
// their changed elements. Neither requires building a BVH, computing normals or copying an undo stack.
struct CheckpointMesh {
    const Mesh& mesh;
    std::vector<const UndoRecord*> changes;
    std::shared_ptr<const SelectionSetList> sets = mesh.selectionSets;
    std::span<const QRemeshGuide> guides;

    template <class T>
    bool channel(Hasher& h, const std::vector<T>& current, UndoChannel<T> UndoRecord::* field, size_t size,
                 const T& fill, bool positions = false, std::vector<T> UvUndo::* wholeUv = nullptr,
                 UndoChannel<T> UvUndo::* sparseUv = nullptr) const {
        const std::vector<T>* values = &current;
        std::vector<T> edited;
        auto apply = [&](const UndoChannel<T>& delta) {
            if (delta.empty()) return true;
            if (values != &edited) {
                if (values->size() == size)
                    edited = *values;
                else
                    edited.assign(size, fill);
                values = &edited;
            }
            for (size_t i = 0; i < delta.indices.size(); ++i) {
                if (delta.indices[i] >= size) return false;
                edited[delta.indices[i]] = delta.values[i];
            }
            return true;
        };
        for (const UndoRecord* r : changes) {
            if (wholeUv && r->uv) {
                if (!r->uv->sparse)
                    values = &(r->uv.get()->*wholeUv);
                else if (values->size() == size && !apply(r->uv.get()->*sparseUv))
                    return false;
            }
            if (field && !apply(r->*field)) return false;
        }
        if (positions)
            h.vec(*values);
        else
            h.attribute(*values, fill);
        return true;
    }

    bool hash(Hasher& h) const {
        const size_t nv = mesh.vertexCount(), nt = mesh.triangleCount();
        if (!channel(h, mesh.positions, &UndoRecord::positions, nv, glm::vec3(0.0f), true)) return false;
        h.vec(mesh.triangles);
        if (!channel(h, mesh.mask, &UndoRecord::mask, nv, 0.0f) ||
            !channel(h, mesh.colors, &UndoRecord::colors, nv, glm::vec3(1.0f)) ||
            !channel(h, mesh.facegroups, &UndoRecord::facegroups, nt, uint32_t(0)) ||
            !channel(h, mesh.hidden, &UndoRecord::hidden, nt, uint8_t(0)) ||
            !channel(h, mesh.uvs, &UndoRecord::uvs, nt * 3, glm::vec2(0.0f), false, &UvUndo::uvs, &UvUndo::uvCorners) ||
            !channel<uint8_t>(h, mesh.uvFlags, nullptr, nt * 3, 0, false, &UvUndo::flags, &UvUndo::flagCorners) ||
            !channel(h, mesh.creases, &UndoRecord::creases, nt * 3, 0.0f))
            return false;
        h.vec(mesh.base);
        h.vec(mesh.paintBase);
        h.attribute(mesh.quadPartners, Mesh::kNoQuad);
        for (const PaintLayer& l : mesh.paintLayers) {
            h.str(l.name);
            h.bytes(&l.opacity, sizeof l.opacity);
            h.word(uint64_t(l.blend) | uint64_t(l.visible) << 8 | uint64_t(l.locked) << 9);
            h.vec(l.color);
            h.vec(l.alpha);
        }
        for (const SculptLayer& l : mesh.layers) {
            h.str(l.name);
            h.bytes(&l.weight, sizeof l.weight);
            h.vec(l.delta);
        }
        return true;
    }
};

static void hashPart(Hasher& h, const Part& p) {
    h.word(p.id);
    h.str(p.name);
    h.word(uint64_t(p.visible) | uint64_t(p.boolean) << 8 | uint64_t(p.shading) << 16 | uint64_t(p.folder) << 24);
    h.bytes(&p.transform, sizeof p.transform);
}

// These scene edits have no undo record. The projected Part list can differ from the current one,
// but folder and reference edits would survive the walk unchanged.
static void hashLayout(Hasher& h, const Document& source, int partCount) {
    h.word(partCount);
    for (const PartFolder& folder : source.partFolders) {
        h.word(folder.id);
        h.str(folder.name);
    }
    for (const ReferenceImage& ref : source.references) {
        h.str(referenceToJson(ref).dump());
        if (ref.image) {
            h.word(ref.image->width);
            h.word(ref.image->height);
            h.vec(ref.image->rgba);
        }
    }
}

static void hashGuides(Hasher& h, std::span<const QRemeshGuide> guides) {
    for (const QRemeshGuide& guide : guides) {
        h.word(guide.stroke);
        h.word(guide.closed);
        h.vec(guide.points);
    }
}

uint64_t sceneHash(const Document& doc) {
    Hasher h;
    hashLayout(h, doc, doc.partCount());
    for (int i = 0; i < doc.partCount(); ++i) {
        hashPart(h, doc.parts[size_t(i)]);
        hashGuides(h, doc.checkedOut(i) ? doc.qremeshGuides : doc.parts[size_t(i)].data->qremeshGuides);
        CheckpointMesh{doc.partMesh(i), {}}.hash(h);
    }
    h.word(doc.splats.size());
    return h.h;
}

static uint64_t splatHash(const SplatCloud& cloud) {
    Hasher h;
    h.str(cloud.name);
    h.bytes(&cloud.transform, sizeof cloud.transform);
    h.word(cloud.shDegree);
    h.vec(cloud.positions);
    h.vec(cloud.rotations);
    h.vec(cloud.scales);
    h.vec(cloud.opacities);
    h.vec(cloud.sh);
    return h.h;
}

HistoryMark markHistory(const Document& doc, bool checkpoint) {
    HistoryMark m;
    m.epoch = undoClearEpoch();
    m.hash = sceneHash(doc);
    m.tops = topsOf(doc);
    if (!checkpoint) return m;
    m.splatsVersion = doc.splatsVersion;
    m.splatsHash = splatHash(doc.splats);
    for (int i : historyParts(doc))
        if (const auto& sets = doc.partMesh(i).selectionSets) m.selectionSets.emplace(doc.meshKey(i), sets);
    return m;
}

HistoryWalk walkToMark(Document& doc, const HistoryMark& mark, uint64_t groupId) {
    HistoryWalk w;
    if (mark.epoch != undoClearEpoch()) {
        w.problem = "the history was cleared since (a new scene, an open or a step that can't be undone)";
        return w;
    }
    KeepActive keep(doc);
    for (int guard = 0; guard < kMaxWalkSteps; ++guard) {
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

// Validate coupled Part histories using only their cursors and structural steps. Geometry stays in the source
// document and the retained undo records until a destination channel needs to be read.
struct CheckpointPreview {
    const Document& source;
    const HistoryMark& mark;
    std::map<const PartData*, std::shared_ptr<PartData>> data;
    std::map<uint32_t, CheckpointMesh> projected;
    bool lost = false;
    size_t independentSteps = 0;

    ~CheckpointPreview() {
        // A stashed linked Part can refer back to the history that owns it.
        for (auto& [original, copy] : data) {
            copy->undo.undo_.clear();
            copy->undo.redo_.clear();
        }
    }

    static std::shared_ptr<MultiresStack> levels(const std::shared_ptr<MultiresStack>& src) {
        if (!src) return {};
        auto copy = std::make_shared<MultiresStack>(*src); // level data is copy-on-write, the working cache is not
        copy->cache.reset();
        return copy;
    }

    Part part(const Part& src) {
        Part p;
        p.id = src.id;
        p.name = src.name;
        p.visible = src.visible;
        p.shading = src.shading;
        p.boolean = src.boolean;
        p.folder = src.folder;
        p.transform = src.transform;
        auto [it, fresh] = data.try_emplace(src.data.get(), p.data);
        p.data = it->second;
        if (fresh) {
            const bool active = src.data == source.activePartInfo().data;
            p.data->key = src.data->key;
            const UndoStack& stack = active ? source.undo : src.data->undo;
            const uint64_t target = targetOf(mark, p.data->key);
            const Mesh& mesh = active ? source.mesh : src.data->mesh;
            const StackRange range = rangeTo(stack, target);
            if (range.destination != target && mark.tops.contains(p.data->key)) {
                lost = true;
                return p;
            }
            std::vector<const UndoRecord*> changes;
            changes.reserve(range.back + range.ahead);
            for (size_t i = 0; i < range.back; ++i)
                changes.push_back(stack.undoStep(i));
            for (size_t i = 0; i < range.ahead; ++i)
                changes.push_back(stack.redoStep(i));

            // Part steps and Figure Pose groups can move another Part's cursor or change the Part list.
            // For independent stacks the sequence check above already established their destination.
            const auto structural = [](const UndoRecord& r) { return r.part || r.group; };
            const bool coupled = std::any_of(stack.undo_.begin(), stack.undo_.end(), structural) ||
                                 std::any_of(stack.redo_.begin(), stack.redo_.end(), structural);
            const Mesh* base = &mesh;
            const auto snapshot = std::find_if(changes.rbegin(), changes.rend(),
                                               [](const UndoRecord* r) { return r->mesh && !r->subdiv && !r->ink; });
            if (snapshot != changes.rend()) {
                base = (*snapshot)->mesh.get();
                changes.erase(changes.begin(), snapshot.base());
            }
            // Ink moves only micro-displacement and takes precedence over base channels in UndoStack::apply.
            std::erase_if(changes, [](const UndoRecord* r) { return bool(r->ink) && !r->subdiv; });
            auto& view = projected.emplace(p.data->key, CheckpointMesh{*base, std::move(changes)}).first->second;
            view.guides = active ? source.qremeshGuides : src.data->qremeshGuides;
            for (const UndoRecord* r : view.changes)
                if (r->sets) view.sets = r->sets->list;

            if (!coupled) {
                p.data->undo.retainedBase_ = range.destination;
                independentSteps += range.back + range.ahead;
            } else {
                p.data->undo.retainedBase_ = stack.retainedBase();
                for (const UndoRecord& r : stack.undo_)
                    p.data->undo.undo_.push_back(cursor(r, r.seq > target));
                for (const UndoRecord& r : stack.redo_)
                    p.data->undo.redo_.push_back(cursor(r, r.seq <= target));
            }
        }
        return p;
    }

    UndoRecord cursor(const UndoRecord& r, bool crosses) {
        UndoRecord copy;
        copy.seq = r.seq;
        copy.group = r.group;
        if (!crosses && !r.group) return copy;
        if (r.part) {
            copy.part = std::make_shared<PartStep>();
            copy.part->id = r.part->id;
            copy.part->at = r.part->at;
            copy.part->isTransform = r.part->isTransform;
            copy.part->transform = r.part->transform;
            copy.part->hid = r.part->hid;
            copy.part->returnTo = r.part->returnTo;
            if (r.part->stashed) copy.part->stashed = std::make_unique<Part>(part(*r.part->stashed));
        }
        return copy;
    }

    // Only steps that derive values (e.g. subdivision or layered paint) need the undo engine during validation.
    // The last whole-mesh snapshot is already our base. Hold at most one working Part and one inverse record.
    static UndoRecord geometryStep(const UndoRecord& r) {
        UndoRecord copy;
        copy.layer = r.layer;
        copy.paintLayer = r.paintLayer;
        copy.positions = r.positions;
        copy.layerOffsets = r.layerOffsets;
        copy.colors = r.colors;
        copy.paintAlpha = r.paintAlpha;
        copy.mask = r.mask;
        copy.facegroups = r.facegroups;
        copy.hidden = r.hidden;
        copy.uvs = r.uvs;
        copy.creases = r.creases;
        if (r.paint) copy.paint = std::make_unique<PaintStackUndo>(*r.paint);
        if (r.uv) copy.uv = std::make_unique<UvUndo>(*r.uv);
        if (r.topology) copy.topology = std::make_unique<TopologyUndo>(*r.topology);
        if (r.subdiv) {
            copy.subdiv = std::make_unique<SubdivUndo>(*r.subdiv);
            copy.subdiv->stack = levels(r.subdiv->stack);
        }
        return copy;
    }

    static bool hashMesh(Hasher& h, const CheckpointMesh& view) {
        const bool derived = std::any_of(view.changes.begin(), view.changes.end(), [&](const UndoRecord* r) {
            return r->subdiv || r->topology || r->paint || (!r->positions.empty() && !view.mesh.layers.empty()) ||
                   (!r->colors.empty() && !view.mesh.paintLayers.empty());
        });
        if (!derived) return view.hash(h);
        Mesh working = view.mesh;
        working.multires = levels(working.multires);
        BVH bvh;
        bvh.build(working);
        for (const UndoRecord* r : view.changes) {
            UndoRecord step = geometryStep(*r);
            UndoStack::apply(step, working, bvh);
        }
        return CheckpointMesh{working, {}}.hash(h);
    }

    std::string problem() {
        Document preview;
        preview.parts.clear();
        for (const Part& p : source.parts)
            preview.parts.push_back(part(p));
        if (lost) return "the checkpoint is outside the retained undo or redo history";
        preview.activePart = source.activePart;
        PartData& active = *preview.activePartInfo().data;
        std::swap(preview.undo, active.undo);
        const HistoryWalk w = walkToMark(preview, mark);
        if (!w.exact) return w.problem;
        if (independentSteps + w.undone + w.redone > kMaxWalkSteps)
            return "the checkpoint exceeds the history walk limit";
        Hasher h;
        hashLayout(h, source, preview.partCount());
        for (int i = 0; i < preview.partCount(); ++i) {
            hashPart(h, preview.parts[size_t(i)]);
            const uint32_t key = preview.meshKey(i);
            const CheckpointMesh& view = projected.at(key);
            hashGuides(h, view.guides);
            const auto saved = mark.selectionSets.find(key);
            if (saved == mark.selectionSets.end() ? bool(view.sets) : !view.sets || saved->second.lock() != view.sets)
                return "the named selection sets differ from the checkpoint";
            if (!hashMesh(h, view)) return "the mesh differs from the checkpoint";
        }
        h.word(source.splats.size());
        if (h.h != mark.hash)
            return "the scene differs from the checkpoint (something in it was changed outside the undo history)";
        return {};
    }
};

HistoryWalk restoreCheckpointMark(Document& doc, const HistoryMark& mark) {
    HistoryWalk result;
    if (mark.epoch != undoClearEpoch()) {
        result.problem = "the history was cleared since (a new scene, an open or a step that can't be undone)";
        return result;
    }
    if (doc.splatsVersion != mark.splatsVersion && splatHash(doc.splats) != mark.splatsHash) {
        result.problem = "the splat cloud differs from the checkpoint";
        return result;
    }
    try {
        result.problem = CheckpointPreview{doc, mark, {}, {}}.problem();
    } catch (const std::bad_alloc&) {
        result.problem = "not enough memory to verify the checkpoint";
    }
    if (!result.problem.empty()) return result;
    return walkToMark(doc, mark, nextUndoSeq());
}

HistoryDistance distanceToMark(const Document& doc, const HistoryMark& mark) {
    HistoryDistance d;
    if (mark.epoch != undoClearEpoch()) return {-1, -1, false};
    for (int i : historyParts(doc)) {
        const UndoStack& s = doc.partUndo(i);
        const uint64_t target = targetOf(mark, doc.meshKey(i));
        const StackRange range = rangeTo(s, target);
        if (mark.tops.contains(doc.meshKey(i)) && range.destination != target) return {-1, -1, false};
        d.back += int(range.back);
        d.ahead += int(range.ahead);
    }
    return d;
}

} // namespace sf
