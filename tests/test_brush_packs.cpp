// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The built-in brush packs (core/BrushPacks.h), the procedural alphas they use, the recipe's alpha keys and the
// asset lists' order (core/ListSort.h). Every pack brush must leave a visible, clean mark on the preview ball.
#include "TestHarness.h"

#include "app/BrushPreview.h"
#include "core/BrushAlpha.h"
#include "core/BrushGallery.h"
#include "core/BrushPacks.h"
#include "core/BrushRecipe.h"
#include "core/ListSort.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <set>

using namespace sf;

TEST(brush_packs_seven_packs_of_ten) {
    const std::vector<BrushRecipe> all = builtinBrushRecipes();
    const std::vector<BrushRecipe> packs = builtinPackRecipes();
    CHECK(builtinBrushPacks().size() == 7);
    CHECK(packs.size() == 70);
    CHECK(all.size() == 7 + packs.size()); // the classic seven first
    std::map<std::string, int> perPack;
    std::set<std::string> names;
    for (const BrushRecipe& r : all) {
        CHECK(names.insert(r.name).second); // unique names
        CHECK(r.origin.source == BrushSource::Builtin);
    }
    for (const BrushRecipe& r : packs) {
        ++perPack[r.origin.pack];
        CHECK(!builtinBrushPackHelp(r.origin.pack).empty());
        CHECK(!r.description.empty() && r.description.size() <= 110); // one line
        CHECK(r.description.back() == '.');
        if (!r.alpha.empty()) {
            BrushAlpha a;
            CHECK(BrushAlpha::builtin(r.alpha, a) && a.name == r.alpha);
        }
    }
    for (const std::string& p : builtinBrushPacks()) CHECK(perPack[p] == 10);
}

TEST(brush_recipe_alpha_keys_round_trip) {
    BrushRecipe r;
    r.name = "Test";
    r.alpha = "Stitches";
    r.alphaFollow = true;
    const BrushRecipe back = BrushRecipe::fromJson(r.toJson());
    CHECK(back.alpha == "Stitches" && back.alphaFollow);
    // Angle and Angle jitter too (#136), clamped as the Alpha palette's sliders are.
    r.alphaAngle = 42.5f;
    r.alphaJitter = 20.0f;
    const BrushRecipe turned = BrushRecipe::fromJson(r.toJson());
    CHECK(turned.alphaAngle == 42.5f && turned.alphaJitter == 20.0f);
    Json wild = r.toJson();
    wild.set("alphaAngle", 900.0);
    wild.set("alphaAngleJitter", -4.0);
    const BrushRecipe clamped = BrushRecipe::fromJson(wild);
    CHECK(clamped.alphaAngle == 180.0f && clamped.alphaJitter == 0.0f);
    r.alphaAngle = r.alphaJitter = 0.0f;
    // Lenient names, as brush_set_alpha takes them; unknown names are dropped.
    Json j = r.toJson();
    j.set("alpha", "fabric_weave");
    CHECK(BrushRecipe::fromJson(j).alpha == "Fabric Weave");
    j.set("alpha", "no such alpha");
    CHECK(BrushRecipe::fromJson(j).alpha.empty());
    // Recipes without them write neither key (older files and brush codes stay byte for byte as they were).
    BrushRecipe plain;
    const std::string text = plain.toJson().dump();
    CHECK(text.find("alpha") == std::string::npos);
}

TEST(brush_alphas_builtin_set) {
    const auto& all = BrushAlpha::builtins();
    CHECK(all.size() == 31); // Round, Square, Noise, the twelve patterns, the six for the packs and the ten Forge Stamps
    std::set<std::string> names;
    for (const BrushAlpha& a : all) CHECK(names.insert(a.name).second);
    for (const char* name : {"Cobblestone", "Leather Grain", "Hex Plates", "Knit", "Bolt", "Chips", "Nostril", "Ear Whorl",
                             "Scale Plate", "Rivet Head", "Hatch Seam", "Louvre", "Cross Screw", "Suction Cup", "Gill Slits",
                             "Socket"}) {
        BrushAlpha a;
        CHECK(BrushAlpha::builtin(name, a));
        CHECK(!a.empty());
        CHECK(!BrushAlpha::builtinHelp(name).empty());
        double sum = 0.0;
        float hi = 0.0f;
        for (float v : a.values) {
            CHECK(v >= 0.0f && v <= 1.0f);
            sum += v;
            hi = std::max(hi, v);
        }
        const double mean = sum / double(a.values.size());
        CHECK(hi > 0.6f);                     // a real stamp, not a faint smudge
        CHECK(mean > 0.03 && mean < 0.8);     // a pattern, neither empty nor a flat plate
        CHECK(a.sample(0.99f, 0.99f) < 0.3f); // fades out at the corners: no hard square edge
    }
}

