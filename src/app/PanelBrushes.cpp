// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Brush options panel (stroke type, lazy mouse, spray, alpha) and the
// stroke handling of the SnakeHook brush and the DragRect stroke.
#include "app/Theme.h"
#include "app/App.h"
#include "app/I18n.h"
#include "app/Ui.h"

#include <iterator>

#include <algorithm>
#include <cmath>

namespace sf {


void SDLCALL App::onAlphaChosen(void* userdata, const char* const* files, int) {
    wakeMainLoop(); // a file dialog answered (maybe on another thread): redraw now
    if (!files || !files[0]) return;
    auto* app = static_cast<App*>(userdata);
    std::lock_guard lock(app->alphaMutex_);
    app->pendingAlphaPath_ = files[0];
}

glm::vec3 App::screenUpVector(float angle) const {
    const glm::quat q = camera_.orientation();
    return q * glm::vec3(-std::sin(angle), std::cos(angle), 0.0f);
}

void App::pollAlphaFile() {
    std::string alphaPath;
    {
        std::lock_guard lock(alphaMutex_);
        alphaPath.swap(pendingAlphaPath_);
    }
    if (!alphaPath.empty()) runCommand("brush_set_alpha", {{"path", alphaPath}, {"invert", alphaInvert_}});
}

// Alpha palette: the stamp that shapes the brush (alpha).
void App::drawAlphaSection() {
    const BrushAlpha& alpha = doc_.brushAlpha;
    const float s = uiScale_;
    auto preview = [](ImDrawList* dl, const BrushAlpha& a, ImVec2 p0, float size, int cells) {
        const float cell = size / float(cells);
        dl->AddRectFilled(p0, ImVec2(p0.x + size, p0.y + size), IM_COL32(0, 0, 0, 255), theme::rounding(3.0f));
        for (int y = 0; y < cells; ++y)
            for (int x = 0; x < cells; ++x) {
                const float u = (float(x) + 0.5f) / float(cells) * 2.0f - 1.0f, v = 1.0f - (float(y) + 0.5f) / float(cells) * 2.0f;
                const int g = int(std::clamp(a.sample(u, v), 0.0f, 1.0f) * 255.0f);
                if (g < 3) continue;
                dl->AddRectFilled(ImVec2(p0.x + float(x) * cell, p0.y + float(y) * cell),
                                  ImVec2(p0.x + float(x + 1) * cell + 0.5f, p0.y + float(y + 1) * cell + 0.5f), IM_COL32(g, g, g, 255));
            }
    };
    // Large preview of the current alpha, with its name.
    const float big = std::min(128.0f * s, ImGui::GetContentRegionAvail().x);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - big) * 0.5f);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(big, big));
    if (ImTextureID tex = alpha.tiled() ? ImTextureID{} : thumbs_.alpha(alpha)) // (a tiled alpha: drawn as it stamps)
        ImGui::GetWindowDrawList()->AddImage(tex, p0, ImVec2(p0.x + big, p0.y + big));
    else
        preview(ImGui::GetWindowDrawList(), alpha, p0, big, 40);
    const ImVec2 nameSize = ImGui::CalcTextSize(alpha.name.c_str());
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - nameSize.x) * 0.5f);
    ImGui::TextUnformatted(alpha.name.c_str());

    drawAlphaOptions();
    // Built-in and library alphas, then Load image... and Invert loaded image (Pickers.cpp), as in the alpha picker.
    drawAlphaGrid(*this, GridMode::Palette);
}

// Alpha options (#76): how the alpha turns and repeats on each dab, kept per brush (#78).
void App::drawAlphaOptions(bool vdm) {
    AlphaOptions& o = alphaOptions_;
    const char* where = vdm ? "Brush > Vector Displacement" : "Alpha";
    if (vdm) {
        ImGui::SeparatorText(tr("Stamp rotation"));
        ui::Checkbox("Follow stroke direction##vdm.follow_stroke", &o.followStroke);
        ui::HelpText("Follow stroke direction", nullptr,
                     "The stamp turns with you: with Drag Stamp its top points the way you drag; in a dots stroke it "
                     "turns along the stroke. Off, Drag Stamp only sets the size and the stamp keeps Angle's turn. "
                     "Slide Stamp never turns while you slide it.",
                     where);
    } else {
        ui::Checkbox("Follow stroke direction##alpha.follow_stroke", &o.followStroke);
        ui::HelpText("Follow stroke direction", nullptr,
                     "The alpha turns with the stroke: its top points the way you are drawing, for scales, stitches "
                     "and hair that line up along the stroke. Small wobbles are smoothed out (the direction is taken "
                     "every 20 pixels, as Blender's Rake does). Angle still adds its turn.",
                     where);
    }
    ImGui::SetNextItemWidth(-1);
    ui::SliderInside(vdm ? "##vdm_angle" : "##alpha_angle", &o.angle, -180.0f, 180.0f, "Angle %.0f");
    ui::HelpText("Angle", nullptr,
                 vdm ? "Turn the stamp by a fixed angle in degrees (counter-clockwise), on top of the drag's turn when "
                       "Follow stroke direction is on; Ctrl+click to type."
                     : "Turn the alpha by a fixed angle in degrees (counter-clockwise); Ctrl+click to type.",
                 where);
    ImGui::SetNextItemWidth(-1);
    ui::SliderInside(vdm ? "##vdm_angle_jitter" : "##alpha_angle_jitter", &o.jitter, 0.0f, 180.0f, "Angle jitter %.0f");
    ui::HelpText("Angle jitter", nullptr,
                 vdm ? "Turn each stamp by a random angle, up to this many degrees either way: scattered horns, "
                       "scales or rivets that don't all face the same way."
                     : "Turn each dab by a random angle, up to this many degrees either way: breaks up repeats in "
                       "skin, rock and noise alphas.",
                 where);
    if (vdm) {
        ImGui::Dummy(ImVec2(0, 2.0f * uiScale_));
        return;
    }
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    ImGui::SetNextItemWidth(half);
    float tileX = float(o.tileX), tileY = float(o.tileY);
    if (ui::SliderInside("##alpha_tile_x", &tileX, 1.0f, 8.0f, "Tile across %.0f")) o.tileX = int(std::lround(tileX));
    ui::HelpText("Tile across", nullptr, "Repeat the alpha this many times across the brush (1 = once).", "Alpha");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    if (ui::SliderInside("##alpha_tile_y", &tileY, 1.0f, 8.0f, "Tile down %.0f")) o.tileY = int(std::lround(tileY));
    ui::HelpText("Tile down", nullptr, "Repeat the alpha this many times down the brush (1 = once).", "Alpha");
    syncAlphaTile();
    ImGui::Dummy(ImVec2(0, 2.0f * uiScale_));
}

