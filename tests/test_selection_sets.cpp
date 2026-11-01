// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Selection Sets (core/SelectionSets.h): store / restore / rename / delete with undo, compact storage and the
// binary form, combining, the quick set from visible faces, carrying sets across subdivision levels, the Morph
// brush and Morph to Set, projects (round trip, older projects, damaged sets), .sfsets files and USD.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Json.h"
#include "core/Multires.h"
#include "core/Primitives.h"
#include "core/Project.h"
#include "core/SelectionSets.h"
#include "io/UsdIO.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

using namespace sf;

namespace {

const CommandRegistry& registry() {
    static CommandRegistry r;
    return r;
}

CommandResult run(Document& d, const std::string& name, const Args& a = {}) {
    CommandResult r = registry().execute(d, name, a);
    if (!r.ok) std::printf("  (%s: %s)\n", name.c_str(), r.message.c_str());
    return r;
}

size_t setCount(const Mesh& m) { return m.selectionSets ? m.selectionSets->sets.size() : 0; }
const SelectionSet* setNamed(const Mesh& m, const std::string& name) {
    const int i = m.selectionSets ? m.selectionSets->find(name) : -1;
    return i < 0 ? nullptr : m.selectionSets->sets[size_t(i)].get();
}

// Mask the vertices above y = cut (soft values when `soft`), hide the triangles left of x = 0, groups by z.
void paintState(Mesh& m, float cut = 0.0f, bool soft = false) {
    m.ensureMask();
    for (size_t v = 0; v < m.vertexCount(); ++v) m.mask[v] = m.positions[v].y > cut ? (soft ? 0.5f : 1.0f) : 0.0f;
    m.ensureHidden();
    m.ensurePolygroups();
    for (size_t t = 0; t < m.triangleCount(); ++t) {
        const glm::vec3 c = (m.positions[m.triangles[t].x] + m.positions[m.triangles[t].y] + m.positions[m.triangles[t].z]) / 3.0f;
        m.hidden[t] = c.x < 0.0f;
        m.polygroups[t] = c.z > 0.0f ? 7u : 3u;
    }
    m.ensureColors();
    for (size_t v = 0; v < m.vertexCount(); ++v)
        if (m.positions[v].z > 0.5f) m.colors[v] = glm::vec3(0.8f, 0.1f, 0.2f);
}

size_t countHidden(const Mesh& m) {
    size_t n = 0;
    for (size_t t = 0; t < m.triangleCount(); ++t) n += m.triangleHidden(uint32_t(t));
    return n;
}

float maxDistance(const std::vector<glm::vec3>& a, const std::vector<glm::vec3>& b) {
    if (a.size() != b.size()) return 1e30f;
    float d = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) d = std::max(d, glm::length(a[i] - b[i]));
    return d;
}

} // namespace

