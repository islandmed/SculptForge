// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Image::withoutPlainBackground: a picture with a flat, vignetted or plate-with-a-rim backdrop gets it cut out before
// Generate 3D sends it (the models otherwise build the backdrop too: a plate came back as a slab through a bust); a busy
// border or a picture that is transparent already is left alone; prepareImagePng uses it unless "Remove flat background" is off.
#include "TestHarness.h"

#include "ai/ImageTo3DService.h"
#include "core/Image.h"

#include <cmath>
#include <filesystem>

using namespace sf;

namespace {

Image blank(int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    Image img;
    img.width = w;
    img.height = h;
    img.rgba.resize(size_t(w) * size_t(h) * 4);
    for (size_t i = 0; i < size_t(w) * size_t(h); ++i) {
        img.rgba[i * 4] = r;
        img.rgba[i * 4 + 1] = g;
        img.rgba[i * 4 + 2] = b;
        img.rgba[i * 4 + 3] = 255;
    }
    return img;
}
void put(Image& img, int x, int y, int r, int g, int b) {
    uint8_t* p = &img.rgba[(size_t(y) * size_t(img.width) + size_t(x)) * 4];
    p[0] = uint8_t(r);
    p[1] = uint8_t(g);
    p[2] = uint8_t(b);
}
uint8_t alphaAt(const Image& img, int x, int y) { return img.rgba[(size_t(y) * size_t(img.width) + size_t(x)) * 4 + 3]; }

// An orange figure (a disc with a darker lump) in the middle.
void subject(Image& img, int cx, int cy, int rad) {
    for (int y = 0; y < img.height; ++y)
        for (int x = 0; x < img.width; ++x) {
            const float d = std::hypot(float(x - cx), float(y - cy));
            if (d < float(rad)) put(img, x, y, 200 - int(d), 110, 50);
        }
}

// The app-icon look: a dark margin, a thin bright rim along a rounded rectangle, a grey plate (with a vignette) inside.
Image plate(int s) {
    Image img = blank(s, s, 47, 47, 45);
    const int m = s / 40, rad = s / 8;
    for (int y = 0; y < s; ++y)
        for (int x = 0; x < s; ++x) {
            const int dx = std::max({m - x, x - (s - 1 - m), 0}), dy = std::max({m - y, y - (s - 1 - m), 0});
            auto inside = [&](int inset) {
                const int ix = std::min(x - m - inset, s - 1 - m - inset - x), iy = std::min(y - m - inset, s - 1 - m - inset - y);
                if (ix < 0 || iy < 0) return false;
                if (ix < rad && iy < rad) return std::hypot(float(rad - ix), float(rad - iy)) <= float(rad);
                return true;
            };
            (void)dx;
            (void)dy;
            if (inside(0) && !inside(3)) put(img, x, y, 135, 134, 135); // the rim
            else if (inside(3)) {
                const float v = std::hypot(float(x - s / 2), float(y - s / 2)) / float(s); // darker toward the corners
                const int g = 66 - int(v * 24.0f);
                put(img, x, y, g, g, g);
            }
        }
    subject(img, s / 2, s / 2, s / 5);
    return img;
}

} // namespace

TEST(plain_backdrop_flat_colour_is_cut_out) {
    Image img = blank(240, 240, 250, 250, 250);
    subject(img, 120, 120, 55);
    bool removed = false;
    const Image out = img.withoutPlainBackground(&removed);
    CHECK(removed);
    CHECK(alphaAt(out, 3, 3) == 0 && alphaAt(out, 120, 4) == 0 && alphaAt(out, 236, 120) == 0);
    CHECK(alphaAt(out, 120, 120) == 255);
    CHECK(img.rgba.size() == out.rgba.size() && img.width == out.width);
    // framed: the subject now fills the frame instead of sitting small in a white one
    const Image framed = out.framedSubject(0.08f);
    CHECK(framed.width < 140 && framed.width > 100);
}

