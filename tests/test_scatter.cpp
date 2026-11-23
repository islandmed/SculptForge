// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Scatter (src/core/Scatter.cpp): the promises, on a dense curved surface. Every copy sits on the surface (or only on
// its masked area), its up side along the normal when aligned; a seed gives the same scatter; Keep apart leaves no two
// copies touching (their boxes for the tight fit, their bounding spheres for the round one), with the spacing kept
// between them, and accounts for every copy it had to leave out; the scatter command is one undo step, joined or as
// separate Parts.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Primitives.h"
#include "core/Scatter.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <set>

using namespace sf;
using glm::vec3;

namespace {

// A sphere of radius 1 in about 20k triangles: curved everywhere, dense enough that copies slide over many triangles.
const Mesh& ball() {
    static const Mesh m = [] {
        Mesh s = makeIcosphere(5, 1.0f);
        s.recomputeAllNormals();
        return s;
    }();
    return m;
}

ScatterSettings settings(int count, ScatterSettings::Fit fit = ScatterSettings::Fit::Tight) {
    ScatterSettings s;
    s.count = count;
    s.fit = fit;
    return s;
}

size_t touchingPairs(const std::vector<ScatterCopy>& c, const vec3& half, const ScatterSettings& s) {
    size_t n = 0;
    for (size_t i = 0; i < c.size(); ++i)
        for (size_t j = i + 1; j < c.size(); ++j) n += scatterTouching(c[i], c[j], half, s) ? 1 : 0;
    return n;
}

// The worst distance of a copy's surface point from the unit sphere, and the worst angle (as 1 - cos) between its up
// side and the sphere's normal there.
void onBall(const std::vector<ScatterCopy>& c, float& off, float& tilt) {
    off = 0.0f, tilt = 0.0f;
    for (const ScatterCopy& k : c) {
        off = std::max(off, std::abs(glm::length(k.anchor) - 1.0f));
        tilt = std::max(tilt, 1.0f - glm::dot(k.rotation[1], glm::normalize(k.anchor)));
    }
}

} // namespace

TEST(scatter_copies_sit_on_the_surface_and_follow_its_normal) {
    const vec3 half(0.03f, 0.02f, 0.05f);
    ScatterSettings s = settings(400);
    ScatterStats st;
    const std::vector<ScatterCopy> c = scatterPlace(ball(), half, s, &st);
    CHECK(c.size() == 400 && st.placed == 400 && st.dropped == 0);
    float off, tilt;
    onBall(c, off, tilt);
    CHECK(off < 0.002f);   // the icosphere's flat triangles sit at most this far inside the true sphere
    CHECK(tilt < 0.002f);  // up side along the (smooth) normal
    for (const ScatterCopy& k : c) {
        CHECK(glm::distance(k.position, k.anchor) < 1e-6f); // offset 0: centred on the surface
        CHECK(std::abs(glm::determinant(k.rotation) - 1.0f) < 1e-4f); // a turn, never a mirror
        CHECK(k.scale == 1.0f);
    }
    // Resting on it (offset 0.5): the centre is half the copy's height (along its up side) above the surface.
    s.offset = 0.5f;
    for (const ScatterCopy& k : scatterPlace(ball(), half, s))
        CHECK(std::abs(glm::distance(k.position, k.anchor) - half.y) < 1e-4f);
    // Upright (not aligned): every up side is +Y wherever the copy sits.
    s.offset = 0.0f;
    s.align = false;
    for (const ScatterCopy& k : scatterPlace(ball(), half, s)) CHECK(glm::dot(k.rotation[1], vec3(0, 1, 0)) > 0.9999f);
    // Tilt leans the up side off the normal by at most that much.
    s.align = true;
    s.tilt = 30.0f;
    float worst = 0.0f;
    for (const ScatterCopy& k : scatterPlace(ball(), half, s))
        worst = std::max(worst, glm::degrees(std::acos(std::clamp(glm::dot(k.rotation[1], k.normal), -1.0f, 1.0f))));
    CHECK(worst <= 30.01f && worst > 20.0f);
}

TEST(scatter_seed_repeats_and_scale_range_holds) {
    const vec3 half(0.02f);
    ScatterSettings s = settings(300);
    s.scaleMin = 0.5f, s.scaleMax = 2.0f;
    const auto a = scatterPlace(ball(), half, s), b = scatterPlace(ball(), half, s);
    CHECK(a.size() == b.size());
    bool same = true;
    float lo = 1e9f, hi = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
        same &= a[i].position == b[i].position && a[i].scale == b[i].scale;
        lo = std::min(lo, a[i].scale), hi = std::max(hi, a[i].scale);
    }
    CHECK(same);
    CHECK(lo >= 0.5f && hi <= 2.0f && hi - lo > 1.2f);
    s.seed = 2;
    const auto c = scatterPlace(ball(), half, s);
    CHECK(c[0].position != a[0].position);
}

