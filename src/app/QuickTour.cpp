// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Help > Quick tour: ten short slides for sculptors who already know another app, each with a screenshot of
// SculptForge (docs/guide/images/quick-tour, made by tests/ui/quick_tour.sfui) and a "Show me" that opens the
// panel and flashes it. The same text is the guide chapter quick-tour.md. Offered in the welcome, in the Help menu
// and once when the Classic sculpting keys are picked; the last slide is about memberships and never comes back
// by itself.
#include "app/App.h"
#include "app/Support.h"
#include "app/Theme.h"

#include <IconsLucide.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>

namespace sf {

namespace {

struct TourSlide {
    const char* title;
    const char* image;   // a guide image (UserGuide::image)
    const char* text;
    const char* section; // a Tool sub-palette to open and flash, or nullptr
    const char* action;  // the action "Show me" runs (opens the palette, the search or the Library); "@Label": the
                         // action with that label in the palette of the same name
};

// Keep in step with docs/guide/quick-tour.md.
constexpr TourSlide kSlides[] = {
    {"Where things are", "images/quick-tour/01-where.jpg",
     "The menus along the top are sorted by task: Sculpt (brushes, strokes, alphas, symmetry), Paint, Mesh, View and "
     "Render. The right tray holds the Tool palette: Parts, Geometry, Remesh, Masking, Cut, Bake and more. The shelves "
     "around the view hold the brush, its size and strength, and the brush, stroke, alpha and material tiles. Lost? "
     "Press F3 and type what you are after in your own words: remesh, retopo, groups, trim, multires.",
     nullptr, "ui.command_palette"},
    {"Voxel Remesh", "images/quick-tour/02-voxel.jpg",
     "A fast remesh for blocking out. Pull shapes far out or merge pieces, then rebuild one even surface: open "
     "Tool > Remesh in the right tray, set Resolution and click Voxel Remesh. A higher resolution keeps more detail. "
     "The mask and vertex paint carry over, so you can keep going.",
     "tool.remesh", "ui.palette.tool"},
    {"Dynamic topology", "images/quick-tour/03-dynamic.jpg",
     "Dynamic topology adds detail where you sculpt. Turn on Dynamic on the top shelf and the brush adds triangles "
     "where it needs them and removes them where it doesn't; Detail sets how small they get. Pull horns, ears or "
     "fingers out of a ball without remeshing. Wire (Shift+F) shows the new triangles.",
     nullptr, "ui.palette.draw"},
    {"Quad Remesh", "images/quick-tour/04-quad.jpg",
     "Automatic quad retopology: Tool > Remesh > Quad Remesh rebuilds a sculpt as clean quads whose edge loops follow "
     "the forms. Set Target Polygons and leave Keep Original on. Divide the result, then Project Detail (Tool > Parts) "
     "brings the detail back. Draw Quad Remesh Guides to steer the loops.",
     "tool.remesh", "ui.palette.tool"},
    {"Levels, Parts and Face Groups", "images/quick-tour/05-levels.jpg",
     "Ctrl+D divides and keeps the lower levels: Shift+D steps down, D steps up, and you can sculpt at any level "
     "(Tool > Geometry). A scene holds several Parts (Tool > Parts); Alt+click one in the view to work on it. Face "
     "Groups split a Part into coloured regions to hide, isolate or mask (Tool > Face Groups).",
     "tool.geometry", "ui.palette.tool"},
    {"Masking", "images/quick-tour/06-masking.jpg",
     "Hold Ctrl and paint to mask, Ctrl+Alt to unmask. Tool > Masking inverts, blurs and grows the mask, and builds "
     "one By cavity, By face group or along the border. Press G for the stroke popup: its Shapes row picks Mask Pen, "
     "Lasso Mask or Rectangle Mask for the next Ctrl stroke.",
     "tool.masking", "ui.palette.tool"},
    {"Cut, Clip and Close Holes", "images/quick-tour/07-cut.jpg",
     "Pick a shape in Tool > Cut and draw it with Ctrl+Shift: a line, lasso, rectangle or polyline goes straight "
     "through the whole Part, and a closed Part stays closed. Cut it away, cut in, add, or Clip: press everything "
     "outside the shape flat onto its outline. Close Holes (Tool > Geometry) caps every open boundary.",
     "tool.cut", "ui.palette.tool"},
    {"Brushes, alphas and the Library", "images/quick-tour/08-library.jpg",
     "B opens the brush picker; type the first letters of a name to pick one. The Alpha tile picks the stamp shape. "
     "The Library (comma) holds starter meshes, brushes, alphas, Lit Spheres and snapshots. A pack (.sfpack) brings "
     "brushes, alphas and Lit Spheres in one file; File > Install Pack shows what is inside before anything is "
     "installed.",
     nullptr, "@Library"},
    {"Baking and texturing", "images/quick-tour/09-bake.jpg",
     "Tool > Bake makes normal, displacement, ambient occlusion, cavity and colour maps from your sculpt onto a "
     "low-poly Part with UVs, in the background. P picks the Paint brush for vertex paint; New Texture on the top "
     "shelf paints into a texture instead (Auto UV runs first), with paint layers and material channels.",
     "tool.bake", "ui.palette.tool"},
    {"SculptForge stays free", "images/wn-first-sculpt.jpg",
     "Everything in this tour is free, and stays free. If SculptForge helps your work, a membership keeps it moving: "
     "members get betas and early builds, vote on what we build next and ask for the brushes and alphas they want.",
     nullptr, nullptr},
};
constexpr int kSlideCount = int(std::size(kSlides));

} // namespace

int App::quickTourSlides() { return kSlideCount; }

void App::showQuickTour(int slide) {
    quickTourOpen_ = true;
    quickTourAside_ = false;
    quickTourOffer_ = false;
    quickTourSlide_ = std::clamp(slide, 0, kSlideCount - 1);
    showWelcome_ = false;
    if (!quickTourDone_) {
        quickTourDone_ = true; // offered once: from now on only the welcome and the Help menu open it
        saveProjectPrefs();
    }
}

void App::offerQuickTour() {
    if (uiConfigDeterministic_ && !quickTourOffersInTests_) return; // (UiTest.cpp: quick_tour_offers)
    if (!quickTourDone_ && !quickTourOpen_) quickTourOffer_ = true;
}

void App::closeQuickTour() {
    quickTourOpen_ = quickTourAside_ = false;
}

void App::quickTourShowMe(int slide) {
    const TourSlide& s = kSlides[std::clamp(slide, 0, kSlideCount - 1)];
    if (!s.action) return;
    quickTourAside_ = true; // out of the way: a small "Back to the tour" bar instead
    if (s.section) {
        auto& open = layout_.openSubpalettes;
        if (uiPrefs_.oneSubpalette) // as a click on its header would: the other Tool sub-palettes close
            open.erase(std::remove_if(open.begin(), open.end(), [](const std::string& o) { return o.rfind("tool.", 0) == 0; }),
                       open.end());
        if (std::find(open.begin(), open.end(), s.section) == open.end()) open.push_back(s.section);
        flashSubpalette_ = s.section;
        flashSubpaletteStart_ = ImGui::GetTime();
    }
    const Action* a = actions_.find(s.action);
    if (s.action[0] == '@')
        for (const Action& other : actions_.all())
            if (other.label == s.action + 1 && other.palette == s.action + 1) a = &other;
    if (a && a->run) a->run();
    status_ = std::string("Quick tour: ") + s.title;
}

void App::drawQuickTour() {
    const float s = uiScale_;
    const CanvasRect c = canvasRect();
    if (quickTourOffer_ && !quickTourOpen_ && !showWelcome_) {
        // Once, when the Classic sculpting keys are picked: a quiet note along the bottom, no focus taken.
        ImGui::SetNextWindowPos(ImVec2(c.x + c.w * 0.5f, c.y + c.h - 12.0f * s), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
        ImGui::SetNextWindowBgAlpha(0.94f);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                       ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
        if (ImGui::Begin("##quicktour_offer", nullptr, flags)) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("New to SculptForge? A two-minute tour shows where everything is.");
            ImGui::SameLine();
            if (ImGui::SmallButton("Take the tour##quicktour.offer")) showQuickTour(0);
            ImGui::SameLine();
            if (ImGui::SmallButton("No thanks##quicktour.offer")) {
                quickTourOffer_ = false;
                quickTourDone_ = true;
                saveProjectPrefs();
            }
        }
        ImGui::End();
    }
    if (!flashSubpalette_.empty() && ImGui::GetTime() - flashSubpaletteStart_ < 1.6) redrawIn(0.03); // the flash fades
    if (!quickTourOpen_) return;
    const int i = std::clamp(quickTourSlide_, 0, kSlideCount - 1);
    const TourSlide& slide = kSlides[i];
    const bool last = i == kSlideCount - 1;
    if (quickTourAside_) {
        // "Show me" opened a panel: the tour waits as a small bar along the bottom of the view.
        ImGui::SetNextWindowPos(ImVec2(c.x + c.w * 0.5f, c.y + c.h - 12.0f * s), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
        ImGui::SetNextWindowBgAlpha(0.94f);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                       ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
        if (ImGui::Begin("##quicktour_aside", nullptr, flags)) {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Quick tour %d / %d: %s", i + 1, kSlideCount, slide.title);
            ImGui::SameLine();
            if (ImGui::SmallButton("Back to the tour##quicktour")) quickTourAside_ = false;
            ImGui::SameLine();
            if (ImGui::SmallButton("Next##quicktour.aside")) {
                quickTourSlide_ = std::min(i + 1, kSlideCount - 1);
                quickTourAside_ = false;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Close##quicktour.aside")) closeQuickTour();
        }
        ImGui::End();
        return;
    }
    const float w = std::min(680.0f * s, c.w - 32.0f * s);
    // The top stays put and every slide is as tall (a fixed picture band and text box), so Next doesn't move.
    ImGui::SetNextWindowPos(ImVec2(c.x + c.w * 0.5f, c.y + std::max(16.0f * s, c.h * 0.08f)), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(w, 0.0f), ImGuiCond_Always);
    bool open = true;
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize;
    if (ImGui::Begin("New to SculptForge? A quick tour##quicktour", &open, flags)) {
        // The picture: a screenshot of the part of SculptForge the slide is about, in a band of one height.
        const float iw = ImGui::GetContentRegionAvail().x, band = std::floor(std::min(iw * 0.577f, 360.0f * s));
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImVec2 size;
        if (const ImTextureID tex = guide_.image(slide.image, &size); tex && size.x > 0.0f && size.y > 0.0f) {
            const float k = std::min(iw / size.x, band / size.y);
            const ImVec2 shown(std::floor(size.x * k), std::floor(size.y * k));
            const ImVec2 a(std::floor(at.x + (iw - shown.x) * 0.5f), std::floor(at.y + (band - shown.y) * 0.5f));
            ImGui::GetWindowDrawList()->AddImageRounded(tex, a, ImVec2(a.x + shown.x, a.y + shown.y), ImVec2(0, 0), ImVec2(1, 1),
                                                        IM_COL32_WHITE, theme::rounding(4.0f * s));
        }
        ImGui::Dummy(ImVec2(iw, band));
        ImGui::Spacing();
        ImGui::TextDisabled("%d / %d", i + 1, kSlideCount);
        ImGui::SameLine();
        if (fontBold_) ImGui::PushFont(fontBold_);
        ImGui::TextUnformatted(slide.title);
        if (fontBold_) ImGui::PopFont();
        ImGui::Spacing();
        const float textTop = ImGui::GetCursorPosY();
        ImGui::TextWrapped("%s", slide.text);
        if (last) {
            ImGui::Spacing();
            ImGui::TextDisabled("%s", kSupportUrl + 8); // "sculptforge.app/support.html"
        }
        const float box = 5.0f * ImGui::GetTextLineHeightWithSpacing(); // the longest slide's lines at this width
        if (ImGui::GetCursorPosY() < textTop + box) ImGui::Dummy(ImVec2(0, textTop + box - ImGui::GetCursorPosY()));
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        const ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
        if (last) {
            ImGui::PushStyleColor(ImGuiCol_Button, accent);
            if (ImGui::Button(ICON_LC_HEART "  See memberships##quicktour")) openSupportPage();
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Opens sculptforge.app/support.html in your browser.");
            ImGui::SameLine();
            if (ImGui::Button("Maybe later##quicktour")) closeQuickTour(); // and it never comes back by itself
        } else if (slide.action) {
            ImGui::PushStyleColor(ImGuiCol_Button, accent);
            if (ImGui::Button(ICON_LC_EYE "  Show me##quicktour")) quickTourShowMe(i);
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Opens it in SculptForge; the tour waits at the bottom.");
        }
        // Back and Next at the right.
        const ImGuiStyle& st = ImGui::GetStyle();
        const char* next = last ? "Done##quicktour" : "Next##quicktour";
        const float bw = ImGui::CalcTextSize("Back").x + ImGui::CalcTextSize(last ? "Done" : "Next").x + 4.0f * st.FramePadding.x +
                         st.ItemSpacing.x;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - bw));
        ImGui::BeginDisabled(i == 0);
        if (ImGui::Button("Back##quicktour")) quickTourSlide_ = i - 1;
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(next)) {
            if (last) closeQuickTour();
            else quickTourSlide_ = i + 1;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false) && ImGui::IsWindowFocused() && !last) quickTourSlide_ = i + 1;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false) && ImGui::IsWindowFocused() && i > 0) quickTourSlide_ = i - 1;
    }
    ImGui::End();
    if (!open) closeQuickTour();
}

} // namespace sf
