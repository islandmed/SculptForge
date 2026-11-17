// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The brush shelf previews (app/BrushPreview.cpp): every built-in brush sculpts its mark on the clay ball, and the
// marks keep their shape. Tendril and Elastic Tendril pull a long horn out past the silhouette (they used to show a
// tiny spike when the preview path ran off the ball), and no preview leaves torn, folded or degenerate triangles
// (Layer's pinched corners, Nudge's torn ridge).
#include "TestHarness.h"

#include "app/AssetLibrary.h"
#include "app/BrushPreview.h"
#include "core/Image.h"
#include "core/Primitives.h"
#include "core/Sculpt.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>

using namespace sf;

namespace {

struct PreviewShape {
    float reach = 0.0f;   // how far the mesh sticks out past the ball's silhouette (in the xy view plane)
    float lift = 0.0f;    // the largest change of a vertex's distance from the centre (in or out)
    int degenerate = 0;   // triangles with (nearly) no area
    int folded = 0;       // neighbouring triangles turned back on each other (> 150 degrees apart)
};

PreviewShape measure(const Mesh& m) {
    PreviewShape s;
    for (const glm::vec3& p : m.positions) {
        s.reach = std::max(s.reach, glm::length(glm::vec2(p)) - kBallRadius);
        s.lift = std::max(s.lift, std::abs(glm::length(p) - kBallRadius));
    }
    std::vector<glm::vec3> normals(m.triangles.size());
    for (size_t t = 0; t < m.triangles.size(); ++t) {
        const glm::uvec3 tri = m.triangles[t];
        const glm::vec3 a = m.positions[tri.x], b = m.positions[tri.y], c = m.positions[tri.z];
        const glm::vec3 n = glm::cross(b - a, c - a);
        const float area2 = glm::length(n);
        const float longest = std::max({glm::length(b - a), glm::length(c - b), glm::length(a - c)});
        if (area2 < 1e-12f || longest <= 0.0f) {
            ++s.degenerate;
            continue;
        }
        normals[t] = n / area2;
    }
    // Folds: across each edge (found by its sorted vertex pair), the two triangles' normals.
    std::vector<std::pair<uint64_t, uint32_t>> edges;
    edges.reserve(m.triangles.size() * 3);
    for (size_t t = 0; t < m.triangles.size(); ++t)
        for (int k = 0; k < 3; ++k) {
            const uint32_t u = m.triangles[t][k], v = m.triangles[t][(k + 1) % 3];
            edges.emplace_back((uint64_t(std::min(u, v)) << 32) | std::max(u, v), uint32_t(t));
        }
    std::sort(edges.begin(), edges.end());
    for (size_t i = 0; i + 1 < edges.size(); ++i)
        if (edges[i].first == edges[i + 1].first &&
            glm::dot(normals[edges[i].second], normals[edges[i + 1].second]) < -0.866f)
            ++s.folded;
    return s;
}

} // namespace

TEST(brush_previews) {
    const bool verbose = std::getenv("SF_BRUSH_PREVIEW_METRICS") != nullptr;
    std::vector<int> brushes;
    for (int t = 0; t < int(BrushType::Count); ++t)
        if (BrushType(t) != BrushType::Morph) brushes.push_back(t); // (Morph needs a morph target; not on the shelf)
    brushes.push_back(kPaintBrush);
    for (int t : brushes) {
        const auto mesh = sculptBrushPreview(nullptr, t);
        CHECK(mesh != nullptr);
        if (!mesh) continue;
        const PreviewShape s = measure(*mesh);
        const char* name = t == kPaintBrush ? "Paint" : brushLabel(BrushType(t));
        if (verbose)
            std::printf("  %-16s reach %.3f lift %.3f degenerate %d folded %d\n", name, s.reach, s.lift, s.degenerate,
                        s.folded);
        CHECK(s.degenerate == 0);
        CHECK(s.folded == 0);
        if (s.degenerate || s.folded) std::printf("  (%s)\n", name);
        if (t != kPaintBrush) CHECK(s.lift > 0.01f); // every brush leaves a mark
    }
    // Tendril and Elastic Tendril pull a horn well past the silhouette, as a drag in the viewport does.
    for (BrushType t : {BrushType::SnakeHook, BrushType::KelvinSnake}) {
        const auto mesh = sculptBrushPreview(nullptr, int(t));
        CHECK(mesh && measure(*mesh).reach >= 0.25f);
    }
}