TEST(selection_sets_store_restore_undo) {
    Document d = makeDoc(makeIcosphere(4));
    paintState(d.mesh);
    const Mesh before = d.mesh;
    CHECK(run(d, "selection_set_store", {{"name", "Head"}, {"channels", "mask,polygroups,paint,visibility"}}).ok);
    CHECK(setCount(d.mesh) == 1);
    const SelectionSet* s = setNamed(d.mesh, "Head");
    CHECK(s && s->has(kSetMask) && s->has(kSetPaint) && s->has(kSetVisibility) && s->has(kSetPolygroups) && !s->has(kSetMorph));
    // Compact: a binary mask on half a sphere is a few runs and no values; groups are run-length coded.
    CHECK(s && s->maskValues.empty() && s->maskRuns.size() < s->vertexCount / 4);
    CHECK(s && s->groupRuns.size() < s->triangleCount / 2);
    CHECK(s && s->bytes() * 2 < d.mesh.vertexCount() * 16 + d.mesh.triangleCount() * 5); // under half the plain arrays

    // Change everything, then restore: back exactly; one undo step brings the changes back.
    d.mesh.clearMask();
    d.mesh.clearHidden();
    d.mesh.clearPolygroups();
    d.mesh.clearColors();
    CHECK(run(d, "selection_set_restore", {{"name", "Head"}}).ok);
    CHECK(d.mesh.mask == before.mask);
    CHECK(d.mesh.hidden == before.hidden);
    CHECK(d.mesh.polygroups == before.polygroups);
    CHECK(d.mesh.colors == before.colors);
    CHECK(run(d, "undo").ok);
    CHECK(!d.mesh.hasMask() || std::all_of(d.mesh.mask.begin(), d.mesh.mask.end(), [](float v) { return v == 0.0f; }));
    CHECK(countHidden(d.mesh) == 0);
    CHECK(run(d, "redo").ok);
    CHECK(d.mesh.mask == before.mask);

    // Only some channels.
    d.mesh.clearHidden();
    d.mesh.clearMask();
    CHECK(run(d, "selection_set_restore", {{"name", "Head"}, {"channels", "visibility"}}).ok);
    CHECK(d.mesh.hidden == before.hidden);
    CHECK(!d.mesh.hasMask() || std::all_of(d.mesh.mask.begin(), d.mesh.mask.end(), [](float v) { return v == 0.0f; }));

    // Storing is undoable too, and so are rename and delete.
    CHECK(run(d, "undo").ok); // the restore
    CHECK(run(d, "undo").ok); // ... and the earlier restore
    CHECK(run(d, "undo").ok); // the store
    CHECK(setCount(d.mesh) == 0);
    CHECK(run(d, "redo").ok);
    CHECK(setCount(d.mesh) == 1);
    CHECK(run(d, "selection_set_rename", {{"name", "Head"}, {"to", "Face"}}).ok);
    CHECK(setNamed(d.mesh, "Face") && !setNamed(d.mesh, "Head"));
    CHECK(run(d, "undo").ok);
    CHECK(setNamed(d.mesh, "Head"));
    CHECK(run(d, "selection_set_delete", {{"name", "Head"}}).ok);
    CHECK(setCount(d.mesh) == 0);
    CHECK(run(d, "undo").ok);
    CHECK(setCount(d.mesh) == 1);
    CHECK(!registry().execute(d, "selection_set_restore", {{"name", "Nope"}}).ok);
    CHECK(!registry().execute(d, "selection_set_store", {{"name", ""}}).ok);
    CHECK(!registry().execute(d, "selection_set_store", {{"name", "X"}, {"channels", "nonsense"}}).ok);
}

TEST(selection_sets_unlimited_and_list) {
    Document d = makeDoc(makeIcosphere(2));
    for (int i = 0; i < 300; ++i) {
        d.mesh.ensureMask();
        std::fill(d.mesh.mask.begin(), d.mesh.mask.end(), 0.0f);
        d.mesh.mask[size_t(i) % d.mesh.vertexCount()] = 1.0f;
        CHECK(run(d, "selection_set_store", {{"name", "S" + std::to_string(i)}, {"channels", "mask"}}).ok);
    }
    CHECK(setCount(d.mesh) == 300);
    CHECK(d.mesh.selectionSets->bytes() < 300 * 1024); // a one-vertex mask is a single run
    // Same name replaces.
    CHECK(run(d, "selection_set_store", {{"name", "S0"}, {"channels", "visibility"}}).ok);
    CHECK(setCount(d.mesh) == 300 && setNamed(d.mesh, "S0")->channels == kSetVisibility);
    const CommandResult list = run(d, "selection_set_list", {{"json", true}});
    Json j;
    std::string err;
    CHECK(list.ok && Json::parse(list.message, j, err) && j["sets"].size() == 300);
    CHECK(j["sets"][0]["name"].asString() == "S0" && j["sets"][0]["fits"].asBool());
}

TEST(selection_sets_pack_roundtrip_and_damage) {
    Mesh m = makePlane(12);
    paintState(m, -0.5f, true);
    const SelectionSet s = captureSelectionSet(m, "All", kSetAllChannels);
    CHECK(s.has(kSetUvs) == m.hasUvs());
    CHECK(!s.maskValues.empty()); // soft values are kept
    const std::vector<uint8_t> packed = packSelectionSet(s);
    SelectionSet back;
    std::string err;
    CHECK(unpackSelectionSet(packed.data(), packed.size(), back, err));
    SelectionSetData a, b;
    decodeSelectionSet(s, kSetAllChannels, a);
    decodeSelectionSet(back, kSetAllChannels, b);
    CHECK(back.name == "All" && back.channels == s.channels);
    CHECK(a.mask == b.mask && a.polygroups == b.polygroups && a.colors == b.colors && a.hidden == b.hidden);
    CHECK(a.uvs == b.uvs && a.positions == b.positions);
    CHECK(a.mask == m.mask && a.hidden == m.hidden && a.positions == m.positions);
    // Every truncation is refused, never read past the end.
    for (size_t n = 0; n + 1 < packed.size(); n += std::max<size_t>(1, packed.size() / 97)) {
        SelectionSet t;
        CHECK(!unpackSelectionSet(packed.data(), n, t, err));
    }
    // A run past the end is refused; a newer set format names itself.
    std::vector<uint8_t> bad = packed;
    bad[4] = 9; // version 9
    CHECK(!unpackSelectionSet(bad.data(), bad.size(), back, err) && err.find("newer") != std::string::npos);
    SelectionSet huge = s;
    huge.hiddenRuns.push_back({uint32_t(m.triangleCount()), 5});
    const std::vector<uint8_t> hp = packSelectionSet(huge);
    CHECK(!unpackSelectionSet(hp.data(), hp.size(), back, err));
}

