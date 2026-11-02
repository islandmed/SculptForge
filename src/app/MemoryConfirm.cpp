// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// "Not enough free memory": Subdivide, Divide, a micro level or a voxel remesh that would build a mesh larger than
// the memory free now (core/MemoryGuard.h) asks before it runs. Running out takes the whole system down (other
// programs hold memory the hardware profile, which budgets from the total, counts as ours).
#include "app/App.h"

#include "app/I18n.h"
#include "app/Theme.h"
#include "core/MemoryGuard.h"

#include <utility>

namespace sf {

namespace {

const char* commandTitle(const std::string& name) {
    if (name == "subdivide") return "Subdivide";
    if (name == "subdiv_divide") return "Divide";
    if (name == "micro_divide") return "Divide to a micro level";
    if (name == "remesh") return "Voxel remesh";
    return "This step";
}

} // namespace

bool App::memoryAllows(const std::string& command, const Args& args, std::function<void()> proceed) {
    if (std::exchange(memoryConfirmed_, false)) return true; // (Continue was pressed: this is that run)
    const size_t need = commandMemoryNeed(doc_.mesh, command, args);
    if (need == 0) return true;
    const MemoryCheck c = checkMemory(need);
    if (c.enough) return true;
    memoryConfirm_.open = true;
    memoryConfirm_.what = commandTitle(command);
    memoryConfirm_.message = commandShortMessage(memoryConfirm_.what, c);
    memoryConfirm_.proceed = std::move(proceed);
    status_ = memoryConfirm_.message + " Continue or cancel in the dialog.";
    SDL_Log("Memory: %s", memoryConfirm_.message.c_str());
    return false;
}

void App::drawMemoryConfirm() {
    if (!memoryConfirm_.open) return;
    const float s = uiScale_;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    bool open = true;
    if (!ImGui::Begin(tr("Not enough free memory"), &open,
                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        if (!open) memoryConfirm_ = {};
        return;
    }
    ImGui::PushTextWrapPos(500 * s);
    ImGui::TextColored(theme::danger, "%s", memoryConfirm_.message.c_str());
    ImGui::TextWrapped("%s", tr("If the computer runs out of memory it can stop responding and SculptForge may close "
                                "without saving. Close other apps (or save first) before you go on."));
    ImGui::PopTextWrapPos();
    bool go = false;
    if (ImGui::Button(tr("Continue anyway##memoryconfirm"), ImVec2(160 * s, 0))) go = true;
    ImGui::SameLine();
    if (ImGui::Button(tr("Cancel##memoryconfirm"), ImVec2(120 * s, 0))) open = false;
    ImGui::End();
    if (go) {
        std::function<void()> proceed = std::move(memoryConfirm_.proceed);
        memoryConfirm_ = {};
        memoryConfirmed_ = true;
        if (proceed) proceed();
        memoryConfirmed_ = false; // (in case it didn't get as far as the check)
    } else if (!open) {
        status_ = memoryConfirm_.what + " cancelled: not enough free memory";
        memoryConfirm_ = {};
    }
}

} // namespace sf
