// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tool > Reshape > Stack (docs/design/deform-panel.md, foirgairwolf on Discord): a list of whole-Part shape operations
// (core/Deform.h) to stack up, each with its amount, axes, direction and Soft; the preview is the stack really
// applied as one undo step (deform_stack), redone after each change while it is still the last step; Apply keeps
// it, Apply Selected keeps one step, Reset takes it back.
#include "app/App.h"
#include "app/Demo.h"
#include "app/I18n.h"
#include "app/Palettes.h"
#include "app/Ui.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace sf {

namespace {

// The add menu's groups, in our order.
struct OpGroup {
    const char* name;
    std::initializer_list<DeformOp> ops;
};
const OpGroup kGroups[] = {
    {"Transform", {DeformOp::Move, DeformOp::Turn, DeformOp::Scale, DeformOp::Squash}},
    {"Bend & Twist", {DeformOp::Bend, DeformOp::Twist, DeformOp::Taper, DeformOp::Shear}},
    {"Shape", {DeformOp::Round, DeformOp::Box, DeformOp::Flatten, DeformOp::Sag, DeformOp::Depth}},
    {"Surface", {DeformOp::Polish, DeformOp::Relax, DeformOp::Smooth, DeformOp::Balloon}},
};

// Above this many vertices the preview follows a slider when it is let go, not while it moves.
constexpr size_t kLiveVertices = 1000000;

// "Bend 45 Y Soft" for the list.
std::string stepLabel(const DeformStep& st) {
    char amount[32];
    std::snprintf(amount, sizeof amount, "%.0f", double(st.options.amount));
    std::string s = std::string(tr(deformName(st.op))) + " " + amount;
    if (deformUsesAxes(st.op)) {
        std::string axes = axesLetters(st.options.axes);
        for (char& c : axes) c = char(c - 'a' + 'A');
        s += " " + axes;
    }
    if (st.options.soft && deformUsesSoft(st.op)) s += std::string(" ") + tr("Soft");
    return s;
}

} // namespace

bool App::deformStackUndoPreview() {
    DeformStackUi& ds = deformStack_;
    if (!ds.previewed) return false;
    ds.previewed = false;
    // Only while it is still the last step on the same Part: otherwise it is simply kept.
    if (doc_.undo.revision() != ds.revision || doc_.activePartInfo().id != ds.part) return false;
    runCommand("undo");
    return true;
}

void App::deformStackRun(const std::vector<DeformStep>& steps) {
    if (steps.empty()) return;
    Args a{{"ops", deformStackText(steps)}};
    if (gizmo_.mode != GizmoMode::Off && gizmo_.pivotPlaced) { // the gizmo's pivot, when it was placed
        char p[96];
        std::snprintf(p, sizeof p, "%g,%g,%g", double(gizmo_.pivot.x), double(gizmo_.pivot.y), double(gizmo_.pivot.z));
        a["pivot"] = std::string(p);
    }
    runCommand("deform_stack", a);
}

void App::deformStackRefresh() {
    DeformStackUi& ds = deformStack_;
    deformStackUndoPreview();
    if (!ds.preview) return;
    std::vector<DeformStep> on;
    for (size_t i = 0; i < ds.steps.size(); ++i)
        if (ds.on[i]) on.push_back(ds.steps[i]);
    if (on.empty()) return;
    const uint64_t before = doc_.undo.revision();
    deformStackRun(on);
    if (!lastCommandOk_ || doc_.undo.revision() == before) return;
    ds.previewed = true;
    ds.revision = doc_.undo.revision();
    ds.part = doc_.activePartInfo().id;
}

void App::deformStackApply(bool selectedOnly) {
    DeformStackUi& ds = deformStack_;
    if (selectedOnly) {
        if (ds.selected < 0 || ds.selected >= int(ds.steps.size())) return;
        deformStackUndoPreview();
        deformStackRun({ds.steps[size_t(ds.selected)]});
        ds.steps.erase(ds.steps.begin() + ds.selected);
        ds.on.erase(ds.on.begin() + ds.selected);
        ds.selected = std::min(ds.selected, int(ds.steps.size()) - 1);
        deformStackRefresh(); // the rest preview again on top
        return;
    }
    // The preview (current or not) is already on the Part: it stays. Without one, run the stack now.
    const bool kept = ds.previewed;
    ds.previewed = false;
    if (!kept) {
        std::vector<DeformStep> on;
        for (size_t i = 0; i < ds.steps.size(); ++i)
            if (ds.on[i]) on.push_back(ds.steps[i]);
        deformStackRun(on);
    }
    ds.steps.clear();
    ds.on.clear();
    ds.selected = -1;
}