TEST(selection_sets_combine_and_visible) {
    Document d = makeDoc(makeIcosphere(3));
    Mesh& m = d.mesh;
    auto maskWhere = [&](auto pred) {
        m.ensureMask();
        for (size_t v = 0; v < m.vertexCount(); ++v) m.mask[v] = pred(m.positions[v]) ? 1.0f : 0.0f;
    };
    maskWhere([](const glm::vec3& p) { return p.x > 0.0f; });
    CHECK(run(d, "selection_set_store", {{"name", "Right"}, {"channels", "mask"}}).ok);
    maskWhere([](const glm::vec3& p) { return p.y > 0.0f; });
    CHECK(run(d, "selection_set_store", {{"name", "Top"}, {"channels", "mask"}}).ok);
    auto count = [&](auto pred) {
        size_t n = 0;
        for (size_t v = 0; v < m.vertexCount(); ++v) n += pred(m.positions[v]);
        return n;
    };
    auto masked = [&] {
        size_t n = 0;
        for (float v : m.mask) n += v > 0.5f;
        return n;
    };
    CHECK(run(d, "selection_set_combine", {{"a", "Right"}, {"b", "Top"}, {"op", "intersect"}}).ok);
    CHECK(setNamed(d.mesh, "Right & Top"));
    CHECK(run(d, "selection_set_restore", {{"name", "Right & Top"}}).ok);
    CHECK(masked() == count([](const glm::vec3& p) { return p.x > 0.0f && p.y > 0.0f; }));
    CHECK(run(d, "selection_set_combine", {{"a", "Right"}, {"b", "Top"}, {"op", "subtract"}, {"into", "RightLow"}}).ok);
    CHECK(run(d, "selection_set_restore", {{"name", "RightLow"}}).ok);
    CHECK(masked() == count([](const glm::vec3& p) { return p.x > 0.0f && !(p.y > 0.0f); }));
    // Restore modes combine with the current mask.
    CHECK(run(d, "selection_set_restore", {{"name", "Top"}, {"mode", "union"}}).ok);
    CHECK(masked() == count([](const glm::vec3& p) { return p.x > 0.0f || p.y > 0.0f; }));
    CHECK(!registry().execute(d, "selection_set_combine", {{"a", "Right"}, {"b", "Top"}, {"op", "replace"}}).ok);

    // Quick set from the visible faces; visibility unions and subtracts by the shown faces.
    m.ensureHidden();
    for (size_t t = 0; t < m.triangleCount(); ++t) m.hidden[t] = m.positions[m.triangles[t].x].z < 0.0f;
    const std::vector<uint8_t> front = m.hidden;
    CHECK(run(d, "selection_set_from_visible").ok);
    CHECK(setNamed(d.mesh, "Visible") && setNamed(d.mesh, "Visible")->channels == kSetVisibility);
    CHECK(run(d, "show_all").ok);
    CHECK(run(d, "selection_set_restore", {{"name", "Visible"}}).ok);
    CHECK(m.hidden == front);
    CHECK(run(d, "selection_set_restore", {{"name", "Visible"}, {"mode", "subtract"}}).ok); // shown and not shown
    CHECK(countHidden(m) == m.triangleCount());
    CHECK(run(d, "selection_set_from_visible").ok);
    CHECK(setNamed(d.mesh, "Visible 2"));
}