// SnakeHook: pull the region under the click along with the cursor, like Move,
// but re-centred on the pulled tip every step so it can be dragged out into horns.
void App::snakeHookTo(float x, float y) {
    const float h = canvasHeight();
    glm::vec3 origin, dir;
    canvasRay(x, y, origin, dir);
    const glm::vec3 n = camera_.forward();
    const float denom = glm::dot(dir, n);
    if (std::abs(denom) < 1e-6f) return;
    // Along the normal (App::beginAlongNormal): the tip slides out along the normal at the press.
    const glm::vec3 target = strokeAlongNormal_ ? alongNormalTarget(x, y) : origin + dir * (glm::dot(grabLast_ - origin, n) / denom);

    const float pressure = strokePressure();
    DabParams p;
    p.viewDir = n;
    p.radius = radiusPx_ * (pressureSizeOn() ? std::max(pressure, 0.05f) : 1.0f) * camera_.worldPerPixel(grabLast_, h);
    p.strength = strength_;
    p.pressure = pressureStrengthOn() ? pressure : 1.0f;
    p.invert = strokeInvert_;
    p.focal = focalShift_;
    p.connectedOnly = uiPrefs_.connectedOnly;
    p.viewFalloff = uiPrefs_.viewFalloff;
    p.alpha = &doc_.brushAlpha;
    p.alphaUp = screenUpVector(0.0f);
    // Small steps keep the tip under the cursor and the pulled surface even.
    const glm::vec3 delta = target - grabLast_;
    const int steps = std::clamp(int(std::ceil(glm::length(delta) / (p.radius * 0.15f))), 1, 64);
    p.strokeDelta = delta / float(steps);
    for (int i = 0; i < steps; ++i) {
        p.center = grabLast_;
        doc_.sculptor.dab(doc_.mesh, doc_.bvh, strokeBrush_, p, symmetry_); // SnakeHook or KelvinSnake
        grabLast_ += p.strokeDelta;
    }
}

// DragRect: undo the previous stamp and press a new one sized and rotated by the drag (without remeshing:
// restoreStroke puts positions back, not topology).
void App::dragRectTo(float x, float y) {
    doc_.sculptor.restoreStroke(doc_.mesh, doc_.bvh);
    const glm::vec2 drag = glm::vec2(x, y) - dragAnchor_;
    const float len = glm::length(drag);
    if (len < 2.0f) return;
    const glm::quat q = camera_.orientation();
    const float pressure = strokePressure();

    DabParams p;
    p.center = dragAnchorHit_;
    p.viewDir = camera_.forward();
    p.radius = len * camera_.worldPerPixel(dragAnchorHit_, canvasHeight());
    p.strength = strength_;
    p.pressure = pressureStrengthOn() ? pressure : 1.0f;
    p.invert = strokeInvert_;
    p.frontFacesOnly = frontFacesOnly_;
    p.connectedOnly = uiPrefs_.connectedOnly;
    p.viewFalloff = uiPrefs_.viewFalloff;
    p.focal = focalShift_;
    p.alpha = &doc_.brushAlpha;
    // The stamp's top follows the drag (a VDM's only with Follow stroke direction, #136), turned by Angle and jitter.
    const bool follow = strokeBrush_ != BrushType::Vdm || alphaOptions_.followStroke;
    const float a = alphaStampAngle(alphaOptions_, follow, drag, stampJitter_);
    lastDabAngle_ = a;
    p.alphaUp = q * glm::vec3(-std::sin(a), std::cos(a), 0.0f); // (screenUpVector)
    p.vdm = doc_.brushVdm.get();
    if (strokePaint_) paintAt(p); // restoreStroke reverted the previous stamp's colours too
    if (!strokeSculpt_) return;
    const BrushRecipe* recipe = strokeRecipe();
    if (recipe) doc_.sculptor.dabRecipe(doc_.mesh, doc_.bvh, *recipe, p, symmetry_);
    else doc_.sculptor.dab(doc_.mesh, doc_.bvh, strokeBrush_, p, symmetry_);
}

} // namespace sf
