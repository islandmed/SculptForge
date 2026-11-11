// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/BrandMark.h"

#include "app/Support.h"
#include "core/Image.h"
#include "render/GL.h"

#include <cstdint>
#include <string>

namespace sf {
namespace assets {
extern const unsigned char appIcon[]; // the cube app icon, 256 px
extern const unsigned long long appIcon_size;
extern const unsigned char appIconSmall[]; // its small cut, 32 px
extern const unsigned long long appIconSmall_size;
extern const unsigned char bustMark[]; // the bust illustration, 256 px
extern const unsigned long long bustMark_size;
extern const unsigned char socialIcons[]; // the community icons, 6 x 64 px in a row
extern const unsigned long long socialIcons_size;
} // namespace assets

namespace brand {
namespace {

struct MarkTexture {
    unsigned tex = 0;
    bool tried = false;
};

// Uploaded once, with mipmaps (drawn from about 16 px to a few hundred).
unsigned texture(MarkTexture& t, const unsigned char* bytes, unsigned long long size) {
    if (t.tried) return t.tex;
    t.tried = true;
    Image image;
    std::string err;
    if (!Image::decode(bytes, size_t(size), image, err)) return 0;
    gl::GenTextures(1, &t.tex);
    gl::BindTexture(GL_TEXTURE_2D, t.tex);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GLint(GL_LINEAR_MIPMAP_LINEAR));
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl::PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl::TexImage2D(GL_TEXTURE_2D, 0, GLint(GL_RGBA8), image.width, image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                   image.rgba.data());
    gl::GenerateMipmap(GL_TEXTURE_2D);
    gl::BindTexture(GL_TEXTURE_2D, 0);
    return t.tex;
}

void draw(ImDrawList* dl, unsigned tex, ImVec2 at, float size, float alpha) {
    if (!tex) return;
    dl->AddImage(ImTextureID(uintptr_t(tex)), at, ImVec2(at.x + size, at.y + size), ImVec2(0, 0), ImVec2(1, 1),
                 ImGui::GetColorU32(ImVec4(1, 1, 1, alpha)));
}

void item(void (*paint)(ImDrawList*, ImVec2, float, float), float size, float alpha) {
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(size, size));
    paint(ImGui::GetWindowDrawList(), at, size, alpha);
}

} // namespace

void appMark(ImDrawList* dl, ImVec2 at, float size, float alpha) {
    static MarkTexture full, small;
    draw(dl,
         size > 32.0f ? texture(full, assets::appIcon, assets::appIcon_size)
                      : texture(small, assets::appIconSmall, assets::appIconSmall_size),
         at, size, alpha);
}

void bust(ImDrawList* dl, ImVec2 at, float size, float alpha) {
    static MarkTexture tex;
    draw(dl, texture(tex, assets::bustMark, assets::bustMark_size), at, size, alpha);
}

unsigned imageTexture(const unsigned char* bytes, unsigned long long size) {
    MarkTexture t;
    return texture(t, bytes, size);
}

void socialIcon(ImDrawList* dl, int index, ImVec2 at, float size, ImU32 color) {
    static MarkTexture tex;
    const unsigned t = texture(tex, assets::socialIcons, assets::socialIcons_size);
    if (!t || index < 0 || index >= kSocialCount) return;
    const float u0 = float(index) / float(kSocialCount), u1 = float(index + 1) / float(kSocialCount);
    dl->AddImage(ImTextureID(uintptr_t(t)), at, ImVec2(at.x + size, at.y + size), ImVec2(u0, 0), ImVec2(u1, 1), color);
}

void appMarkItem(float size, float alpha) { item(appMark, size, alpha); }
void bustItem(float size, float alpha) { item(bust, size, alpha); }

} // namespace brand
} // namespace sf