// The quality bar for every pack brush: a mark that is clearly visible on the preview ball (at least 0.5% of
// its radius) but no tower (at most ~28%), no folded triangles (stricter than the classic seven, two of which fold), and a stroke that sculpts in under 400 ms on the ~82k triangle ball.
TEST(brush_packs_every_brush_leaves_a_clean_mark) {
    double slowestMs = 0.0, totalMs = 0.0;
    std::string slowest;
    const bool all = std::getenv("SF_PACK_METRICS") != nullptr; // print every brush, the classic seven too
    for (const BrushRecipe& r : all ? builtinBrushRecipes() : builtinPackRecipes()) {
        const auto t0 = std::chrono::steady_clock::now();
        const std::unique_ptr<Mesh> m = sculptBrushPreview(&r, 0);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        CHECK(m != nullptr);
        if (!m) continue;
        float lift = 0.0f;
        size_t moved = 0;
        for (const glm::vec3& p : m->positions) {
            const float d = std::abs(glm::length(p) - kBallRadius);
            lift = std::max(lift, d);
            if (d > 0.001f) ++moved;
        }
        int folded = 0;
        for (const glm::uvec3& t : m->triangles) {
            const glm::vec3 a = m->positions[t.x], b = m->positions[t.y], c = m->positions[t.z];
            const glm::vec3 n = glm::cross(b - a, c - a);
            if (glm::length(n) > 1e-12f && glm::dot(glm::normalize(n), glm::normalize(a + b + c)) < -0.5f) ++folded;
        }
        if (all || lift < 0.0045f || lift > 0.25f || moved < 200 || folded > 0)
            std::printf("  %s (%s): lift %.4f, moved %zu, folded %d\n", r.name.c_str(), r.origin.pack.c_str(), lift,
                        moved, folded);
        CHECK(lift >= 0.0045f);
        CHECK(lift <= 0.25f); // no towers: a stroke builds at most about a quarter of the ball's radius
        CHECK(moved >= 200);
        CHECK(folded == 0);
        totalMs += ms;
        if (ms > slowestMs) {
            slowestMs = ms;
            slowest = r.name;
        }
    }
    std::printf("  60 pack brush strokes: %.0f ms in all, slowest %s %.0f ms\n", totalMs, slowest.c_str(), slowestMs);
    CHECK(slowestMs < timeLimit(400.0));
}

TEST(list_sort_orders) {
    CHECK(naturalLess("brush 2", "Brush 10"));
    CHECK(!naturalLess("Brush 10", "brush 2"));
    CHECK(naturalLess("alpha", "Beta"));
    CHECK(naturalLess("Bark", "bark") != naturalLess("bark", "Bark")); // a strict, deterministic order
    std::vector<ListSortKey> keys = {
        {"Wrinkle", "", -1}, {"bolt Head", "Hard Surface", 1}, {"Clay 10", "Clay & Form", -1},
        {"Clay 2", "Clay & Form", 0}, {"Mine", "Zeta Pack", -1},
    };
    keys[1].packRank = 1;
    keys[2].packRank = keys[3].packRank = 0;
    auto names = [&](ListSort s) {
        std::string out;
        for (size_t i : listSortOrder(keys, s)) out += keys[i].name + ";";
        return out;
    };
    CHECK(names(ListSort::AZ) == "bolt Head;Clay 2;Clay 10;Mine;Wrinkle;");
    CHECK(names(ListSort::Recent) == "Clay 2;bolt Head;Clay 10;Mine;Wrinkle;");
    // No pack first, then the built-in packs in their order, then other packs A-Z; A-Z inside each.
    CHECK(names(ListSort::Pack) == "Wrinkle;Clay 2;Clay 10;bolt Head;Mine;");
    CHECK(listSortFromId(listSortId(ListSort::Pack)) == ListSort::Pack);
    CHECK(listSortFromId("nonsense") == ListSort::AZ);
}

// A built-in pack brush must not share a name with a community gallery brush: the gallery would show it as
// installed and offer no Install (brush_gallery.sfui).
TEST(brush_packs_names_clear_of_the_gallery) {
    std::ifstream f(std::string(SF_TEST_DATA_DIR) + "/../../tools/brush-gallery/repo/index.json", std::ios::binary);
    CHECK(f.good());
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    GalleryIndex index;
    std::string error;
    CHECK(parseGalleryIndex(text, index, error));
    CHECK(index.brushes.size() >= 20);
    for (const BrushRecipe& r : builtinPackRecipes())
        for (const GalleryBrush& g : index.brushes) {
            if (r.name == g.name) std::printf("  pack brush %s is also a gallery brush\n", r.name.c_str());
            CHECK(r.name != g.name);
        }
}
