// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pinching carving recipes (Engrave, Crease Line, Fold Valley...) cut clean grooves (Discord 0.13.11, "carving brushes seem to have
// excessive pinching": Engrave in ONE stroke, the pen never lifted, dragged up and down the same line a few times on the 81.9k
// starter sphere, left a ruffled, sawtooth wall of thin triangles instead of a clean cut). Every dab pulled the walls toward the
// stroke with nothing to stop it, so the laps of one stroke compounded the pull until neighbours crossed. Geometric invariants
// (GeomInvariants.h): after the laps, no pair of neighbouring triangles folded against each other (normals more than 120
// degrees apart), no triangle turned over against the way it faced (past the cut's own lean), and the groove is still a cut.
// By hand: SF_CARVE_TRIS / SF_CARVE_RADIUS / SF_CARVE_PASSES (laps) / SF_CARVE_ONLY (a recipe name) / SF_CARVE_PINCH /
// SF_CARVE_SYM (X symmetry) / SF_CARVE_SEPARATE (a stroke per lap) / SF_CARVE_SPACING / SF_CARVE_WOBBLE / SF_CARVE_UMB /
// SF_CARVE_LEVEL rerun it at another scale or setting.
#include "TestHarness.h"

#include "GeomInvariants.h"

#include "core/Benchmark.h"
#include "core/BrushPacks.h"
#include "core/Dyntopo.h"
#include "core/Micro.h"
#include "core/PathStroke.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

using namespace sf;

namespace {

double envNumber(const char* name, double fallback) {
    const char* v = std::getenv(name);
    return v && *v ? std::atof(v) : fallback;
}

// The carving recipes that pull the surface toward the stroke (or the one named).
std::vector<BrushRecipe> pinchers() {
    const char* only = std::getenv("SF_CARVE_ONLY");
    std::vector<BrushRecipe> out;
    for (BrushRecipe r : builtinBrushRecipes()) {
        if (r.depth >= 0.0f || (only && *only ? r.name != only : r.pinch <= 0.0f || r.pattern != BrushRecipe::Pattern::None || !r.alpha.empty())) continue;
        if (const char* k = std::getenv("SF_CARVE_PINCH")) r.pinch = float(std::atof(k));
        out.push_back(std::move(r));
    }
    return out;
}

// `passes` laps, down and up the middle of a sphere facing +z (the view looks into it; a groove 0.8 long) at Intensity 50.
void stroke(Document& doc, const BrushRecipe& r, float radius, int passes, bool mirror, bool lift = false, float x0 = 0.0f, bool facing = false) {
    PathStroke s;
    s.contactFacing = facing; // (Brush > Contact facing at its Facing angle 180)
    for (int i = 0; i <= 8; ++i) s.points.push_back(glm::normalize(glm::vec3(x0, -0.4f + 0.1f * float(i), 1.0f)) * 1.2f);
    if (const double wobble = envNumber("SF_CARVE_WOBBLE", 0.0); wobble > 0.0) { // (by hand: a pen's path, dense samples with a shaky hand)
        s.points.clear();
        uint32_t seed = 12345;
        auto rnd = [&seed] { seed = seed * 1664525u + 1013904223u; return float(seed >> 8) / float(1u << 24) - 0.5f; };
        float x = 0.0f;
        for (int i = 0; i <= 400; ++i) {
            x = 0.9f * x + float(wobble) * rnd();
            s.points.push_back(glm::normalize(glm::vec3(x, -0.4f + 0.8f * float(i) / 400.0f, 1.0f)) * 1.2f);
        }
        s.curve = false;
    }
    s.view = glm::vec3(0, 0, -1);
    s.radius = radius;
    s.strength = 0.5f;
    s.spacing = float(envNumber("SF_CARVE_SPACING", -1.0)); // (by hand: wider dabs, as a stroke thinned under load places them)
    s.symmetry = Symmetry{mirror, false, false};
    std::string err;
    CHECK(BrushAlpha::builtin(r.alpha, doc.brushAlpha) || r.alpha.empty());
    if (!lift && !std::getenv("SF_CARVE_SEPARATE")) { // (all the laps in ONE stroke, the pen never lifted: how the report was drawn)
        std::vector<glm::vec3> lap = s.points;
        s.points.clear();
        for (int pass = 0; pass < passes; ++pass) {
            std::reverse(lap.begin(), lap.end());
            s.points.insert(s.points.end(), lap.begin() + (pass ? 1 : 0), lap.end());
        }
        CHECK(strokePath(doc, &r, BrushType::Standard, s, err) > 0);
        return;
    }
    for (int pass = 0; pass < passes; ++pass) {
        std::reverse(s.points.begin(), s.points.end());
        CHECK(strokePath(doc, &r, BrushType::Standard, s, err) > 0);
    }
}

} // namespace