void App::deformStackReset() {
    DeformStackUi& ds = deformStack_;
    deformStackUndoPreview();
    ds.steps.clear();
    ds.on.clear();
    ds.selected = -1;
}

void App::deformStackAdd(DeformOp op) {
    DeformStackUi& ds = deformStack_;
    DeformStep st;
    st.op = op;
    float lo, hi;
    deformRange(op, lo, hi, st.options.amount);
    // A new step starts on the axes of the step before it (most stacks work along one axis).
    if (!ds.steps.empty()) st.options.axes = ds.steps.back().options.axes;
    ds.steps.push_back(st);
    ds.on.push_back(1);
    ds.selected = int(ds.steps.size()) - 1;
    deformStackRefresh();
}

void App::drawDeformSize() {
    DeformSizeUi& sz = deformSize_;
    const Mesh& mesh = doc_.mesh;
    if (sz.revision != doc_.undo.revision() || sz.part != doc_.activePartInfo().id || sz.vertices != mesh.vertexCount()) {
        const AABB box = mesh.vertexCount() ? mesh.bounds() : AABB{glm::vec3(0.0f), glm::vec3(0.0f)};
        sz.revision = doc_.undo.revision();
        sz.part = doc_.activePartInfo().id;
        sz.vertices = mesh.vertexCount();
        sz.size = sz.edit = box.max - box.min;
    }
    Demo::pushScope("Size");
    tray::GroupLabel(*this, "Size");
    const ImGuiStyle& style = ImGui::GetStyle();
    const float lock = ImGui::CalcTextSize(tr("Uniform")).x + ImGui::GetFrameHeight() + style.ItemInnerSpacing.x;
    const float each = (ImGui::GetContentRegionAvail().x - lock - 3.0f * style.ItemSpacing.x) / 3.0f;
    static const char* const kIds[3] = {"##deform_size_x", "##deform_size_y", "##deform_size_z"};
    static const char* const kFormats[3] = {"X %.4g", "Y %.4g", "Z %.4g"};
    int edited = -1;
    for (int a = 0; a < 3; ++a) {
        if (a) ImGui::SameLine();
        ImGui::SetNextItemWidth(each);
        const float speed = std::max(sz.size[a], 1e-3f) * 0.002f;
        ImGui::DragFloat(kIds[a], &sz.edit[a], speed, 0.0f, 1e9f, kFormats[a], ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::IsItemDeactivatedAfterEdit()) edited = a;
        ui::HelpText("Size", nullptr,
                     "The active Part's width (X), height (Y) and depth (Z) in scene units. Drag, or double-click and "
                     "type an exact value: the Part resizes about its centre (one undo step). With Uniform, the other "
                     "sides follow in proportion.",
                     "Tool > Reshape > Size");
    }
    ImGui::SameLine();
    ui::Checkbox((std::string(tr("Uniform")) + "##deform_size_uniform").c_str(), &sz.uniform);
    ui::HelpText("Uniform", nullptr, "Resizing one side resizes the others in proportion, keeping the Part's shape.",
                 "Tool > Reshape > Size");
    if (edited >= 0 && sz.edit[edited] > 0.0f && sz.edit[edited] != sz.size[edited]) {
        Args a;
        if (sz.uniform && sz.size[edited] > 0.0f) {
            const glm::vec3 to = sz.size * (sz.edit[edited] / sz.size[edited]);
            a = {{"x", double(to.x)}, {"y", double(to.y)}, {"z", double(to.z)}};
        } else {
            a[std::string(1, char('x' + edited))] = double(sz.edit[edited]);
        }
        runCommand("deform_size", a);
        sz.revision = ~uint64_t(0); // read the new size back
    } else if (edited >= 0) {
        sz.edit = sz.size;
    }
    Demo::popScope();
}

