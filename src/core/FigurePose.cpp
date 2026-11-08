// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Figure Pose (core/FigurePose.h, docs/design/pose-all.md).
#include "core/FigurePose.h"

#include "core/Commands.h"
#include "core/Decimate.h"
#include "core/Document.h"
#include "core/MeshTransform.h"
#include "core/Multires.h"
#include "core/SurfaceQuery.h"
#include "core/ThreadPool.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <limits>

namespace sf {

namespace {

using Clock = std::chrono::steady_clock;

double since(Clock::time_point t) { return std::chrono::duration<double>(Clock::now() - t).count(); }

FigurePoseState* state(const Document& doc) {
    FigurePoseState* s = doc.figurePose.get();
    return s && doc.partIndex(s->poseMesh) >= 0 ? s : nullptr;
}

UndoStack& undoOf(Document& doc, int i) { return doc.partUndo(i); }

// A linked copy (its mesh shared with another Part, #94) can't take a pose of its own: posing the mesh would pose
// every copy. Figure Pose leaves those out (Make Unique first).
bool sharesMesh(const Document& doc, int i) {
    for (int j = 0; j < doc.partCount(); ++j)
        if (j != i && doc.parts[size_t(j)].data == doc.parts[size_t(i)].data) return true;
    return false;
}

// Shows the Parts as they were, makes the Part that was active before Begin active again, and removes the Pose Mesh.
void endPose(Document& doc) {
    const std::shared_ptr<FigurePoseState> s = std::move(doc.figurePose);
    if (!s) return;
    for (const auto& [id, visible] : s->visibility)
        if (const int i = doc.partIndex(id); i >= 0) doc.parts[size_t(i)].visible = visible;
    if (const int r = doc.partIndex(s->returnTo); r >= 0) doc.selectPart(r);
    if (const int p = doc.partIndex(s->poseMesh); p >= 0) {
        if (p == doc.activePart) // the Part to return to is gone: any other one
            doc.selectPart(p == 0 ? 1 : 0);
        doc.removePart(p);
    }
}

// Old values of the vertices any of `records` moved, each from the first record that has it (the value before
// the whole sequence), as one record. Every record is a base-mesh transform step on the same mesh.
UndoRecord mergeSteps(std::vector<UndoRecord>& records, size_t vertexCount, std::string label) {
    UndoRecord out;
    out.label = std::move(label);
    if (records.empty()) return out;
    const size_t layers = records.front().layerOffsets.size();
    std::vector<uint8_t> seen(vertexCount, 0);
    std::vector<glm::vec3> pos(vertexCount);
    std::vector<std::vector<glm::vec3>> offs(layers, std::vector<glm::vec3>(vertexCount));
    for (UndoRecord& r : records) {
        const auto& idx = r.positions.indices;
        for (size_t k = 0; k < idx.size(); ++k) {
            const uint32_t v = idx[k];
            if (seen[v]) continue;
            seen[v] = 1;
            pos[v] = r.positions.values[k];
            for (size_t l = 0; l < layers && l < r.layerOffsets.size(); ++l) offs[l][v] = r.layerOffsets[l].values[k];
        }
    }
    out.layerOffsets.resize(layers);
    for (uint32_t v = 0; v < vertexCount; ++v) {
        if (!seen[v]) continue;
        out.positions.add(v, pos[v]);
        for (size_t l = 0; l < layers; ++l) out.layerOffsets[l].add(v, offs[l][v]);
    }
    return out;
}

std::string seconds(double s) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f s", s);
    return buf;
}

} // namespace

bool figurePoseActive(const Document& doc) { return state(doc) != nullptr; }

bool figurePoseEditing(const Document& doc) {
    const FigurePoseState* s = state(doc);
    return s && doc.activePartInfo().id == s->poseMesh;
}