namespace {

struct Cut {
    float sink = 0.0f, rise = 0.0f;
    size_t flipped = 0, folded = 0, triangles = 0;
};

// One recipe, `laps` laps of one stroke at `radius` on the 81.9k starter sphere.
Cut carve(const BrushRecipe& r, float radius, int laps, bool mirror, bool lift = false, float x0 = 0.0f, bool facing = false) {
    Document doc = makeDoc(makeBenchSphere(size_t(envNumber("SF_CARVE_TRIS", 81920))));
    doc.mesh.recomputeAllNormals(); // (the app's mesh has its normals; Contact facing and the groove's depth read them)
    const std::vector<glm::vec3> before = doc.mesh.positions;
    stroke(doc, r, radius, laps, mirror, lift, x0, facing);
    CHECK(doc.mesh.vertexCount() == before.size());
    Cut c;
    for (uint32_t v = 0; v < before.size(); ++v) {
        const float h = glm::dot(doc.mesh.positions[v] - before[v], glm::normalize(before[v]));
        c.sink = std::max(c.sink, -h), c.rise = std::max(c.rise, h);
    }
    c.flipped = geom::flippedFaces(before, doc.mesh), c.folded = geom::foldedFaces(before, doc.mesh), c.triangles = doc.mesh.triangles.size();
    std::printf("  %-16s r %.2f, %d laps: groove %.3f deep, %zu flipped, %zu folded of %zu triangles\n", r.name.c_str(), radius, laps, c.sink,
                c.flipped, c.folded, c.triangles);
    return c;
}

} // namespace

// A plain mesh: the pinch moves the vertices themselves, dab after dab, and the walls of a groove stroked up and down over
// the same line used to be pressed over it until they crossed. Three laps of one stroke, at the report's brush size (0.24 of
// the sphere's radius: Size 156 px on his view) and at a smaller one.
TEST(pinching_recipes_cut_clean_grooves_in_one_stroke) {
    const std::vector<BrushRecipe> recipes = pinchers();
    CHECK(!recipes.empty());
    const int laps = int(envNumber("SF_CARVE_PASSES", 3));
    const bool mirror = std::getenv("SF_CARVE_SYM") != nullptr;
    for (const float radius : {float(envNumber("SF_CARVE_RADIUS", 0.236)), 0.1f})
        for (const BrushRecipe& r : recipes) {
            // (Wrinkle pinches at 0.7 and cuts 0.55 deep: its third lap in one stroke is past the depth the walls hold)
            const Cut c = carve(r, radius, r.pinch > 0.6f ? std::min(laps, 2) : laps, mirror);
            CHECK(c.sink > 0.01f);  // (a stroke that cuts nothing folds nothing either)
            CHECK(c.rise < 0.002f); // (a carve: nothing rises beyond the rounding of the falloff's rim)
            CHECK(c.folded == 0);
            // (a groove deeper than its brush leans its steepest wall past vertical: the cut's own, not the pinch's)
            CHECK(c.flipped * 1000 <= c.triangles);
        }
}

// Engrave, the reported brush, eight laps of one stroke (a groove more than two brush radii deep): the walls still don't cross.
TEST(engrave_eight_laps_in_one_stroke_fold_nothing) {
    BrushRecipe engrave;
    for (const BrushRecipe& r : builtinBrushRecipes())
        if (r.name == "Engrave") engrave = r;
    CHECK(engrave.name == "Engrave");
    for (const float radius : {0.236f, 0.1f}) {
        const Cut c = carve(engrave, radius, 8, false);
        CHECK(c.folded == 0);
    }
}

