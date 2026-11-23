// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tool > Scatter (core/Scatter.h): copies of a piece (another Part or a built-in shape) over the active Part, or only its
// masked area. Keep apart relaxes them until none overlap. Scatter makes a new Part (one undo step); while that is the
// last step, changing a setting scatters again in its place (a slider held does it as it moves when that is quick), so
// the settings preview on the model and one undo step stays.
#include "app/App.h"
#include "app/Demo.h"
#include "app/I18n.h"
#include "app/Palettes.h"
#include "app/Ui.h"

#include "core/Scatter.h"

#include <chrono>
#include <string>

namespace sf {

namespace {

constexpr const char* kScatterPath = "Tool > Scatter";
// The built-in pieces (the scatter command's shape=), as the list shows them.
constexpr const char* kShapes[] = {"pebble", "disc", "hex", "sphere", "cube", "cone", "cylinder", "capsule", "torus"};
constexpr const char* kShapeLabels[] = {"Pebble", "Disc", "Hex", "Sphere", "Cube", "Cone", "Cylinder", "Capsule", "Torus"};
constexpr int kShapeCount = int(sizeof kShapes / sizeof kShapes[0]);
// A slider held re-scatters as it moves while one scatter takes less than this; else when it is let go.
constexpr double kLiveMs = 60.0;

} // namespace

void App::runScatter() {
    const int piece = scatterPiece_ ? doc_.partIndex(scatterPiece_) : -1;
    if (scatterPiece_ && (piece < 0 || piece == doc_.activePart)) scatterPiece_ = 0;
    Args a{{"count", double(scatterCount_)},
           {"scale_min", double(scatterScaleMin_)},
           {"scale_max", double(scatterScaleMax_)},
           {"turn", double(scatterTurn_)},
           {"tilt", double(scatterTilt_)},
           {"align", scatterAlign_},
           {"flat", scatterFlat_},
           {"offset", double(scatterOffset_)},
           {"seed", double(scatterSeed_)},
           {"masked", scatterMasked_ && doc_.mesh.hasMask()},
           {"keep_apart", scatterKeepApart_},
           {"spacing", double(scatterSpacing_)},
           {"fit", std::string(scatterTight_ ? "tight" : "round")},
           {"separate", scatterSeparate_}};
    if (scatterPiece_) a["part"] = doc_.parts[size_t(doc_.partIndex(scatterPiece_))].name;
    else a["shape"] = std::string(kShapes[std::clamp(scatterShape_, 0, kShapeCount - 1)]), a["size"] = double(scatterSize_);
    // The last Scatter, still the last step on this surface: this one takes its place.
    const uint32_t surface = doc_.activePartInfo().id;
    const bool replacing = scatterRevision_ && doc_.undo.revision() == scatterRevision_ && scatterTarget_ == surface &&
                           doc_.partIndex(scatterMade_) >= 0;
    if (replacing) runCommand("undo");
    const size_t before = doc_.parts.size();
    const auto t0 = std::chrono::steady_clock::now();
    runCommand("scatter", a);
    scatterMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (!lastCommandOk_ || doc_.parts.size() <= before) {
        // Refused (too many copies for separate Parts, nothing masked...): the scatter it was to replace comes back,
        // still live, and the reason stays on the status line and under the buttons.
        const std::string why = status_;
        if (replacing) {
            runCommand("redo");
            scatterRevision_ = doc_.undo.revision();
        } else {
            scatterRevision_ = 0;
        }
        status_ = scatterResult_ = why;
        return;
    }
    scatterResult_ = status_;
    scatterRevision_ = doc_.undo.revision();
    scatterTarget_ = surface;
    scatterMade_ = doc_.parts[size_t(doc_.activePart + 1)].id; // (it goes right below its surface)
}

void App::drawScatterContents() {
    const float full = ImGui::GetContentRegionAvail().x;
    const float half = (full - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    // While the last Scatter is the last step on the active Part, a changed setting scatters again in its place.
    const bool live = scatterRevision_ && doc_.undo.revision() == scatterRevision_ && scatterTarget_ == doc_.activePartInfo().id &&
                      doc_.partIndex(scatterMade_) >= 0;
    bool redo = false;
    // A slider: let go after a change redoes it; held, it redoes as it moves while that is quick.
    const auto edited = [&] {
        if (ImGui::IsItemDeactivatedAfterEdit() || (ImGui::IsItemEdited() && scatterMs_ < kLiveMs)) redo = true;
    };
    const auto toggled = [&](bool changed) { redo |= changed; };

    tray::GroupLabel(*this, "Piece");
    if (scatterPiece_ && (doc_.partIndex(scatterPiece_) < 0 || doc_.partIndex(scatterPiece_) == doc_.activePart)) scatterPiece_ = 0;
    const int picked = scatterPiece_ ? doc_.partIndex(scatterPiece_) : -1;
    const std::string shown = picked >= 0 ? doc_.parts[size_t(picked)].name
                                          : std::string(tr(kShapeLabels[std::clamp(scatterShape_, 0, kShapeCount - 1)]));
    ImGui::SetNextItemWidth(full);
    if (ImGui::BeginCombo("##scatterpiece", shown.c_str())) {
        for (int k = 0; k < kShapeCount; ++k)
            if (ImGui::Selectable((std::string(tr(kShapeLabels[k])) + "##scshape" + std::to_string(k)).c_str(), picked < 0 && k == scatterShape_))
                scatterPiece_ = 0, scatterShape_ = k, redo = true;
        if (doc_.parts.size() > 1) ImGui::Separator();
        for (int i = 0; i < int(doc_.parts.size()); ++i) {
            if (i == doc_.activePart) continue;
            if (ImGui::Selectable((doc_.parts[size_t(i)].name + "##scpart" + std::to_string(i)).c_str(), i == picked))
                scatterPiece_ = doc_.parts[size_t(i)].id, redo = true;
        }
        ImGui::EndCombo();
    }
    ui::HelpText("Piece", nullptr,
                 "What is scattered over the active Part: a built-in shape (a pebble, a cube...) or another Part, such as "
                 "a scale, a stud or a leaf you made. Its copies go on the active Part.",
                 kScatterPath);
    if (picked < 0) {
        ImGui::SetNextItemWidth(full);
        ui::SliderInside("##scattersize", &scatterSize_, 0.005f, 0.3f, tr("Shape size %.3f"), ImGuiSliderFlags_Logarithmic);
        edited();
        ui::HelpText("Shape size", nullptr, "The built-in shape's size, as a fraction of the active Part's size.", kScatterPath);
    }

    tray::GroupLabel(*this, "Copies");
    ImGui::SetNextItemWidth(full);
    tray::SliderInt("##scattercount", &scatterCount_, 1, 10000, tr("Count %d"), ImGuiSliderFlags_Logarithmic);
    edited();
    ui::HelpText("Count", nullptr, "How many copies to scatter.", kScatterPath);
    ImGui::SetNextItemWidth(half);
    ui::SliderInside("##scatterscalemin", &scatterScaleMin_, 0.1f, 3.0f, tr("Size from %.2f"));
    if (scatterScaleMax_ < scatterScaleMin_) scatterScaleMax_ = scatterScaleMin_;
    edited();
    ui::HelpText("Size from", nullptr, "Each copy gets a random size between these two (1 = the piece's own size).", kScatterPath);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ui::SliderInside("##scatterscalemax", &scatterScaleMax_, 0.1f, 3.0f, tr("to %.2f"));
    if (scatterScaleMin_ > scatterScaleMax_) scatterScaleMin_ = scatterScaleMax_;
    edited();
    ui::HelpText("Size to", nullptr, "Each copy gets a random size between these two (1 = the piece's own size).", kScatterPath);
    ImGui::SetNextItemWidth(half);
    ui::SliderInside("##scatterturn", &scatterTurn_, 0.0f, 180.0f, tr("Turn %.0f\xc2\xb0"));
    edited();
    ui::HelpText("Random turn", nullptr,
                 "Each copy is turned about its up side by a random angle up to this either way (180: any way round).", kScatterPath);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ui::SliderInside("##scattertilt", &scatterTilt_, 0.0f, 90.0f, tr("Tilt %.0f\xc2\xb0"));
    edited();
    ui::HelpText("Random tilt", nullptr, "Each copy leans off the surface by a random angle up to this.", kScatterPath);
    ImGui::SetNextItemWidth(half);
    ui::SliderInside("##scatteroffset", &scatterOffset_, -1.0f, 1.0f, tr("Height %.2f"));
    edited();
    ui::HelpText("Height", nullptr,
                 "Where the copies sit, in their own heights along the surface: 0 is half sunk in it, 0.5 resting on it, "
                 "below 0 deeper in.",
                 kScatterPath);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    tray::SliderInt("##scatterseed", &scatterSeed_, 1, 999, tr("Seed %d"));
    edited();
    ui::HelpText("Seed", nullptr, "Another seed gives another random scatter with the same settings.", kScatterPath);
    toggled(ui::Checkbox(tr("Follow the surface##scatter_align"), &scatterAlign_));
    ui::HelpText("Follow the surface", nullptr,
                 "Each copy stands along the surface where it sits (its up side along the surface's normal). Off: every "
                 "copy stays upright, as the piece was made.",
                 kScatterPath);
    toggled(ui::Checkbox(tr("Lie flat##scatter_flat"), &scatterFlat_));
    ui::HelpText("Lie flat", nullptr,
                 "The piece's thinnest side faces out of the surface, so scales, coins and leaves lie flat. Off: its own up "
                 "side (+Y) does.",
                 kScatterPath);
    ImGui::BeginDisabled(!doc_.mesh.hasMask());
    toggled(ui::Checkbox(tr("Only the masked area##scatter_masked"), &scatterMasked_));
    ImGui::EndDisabled();
    ui::HelpText("Only the masked area", nullptr,
                 "Scatter only where the active Part is masked (mask it first: Ctrl+drag), not over all of it.", kScatterPath);

    tray::GroupLabel(*this, "Keep apart");
    toggled(ui::Checkbox(tr("Keep apart##scatter_keep"), &scatterKeepApart_));
    ui::HelpText("Keep apart", nullptr,
                 "Slides the copies over the surface until no two overlap or touch. Copies that still have no room are "
                 "left out (the status line says how many): fewer or smaller copies, or less spacing, fit more.",
                 kScatterPath);
    ImGui::BeginDisabled(!scatterKeepApart_);
    ImGui::SetNextItemWidth(full);
    ui::SliderInside("##scatterspacing", &scatterSpacing_, 0.0f, 2.0f, tr("Spacing %.2f"));
    edited();
    ui::HelpText("Spacing", nullptr, "The gap kept between copies, in copy sizes: 0 lets them just touch.", kScatterPath);
    toggled(ui::Checkbox(tr("Tight fit##scatter_tight"), &scatterTight_));
    ui::HelpText("Tight fit", nullptr,
                 "Keeps the copies' boxes apart, so long or flat pieces pack close. Off: a round space around each copy "
                 "(looser, more even gaps).",
                 kScatterPath);
    ImGui::EndDisabled();

    tray::GroupLabel(*this, "Result");
    // One Part per copy goes up to kScatterMaxSeparate copies: above that the box can't be ticked.
    if (scatterCount_ > kScatterMaxSeparate) scatterSeparate_ = false;
    ImGui::BeginDisabled(scatterCount_ > kScatterMaxSeparate);
    toggled(ui::Checkbox(tr("Separate Parts##scatter_separate"), &scatterSeparate_));
    ImGui::EndDisabled();
    ui::HelpText("Separate Parts", nullptr,
                 "Each copy becomes its own Part (up to 500). Off: all of them are one Part, each copy its own face group.",
                 kScatterPath);
    if (ImGui::Button(tr("Scatter##scatter_run"), ImVec2(live ? half : full, 0))) {
        scatterRevision_ = 0; // a new Scatter, beside the last one
        runScatter();
    }
    ui::HelpText("Scatter", nullptr,
                 "Scatters the copies over the active Part as a new Part (one undo step). Until you do something else, "
                 "changing a setting scatters them again in its place.",
                 kScatterPath);
    if (live) {
        ImGui::SameLine();
        if (ImGui::Button(tr("Keep it##scatter_keep_result"), ImVec2(-1, 0))) scatterRevision_ = 0;
        ui::HelpText("Keep it", nullptr, "Keeps this Scatter as it is: the settings no longer change it.", kScatterPath);
    }
    // What the last Scatter did (how many, how many left out, how long), in full: the status line cuts it short.
    if (!scatterResult_.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", scatterResult_.c_str());
        ImGui::PopTextWrapPos();
    }
    if (redo && live) runScatter();
}

} // namespace sf