TEST(selection_sets_across_levels) {
    Document d = makeDoc(makeCube(3));
    CHECK(run(d, "subdiv_divide").ok);
    CHECK(run(d, "subdiv_divide").ok); // levels 1..3, at the top
    Mesh& m = d.mesh;
    // A mask stored at the top restores at the base as the stack itself carries it down.
    m.ensureMask();
    for (size_t v = 0; v < m.vertexCount(); ++v) m.mask[v] = m.positions[v].y > 0.2f ? 1.0f : 0.0f;
    m.markAllDirty();
    CHECK(run(d, "selection_set_store", {{"name", "Top mask"}, {"channels", "mask,polygroups,visibility"}}).ok);
    CHECK(run(d, "subdiv_level", {{"level", 1.0}}).ok);
    const std::vector<float> reference = d.mesh.mask; // what the levels made of it
    CHECK(!reference.empty());
    d.mesh.clearMask();
    CHECK(run(d, "selection_set_restore", {{"name", "Top mask"}}).ok);
    CHECK(d.mesh.mask == reference);
    const CommandResult msg = run(d, "selection_set_restore", {{"name", "Top mask"}, {"channels", "mask"}});
    CHECK(msg.ok && msg.message.find("already matches") != std::string::npos);

    // A mask stored at the base restores at the top as the stack spreads it up.
    std::fill(d.mesh.mask.begin(), d.mesh.mask.end(), 0.0f);
    d.mesh.mask[0] = 1.0f;
    d.mesh.mask[1] = 0.5f;
    d.mesh.markAllDirty();
    CHECK(run(d, "selection_set_store", {{"name", "Base mask"}, {"channels", "mask"}}).ok);
    CHECK(run(d, "subdiv_level", {{"level", 3.0}}).ok);
    const std::vector<float> up = d.mesh.mask;
    d.mesh.clearMask();
    CHECK(run(d, "selection_set_restore", {{"name", "Base mask"}}).ok);
    CHECK(d.mesh.mask.size() == up.size());
    float diff = 0.0f;
    for (size_t v = 0; v < up.size() && v < d.mesh.mask.size(); ++v) diff = std::max(diff, std::abs(up[v] - d.mesh.mask[v]));
    CHECK(diff < 1e-5f);

    // The middle level, both ways; sets also survive stepping (they belong to the Part).
    CHECK(run(d, "subdiv_level", {{"level", 2.0}}).ok);
    CHECK(setCount(d.mesh) == 2);
    CHECK(run(d, "selection_set_restore", {{"name", "Top mask"}}).ok);
    CHECK(run(d, "selection_set_restore", {{"name", "Base mask"}}).ok);
    CHECK(d.mesh.mask.size() == d.mesh.vertexCount());
    // Undo across a level step brings the list along unchanged.
    CHECK(run(d, "undo").ok);
    CHECK(setCount(d.mesh) == 2);

    // A set that fits no level is refused with a message.
    Document other = makeDoc(makeIcosphere(2));
    CHECK(run(other, "selection_set_store", {{"name", "Other"}}).ok);
    SelectionSetData data;
    std::string err;
    CHECK(!selectionSetAt(d.mesh, *setNamed(other.mesh, "Other"), kSetMask, data, err) && !err.empty());
}

TEST(selection_sets_morph_brush_and_morph_to_set) {
    Document d = makeDoc(makeIcosphere(4));
    const std::vector<glm::vec3> original = d.mesh.positions;
    CHECK(!registry().execute(d, "selection_set_morph_target", {{"name", "Rest"}}).ok);
    CHECK(run(d, "selection_set_store", {{"name", "Rest"}, {"channels", "morph"}}).ok);
    // Sculpt: a bump.
    for (int i = 0; i < 3; ++i)
        CHECK(run(d, "stroke", {{"points", "-0.3,0,2 0.3,0,2"}, {"brush", "Standard"}, {"radius", 0.5}, {"strength", 1.0}}).ok);
    const float sculpted = maxDistance(d.mesh.positions, original);
    CHECK(sculpted > 0.01f);
    // Without a target the Morph brush leaves the surface alone.
    const std::vector<glm::vec3> bumped = d.mesh.positions;
    run(d, "stroke", {{"points", "-0.3,0,2 0.3,0,2"}, {"brush", "Morph"}, {"radius", 0.6}, {"strength", 1.0}});
    CHECK(d.mesh.positions == bumped);
    // With one it pulls the bump back.
    CHECK(run(d, "selection_set_morph_target", {{"name", "Rest"}}).ok);
    for (int i = 0; i < 4; ++i)
        CHECK(run(d, "stroke", {{"points", "-0.3,0,2 0.3,0,2"}, {"brush", "Morph"}, {"radius", 0.6}, {"strength", 1.0}}).ok);
    CHECK(maxDistance(d.mesh.positions, original) < sculpted * 0.5f);
    // Morph to Set: halfway, then all of the way, within the mask; undo puts it back.
    CHECK(run(d, "undo").ok);
    const std::vector<glm::vec3> start = d.mesh.positions;
    d.mesh.ensureMask();
    for (size_t v = 0; v < d.mesh.vertexCount(); ++v) d.mesh.mask[v] = d.mesh.positions[v].x > 0.1f ? 1.0f : 0.0f;
    CHECK(run(d, "selection_set_morph", {{"name", "Rest"}, {"amount", 1.0}}).ok);
    for (size_t v = 0; v < d.mesh.vertexCount(); ++v) {
        if (d.mesh.mask[v] == 1.0f) CHECK(d.mesh.positions[v] == start[v]);
        else CHECK(glm::length(d.mesh.positions[v] - original[v]) < 1e-5f);
    }
    CHECK(run(d, "undo").ok);
    CHECK(d.mesh.positions == start);
    d.mesh.clearMask();
    // Restoring the shape needs asking for it.
    CHECK(run(d, "selection_set_restore", {{"name", "Rest"}, {"channels", "morph"}}).ok);
    CHECK(maxDistance(d.mesh.positions, original) < 1e-5f);
    // The target goes with a delete (and comes back with its undo).
    CHECK(run(d, "selection_set_delete", {{"name", "Rest"}}).ok);
    CHECK(!d.mesh.selectionSets);
    CHECK(run(d, "undo").ok);
    CHECK(d.mesh.selectionSets && d.mesh.selectionSets->morphTarget == "Rest");
}