FigurePoseResult figurePoseBegin(Document& doc, size_t triangles) {
    FigurePoseResult res;
    if (figurePoseActive(doc)) {
        res.message = "A Figure Pose is already in progress: Apply or Cancel it first";
        return res;
    }
    doc.figurePose.reset();
    const auto t0 = Clock::now();
    doc.dropExtractPreview();
    triangles = std::clamp(triangles, kFigurePoseMinTriangles, kFigurePoseMaxTriangles);

    std::vector<int> src;
    std::vector<std::string> linked;
    size_t total = 0;
    for (int i = 0; i < doc.partCount(); ++i) {
        const Part& p = doc.parts[size_t(i)];
        if (!p.visible || doc.partMesh(i).triangleCount() == 0) continue;
        if (sharesMesh(doc, i)) {
            linked.push_back(p.name);
            continue;
        }
        src.push_back(i);
        total += doc.partMesh(i).triangleCount();
    }
    if (src.empty()) {
        res.message = linked.empty() ? "Nothing to pose: no visible Part has triangles"
                                     : "Nothing to pose: linked copies can't be posed on their own (Make Unique first)";
        return res;
    }

    // Each Part to its share of the budget: a Part within its share is copied as it is. A Part with subdivision
    // levels starts from its lowest level that has the share (levels grow 4x, so 1-4 shares), which is the light
    // shape posing wants and skips decimating the full resolution; only a level past twice the share is decimated.
    std::vector<Mesh> pieces(src.size());
    poolFor(src.size(), [&](size_t b, size_t e) {
        for (size_t k = b; k < e; ++k) {
            const Mesh& m = doc.partMesh(src[k]);
            const size_t share =
                std::max(kFigurePosePartFloor, size_t(double(triangles) * double(m.triangleCount()) / double(total)));
            Mesh level;
            const Mesh* from = &m;
            if (m.triangleCount() > share && subdivLevelCount(m) > 1)
                for (int l = 0; l < subdivCurrentLevel(m); ++l)
                    if (subdivLevelTriangles(m, l) >= share) {
                        level = subdivLevelMesh(m, l);
                        from = &level;
                        break;
                    }
            Mesh piece;
            if (from->triangleCount() <= (from == &m ? share : 2 * share)) {
                piece.positions = from->positions;
                piece.triangles = from->triangles;
            } else {
                Mesh d = decimate(*from, share);
                piece.positions = std::move(d.positions);
                piece.triangles = std::move(d.triangles);
            }
            if (const Part& p = doc.parts[size_t(src[k])]; p.moved()) // the Pose Mesh is in scene space
                for (glm::vec3& q : piece.positions) q = glm::vec3(p.transform * glm::vec4(q, 1.0f));
            pieces[k] = std::move(piece);
        }
    }, 1, 2);

    auto st = std::make_shared<FigurePoseState>();
    Mesh pose;
    for (size_t k = 0; k < src.size(); ++k) {
        const Mesh& piece = pieces[k];
        FigurePoseSource s;
        s.part = doc.parts[size_t(src[k])].id;
        s.firstVertex = uint32_t(pose.positions.size());
        s.vertexCount = uint32_t(piece.positions.size());
        s.firstTriangle = uint32_t(pose.triangles.size());
        s.triangleCount = uint32_t(piece.triangles.size());
        pose.positions.insert(pose.positions.end(), piece.positions.begin(), piece.positions.end());
        for (const glm::uvec3& t : piece.triangles) pose.triangles.push_back(t + glm::uvec3(s.firstVertex));
        pose.facegroups.resize(pose.triangles.size(), uint32_t(k + 1)); // one face group per source Part
        st->sources.push_back(s);
    }
    pieces.clear();
    pose.rebuildTopology();
    pose.recomputeAllNormals();
    st->rest = pose.positions;
    for (const Part& p : doc.parts) st->visibility.emplace_back(p.id, p.visible);
    st->returnTo = doc.activePartInfo().id;

    for (const int i : src) doc.parts[size_t(i)].visible = false; // linked copies stay shown, as they are
    const int at = doc.addPart(std::move(pose), doc.uniquePartName("Pose Mesh"), true);
    st->poseMesh = doc.parts[size_t(at)].id;
    doc.undo.byteBudget = std::numeric_limits<size_t>::max(); // the pose is this history: never drop a step
    for (FigurePoseSource& s : st->sources)
        if (const int i = doc.partIndex(s.part); i >= 0) s.revision = undoOf(doc, i).revision();
    st->beginSeconds = since(t0);
    const size_t poseTris = doc.mesh.triangleCount();
    doc.figurePose = st;

    res.ok = true;
    res.parts = int(st->sources.size());
    res.message = "Figure Pose: " + std::to_string(res.parts) + (res.parts == 1 ? " Part, " : " Parts, ") +
                  std::to_string(poseTris) + " triangles in the Pose Mesh (" + seconds(st->beginSeconds) +
                  "). Pose it with the Gizmo and masks, then Apply Pose";
    if (!linked.empty()) {
        res.message += ". Linked copies left out (Make Unique to pose them):";
        for (const std::string& n : linked) res.message += " " + n;
    }
    return res;
}