void App::drawDeformStack() {
    DeformStackUi& ds = deformStack_;
    // Another undo step since the preview: it is part of the history now (kept, never undone behind your back).
    if (ds.previewed && (doc_.undo.revision() != ds.revision || doc_.activePartInfo().id != ds.part)) ds.previewed = false;
    Demo::pushScope("Stack");
    tray::GroupLabel(*this, "Stack");
    const ImGuiStyle& style = ImGui::GetStyle();
    const float half = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.5f;

    // Add: one menu, grouped.
    ImGui::SetNextItemWidth(half);
    if (ImGui::BeginCombo("##deform_stack_add", tr("Add step"))) {
        for (const OpGroup& g : kGroups) {
            ImGui::TextDisabled("%s", tr(g.name));
            ImGui::Indent();
            for (DeformOp op : g.ops) {
                const std::string item = std::string(tr(deformName(op))) + "##deform_add_" + deformName(op);
                if (ImGui::Selectable(item.c_str())) deformStackAdd(op);
            }
            ImGui::Unindent();
        }
        ImGui::EndCombo();
    }
    ui::HelpText("Add step", nullptr,
                 "Add a deformation to the stack: Transform (Move, Turn, Scale, Squash), Bend & Twist (Bend, Twist, "
                 "Taper, Shear), Shape (Round, Box, Flatten, Sag, Depth) or Surface (Polish, Relax, Smooth, Balloon). "
                 "The steps run in order on the whole Part.",
                 "Tool > Reshape > Stack");
    ImGui::SameLine();
    if (ui::Checkbox((std::string(tr("Preview")) + "##deform_stack_preview").c_str(), &ds.preview)) {
        if (ds.preview) deformStackRefresh();
        else deformStackUndoPreview();
    }
    ui::HelpText("Preview", nullptr,
                 "Show the stack on the Part as you edit it. It is applied for real (one undo step), so it saves and "
                 "exports like any change; Reset takes it back.",
                 "Tool > Reshape > Stack");

    // The list: on/off and the step; click a step to edit it.
    if (ds.steps.empty()) {
        ImGui::TextDisabled("%s", tr("No steps yet: Add step"));
    } else {
        for (size_t i = 0; i < ds.steps.size(); ++i) {
            ImGui::PushID(int(i));
            bool on = ds.on[i] != 0;
            if (ui::Checkbox("##on", &on)) {
                ds.on[i] = on;
                deformStackRefresh();
            }
            ImGui::SameLine();
            const std::string label = stepLabel(ds.steps[i]) + "##deform_step";
            if (ImGui::Selectable(label.c_str(), ds.selected == int(i))) ds.selected = int(i);
            ImGui::PopID();
        }
    }

    // The selected step's settings.
    if (ds.selected >= 0 && ds.selected < int(ds.steps.size())) {
        DeformStep& st = ds.steps[size_t(ds.selected)];
        float lo, hi, def;
        deformRange(st.op, lo, hi, def);
        const bool live = doc_.mesh.vertexCount() < kLiveVertices;
        bool changed = false;
        const std::string format = std::string(tr(deformName(st.op))) + " %.0f";
        ImGui::SetNextItemWidth(-1);
        const bool moved = ui::SliderInside("##deform_step_amount", &st.options.amount, lo, hi, format.c_str());
        changed |= live ? moved : ImGui::IsItemDeactivatedAfterEdit();
        if (deformUsesAxes(st.op))
            ui::Segmented(*this, {"deformation.stack_x", "deformation.stack_y", "deformation.stack_z"}, -1.0f);
        if (deformUsesDirection(st.op)) {
            ImGui::SetNextItemWidth(-1);
            const bool d = ui::SliderInside("##deform_step_direction", &st.options.direction, 0.0f, 360.0f,
                                            (std::string(tr("Direction")) + " %.0f").c_str());
            changed |= live ? d : ImGui::IsItemDeactivatedAfterEdit();
            ui::HelpText("Direction", nullptr,
                         "Which way the step bends or leans, in degrees around its axis: 0 is toward +Y (toward +X "
                         "for the Y axis).",
                         "Tool > Reshape > Stack");
        }
        if (deformUsesSoft(st.op)) {
            changed |= ui::Checkbox((std::string(tr("Soft")) + "##deform_step_soft").c_str(), &st.options.soft);
            ui::HelpText("Soft", nullptr,
                         "Ease the step out toward the ends of the Part instead of an even change along it (a bend "
                         "that curves most in the middle, a cut with a rounded edge).",
                         "Tool > Reshape > Stack");
        }
        if (st.op == DeformOp::Smooth)
            changed |= ui::Checkbox((std::string(tr("Keep volume")) + "##deform_step_volume").c_str(), &st.options.keepVolume);
        if (changed) deformStackRefresh();
        ui::ActionButton(*this, "deformation.stack_repeat", ImVec2(half, 0));
        ui::HelpAction(*this, "deformation.stack_repeat");
        ImGui::SameLine();
        ui::ActionButton(*this, "deformation.stack_remove", ImVec2(-1, 0));
        ui::HelpAction(*this, "deformation.stack_remove");
        ui::ActionButton(*this, "deformation.stack_apply_selected", ImVec2(-1, 0));
        ui::HelpAction(*this, "deformation.stack_apply_selected");
    }

    ui::ActionButton(*this, "deformation.stack_apply", ImVec2(half, 0));
    ui::HelpAction(*this, "deformation.stack_apply");
    ImGui::SameLine();
    ui::ActionButton(*this, "deformation.stack_reset", ImVec2(-1, 0));
    ui::HelpAction(*this, "deformation.stack_reset");
    Demo::popScope();
}

} // namespace sf