TEST(selection_sets_morph_across_levels) {
    Document d = makeDoc(makeCube(3));
    CHECK(run(d, "subdiv_divide").ok); // levels 1..2, at the top
    const std::vector<glm::vec3> topRest = d.mesh.positions;
    CHECK(run(d, "subdiv_level", {{"level", 1.0}}).ok);
    CHECK(run(d, "selection_set_store", {{"name", "Rest"}, {"channels", "morph"}}).ok); // stored at the base
    // Reshape the base, step up: the top moved too.
    for (glm::vec3& p : d.mesh.positions) p *= 1.3f;
    d.mesh.recomputeAllNormals();
    d.mesh.markAllDirty();
    d.bvhDirty = true;
    CHECK(run(d, "subdiv_level", {{"level", 2.0}}).ok);
    const float moved = maxDistance(d.mesh.positions, topRest);
    CHECK(moved > 0.1f);
    // Morph to Set at the top, from a set stored at the base: back to (nearly) the old top.
    CHECK(run(d, "selection_set_morph", {{"name", "Rest"}}).ok);
    CHECK(maxDistance(d.mesh.positions, topRest) < moved * 0.05f);
    // The Morph brush resolves the target at this level too.
    CHECK(run(d, "selection_set_morph_target", {{"name", "Rest"}}).ok);
    Sculptor probe;
    const std::vector<glm::vec3>* target = probe.morphTarget(d.mesh);
    CHECK(target && target->size() == d.mesh.vertexCount());
}

TEST(selection_sets_project_roundtrip) {
    Document d = makeDoc(makeIcosphere(3));
    paintState(d.mesh, 0.0f, true);
    CHECK(run(d, "selection_set_store", {{"name", "Everything"}, {"channels", "all"}}).ok);
    CHECK(run(d, "selection_set_store", {{"name", "Mask only"}, {"channels", "mask"}}).ok);
    CHECK(run(d, "selection_set_morph_target", {{"name", "Everything"}}).ok);
    const Mesh saved = d.mesh;
    // A second Part with its own set.
    d.addSubtool(makeCube(2), "Box");
    CHECK(run(d, "selection_set_from_visible", {{"name", "Box visible"}}).ok);
    const std::string path = tempPath("sf_selection_sets.sfproj");
    std::string err;
    CHECK(saveProject(path, d, err));
    Document back;
    CHECK(loadProject(path, back, err));
    CHECK(back.subtoolCount() == 2);
    back.selectSubtool(0);
    CHECK(setCount(back.mesh) == 2);
    CHECK(back.mesh.selectionSets && back.mesh.selectionSets->morphTarget == "Everything");
    SelectionSetData a, b;
    decodeSelectionSet(*setNamed(saved, "Everything"), kSetAllChannels, a);
    decodeSelectionSet(*setNamed(back.mesh, "Everything"), kSetAllChannels, b);
    CHECK(a.mask == b.mask && a.colors == b.colors && a.hidden == b.hidden && a.polygroups == b.polygroups &&
          a.positions == b.positions && a.uvs == b.uvs);
    back.selectSubtool(1);
    CHECK(setNamed(back.mesh, "Box visible"));
    // Undo isn't carried by projects: the loaded list is simply there.
    CHECK(!back.undo.canUndo());
    std::filesystem::remove(path);
}