// #208: a brush's thumbnail shows its alpha pressed into the ball in the brush's own stroke type, so brushes that
// differ only by alpha tell apart at a glance.
namespace {

// How far apart two previews are (they share the ball's topology): the summed distance between their vertices.
float apart(const Mesh& a, const Mesh& b) {
    if (a.positions.size() != b.positions.size()) return 1e9f;
    float sum = 0.0f;
    for (size_t i = 0; i < a.positions.size(); ++i) sum += glm::length(a.positions[i] - b.positions[i]);
    return sum;
}

std::unique_ptr<Mesh> withLook(const BrushRecipe* recipe, int type, const BrushToolSettings& s) {
    AlphaLook look;
    if (!makeAlphaLook(s, look)) return nullptr;
    return sculptBrushPreview(recipe, type, &look);
}

BrushToolSettings settingsWith(const char* alpha, StrokeType stroke = StrokeType::Dots) {
    BrushToolSettings s;
    s.alphaName = alpha;
    s.stroke = stroke;
    return s;
}

} // namespace

TEST(brush_preview_alpha_in_every_stroke_type) {
    BrushRecipe recipe;
    recipe.name = "Alpha Test";
    const auto plain = sculptBrushPreview(&recipe, 0);
    CHECK(plain != nullptr);
    std::vector<std::unique_ptr<Mesh>> seen;
    for (int st = 0; st < int(StrokeType::Count); ++st) {
        for (const char* alpha : {"Scales", "Rivets", "Cracks"}) {
            const auto mesh = withLook(&recipe, 0, settingsWith(alpha, StrokeType(st)));
            CHECK(mesh != nullptr);
            if (!mesh) continue;
            const PreviewShape s = measure(*mesh);
            if (std::getenv("SF_BRUSH_PREVIEW_METRICS"))
                std::printf("  %-10s %-8s lift %.3f reach %.3f degenerate %d folded %d\n", strokeTypeName(StrokeType(st)), alpha,
                            s.lift, s.reach, s.degenerate, s.folded);
            CHECK(s.degenerate == 0);
            CHECK(s.folded == 0);
            CHECK(s.lift > 0.01f); // it leaves a mark...
            CHECK(s.lift < 0.45f); // ...and no spike through the ball
            CHECK(apart(*mesh, *plain) > 0.5f); // not the brush's plain thumbnail
        }
    }
}

TEST(brush_preview_alpha_differs_by_alpha_stroke_angle_and_tile) {
    BrushRecipe recipe;
    recipe.name = "Alpha Test";
    const auto scales = withLook(&recipe, 0, settingsWith("Scales"));
    const auto rivets = withLook(&recipe, 0, settingsWith("Rivets"));
    CHECK(scales && rivets && apart(*scales, *rivets) > 0.5f);
    // The stroke type: Dots, Drag Stamp, Drag Dot and Spray each leave their own mark with the same alpha.
    std::vector<std::unique_ptr<Mesh>> byStroke;
    for (StrokeType st : {StrokeType::Dots, StrokeType::DragRect, StrokeType::DragDot, StrokeType::Spray})
        byStroke.push_back(withLook(&recipe, 0, settingsWith("Wrinkles", st)));
    for (size_t i = 0; i < byStroke.size(); ++i)
        for (size_t j = i + 1; j < byStroke.size(); ++j)
            CHECK(byStroke[i] && byStroke[j] && apart(*byStroke[i], *byStroke[j]) > 0.5f);
    // Angle turns a stamp; Tile repeats the alpha inside it.
    BrushToolSettings stamp = settingsWith("Wrinkles", StrokeType::DragRect);
    const auto upright = withLook(&recipe, 0, stamp);
    stamp.alphaOptions.angle = 90.0f;
    const auto turned = withLook(&recipe, 0, stamp);
    CHECK(upright && turned && apart(*upright, *turned) > 0.5f);
    stamp.alphaOptions.angle = 0.0f;
    stamp.alphaOptions.tileX = stamp.alphaOptions.tileY = 3;
    const auto tiled = withLook(&recipe, 0, stamp);
    CHECK(upright && tiled && apart(*upright, *tiled) > 0.5f);
}