TEST(scatter_keep_apart_leaves_no_two_copies_touching) {
    // Flat-ish boxes, randomly turned and sized, more than fit loosely: the relax has real work to do.
    const vec3 half(0.06f, 0.02f, 0.03f);
    for (const auto fit : {ScatterSettings::Fit::Tight, ScatterSettings::Fit::Round}) {
        for (const float spacing : {0.0f, 0.5f}) {
            // (A gap of half a copy's size takes about twice the room: fewer copies, so that they still fit.)
            const int count = spacing > 0.0f ? 200 : 500;
            ScatterSettings s = settings(count, fit);
            s.scaleMin = 0.7f, s.scaleMax = 1.3f;
            s.spacing = spacing;
            ScatterSettings off = s;
            CHECK(touchingPairs(scatterPlace(ball(), half, off), half, s) > 20); // before: plenty overlap
            s.keepApart = true;
            ScatterStats st;
            const auto c = scatterPlace(ball(), half, s, &st);
            CHECK(touchingPairs(c, half, s) == 0);
            CHECK(st.placed + st.dropped == size_t(count) && c.size() == st.placed);
            CHECK(st.placed >= size_t(count) * 9 / 10); // there is room for (nearly) all of them
            float offBall, tilt;
            onBall(c, offBall, tilt);
            CHECK(offBall < 0.002f && tilt < 0.002f); // still on the surface, still along its normal
            if (fit == ScatterSettings::Fit::Round) { // the spacing is a real gap between the bounding spheres
                const float r = glm::length(half);
                float worst = 1e9f;
                for (size_t i = 0; i < c.size(); ++i)
                    for (size_t j = i + 1; j < c.size(); ++j) {
                        const float ri = r * c[i].scale, rj = r * c[j].scale;
                        worst = std::min(worst, glm::distance(c[i].position, c[j].position) - (ri + rj) * (1.0f + spacing));
                    }
                CHECK(worst >= -1e-5f);
            }
            std::printf("  keep apart %s spacing %.1f: %zu kept, %zu left out, %d passes, %.1f ms\n",
                        fit == ScatterSettings::Fit::Tight ? "tight" : "round", spacing, st.placed, st.dropped, st.passes,
                        st.relaxMs);
        }
    }
}

TEST(scatter_tight_fit_meshes_do_not_intersect) {
    // The promise on the mesh itself: no vertex of a copy lies inside another copy's (convex) cube.
    const Mesh piece = scatterPieceMesh(makeCube(1, 0.1f), false);
    const vec3 half = 0.5f * piece.bounds().extent();
    ScatterSettings s = settings(600);
    s.keepApart = true;
    const auto c = scatterPlace(ball(), half, s);
    const Mesh joined = scatterJoin(piece, c);
    CHECK(joined.triangleCount() == c.size() * piece.triangleCount());
    const size_t nv = piece.vertexCount();
    size_t inside = 0;
    for (size_t i = 0; i < c.size(); ++i)
        for (size_t j = 0; j < c.size(); ++j) {
            if (i == j || glm::distance(c[i].position, c[j].position) > 4.0f * glm::length(half)) continue;
            const glm::mat3 to = glm::transpose(c[j].rotation);
            for (size_t v = 0; v < nv; ++v) {
                const vec3 q = to * (joined.positions[i * nv + v] - c[j].position) / c[j].scale;
                if (std::abs(q.x) < half.x * 0.999f && std::abs(q.y) < half.y * 0.999f && std::abs(q.z) < half.z * 0.999f) ++inside;
            }
        }
    CHECK(inside == 0);
}