// X symmetry with the stroke on the mirror plane (the head's centre line): both copies pull the same vertices from their own
// centres, and each used to find it pulled by the other already, so the two tapered differently: a serrated, torn end.
TEST(pinching_recipes_keep_the_mirror_seam_clean) {
    for (const char* name : {"Engrave", "Fold Valley"}) { // (Crease Line folds at the seam with the pinch at 0 too: its cut, doubled by the copies)
        BrushRecipe recipe;
        for (const BrushRecipe& r : builtinBrushRecipes())
            if (r.name == name) recipe = r;
        CHECK(recipe.name == name);
        for (const float radius : {0.236f, 0.1f}) {
            const Cut c = carve(recipe, radius, 2, true); // (both copies cut the one line: twice the depth of a lap)
            CHECK(c.folded == 0);
            CHECK(c.flipped * 1000 <= c.triangles);
        }
    }
}

// Crease Line dug up and down one line ten times (the report: "pinching feels stronger", then serrated walls inside the groove, with
// the mirror on, the default, and Contact facing on or off: it never cut here, its weights are all 1 on such a groove). The mesh
// holds a groove only so deep: dug on past that, the walls' rows overlapped in a sawtooth. Now the dig and the pinch fade out at about
// a brush radius under the rim: the groove stops deepening, and ten laps fold (almost) nothing, lifted or not, mirrored or not, with
// Contact facing on or off.
TEST(crease_line_ten_laps_keep_a_clean_groove) {
    BrushRecipe crease;
    for (const BrushRecipe& r : builtinBrushRecipes())
        if (r.name == "Crease Line") crease = r;
    CHECK(crease.name == "Crease Line");
    const float radius = 0.236f;
    const Cut first = carve(crease, radius, 1, false);
    CHECK(first.sink > 0.08f); // (the first laps dig as they did: 0.09, 0.18, 0.24 against 0.09, 0.18, 0.27 before)
    CHECK(carve(crease, radius, 2, false).sink > 0.16f);
    CHECK(carve(crease, radius, 3, false).sink > 0.22f);
    for (const bool mirror : {false, true})
        for (const bool lift : {false, true})
            for (const bool facing : {false, true}) {
                const Cut six = carve(crease, radius, 6, mirror, lift, 0.0f, facing), ten = carve(crease, radius, 10, mirror, lift, 0.0f, facing);
                CHECK(ten.folded * 10000 <= ten.triangles); // (8 of 84k at most)
                CHECK(ten.flipped * 1000 <= ten.triangles);
                CHECK(ten.sink < 0.35f);                     // (a brush radius and a quarter, where the rows overlap past it)
                CHECK(ten.sink - six.sink < 0.03f);          // (it has stopped deepening)
                CHECK(ten.rise < 0.002f);
            }
}