TEST(brush_preview_drag_stamp_lands_in_the_middle) {
    // Drag Stamp presses one stamp: a symmetric alpha's mark is centred on the front of the ball and stays inside the
    // stamp's square (radius 0.55 x kAlphaReach), not smeared along an S.
    BrushRecipe recipe;
    recipe.name = "Alpha Test";
    const auto mesh = withLook(&recipe, 0, settingsWith("Square", StrokeType::DragRect));
    CHECK(mesh != nullptr);
    if (!mesh) return;
    Mesh ball = makeIcosphere(kBallDetail, kBallRadius);
    glm::vec2 sum(0.0f);
    float weight = 0.0f, farthest = 0.0f;
    for (size_t i = 0; i < mesh->positions.size() && i < ball.positions.size(); ++i) {
        const float moved = glm::length(mesh->positions[i] - ball.positions[i]);
        if (moved < 1e-4f) continue;
        sum += glm::vec2(ball.positions[i]) * moved;
        weight += moved;
        farthest = std::max(farthest, glm::length(glm::vec2(ball.positions[i])));
    }
    CHECK(weight > 0.0f);
    if (weight > 0.0f) {
        const glm::vec2 centre = sum / weight;
        CHECK(std::abs(centre.x) < 0.05f && std::abs(centre.y) < 0.05f);
    }
    CHECK(farthest < 0.55f * 1.4143f + 0.05f);
}

TEST(brush_preview_alpha_from_file_and_missing_file) {
    // An alpha image (the user's own library): its preview is the stamp of that image, and a file that went away
    // gives no look (the plain thumbnail), never a crash.
    const std::string dir = (std::filesystem::temp_directory_path() / "sf-preview-alpha-test").string();
    std::filesystem::create_directories(dir);
    Image img;
    img.width = img.height = 64;
    img.rgba.assign(size_t(64) * 64 * 4, 255);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            uint8_t* p = &img.rgba[(size_t(y) * 64 + size_t(x)) * 4];
            p[0] = p[1] = p[2] = (x / 8 + y / 8) % 2 ? 255 : 20; // a checker
        }
    const std::string path = dir + "/checker.png";
    std::string err;
    CHECK(img.savePng(path, err));
    BrushToolSettings s;
    s.alphaName = "checker";
    s.alphaPath = path;
    s.stroke = StrokeType::DragRect;
    CHECK(hasAlphaLook(s));
    BrushRecipe recipe;
    recipe.name = "Alpha Test";
    const auto mesh = withLook(&recipe, 0, s);
    CHECK(mesh != nullptr);
    if (mesh) {
        const PreviewShape shape = measure(*mesh);
        CHECK(shape.lift > 0.01f && shape.degenerate == 0 && shape.folded == 0);
    }
    s.alphaInvert = true; // inverted: the other squares are high
    const auto inverted = withLook(&recipe, 0, s);
    CHECK(mesh && inverted && apart(*mesh, *inverted) > 0.5f);
    s.alphaPath = dir + "/gone.png";
    AlphaLook look;
    CHECK(!makeAlphaLook(s, look));
    std::filesystem::remove_all(dir);
}

TEST(brush_preview_alpha_only_where_it_applies) {
    // Round (or none) is no alpha; a built-in brush that takes the alpha shows it, one that ignores it (Smooth, Move)
    // keeps its own mark.
    CHECK(!hasAlphaLook(BrushToolSettings()));
    CHECK(!hasAlphaLook(settingsWith("none")));
    CHECK(hasAlphaLook(settingsWith("Pores")));
    for (BrushType t : {BrushType::Smooth, BrushType::Move, BrushType::SnakeHook, BrushType::KelvinGrab}) {
        CHECK(!brushTakesAlpha(t));
        const auto plain = sculptBrushPreview(nullptr, int(t));
        const auto with = withLook(nullptr, int(t), settingsWith("Pores"));
        CHECK(plain && with && apart(*plain, *with) < 1e-6f);
    }
    for (BrushType t : {BrushType::Standard, BrushType::Clay, BrushType::Inflate}) {
        CHECK(brushTakesAlpha(t));
        const auto plain = sculptBrushPreview(nullptr, int(t));
        const auto with = withLook(nullptr, int(t), settingsWith("Pores", StrokeType::DragRect));
        CHECK(plain && with && apart(*plain, *with) > 0.5f);
        if (with) {
            const PreviewShape s = measure(*with);
            CHECK(s.degenerate == 0 && s.folded == 0 && s.lift < 0.45f);
        }
    }
}
