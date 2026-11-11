// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Help > Create bug report pack... (core/ReproPack.h): gathers what a bug report needs, shows every file that will be
// in the .zip (each can be left out), and saves it where the user says. Nothing is sent: there is no upload here.
#include "app/App.h"
#include "app/Calibration.h"
#include "app/Config.h"
#include "app/CrashLog.h"
#include "app/I18n.h"
#include "app/NativeDialog.h"
#include "core/HardwareProfile.h"
#include "core/ReproPack.h"
#include "core/StudioProfile.h"

#include <IconsLucide.h>
#include <imgui.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <mutex>

namespace sf {

struct ReproPackUi {
    bool open = false;
    std::vector<repro::Item> items;
    std::mutex mutex;
    std::string answer; // the save dialog's path (its thread), taken by drawReproPack
    bool answered = false;
    uint64_t savedBytes = 0;
};

namespace {

constexpr size_t kLogLines = 200;

void SDLCALL onReproSave(void* userdata, const char* const* files, int) {
    wakeMainLoop();
    auto* ui = static_cast<ReproPackUi*>(userdata);
    std::lock_guard lock(ui->mutex);
    ui->answer = files && files[0] ? files[0] : "";
    ui->answered = true;
}

std::string mb(uint64_t v) { return std::to_string(v) + " MB"; }

std::string fileStem(const std::string& path) {
    const std::u8string s = std::filesystem::path(std::u8string(path.begin(), path.end())).stem().u8string();
    return std::string(s.begin(), s.end());
}

} // namespace

void App::registerReproPack() {
    Action a;
    a.id = "help.repro_pack";
    a.label = "Create bug report pack...";
    a.palette = "Help";
    a.icon = ICON_LC_PACKAGE;
    a.run = [this] { openReproPack(); };
    a.description = "A .zip for a bug report that is safe to post: version, GPU and driver, changed settings, the end of "
                    "the log, the last commands, mesh counts and a generated stand-in mesh. No model, texture, name or "
                    "path goes in, and you see every file before saving. Nothing is sent.";
    actions_.add(std::move(a));
    // bug_report_pack path=...: the same pack without the preview (scripts, UI tests).
    commands_.add({"bug_report_pack",
                   "Writes a bug report pack (Help > Create bug report pack): system, settings, log, actions, mesh counts and "
                   "a stand-in mesh, nothing of the model itself. Nothing is sent.",
                   {{"path", ParamSpec::Type::String, "The .zip to write.", true}},
                   [this](Document&, const Args& args) -> CommandResult {
                       openReproPack();
                       reproPack_->open = false;
                       std::string err;
                       const std::string path = argString(args, "path");
                       if (!repro::writePack(path, reproPack_->items, err)) return {false, err};
                       std::error_code ec;
                       reproPack_->savedBytes = uint64_t(std::filesystem::file_size(std::filesystem::path(std::u8string(path.begin(), path.end())), ec));
                       return {true, "Wrote the bug report pack"};
                   }});
}

void App::openReproPack() {
    if (!reproPack_) reproPack_ = std::make_shared<ReproPackUi>();
    ReproPackUi& ui = *reproPack_;
    repro::Inputs in;
    HardwareInfo hw;
    probeHardware(hw);
    in.system = "SculptForge " + std::string(SCULPTFORGE_VERSION) + (isStudioBuild() ? " (studio build)" : "") +
                (studioMode().active ? ", studio mode" : "") + "\nOS: " + hw.os + "\nCPU: " + hw.cpu + " (" +
                std::to_string(hw.cpuThreads) + " threads; built for " + hw.simdBuild + ")\nMemory: " + mb(hw.ramMB) +
                "\nGPU: " + hw.glRenderer + " (" + hw.glVendor + ")\nDriver: OpenGL " + hw.glVersion +
                "\nVideo memory: " + (hw.vramMB ? mb(hw.vramMB) : std::string("unknown")) + "\nUI scale: " +
                std::to_string(int(std::lround(uiScale_ * 100.0f))) + " %\n";
    // Settings: only what differs from a fresh install.
    const Json now = toJson(uiPrefs_), fresh = toJson(UiPrefs{});
    Json diff = Json::object();
    for (const auto& [key, value] : now.members())
        if (fresh[key].dump() != value.dump()) diff.set(key, value);
    if (const Json keys = actions_.overridesToJson(); keys.isObject() && !keys.members().empty())
        diff.set("changedShortcuts", int(keys.members().size()));
    in.settingsJson = diff.dump();
    in.log = repro::tailLines(crashlog::path(), kLogLines);
    in.actions = repro::recentActions();
    // Names that may turn up in the log: the Parts', the project's, the studio's, the user's and the computer's.
    for (const Part& p : doc_.parts) in.names.push_back(p.name);
    if (!doc_.path.empty()) in.names.push_back(fileStem(doc_.path));
    if (!doc_.bridgeName.empty()) in.names.push_back(doc_.bridgeName);
    const StudioProfile& studio = studioMode().profile;
    for (const std::string* s : {&studio.name, &studio.id, &studio.title}) in.names.push_back(*s);
    for (const char* env : {"USER", "USERNAME", "LOGNAME", "HOSTNAME", "COMPUTERNAME"})
        if (const char* v = std::getenv(env); v && *v) in.names.push_back(v);
    in.doc = &doc_;
    const std::vector<repro::Item> previous = std::move(ui.items);
    ui.items = repro::buildItems(in);
    for (repro::Item& i : ui.items) // (what was left out stays out while the dialog is open)
        for (const repro::Item& p : previous)
            if (p.id == i.id && ui.open) i.include = p.include;
    ui.open = true;
}

void App::drawReproPack() {
    if (!reproPack_) return;
    ReproPackUi& ui = *reproPack_;
    std::string answer;
    {
        std::lock_guard lock(ui.mutex);
        if (ui.answered) {
            answer = ui.answer;
            ui.answered = false;
            if (answer.empty()) return; // cancelled: the preview stays
        }
    }
    if (!answer.empty()) {
        if (answer.size() < 4 || answer.compare(answer.size() - 4, 4, ".zip") != 0) answer += ".zip";
        std::string err;
        if (repro::writePack(answer, ui.items, err)) {
            std::error_code ec;
            ui.savedBytes = uint64_t(std::filesystem::file_size(std::filesystem::path(std::u8string(answer.begin(), answer.end())), ec));
            int files = 1;
            for (const repro::Item& i : ui.items) files += i.include ? 1 : 0;
            status_ = "Saved the bug report pack (" + std::to_string(files) + " files). Attach it to your report; nothing was sent.";
            ui.open = false;
        } else {
            status_ = "Couldn't save the bug report pack: " + err;
        }
    }
    if (!ui.open) return;
    const float s = uiScale_;
    ImGui::SetNextWindowSize(ImVec2(620 * s, 0), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin((std::string(tr("Create bug report pack")) + "###repro").c_str(), &ui.open,
                      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(tr("One .zip on this computer for a bug report. It holds no model, texture, image, name, "
                              "path or link: the mesh is a stand-in with the same counts. Nothing is sent; you attach it "
                              "yourself. Untick anything you'd rather leave out."));
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    for (repro::Item& item : ui.items) {
        ImGui::PushID(item.id.c_str());
        ImGui::Checkbox((std::string(tr(item.label.c_str())) + "##repro." + item.id).c_str(), &item.include);
        ImGui::SameLine();
        ImGui::TextDisabled("%s", item.file.c_str());
        ImGui::Indent(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", item.summary.c_str());
        ImGui::PopTextWrapPos();
        if (item.id != "proxy" && ImGui::TreeNode((std::string(tr("Show what's inside")) + "##repro.show." + item.id).c_str())) {
            std::string text(item.data.begin(), item.data.end());
            ImGui::InputTextMultiline("##text", text.data(), text.size() + 1, ImVec2(-1.0f, 140 * s), ImGuiInputTextFlags_ReadOnly);
            ImGui::TreePop();
        }
        ImGui::Unindent(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::PopID();
    }
    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button((std::string(tr("Save...")) + "##repro.save").c_str())) {
        static const SDL_DialogFileFilter kZip[] = {{"Zip archive", "zip"}};
        sf::dialogs::saveFile(onReproSave, &ui, window_, kZip, 1, "sculptforge-bug-report.zip");
    }
    ImGui::SameLine();
    if (ImGui::Button((std::string(tr("Cancel")) + "##repro.cancel").c_str())) ui.open = false;
    ImGui::End();
}

double App::reproTestValue(const std::string& name) const {
    if (name.rfind("repro_", 0) != 0) return std::nan("");
    const ReproPackUi* ui = reproPack_.get();
    if (name == "repro_open") return ui && ui->open ? 1.0 : 0.0;
    if (name == "repro_saved") return ui ? double(ui->savedBytes) : 0.0;
    if (name == "repro_items") {
        int n = 0;
        if (ui)
            for (const repro::Item& i : ui->items) n += i.include ? 1 : 0;
        return n;
    }
    return std::nan("");
}

} // namespace sf
