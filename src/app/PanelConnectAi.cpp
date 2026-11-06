// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Help > Connect an AI tool (app/ConnectAi.h): lead with "I have a ... subscription", then for each client that plan
// signs in to, the exact command or configuration for this install with Copy buttons. Only while AI features are on
// (Preferences > AI): the action is registered in registerAiActions() and removed with the others.
#include "app/App.h"
#include "core/ManagedPolicy.h"
#include "app/ConnectAi.h"
#include "app/I18n.h"

#include <IconsLucide.h>
#include <imgui.h>

#include <filesystem>
#include <string>

namespace sf {

namespace {

connect::Os thisOs() {
#if defined(_WIN32)
    return connect::Os::Windows;
#elif defined(__APPLE__)
    return connect::Os::Mac;
#else
    return connect::Os::Linux;
#endif
}

// How a client starts this install's server: Flatpak and AppImage through their own entry points, otherwise the
// sculptforge-cli beside the app (macOS: in the bundle's Contents/MacOS).
connect::Launch thisInstall() {
    if (const char* flatpak = SDL_getenv("FLATPAK_ID"); flatpak && *flatpak)
        return {"flatpak", {"run", "--command=sculptforge-cli", flatpak, "--mcp"}};
    if (const char* appimage = SDL_getenv("APPIMAGE"); appimage && *appimage) return {appimage, {"--cli", "--mcp"}};
#ifdef _WIN32
    const char* exe = "sculptforge-cli.exe";
#else
    const char* exe = "sculptforge-cli";
#endif
    namespace fs = std::filesystem;
    if (const char* base = SDL_GetBasePath()) {
        std::error_code ec;
        for (const fs::path& p : {fs::path(base) / exe, fs::path(base) / ".." / "MacOS" / exe})
            if (fs::exists(p, ec)) return {fs::weakly_canonical(p, ec).make_preferred().string(), {"--mcp"}};
    }
    return {exe, {"--mcp"}}; // not found beside the app (a build tree): on PATH, hopefully
}

void copyButton(std::string& status, const std::string& text, const std::string& id) {
    if (ImGui::SmallButton((std::string(tr("Copy")) + "##" + id).c_str())) {
        SDL_SetClipboardText(text.c_str());
        status = tr("Copied to the clipboard");
    }
}

// The text exactly as it is pasted: monospace, read-only, selectable.
void codeBox(const std::string& text, const std::string& id, ImFont* mono) {
    int lines = 1;
    for (const char c : text) lines += c == '\n';
    if (mono) ImGui::PushFont(mono);
    const float h = float(lines) * ImGui::GetTextLineHeight() + 2.0f * ImGui::GetStyle().FramePadding.y + 2.0f;
    ImGui::InputTextMultiline(("##code" + id).c_str(), const_cast<char*>(text.c_str()), text.size() + 1,
                              ImVec2(-1.0f, h), ImGuiInputTextFlags_ReadOnly);
    if (mono) ImGui::PopFont();
}

} // namespace

void App::drawConnectAi() {
    if (!showConnectAi_) return;
    if (!policy::mcpAllowed()) { // (a studio's policy turned AI tools off: core/ManagedPolicy.h)
        showConnectAi_ = false;
        return;
    }
    const float s = uiScale_;
    ImGui::SetNextWindowSize(ImVec2(720 * s, 640 * s), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin((std::string(tr("Connect an AI tool")) + "###connect_ai").c_str(), &showConnectAi_,
                      ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    const connect::Os os = thisOs();
    const connect::Launch launch = thisInstall();
    ImGui::TextWrapped("%s", tr("Let an AI assistant use SculptForge's tools: it starts sculptforge-cli on this computer "
                                "and talks to it directly. Nothing listens on the network. The assistant works on its own "
                                "scene and saves files where you ask; open them here to see the result."));
    ImGui::Spacing();
    std::string command = launch.command;
    for (const std::string& a : launch.args) command += " " + a;
    ImGui::TextDisabled("%s", tr("This install:"));
    ImGui::SameLine();
    ImGui::TextUnformatted(command.c_str());
    ImGui::SameLine();
    copyButton(status_, launch.command, "install");

    ImGui::SeparatorText(tr("I have a subscription to"));
    for (int p = 0; p < connect::kPlanCount; ++p) {
        if (p) ImGui::SameLine();
        const char* name = connect::planName(connect::Plan(p));
        if (ImGui::RadioButton((std::string(p == int(connect::Plan::Other) ? tr("Other tools") : name) + "##plan" +
                                std::to_string(p)).c_str(),
                               connectPlan_ == p))
            connectPlan_ = p;
    }
    const connect::Plan plan = connect::Plan(connectPlan_);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    for (const char* note : connect::planNotes(plan)) ImGui::TextWrapped("%s", tr(note));
    ImGui::PopStyleColor();
    ImGui::Spacing();

    if (ImGui::BeginChild("##clients", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_None)) {
        for (const connect::Client& c : connect::clients(launch, os)) {
            if (c.plan != plan) continue;
            ImGui::PushID(c.id);
            ImGui::SeparatorText(c.name);
            ImGui::TextWrapped("%s", tr(c.how));
            if (std::string_view(c.id) == "claude_desktop" &&
                ImGui::Button((std::string(ICON_LC_DOWNLOAD " ") + tr("Download the Claude Desktop bundle")).c_str()))
                SDL_OpenURL(connect::bundleUrl(os).c_str());
            for (size_t k = 0; k < c.snippets.size(); ++k) {
                const connect::Snippet& sn = c.snippets[k];
                const std::string id = std::to_string(k);
                ImGui::TextDisabled("%s", sn.kind == connect::Snippet::Kind::Terminal ? tr("In a terminal:")
                                          : sn.kind == connect::Snippet::Kind::Fields ? tr("Fields:")
                                                                                      : tr("Add to:"));
                if (!sn.file.empty()) {
                    ImGui::SameLine();
                    ImGui::TextUnformatted(sn.file.c_str());
                }
                ImGui::SameLine();
                copyButton(status_, sn.text, id);
                codeBox(sn.text, id, fontMono_);
            }
            if (ImGui::SmallButton((std::string(ICON_LC_BOOK_OPEN " ") + tr("Their documentation")).c_str()))
                SDL_OpenURL(c.docs);
            ImGui::PopID();
            ImGui::Spacing();
        }
    }
    ImGui::EndChild();
    if (ImGui::Button(tr("User guide: AI agents"))) guide_.open("scripting.md");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", tr("Turn this off in Preferences > AI."));
    ImGui::End();
}

} // namespace sf
