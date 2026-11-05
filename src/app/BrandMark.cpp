// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/BrandMark.h"

#include "core/Image.h"
#include "render/GL.h"

#include <cstdint>
#include <string>

namespace sf {
namespace assets {
extern const unsigned char appIcon[]; // the detailed tile, 256 px
extern const unsigned long long appIcon_size;
extern const unsigned char appIconSmall[]; // the small-size mark, 64 px
extern const unsigned long long appIconSmall_size;
} // namespace assets

namespace brand {
namespace {

struct MarkTexture {
    unsigned tex = 0;
    bool tried = false;
};

// Uploaded once, with mipmaps (the tile is drawn from 48 px to a few hundred).
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

} // namespace

void mark(ImDrawList* dl, ImVec2 at, float size, float alpha) {
    static MarkTexture detailed, small;
    const unsigned tex = size >= 48.0f ? texture(detailed, assets::appIcon, assets::appIcon_size)
                                       : texture(small, assets::appIconSmall, assets::appIconSmall_size);
    if (!tex) return;
    dl->AddImage(ImTextureID(uintptr_t(tex)), at, ImVec2(at.x + size, at.y + size), ImVec2(0, 0), ImVec2(1, 1),
                 ImGui::GetColorU32(ImVec4(1, 1, 1, alpha)));
}

void markItem(float size, float alpha) {
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(size, size));
    mark(ImGui::GetWindowDrawList(), at, size, alpha);
}

} // namespace brand
} // namespace sf
