// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The first-use notice for third-party AI hosts (ai/Consent.h). It is drawn inside the panel that asked (Brush Lab,
// Generate 3D), never as a dialog: what is sent, to which host, who pays, then Allow / Use local instead / Cancel.
// The request functions refuse on their own until Allow, so a script or AI tool can't send first either.
// Preferences > AI lists every online service with its state and Revoke.
#include "app/App.h"
#include "app/I18n.h"
#include "app/Ui.h"

#include "ai/Providers.h"
#include "core/ManagedPolicy.h"

#include <cstdio>
#include <thread>

namespace sf {

namespace {

constexpr const char* kComfyLocal = "http://127.0.0.1:8188";

// A local server "Use local instead" can switch to: "ollama:<model>", "lmstudio:<model>", "comfyui", or "".
std::string probeLocal(bool generate3D, const ai::AiSettings& s) {
    std::string err;
    if (generate3D) {
        ai::comfy::ServerInfo info;
        return ai::comfy::checkServer(kComfyLocal, info, err, 2) ? "comfyui" : "";
    }
    if (std::string m = ai::smallestOllamaModel(err); !m.empty()) return "ollama:" + m;
    const std::vector<std::string> models = ai::listModels(ai::withPreset(s, "lmstudio"), err);
    return models.empty() ? std::string() : "lmstudio:" + models.front();
}

void hint(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

} // namespace

bool App::askToSend(const ai::SendTarget& target, SendPanel panel, std::function<void()> retry) {
    std::string why;
    // Allowed, or a studio's "no" (the request itself says so, nothing is sent): go on.
    if (ai::checkSendAllowed(aiSettings_, target, why) || why.rfind(ai::kSendNotAllowed, 0) != 0) return true;
    const bool fresh = !sendNotice_ || sendNotice_->target.id != target.id || sendNotice_->panel != panel;
    sendNotice_ = SendNotice{target, panel, std::move(retry)};
    if (fresh && !localProbe_.valid()) { // is there a model on this computer to offer instead?
        localFound_.clear();
        localProbed_ = uiConfigDeterministic_; // (UI tests: never probe, whatever runs on the test machine)
    }
    if (fresh && !localProbed_ && !localProbe_.valid()) {
        auto promise = std::make_shared<std::promise<std::string>>();
        localProbe_ = promise->get_future();
        std::thread([promise, generate = panel == SendPanel::Generate3D, settings = aiSettings_] {
            promise->set_value(probeLocal(generate, settings));
        }).detach();
    }
    if (panel == SendPanel::BrushLab) showBrushLab_ = true;
    else showImageTo3D_ = true;
    status_ = "Nothing sent yet: " + target.label + " asks first (in the panel)";
    return false;
}

void App::rememberSend(const std::string& id, bool allow) {
    if (allow) ai::allowSend(aiSettings_, id);
    else ai::revokeSend(aiSettings_, id);
    if (aiSettingsPath_.empty() || uiConfigDeterministic_) return;
    // Only the list: settings typed into the panels are saved when the user presses Save settings.
    ai::AiSettings saved = ai::AiSettings::load(aiSettingsPath_);
    saved.sendAllowed = aiSettings_.sendAllowed;
    std::string err;
    if (!saved.save(aiSettingsPath_, err)) status_ = "Couldn't save the AI settings: " + err;
}

void App::drawSendNotice(SendPanel panel) {
    if (localProbe_.valid() && localProbe_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        localFound_ = localProbe_.get();
        localProbed_ = true;
    }
    if (!sendNotice_ || sendNotice_->panel != panel) return;
    const ai::SendTarget& t = sendNotice_->target;
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::GetStyleColorVec4(ImGuiCol_CheckMark));
    if (ImGui::BeginChild("##send.notice", ImVec2(-1, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY)) {
        ImGui::TextWrapped(tr("Send to %s?"), t.label.c_str());
        if (ImGui::BeginTable("##send.facts", 2)) {
            ImGui::TableSetupColumn("##k", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("##v", ImGuiTableColumnFlags_WidthStretch);
            auto row = [](const char* key, const std::string& value) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", tr(key));
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", value.c_str());
            };
            row("What", tr(t.what.c_str()));
            row("To", t.host);
            row("Pays", tr(t.payer.c_str()));
            ImGui::EndTable();
        }
        hint(tr("Your sculpt stays on this computer. Asked once per service; Preferences > AI can revoke it."));
        const float w = (ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3.0f;
        bool close = false;
        std::function<void()> retry;
        if (ImGui::Button(tr("Allow##send"), ImVec2(w, 0))) {
            rememberSend(t.id, true);
            retry = sendNotice_->retry;
            close = true;
        }
        ImGui::SameLine();
        const bool found = !localFound_.empty();
        ImGui::BeginDisabled(!localProbed_);
        if (ImGui::Button(found ? tr("Use local instead##send") : tr("Set up local##send"), ImVec2(w, 0))) {
            if (!found) {
                guide_.open(panel == SendPanel::BrushLab ? "brush-lab.md#run-a-model-on-your-own-computer"
                                                         : "generate-3d.md#comfyui-your-own-server");
            } else {
                ai::AiSettings saved = aiSettingsPath_.empty() ? aiSettings_ : ai::AiSettings::load(aiSettingsPath_);
                if (localFound_ == "comfyui") {
                    aiSettings_.generatorService = saved.generatorService = "comfyui";
                    aiSettings_.comfyUrl = saved.comfyUrl = kComfyLocal;
                    std::snprintf(comfyUrl_, sizeof comfyUrl_, "%s", kComfyLocal);
                } else {
                    const size_t colon = localFound_.find(':');
                    const std::string preset = localFound_.substr(0, colon), model = localFound_.substr(colon + 1);
                    aiSettings_ = ai::withPreset(aiSettings_, preset);
                    aiSettings_.model = model;
                    saved.preset = aiSettings_.preset;
                    saved.provider = aiSettings_.provider;
                    saved.endpoint = aiSettings_.endpoint;
                    saved.model = model;
                }
                std::string err;
                if (!aiSettingsPath_.empty() && !uiConfigDeterministic_) saved.save(aiSettingsPath_, err);
                retry = sendNotice_->retry;
                close = true;
            }
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("%s", !localProbed_ ? tr("Looking for a model on this computer...")
                                    : found       ? tr("Runs on this computer: nothing leaves it")
                                                  : tr("No local model found: how to run one (User Guide)"));
        ImGui::SameLine();
        if (ImGui::Button(tr("Cancel##send"), ImVec2(w, 0))) {
            close = true;
            status_ = "Nothing was sent";
        }
        if (close) sendNotice_.reset();
        if (retry) retry();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void App::drawAiSendPrefs() {
    ImGui::SeparatorText(tr("Online AI services"));
    hint(tr("Before the first request to a service on someone else's computer, SculptForge says what is sent and asks. "
            "Models on this computer never ask."));
    if (!ImGui::BeginTable("##send.prefs", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) return;
    ImGui::TableSetupColumn("##service", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("##state", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("##revoke", ImGuiTableColumnFlags_WidthFixed);
    for (const ai::SendTarget& t : ai::knownTargets(aiSettings_)) {
        ImGui::PushID(t.id.c_str());
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextWrapped("%s", t.label.c_str()); // (a long name wraps in its column, never under the state)
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", t.host.c_str());
        ImGui::TableNextColumn();
        const bool forbidden = !policy::aiProviderAllowed(t.policyId) || (!t.url.empty() && !policy::aiEndpointAllowed(t.url));
        const bool remembered = ai::sendRemembered(aiSettings_, t.id);
        if (forbidden) ImGui::TextDisabled("%s", tr("Blocked by your studio"));
        else if (ai::policyPreAllowed(t)) ImGui::TextDisabled("%s", tr("Allowed by your studio"));
        else if (remembered) ImGui::TextUnformatted(tr("Allowed"));
        else ImGui::TextDisabled("%s", tr("Asks first"));
        ImGui::TableNextColumn();
        if (remembered && ImGui::SmallButton(tr("Revoke"))) {
            rememberSend(t.id, false);
            status_ = t.label + " will ask again before anything is sent";
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

} // namespace sf