FigurePoseResult figurePoseApply(Document& doc) {
    FigurePoseResult res;
    FigurePoseState* s = state(doc);
    if (!s) {
        doc.figurePose.reset();
        res.message = "No Figure Pose in progress";
        return res;
    }
    const int pi = doc.partIndex(s->poseMesh);
    const Mesh& poseMesh = doc.partMesh(pi);
    if (poseMesh.vertexCount() != s->rest.size()) {
        res.message = "The Pose Mesh's topology changed: undo back to the pose, or Cancel";
        return res;
    }

    // The pose: the Pose Mesh's transform steps, oldest first.
    std::vector<std::shared_ptr<const TransformOp>> ops;
    const UndoStack& history = undoOf(doc, pi);
    for (size_t back = history.undoCount(); back-- > 0;) {
        const UndoRecord* r = history.undoStep(back);
        if (r->transformOp) {
            ops.push_back(r->transformOp);
        } else if (!r->positions.empty() || r->mesh || r->topology || r->subdiv || r->micro) {
            res.message = "\"" + r->label +
                          "\" moved the Pose Mesh in a way Apply can't carry over yet (only Gizmo moves can): undo back "
                          "past it, or Cancel";
            return res;
        }
    }
    if (ops.empty()) {
        figurePoseCancel(doc);
        res.ok = true;
        res.message = "Nothing posed: the Parts are as they were";
        return res;
    }

    const std::shared_ptr<FigurePoseState> keep = doc.figurePose; // `s` outlives endPose
    const Mesh poseCopy = poseMesh;                                 // its triangles, for the binding
    std::vector<std::vector<float>> poseWeights;
    for (const auto& op : ops) poseWeights.push_back(op->weights(s->rest.size()));
    endPose(doc);

    static std::atomic<uint64_t> nextGroup{1};
    const uint64_t group = nextGroup++;
    std::vector<std::string> edited;
    for (const FigurePoseSource& src : keep->sources) {
        const int i = doc.partIndex(src.part);
        if (i < 0) continue;
        Part& info = doc.parts[size_t(i)];
        UndoStack& undo = undoOf(doc, i);
        if (undo.revision() != src.revision) {
            edited.push_back(info.name);
            continue;
        }
        if (sharesMesh(doc, i)) { // linked after Begin (a Linked Duplicate during the pose)
            edited.push_back(info.name);
            continue;
        }
        Mesh& mesh = doc.partMesh(i);
        BVH& bvh = doc.partBvh(i);
        // The pose is in scene space; a moved Part replays it in its own (the bind looks up scene positions).
        const glm::mat4 place = info.transform;
        const bool moved = info.moved();

        // Binding: the closest point on this Part's own rest-pose triangles.
        auto tb = Clock::now();
        std::vector<glm::vec3> restPos(keep->rest.begin() + src.firstVertex,
                                       keep->rest.begin() + src.firstVertex + src.vertexCount);
        std::vector<glm::uvec3> restTris(poseCopy.triangles.begin() + src.firstTriangle,
                                         poseCopy.triangles.begin() + src.firstTriangle + src.triangleCount);
        for (glm::uvec3& t : restTris) t -= glm::uvec3(src.firstVertex);
        TriangleBVH nearest;
        nearest.build(std::move(restPos), restTris);
        const size_t nv = mesh.vertexCount();
        std::vector<glm::uvec3> corner(nv);
        std::vector<glm::vec3> bary(nv);
        poolFor(nv, [&](size_t b, size_t e) {
            for (size_t v = b; v < e; ++v) {
                SurfaceHit hit;
                const glm::vec3 q = moved ? glm::vec3(place * glm::vec4(mesh.positions[v], 1.0f)) : mesh.positions[v];
                if (nearest.closestPoint(q, std::numeric_limits<float>::max(), hit)) {
                    corner[v] = nearest.triangles()[hit.prim] + glm::uvec3(src.firstVertex);
                    bary[v] = hit.bary;
                } else {
                    corner[v] = glm::uvec3(src.firstVertex);
                    bary[v] = glm::vec3(0.0f);
                }
                if (mesh.vertexHidden(uint32_t(v))) bary[v] = glm::vec3(0.0f); // hidden vertices never move
            }
        }, 1 << 13);
        res.bindSeconds += since(tb);

        // Replay every step at full resolution with the interpolated weights.
        auto tr = Clock::now();
        std::vector<UndoRecord> steps;
        std::vector<float> w(nv);
        for (size_t k = 0; k < ops.size(); ++k) {
            const std::vector<float>& pw = poseWeights[k];
            poolFor(nv, [&](size_t b, size_t e) {
                for (size_t v = b; v < e; ++v) {
                    const glm::uvec3 c = corner[v];
                    w[v] = bary[v].x * pw[c.x] + bary[v].y * pw[c.y] + bary[v].z * pw[c.z];
                }
            }, 1 << 14);
            const MeshTransform t = moved ? ops[k]->transform.inSpaceOf(place) : ops[k]->transform;
            TransformSession session;
            if (!session.begin(mesh, w, ops[k]->mirror, t.pivot, -1)) continue;
            session.apply(mesh, t);
            UndoRecord r = session.finish(mesh, bvh, "Figure Pose");
            if (!r.empty()) steps.push_back(std::move(r));
        }
        if (steps.empty()) {
            res.replaySeconds += since(tr);
            continue;
        }
        UndoRecord merged = mergeSteps(steps, nv, "Figure Pose");
        steps.clear();
        merged.group = group;
        res.vertices += merged.positions.indices.size();
        undo.push(std::move(merged));
        res.replaySeconds += since(tr);
        ++res.parts;
    }
    doc.modified = true;
    res.ok = true;
    res.steps = int(ops.size());
    res.message = "Posed " + std::to_string(res.parts) + (res.parts == 1 ? " Part" : " Parts") + " (" +
                  std::to_string(res.vertices) + " vertices, " + std::to_string(res.steps) +
                  (res.steps == 1 ? " move" : " moves") + "; bind " + seconds(res.bindSeconds) + ", apply " +
                  seconds(res.replaySeconds) + "). One undo step";
    if (!edited.empty()) {
        res.message += ". Left as they were (edited during the pose):";
        for (const std::string& n : edited) res.message += " " + n;
    }
    return res;
}