// Dynamic on: the groove adds triangles as it deepens, so the dig is not capped (ten laps go as deep as the pen takes them: Stop at mesh
// depth off does the same on a plain mesh), and the pinch still stops tightening: no folds. Folded neighbours counted on the mesh as it
// ends (it has more vertices than it began).
size_t foldedNow(const Mesh& m) {
    std::unordered_map<uint64_t, uint32_t> first;
    size_t folds = 0;
    for (uint32_t t = 0; t < m.triangleCount(); ++t) {
        const glm::uvec3 f = m.triangles[t];
        const glm::vec3 n = glm::normalize(glm::cross(m.positions[f.y] - m.positions[f.x], m.positions[f.z] - m.positions[f.x]));
        const uint32_t c[3] = {f.x, f.y, f.z};
        for (int k = 0; k < 3; ++k) {
            const uint64_t key = (uint64_t(std::min(c[k], c[(k + 1) % 3])) << 32) | std::max(c[k], c[(k + 1) % 3]);
            auto it = first.find(key);
            if (it == first.end()) { first.emplace(key, t); continue; }
            const glm::uvec3 g = m.triangles[it->second];
            const glm::vec3 o = glm::normalize(glm::cross(m.positions[g.y] - m.positions[g.x], m.positions[g.z] - m.positions[g.x]));
            if (glm::dot(n, o) < -0.5f) ++folds;
        }
    }
    return folds;
}
float deepestNow(const Mesh& m) {
    float deepest = 0.0f;
    for (const glm::vec3& p : m.positions) deepest = std::max(deepest, 1.0f - glm::length(p));
    return deepest;
}
TEST(crease_line_goes_deeper_with_dynamic_or_stop_at_mesh_depth_off) {
    BrushRecipe crease;
    for (const BrushRecipe& r : builtinBrushRecipes())
        if (r.name == "Crease Line") crease = r;
    CHECK(crease.name == "Crease Line");
    float capped = 0.0f;
    for (int mode = 0; mode < 3; ++mode) { // 0: capped, 1: Dynamic on, 2: Stop at mesh depth off
        Document doc = makeDoc(makeIcosphere(6, 1.0f)); // (no subdivision levels: Dynamic works on it)
        doc.mesh.recomputeAllNormals();
        BrushRecipe r = crease;
        r.stopAtMeshDepth = mode != 2;
        doc.sculptor.dyntopo.enabled = mode == 1;
        doc.sculptor.dyntopo.detail = 0.8f;
        stroke(doc, r, 0.236f, 10, false, true);
        const float deep = deepestNow(doc.mesh);
        std::printf("  mode %d: %zu vertices, deepest %.3f, folded %zu\n", mode, doc.mesh.vertexCount(), deep, foldedNow(doc.mesh));
        if (mode == 0) capped = deep;
        else CHECK(deep > capped + 0.15f);
        if (mode == 1) CHECK(foldedNow(doc.mesh) * 10000 <= doc.mesh.triangleCount());
    }
}

// A stroke on the mirror plane is one dab with both copies' weight: as deep as one beside the plane (where the two copies are
// two dabs a little apart that overlap on the plane), within 10%.
TEST(pinching_recipe_on_the_mirror_plane_digs_as_deep_as_beside_it) {
    BrushRecipe crease;
    for (const BrushRecipe& r : builtinBrushRecipes())
        if (r.name == "Crease Line") crease = r;
    CHECK(crease.name == "Crease Line");
    const Cut on = carve(crease, 0.236f, 1, true, true, 0.0f), beside = carve(crease, 0.236f, 1, true, true, 0.03f);
    CHECK(on.sink > 0.1f);
    CHECK(std::fabs(on.sink - beside.sink) < 0.1f * beside.sink);
}

// The per-stroke state of a pinching recipe (24 bytes a vertex) is the stroke's, not the mesh's: held while the stroke runs, gone
// when it ends (120 MB on a 5M-vertex mesh would otherwise stay as long as the mesh does).
TEST(pinching_recipe_frees_its_stroke_memory_when_the_stroke_ends) {
    BrushRecipe engrave;
    for (const BrushRecipe& r : builtinBrushRecipes())
        if (r.name == "Engrave") engrave = r;
    CHECK(engrave.name == "Engrave");
    Document doc = makeDoc(makeBenchSphere(81920));
    const size_t verts = doc.mesh.vertexCount();
    CHECK(doc.sculptor.pinchStateBytes() == 0);
    Symmetry sym{false, false, false};
    CHECK(doc.sculptor.beginStroke(doc.mesh, "Engrave"));
    for (int i = 0; i < 6; ++i) {
        DabParams p;
        p.center = glm::normalize(glm::vec3(0.0f, -0.1f + 0.04f * float(i), 1.0f));
        p.radius = 0.2f;
        p.strength = 0.5f;
        p.viewDir = glm::vec3(0, 0, -1);
        doc.sculptor.dabRecipe(doc.mesh, doc.bvh, engrave, p, sym);
    }
    CHECK(doc.sculptor.pinchStateBytes() >= verts * 24); // (held while the stroke runs)
    doc.undo.push(doc.sculptor.endStroke());
    CHECK(doc.sculptor.pinchStateBytes() == 0);          // (and returned when it ends)
    // The next stroke finds it again, from its own start.
    std::string err;
    PathStroke s;
    for (int i = 0; i <= 4; ++i) s.points.push_back(glm::normalize(glm::vec3(0.0f, -0.2f + 0.1f * float(i), 1.0f)) * 1.2f);
    s.view = glm::vec3(0, 0, -1);
    s.radius = 0.2f;
    s.strength = 0.5f;
    CHECK(strokePath(doc, &engrave, BrushType::Standard, s, err) > 0);
    CHECK(doc.sculptor.pinchStateBytes() == 0);
}