TEST(scatter_round_pieces_pack_by_their_own_sphere) {
    // Balls: their box's corners stick out past them, their own sphere is the whole piece. With it, Keep apart packs
    // them closer, and still no vertex of one copy is inside another's ball.
    const Mesh piece = scatterPieceMesh(makeIcosphere(2, 0.05f), false);
    const vec3 half = 0.5f * piece.bounds().extent();
    float radius = 0.0f;
    for (const vec3& p : piece.positions) radius = std::max(radius, glm::length(p));
    CHECK(radius < 0.6f * glm::length(half));
    ScatterSettings s = settings(1100);
    s.keepApart = true;
    ScatterStats loose, tight;
    scatterPlace(ball(), half, s, &loose);
    s.pieceRadius = radius;
    const auto c = scatterPlace(ball(), half, s, &tight);
    CHECK(tight.placed > loose.placed); // more fit
    const Mesh joined = scatterJoin(piece, c);
    const size_t nv = piece.vertexCount();
    size_t inside = 0;
    for (size_t i = 0; i < c.size(); ++i)
        for (size_t j = 0; j < c.size(); ++j) {
            if (i == j || glm::distance(c[i].position, c[j].position) > 4.0f * radius) continue;
            for (size_t v = 0; v < nv; ++v)
                inside += glm::distance(joined.positions[i * nv + v], c[j].position) < radius * c[j].scale * 0.995f ? 1 : 0;
        }
    CHECK(inside == 0);
    std::printf("  balls: %zu fit by their box, %zu by their own sphere\n", loose.placed, tight.placed);
}

TEST(scatter_keeps_to_the_masked_area) {
    Mesh m = ball();
    std::vector<float>& mask = m.ensureMask();
    for (size_t v = 0; v < m.positions.size(); ++v) mask[v] = m.positions[v].y > 0.5f ? 1.0f : 0.0f;
    const vec3 half(0.03f);
    ScatterSettings s = settings(300);
    s.masked = true;
    for (const bool apart : {false, true}) {
        s.keepApart = apart;
        const auto c = scatterPlace(m, half, s);
        CHECK(!c.empty());
        float lowest = 1.0f;
        for (const ScatterCopy& k : c) {
            lowest = std::min(lowest, k.anchor.y);
            const glm::uvec3 t = m.triangles[k.triangle];
            CHECK(m.mask[t.x] + m.mask[t.y] + m.mask[t.z] >= 1.5f);
        }
        CHECK(lowest > 0.45f); // within the masked cap (its border triangles reach a little below y = 0.5)
        CHECK(touchingPairs(c, half, s) == 0 || !apart);
    }
    // Nothing masked: refused with the reason.
    Mesh bare = ball();
    std::string error;
    CHECK(scatterPlace(bare, half, s, nullptr, &error).empty() && error.find("masked") != std::string::npos);
}

TEST(scatter_keep_apart_overfull_leaves_copies_out_never_overlapping) {
    // A small cap asked to hold far more than fits: the leftovers are dropped, the rest never touch.
    const vec3 half(0.05f);
    Mesh m = ball();
    std::vector<float>& mask = m.ensureMask();
    for (size_t v = 0; v < m.positions.size(); ++v) mask[v] = m.positions[v].y > 0.9f ? 1.0f : 0.0f;
    ScatterSettings s = settings(400);
    s.masked = s.keepApart = true;
    ScatterStats st;
    const auto c = scatterPlace(m, half, s, &st);
    CHECK(st.dropped > 300 && st.placed + st.dropped == 400 && !c.empty());
    CHECK(touchingPairs(c, half, s) == 0);
}

TEST(scatter_keep_apart_10k_copies_timing) {
    // 10,000 small cubes over the sphere with keep apart, packed close enough that the start can't place them all
    // apart and the relax has to make room: how long it takes (measured, printed for REPORT.md).
    const vec3 half(0.0115f);
    ScatterSettings s = settings(10000);
    s.keepApart = true;
    s.scaleMin = 0.8f, s.scaleMax = 1.2f;
    ScatterStats st;
    const auto t0 = std::chrono::steady_clock::now();
    const auto c = scatterPlace(ball(), half, s, &st);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    CHECK(st.placed + st.dropped == 10000 && st.placed > 9000 && st.passes > 1);
    std::printf("  10k keep apart: %zu kept, %zu left out, %d passes, place %.1f ms, relax %.1f ms, total %.1f ms\n",
                st.placed, st.dropped, st.passes, st.placeMs, st.relaxMs, ms);
    // 1,000 at the same density (a tenth of the sphere: the masked cap above y = 0.8): what a slider redo costs.
    {
        Mesh cap = ball();
        std::vector<float>& mask = cap.ensureMask();
        for (size_t v = 0; v < cap.positions.size(); ++v) mask[v] = cap.positions[v].y > 0.8f ? 1.0f : 0.0f;
        ScatterSettings k = s;
        k.count = 1000, k.masked = true;
        ScatterStats ks;
        const auto t1 = std::chrono::steady_clock::now();
        const auto kc = scatterPlace(cap, half, k, &ks);
        std::printf("  1k keep apart: %zu kept, %zu left out, %d passes, total %.1f ms\n", ks.placed, ks.dropped, ks.passes,
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count());
        CHECK(ks.placed > 900 && touchingPairs(kc, half, k) == 0);
    }
    // No touching pair (checked with a grid-free sweep over neighbours by position: sort by x).
    std::vector<size_t> order(c.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return c[a].position.x < c[b].position.x; });
    const float reach = 2.0f * glm::length(half) * 1.2f;
    size_t touching = 0;
    for (size_t a = 0; a < order.size(); ++a)
        for (size_t b = a + 1; b < order.size() && c[order[b]].position.x - c[order[a]].position.x <= reach; ++b)
            touching += scatterTouching(c[order[a]], c[order[b]], half, s) ? 1 : 0;
    CHECK(touching == 0);
}