TEST(selection_sets_old_projects) {
    // A project saved before selection sets existed loads without any, and gains them.
    Document d;
    std::string err;
    CHECK(loadProject(SF_TEST_DATA_DIR "/levels_v5_sphere.sfproj", d, err));
    CHECK(!d.mesh.selectionSets);
    CHECK(subdivLevelCount(d.mesh) > 1);
    CHECK(run(d, "selection_set_store", {{"name", "At level"}, {"channels", "mask,morph"}}).ok);
    const std::string path = tempPath("sf_selection_sets_levels.sfproj");
    CHECK(saveProject(path, d, err));
    Document back;
    CHECK(loadProject(path, back, err));
    CHECK(setCount(back.mesh) == 1 && setNamed(back.mesh, "At level")->level == subdivCurrentLevel(back.mesh));
    CHECK(run(back, "subdiv_level", {{"level", 1.0}}).ok);
    CHECK(run(back, "selection_set_restore", {{"name", "At level"}}).ok);
    std::filesystem::remove(path);
}

TEST(selection_sets_file_export_import) {
    Document d = makeDoc(makeIcosphere(3));
    paintState(d.mesh);
    CHECK(run(d, "selection_set_store", {{"name", "A"}, {"channels", "mask,visibility"}}).ok);
    CHECK(run(d, "selection_set_store", {{"name", "B"}, {"channels", "polygroups"}}).ok);
    const std::string path = tempPath("sf_sets.sfsets");
    CHECK(run(d, "selection_sets_export", {{"path", path}}).ok);
    Document other = makeDoc(makeIcosphere(3));
    CHECK(run(other, "selection_sets_import", {{"path", path}}).ok);
    CHECK(setCount(other.mesh) == 2);
    CHECK(run(other, "selection_set_restore", {{"name", "A"}}).ok);
    CHECK(other.mesh.mask == d.mesh.mask && other.mesh.hidden == d.mesh.hidden);
    CHECK(run(other, "undo").ok);
    CHECK(run(other, "undo").ok);
    CHECK(setCount(other.mesh) == 0);
    // One set only; a mesh it doesn't fit says so.
    CHECK(run(d, "selection_sets_export", {{"path", path}, {"name", "B"}}).ok);
    Document small = makeDoc(makeIcosphere(1));
    const CommandResult r = run(small, "selection_sets_import", {{"path", path}});
    CHECK(r.ok && r.message.find("don't fit") != std::string::npos && setCount(small.mesh) == 1);
    CHECK(!registry().execute(small, "selection_set_restore", {{"name", "B"}}).ok);
    CHECK(!registry().execute(small, "selection_sets_import", {{"path", tempPath("sf_sets_missing.sfsets")}}).ok);
    std::filesystem::remove(path);
}

TEST(selection_sets_usd_roundtrip) {
    Document d = makeDoc(makeIcosphere(3));
    paintState(d.mesh, 0.0f, true);
    CHECK(run(d, "selection_set_store", {{"name", "Front half"}, {"channels", "mask,polygroups,visibility,paint"}}).ok);
    const Mesh expected = d.mesh;
    const std::string path = tempPath("sf_sets.usda");
    std::string err;
    CHECK(saveUsd(path, &d.mesh, nullptr, "Sculpt", err));
    Mesh back;
    SplatCloud splats;
    CHECK(loadUsd(path, back, splats, err));
    CHECK(back.vertexCount() == expected.vertexCount() && back.triangleCount() == expected.triangleCount());
    const SelectionSet* s = setNamed(back, "Front half");
    CHECK(s && s->has(kSetMask) && s->has(kSetVisibility) && s->has(kSetPolygroups) && s->has(kSetPaint));
    if (s) {
        SelectionSetData a;
        decodeSelectionSet(*s, kSetAllChannels, a);
        CHECK(a.hidden == expected.hidden);
        CHECK(a.polygroups == expected.polygroups);
        float diff = 0.0f;
        for (size_t v = 0; v < a.mask.size() && v < expected.mask.size(); ++v) diff = std::max(diff, std::abs(a.mask[v] - expected.mask[v]));
        CHECK(a.mask.size() == expected.mask.size() && diff < 1e-5f);
    }
    std::filesystem::remove(path);
}
