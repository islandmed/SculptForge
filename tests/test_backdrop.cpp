// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Image to 3D drops the flat plane a picture's background often becomes (core/Backdrop.h, request from fenix on Discord):
// a back wall or a ground, apart from the subject or joined to it, goes; a relief or a coin on its own stays.
#include "TestHarness.h"

#include "ai/AiCommands.h"
#include "core/Backdrop.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/ImageTo3D.h"
#include "core/Parts.h"
#include "core/Primitives.h"

#include <glm/glm.hpp>

using namespace sf;

namespace {

Mesh moved(Mesh m, glm::vec3 scale, glm::vec3 offset) {
    for (auto& p : m.positions) p = p * scale + offset;
    m.recomputeAllNormals();
    return m;
}

// A head-sized subject at the origin.
Mesh subject() { return makeIcosphere(3, 0.5f); }

// A wall behind it (a thin slab, 3 x 3 x 0.03) and a single-sided ground under it.
Mesh backWall() { return moved(makeCube(2, 1.0f), {3.0f, 3.0f, 0.03f}, {0.0f, 0.4f, -0.7f}); }
Mesh ground() { return moved(makePlane(6, 3.0f), {1.0f, 1.0f, 1.0f}, {0.0f, -0.55f, 0.0f}); }

// A relief: a flat field with a raised disc in the middle.
Mesh discRelief() {
    Image img;
    img.width = img.height = 64;
    img.rgba.assign(size_t(64) * 64 * 4, 255);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const bool disc = (x - 32) * (x - 32) + (y - 32) * (y - 32) < 18 * 18;
            uint8_t* p = &img.rgba[(size_t(y) * 64 + size_t(x)) * 4];
            p[0] = p[1] = p[2] = disc ? 255 : 0;
        }
    ReliefOptions o;
    o.resolution = 64;
    return makeImageRelief(img, o);
}

} // namespace

TEST(backdrop_wall_behind_a_subject_is_removed) {
    const Mesh head = subject(), wall = backWall();
    Mesh m = mergeMeshes({&head, &wall});
    BackdropRemoval r;
    CHECK(removeFlatBackdrop(m, &r));
    CHECK(r.planes == 1);
    CHECK(r.triangles == wall.triangleCount());
    CHECK(m.triangleCount() == head.triangleCount());
    CHECK(m.bounds().min.z > -0.51f);
}

TEST(backdrop_ground_under_a_subject_is_removed) {
    const Mesh head = subject(), floor = ground();
    Mesh m = mergeMeshes({&head, &floor});
    BackdropRemoval r;
    CHECK(removeFlatBackdrop(m, &r));
    CHECK(r.triangles == floor.triangleCount());
    CHECK(m.triangleCount() == head.triangleCount());

    // A ground and a back wall standing on its far edge (a corner, as a photo of a room makes): both go.
    const Mesh wall = moved(makeCube(2, 1.0f), {3.0f, 3.0f, 0.03f}, {0.0f, 0.95f, -1.5f});
    Mesh both = mergeMeshes({&head, &floor, &wall});
    CHECK(removeFlatBackdrop(both, &r));
    CHECK(r.planes == 2);
    CHECK(both.triangleCount() == head.triangleCount());
}

TEST(backdrop_ground_joined_to_the_subject_is_removed) {
    // A box standing on the ground, its bottom corners welded into the ground's grid: one connected piece.
    const Mesh box = moved(makeCube(4, 1.0f), {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f});
    const Mesh floor = moved(makePlane(8, 4.0f), {1.0f, 1.0f, 1.0f}, {0.0f, -0.5f, 0.0f});
    Mesh m = mergeMeshes({&box, &floor});
    BackdropRemoval r;
    CHECK(removeFlatBackdrop(m, &r));
    CHECK(r.triangles >= floor.triangleCount());
    const AABB b = m.bounds();
    CHECK(b.max.x - b.min.x < 1.01f);
    CHECK(b.max.y - b.min.y > 0.99f); // the box stays, only its bottom (inside the ground) goes with it
}

TEST(backdrop_relief_and_coin_alone_are_kept) {
    Mesh relief = discRelief();
    const size_t reliefTris = relief.triangleCount();
    CHECK(!removeFlatBackdrop(relief));
    CHECK(relief.triangleCount() == reliefTris);

    // The relief on a separate back plate is still a relief.
    Mesh plate = makePlane(4, 1.0f); // XZ: turned to face +Z, just behind the relief
    for (auto& p : plate.positions) p = {p.x, -p.z, -0.01f};
    const Mesh base = discRelief();
    Mesh plated = mergeMeshes({&base, &plate});
    CHECK(flatBackdropTriangles(plated).empty());

    // A flat coin (thin, closed) on its own.
    Mesh coin = moved(makeCylinder(48, 1.0f), {2.0f, 0.06f, 2.0f}, {});
    CHECK(!removeFlatBackdrop(coin));

    // A plane alone is the only part: kept.
    Mesh plane = makePlane(4, 2.0f);
    CHECK(!removeFlatBackdrop(plane));

    // A subject alone: nothing to remove.
    Mesh head = subject();
    CHECK(!removeFlatBackdrop(head));
}

TEST(backdrop_cli_removes_it_unless_asked_not_to) {
    const Mesh head = subject(), wall = backWall();
    Document doc;
    doc.setMesh(mergeMeshes({&head, &wall}));
    CommandRegistry reg;
    CommandResult r = reg.execute(doc, "remove_flat_background", {});
    CHECK(r.ok);
    CHECK(r.message.find("Removed a flat background plane") != std::string::npos);
    CHECK(doc.mesh.triangleCount() == head.triangleCount());
    r = reg.execute(doc, "remove_flat_background", {});
    CHECK(!r.ok); // nothing left to remove
}