TEST(scatter_command_one_undo_step_joined_or_separate) {
    Document doc;
    CommandRegistry reg;
    doc.replaceActiveMesh(ball());
    doc.parts[0].name = "Ball";
    CHECK(reg.execute(doc, "part_append", {{"type", std::string("cube")}, {"name", std::string("Box")}, {"size", 0.05}}).ok);
    doc.selectPart(0);
    const size_t boxTris = doc.partMesh(1).triangleCount();
    // Joined: one new Part, right below the surface, the surface stays active.
    CommandResult r = reg.execute(doc, "scatter", {{"part", std::string("Box")}, {"count", 50.0}, {"keep_apart", true}});
    CHECK(r.ok);
    CHECK(doc.parts.size() == 3 && doc.activePart == 0);
    const int made = doc.findPart("Box Scatter");
    CHECK(made == 1);
    CHECK(doc.partMesh(made).triangleCount() == 50 * boxTris);
    std::set<uint32_t> groups(doc.partMesh(made).facegroups.begin(), doc.partMesh(made).facegroups.end());
    CHECK(groups.size() == 50); // one face group per copy
    CHECK(reg.execute(doc, "undo").ok && doc.parts.size() == 2);
    CHECK(reg.execute(doc, "redo").ok && doc.parts.size() == 3);
    CHECK(reg.execute(doc, "undo").ok && doc.parts.size() == 2);
    // A built-in shape, as separate Parts: one undo takes them all out.
    r = reg.execute(doc, "scatter", {{"shape", std::string("pebble")}, {"count", 12.0}, {"separate", true}, {"flat", true}});
    CHECK(r.ok);
    CHECK(doc.parts.size() == 14 && doc.findPart("Pebble Scatter 12") >= 0);
    CHECK(reg.execute(doc, "undo").ok && doc.parts.size() == 2);
    // Every built-in shape scatters.
    for (const char* shape : {"pebble", "disc", "hex", "sphere", "cube", "cone", "cylinder", "capsule", "torus"}) {
        CHECK(reg.execute(doc, "scatter", {{"shape", std::string(shape)}, {"count", 5.0}, {"keep_apart", true}}).ok);
        CHECK(reg.execute(doc, "undo").ok && doc.parts.size() == 2);
    }
    // By index, over another target.
    CHECK(reg.execute(doc, "scatter", {{"part", 0.0}, {"target", std::string("Box")}, {"count", 3.0}}).ok);
    CHECK(reg.execute(doc, "undo").ok);
    // Refusals.
    CHECK(!reg.execute(doc, "scatter", {}).ok);
    CHECK(!reg.execute(doc, "scatter", {{"part", std::string("Ball")}}).ok);  // onto itself
    CHECK(!reg.execute(doc, "scatter", {{"part", std::string("Nope")}}).ok);
    CHECK(!reg.execute(doc, "scatter", {{"shape", std::string("teapot")}}).ok);
    CHECK(!reg.execute(doc, "scatter", {{"part", std::string("Box")}, {"count", 0.0}}).ok);
    CHECK(!reg.execute(doc, "scatter", {{"part", std::string("Box")}, {"count", 600.0}, {"separate", true}}).ok);
    CHECK(!reg.execute(doc, "scatter", {{"part", std::string("Box")}, {"masked", true}}).ok); // nothing masked
    CHECK(!reg.execute(doc, "scatter", {{"part", std::string("Box")}, {"spacing", 3.0}}).ok);
    CHECK(doc.parts.size() == 2);
}