TEST(plain_backdrop_vignette_is_followed) {
    Image img = blank(300, 300, 0, 0, 0);
    for (int y = 0; y < 300; ++y)
        for (int x = 0; x < 300; ++x) {
            const int g = 200 - int(std::hypot(float(x - 150), float(y - 150)) * 0.45f); // 200 in the middle, ~100 in the corners
            put(img, x, y, g, g, g + 6);
        }
    subject(img, 150, 150, 60);
    bool removed = false;
    const Image out = img.withoutPlainBackground(&removed);
    CHECK(removed);
    CHECK(alphaAt(out, 5, 5) == 0 && alphaAt(out, 290, 10) == 0 && alphaAt(out, 150, 8) == 0);
    CHECK(alphaAt(out, 150, 150) == 255);
}

TEST(plain_backdrop_rounded_plate_with_a_rim_is_cut_out) {
    const Image img = plate(400);
    bool removed = false;
    const Image out = img.withoutPlainBackground(&removed);
    CHECK(removed);
    CHECK(alphaAt(out, 4, 4) == 0);      // the corner margin
    CHECK(alphaAt(out, 200, 6) == 0);    // the rim line
    CHECK(alphaAt(out, 60, 60) == 0);    // the plate itself
    CHECK(alphaAt(out, 340, 330) == 0);
    CHECK(alphaAt(out, 200, 200) == 255); // the figure
    const Image framed = out.framedSubject(0.08f);
    CHECK(framed.width < 200); // not the whole plate
}

TEST(plain_backdrop_left_alone_when_it_is_not_plain_or_not_needed) {
    // a busy border (stripes of every brightness): not a backdrop to cut
    Image busy = blank(200, 200, 0, 0, 0);
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 200; ++x) put(busy, x, y, (x * 37 + y * 91) % 256, (x * 13) % 256, (y * 29) % 256);
    bool removed = true;
    const Image same = busy.withoutPlainBackground(&removed);
    CHECK(!removed && same.rgba == busy.rgba);
    // transparent already: untouched
    Image cut = blank(100, 100, 200, 0, 0);
    for (int y = 0; y < 50; ++y)
        for (int x = 0; x < 100; ++x) cut.rgba[(size_t(y) * 100 + size_t(x)) * 4 + 3] = 0;
    removed = true;
    CHECK(cut.withoutPlainBackground(&removed).rgba == cut.rgba && !removed);
    // a picture that is all one colour has no subject: untouched
    Image flat = blank(100, 100, 90, 90, 90);
    removed = true;
    CHECK(flat.withoutPlainBackground(&removed).rgba == flat.rgba && !removed);
    // tiny
    Image tiny = blank(16, 16, 90, 90, 90);
    CHECK(tiny.withoutPlainBackground(&removed).rgba == tiny.rgba && !removed);
}

TEST(plain_backdrop_the_real_icon_picture_loses_its_plate) {
    // The brand bust on its rounded icon plate: Tripo built the plate as a slab through the model.
    const std::string path = SF_TEST_DATA_DIR "/../../website/press/sculptforge-bust-1024.png";
    Image img;
    std::string err;
    if (!Image::load(path, img, err)) return; // (a checkout without the press folder)
    bool removed = false;
    const Image out = img.withoutPlainBackground(&removed);
    CHECK(removed);
    CHECK(alphaAt(out, 8, 8) == 0 && alphaAt(out, 512, 10) == 0 && alphaAt(out, 100, 500) == 0); // margin, rim, plate
    CHECK(alphaAt(out, 512, 250) == 255);                                                        // the face
    size_t opaque = 0;
    for (size_t i = 3; i < out.rgba.size(); i += 4) opaque += out.rgba[i] > 128;
    CHECK(double(opaque) / double(out.width * out.height) < 0.40);
}

TEST(prepare_image_cuts_a_plain_backdrop_unless_turned_off) {
    const Image img = plate(300);
    const std::string path = (std::filesystem::temp_directory_path() / "sf_plate_input.png").string();
    std::string err;
    CHECK(img.savePng(path, err));
    std::string withCut, withoutCut;
    CHECK(ai::prepareImagePng(path, withCut, err) && ai::prepareImagePng(path, withoutCut, err, false));
    Image a, b;
    CHECK(Image::decode(reinterpret_cast<const unsigned char*>(withCut.data()), withCut.size(), a, err));
    CHECK(Image::decode(reinterpret_cast<const unsigned char*>(withoutCut.data()), withoutCut.size(), b, err));
    CHECK(a.hasTransparency() && !b.hasTransparency()); // cut: transparent margins around the framed subject; off: the picture as it was
    std::error_code ec;
    std::filesystem::remove(path, ec);
}
