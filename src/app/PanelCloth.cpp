// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Cloth brush in the app (core/Cloth.h, core/ClothBrush.cpp): Brush > Cloth (mode, fabric, gravity, reach, steps,
// soft body, self-collision) and the other visible Parts as the surfaces a Cloth stroke collides with, copied once
// and kept until one of them changes.
#include "app/App.h"
#include "app/Palettes.h"
#include "app/Ui.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace sf {

namespace {

void hint(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

} // namespace

void App::prepareClothColliders() {
    doc_.sculptor.clothColliders = nullptr;
    if (!clothCollide_) return;
    // What the colliders were built from: each other visible Part's id, undo revision and size.
    std::vector<uint64_t> key;
    for (int i = 0; i < doc_.partCount(); ++i) {
        if (i == doc_.activePart || !doc_.parts[size_t(i)].visible) continue;
        const Part& part = doc_.parts[size_t(i)];
        key.push_back(part.id);
        key.push_back(part.data->undo.revision());
        key.push_back(uint64_t(part.data->mesh.vertexCount()) << 32 ^ part.data->mesh.triangleCount());
        // (where it sits relative to the active Part: a moved linked copy collides where it's shown)
        const glm::mat4 m = glm::inverse(doc_.parts[size_t(doc_.activePart)].transform) * part.transform;
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r) {
                uint32_t bits;
                std::memcpy(&bits, &m[c][r], sizeof bits);
                key.push_back(bits);
            }
    }
    if (key.empty()) {
        clothColliders_.reset();
        clothCollidersKey_.clear();
        return;
    }
    if (!clothColliders_ || key != clothCollidersKey_) {
        auto colliders = std::make_unique<ClothColliders>();
        const glm::mat4 toActive = glm::inverse(doc_.parts[size_t(doc_.activePart)].transform);
        for (int i = 0; i < doc_.partCount(); ++i) {
            const Part& part = doc_.parts[size_t(i)];
            if (i != doc_.activePart && part.visible) colliders->add(part.data->mesh, toActive * part.transform);
        }
        colliders->build();
        clothColliders_ = std::move(colliders);
        clothCollidersKey_ = std::move(key);
    }
    doc_.sculptor.clothColliders = clothColliders_.get();
}

void App::drawClothSection() {
    ClothBrushSettings& c = doc_.sculptor.cloth;
    const char* help = "Brush > Cloth";
    ImGui::SetNextItemWidth(-1);
    int mode = int(c.mode);
    // (Each item's id names the combo: Pinch and Inflate are also brushes on the shelf.)
    const char* names[] = {tr("Drag"), tr("Push"), tr("Settle"), tr("Grab"), tr("Expand"), tr("Pinch"), tr("Inflate")};
    static_assert(std::size(names) == size_t(ClothBrushMode::Count));
    std::string labels[std::size(names)];
    const char* modes[std::size(names)];
    for (size_t i = 0; i < std::size(names); ++i) labels[i] = std::string(names[i]) + "##clothmode", modes[i] = labels[i].c_str();
    if (ImGui::Combo("##clothmode", &mode, modes, int(ClothBrushMode::Count))) c.mode = ClothBrushMode(mode);
    ui::HelpText("Cloth mode", nullptr,
                 "Drag: the surface under the brush follows the stroke as fabric, folding where it bunches up. Push: "
                 "presses it in along the surface (Alt pulls it out) and the cloth around follows. Settle: only gravity, "
                 "the surface under the brush sags and drapes over what is below it. Grab: the cloth where the stroke "
                 "starts is held and follows the brush, the rest trails and folds. Expand and Pinch: spread the cloth "
                 "out from the brush centre or gather it in (Alt swaps). Inflate: adds fabric under the brush so it "
                 "balloons out and wrinkles (Alt takes fabric away).",
                 help);
    ImGui::SetNextItemWidth(-1);
    int fabric = int(c.fabric);
    const char* fabrics[] = {tr("Silk"), tr("Cotton"), tr("Denim"), tr("Leather")};
    if (ImGui::Combo("##clothfabric", &fabric, fabrics, int(FabricPreset::Count))) c.fabric = FabricPreset(fabric);
    ui::HelpText("Fabric", nullptr,
                 "How the cloth folds: Silk makes many fine folds, Cotton medium ones, Denim broad ones and Leather a few "
                 "wide, stiff ones.",
                 help);
    ImGui::SetNextItemWidth(-1);
    float gravity = c.gravity * 100.0f;
    if (ui::SliderInside("##clothgravity", &gravity, 0.0f, 100.0f, "Gravity %.0f")) c.gravity = gravity / 100.0f;
    ui::HelpText("Gravity", nullptr,
                 "Pulls the simulated cloth down (world down) while you drag, so folds hang. 0 keeps Drag and Push in "
                 "place; Settle always uses at least 50.",
                 help);
    ImGui::SetNextItemWidth(-1);
    ui::SliderInside("##clothreach", &c.reach, 1.5f, 4.0f, "Reach %.1f");
    ui::HelpText("Reach", nullptr,
                 "How much of the surface around the brush is simulated, in brush radii. Further out stays put. More "
                 "reach lets folds travel further and costs more.",
                 help);
    ImGui::SetNextItemWidth(-1);
    tray::SliderInt("##clothsteps", &c.steps, 1, 8, "Steps %d");
    ui::HelpText("Steps", nullptr, "Simulation steps per dab: more is softer and settles further, but slower.", help);
    ImGui::SetNextItemWidth(-1);
    float soft = c.softBody * 100.0f;
    if (ui::SliderInside("##clothsoft", &soft, 0.0f, 100.0f, "Soft body %.0f")) c.softBody = soft / 100.0f;
    ui::HelpText("Soft body", nullptr,
                 "0: the cloth stays where the brush leaves it. Higher: it springs back toward its shape at the "
                 "start of the stroke, like a stuffed or elastic surface.",
                 help);
    ImGui::Checkbox(tr("Self-collision"), &c.selfCollide);
    ui::HelpText("Self-collision", nullptr,
                 "Folds push against each other instead of passing through. A little slower on big regions.", help);
    ImGui::Checkbox(tr("Collide with other Parts"), &clothCollide_);
    ui::HelpText("Collide with other Parts", nullptr,
                 "The cloth rests on the other visible Parts instead of passing through them (a shirt on a body).", help);
    hint(tr("Cloth brush: pick it from the brush shelf. Masked areas hold still, like pins."));
}

bool App::clothUiValue(const std::string& name, double& out) const {
    if (name == "cloth_mode") return out = double(int(doc_.sculptor.cloth.mode)), true;
    if (name == "cloth_fabric") return out = double(int(doc_.sculptor.cloth.fabric)), true;
    if (name == "cloth_soft") return out = double(doc_.sculptor.cloth.softBody), true;
    if (name == "cloth_self") return out = doc_.sculptor.cloth.selfCollide ? 1.0 : 0.0, true;
    if (name == "cloth_region") return out = double(doc_.sculptor.clothRegion()), true;
    if (name == "drape_scale") return out = double(drapeRequest_.scale), true;
    if (name == "drape_running") return out = drape_ ? 1.0 : 0.0, true;
    if (name == "drape_frame") return out = double(drapeFrame()), true;
    if (name == "cloth_colliders") return out = clothColliders_ ? double(clothColliders_->triangles()) : 0.0, true;
    return false;
}

} // namespace sf
