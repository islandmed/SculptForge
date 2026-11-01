// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The UV Editor's 2D view (app/PanelUvEditor.cpp), drawn on the GPU into an offscreen texture the window shows:
// the UDIM tile grid with a checker or the painted texture behind, every UV triangle filled with its colour
// (stretch, texel density, islands, the selection) and its edges on top, antialiased. The UVs, colours and edges
// are uploaded only when they change; a drag moves the selection with a matrix in the vertex shader, so even a
// million triangles move without re-sending anything.
#pragma once

#include "render/GL.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace sf {

class UvViewRenderer {
public:
    bool init(std::string& error);
    void shutdown();

    // Per triangle corner (Mesh::uvs layout).
    void setUvs(const std::vector<glm::vec2>& uvs);
    // Per corner, RGBA8 (0xAABBGGRR): the fill of the corner's triangle, the colour of the edge that starts at the
    // corner, and whether the corner follows the drag matrix.
    void setColors(const std::vector<uint32_t>& face, const std::vector<uint32_t>& edge, const std::vector<uint8_t>& moving);
    // The edges to draw, as corners (each draws the edge from it to the next corner of its triangle).
    void setEdges(const std::vector<uint32_t>& corners);

    struct View {
        glm::vec2 min{0.0f}, max{1.0f};  // the UV rectangle shown
        int imageMode = 1;               // 0 nothing, 1 checker, 2 the texture (`image`)
        GLuint image = 0;                // the painted texture (sRGB, rows bottom-up)
        float imageOpacity = 0.6f;
        float checkerScale = 8.0f;       // squares per UV unit
        glm::ivec2 tiles{1, 1};          // UDIM tiles in use, from 1001 (columns, rows)
        glm::mat3 move{1.0f};            // applied to the moving corners
        bool fill = true;
        bool edges = true;
    };
    // Draws `view` into a `w` x `h` texture and returns it (0 when the GPU can't). The caller's framebuffer and
    // viewport are restored.
    GLuint draw(int w, int h, const View& view);
    size_t triangles() const { return corners_ / 3; }

private:
    GLuint bgProgram_ = 0, meshProgram_ = 0;
    GLuint vao_ = 0, emptyVao_ = 0, uvBuffer_ = 0, faceBuffer_ = 0, edgeBuffer_ = 0, moveBuffer_ = 0, indexBuffer_ = 0;
    GLuint msFbo_ = 0, msColor_ = 0, fbo_ = 0, texture_ = 0;
    int width_ = 0, height_ = 0, samples_ = 4;
    size_t corners_ = 0, edgeCount_ = 0;
    bool colorsValid_ = false;
    GLint bgMin_ = -1, bgMax_ = -1, bgMode_ = -1, bgImage_ = -1, bgOpacity_ = -1, bgChecker_ = -1, bgPixel_ = -1, bgTiles_ = -1;
    GLint mMin_ = -1, mMax_ = -1, mMove_ = -1, mLines_ = -1;
    bool ensureTarget(int w, int h);
};

} // namespace sf