void figurePoseCancel(Document& doc) {
    if (!doc.figurePose) return;
    endPose(doc);
}

int undoGroupOnOtherParts(Document& doc, uint64_t group, bool isUndo) {
    if (!group) return 0;
    int n = 0;
    std::vector<const PartData*> done; // a mesh linked Parts share has one history
    for (int i = 0; i < doc.partCount(); ++i) {
        const Part& p = doc.parts[size_t(i)];
        if (doc.checkedOut(i) || std::find(done.begin(), done.end(), p.data.get()) != done.end()) continue;
        done.push_back(p.data.get());
        PartData& d = *p.data;
        UndoRecord* r = isUndo ? d.undo.lastUndo() : d.undo.lastRedo();
        if (!r || r->group != group) continue;
        BVH& bvh = doc.partBvh(i);
        if (isUndo ? d.undo.undo(d.mesh, bvh) : d.undo.redo(d.mesh, bvh)) ++n;
    }
    return n;
}

void registerFigurePoseCommands(CommandRegistry& reg) {
    using T = ParamSpec::Type;
    reg.add({"figure_pose_begin",
             "Figure Pose: merge every visible Part into one light Pose Mesh (each Part decimated to its share of the "
             "budget, one face group per Part) and hide the Parts. Pose it with the Gizmo and masks (or `transform`), "
             "then figure_pose_apply.",
             {{"triangles", T::Number,
               "Triangle budget of the Pose Mesh (50000-2000000, default 300000), shared by the Parts' sizes.", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 const FigurePoseResult r =
                     figurePoseBegin(doc, size_t(std::max(0.0, argNumber(a, "triangles", double(kFigurePoseDefaultTriangles)))));
                 return {r.ok, r.message};
             }});
    reg.add({"figure_pose_apply",
             "Figure Pose: apply the Pose Mesh's moves to every source Part at full resolution (sculpt layers, "
             "subdivision and micro levels follow) as one undo step, then remove the Pose Mesh.",
             {},
             [](Document& doc, const Args&) -> CommandResult {
                 const FigurePoseResult r = figurePoseApply(doc);
                 return {r.ok, r.message};
             }});
    reg.add({"figure_pose_cancel", "Figure Pose: remove the Pose Mesh and show the Parts again; nothing changes.", {},
             [](Document& doc, const Args&) -> CommandResult {
                 if (!doc.figurePose) return {false, "No Figure Pose in progress"};
                 figurePoseCancel(doc);
                 return {true, "Figure Pose cancelled"};
             }});
    reg.add({"figure_pose_info", "Figure Pose: whether a pose is in progress, its Parts, size and moves so far.", {},
             [](Document& doc, const Args&) -> CommandResult {
                 const FigurePoseState* s = state(doc);
                 if (!s) return {true, "No Figure Pose in progress"};
                 const int pi = doc.partIndex(s->poseMesh);
                 const UndoStack& h = undoOf(doc, pi);
                 int moves = 0;
                 for (size_t b = 0; b < h.undoCount(); ++b) moves += h.undoStep(b)->transformOp ? 1 : 0;
                 return {true, "Figure Pose: " + std::to_string(s->sources.size()) + " Parts, " +
                                   std::to_string(doc.partMesh(pi).triangleCount()) + " triangles, " +
                                   std::to_string(moves) + " moves; built in " + seconds(s->beginSeconds)};
             }});
}

} // namespace sf