// A micro level takes the recipe as a grid dab (a height field: nothing slides): Engrave's groove stays clean there too.
TEST(engrave_cuts_a_clean_groove_on_a_micro_level) {
    Document doc = makeDoc(makeBenchSphere(size_t(envNumber("SF_CARVE_TRIS", 81920))));
    std::string err;
    CHECK(microDivide(doc.mesh, int(envNumber("SF_CARVE_LEVEL", 4)), err));
    const float radius = float(envNumber("SF_CARVE_RADIUS", 0.236));
    const glm::vec3 facing(0.0f, 0.0f, 1.0f);
    const geom::MicroSnapshot before = geom::microSnapshot(doc.mesh, facing, 0.5f);
    BrushRecipe engrave;
    for (const BrushRecipe& r : builtinBrushRecipes())
        if (r.name == "Engrave") engrave = r;
    CHECK(engrave.name == "Engrave");
    stroke(doc, engrave, radius, 4, false);
    const geom::MicroSnapshot after = geom::microSnapshot(doc.mesh, facing, 0.5f);
    const MicroMesh& mm = *doc.mesh.micro;
    const uint32_t n = mm.n;
    size_t inside = 0, flipped = 0, folded = 0;
    float sink = 0.0f;
    auto normal = [](const glm::vec3* p, uint32_t x, uint32_t y, uint32_t z) { return glm::cross(p[y] - p[x], p[z] - p[x]); };
    for (size_t k = 0; k < before.tris.size(); ++k) {
        const glm::vec3* b = before.points.data() + k * mm.samples;
        const glm::vec3* a = after.points.data() + k * mm.samples;
        for (uint32_t i = 0; i < mm.samples; ++i)
            if (std::abs(b[i].x) < radius && std::abs(b[i].y) < 0.3f) sink = std::max(sink, -glm::dot(a[i] - b[i], glm::normalize(b[i])));
        for (uint32_t j = 0; j < n; ++j)
            for (uint32_t i = 0; i + j < n; ++i) {
                const uint32_t x = MicroMesh::index(i, j, n), y = MicroMesh::index(i + 1, j, n), z = MicroMesh::index(i, j + 1, n);
                if (std::abs(b[x].x) > radius || std::abs(b[x].y) > 0.3f) continue;
                ++inside;
                const glm::vec3 nb = normal(b, x, y, z), na = normal(a, x, y, z);
                flipped += glm::dot(nb, na) <= 0.0f;
                if (i + j + 1 >= n) continue;
                const uint32_t w = MicroMesh::index(i + 1, j + 1, n); // (the neighbour across the shared edge: folded back on it)
                const glm::vec3 mb = normal(b, y, w, z), ma = normal(a, y, w, z);
                const float lb = glm::length(nb) * glm::length(mb), la = glm::length(na) * glm::length(ma);
                folded += la > 0.0f && glm::dot(na, ma) < -0.5f * la && !(lb > 0.0f && glm::dot(nb, mb) < -0.5f * lb);
            }
    }
    std::printf("  Engrave on Micro %d: groove %.3f deep, %zu flipped, %zu folded of %zu micro triangles\n", mm.level, sink, flipped, folded,
                inside);
    CHECK(inside > 0);
    CHECK(sink > 0.01f);
    CHECK(flipped == 0);
    CHECK(folded == 0);
}
