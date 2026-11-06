// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Help > Connect an AI tool (app/ConnectAi.h): the in-app guide to working with an AI tool, one screen in three steps:
// 1. pick your tool, with the exact command or configuration for this install's live-mode server (--mcp --attach)
// and Copy; 2. the live-mode switch itself; 3. what happens next (the code, the badge, AI: undo steps, what asks
// first); then example prompts. docs/guide/scripting.md "Live mode" says the same in the same words. Only while AI
// features are on (Preferences > AI): the action is registered in registerAiActions() and removed with the others.
#include "app/App.h"
#include "app/ConnectAi.h"
#include "app/I18n.h"
#include "app/Theme.h"

#include <IconsLucide.h>
#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <iterator>
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

// The text exactly as it is pasted: monospace in a framed box, never wrapped (a wrapped command pastes wrong); a line
// wider than the window scrolls sideways. Copy takes the whole text.
void codeBox(const std::string& text, const std::string& id, ImFont* mono) {
    if (mono) ImGui::PushFont(mono);
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
    const bool wide = textSize.x + 2.0f * style.FramePadding.x > ImGui::GetContentRegionAvail().x;
    const float h = textSize.y + 2.0f * style.FramePadding.y + (wide ? style.ScrollbarSize : 0.0f);
    if (ImGui::BeginChild(("##code" + id).c_str(), ImVec2(-1.0f, h), ImGuiChildFlags_FrameStyle,
                          ImGuiWindowFlags_HorizontalScrollbar))
        ImGui::TextUnformatted(text.c_str());
    ImGui::EndChild();
    if (mono) ImGui::PopFont();
}

} // namespace

void App::drawConnectAi() {
    if (!showConnectAi_) return;
    if (aiLinkStateValue() == 2) return; // the Approve dialog takes its place (never two dialogs stacked)
    const float s = uiScale_;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float w = std::min(640.0f * s, vp->WorkSize.x - 32.0f * s);
    ImGui::SetNextWindowSizeConstraints(ImVec2(w, 0), ImVec2(w, vp->WorkSize.y - 32.0f * s));
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin((std::string(tr("Connect an AI tool")) + "###connect_ai").c_str(), &showConnectAi_,
                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    const connect::Os os = thisOs();
    connect::Launch launch = thisInstall();
    launch.args.push_back("--attach"); // live mode: the tool works in this window (headless --mcp: the user guide)
    launch.name = "sculptforge-live";
    const std::vector<connect::Client> all = connect::clients(launch, os);
    ImGui::TextWrapped("%s", tr("The AI blocks out a starting point and shows you options; you do the art."));

    // 1. The tool: the three most asked for as buttons, every other one in a list.
    ImGui::SeparatorText(tr("1. Pick your tool"));
    static constexpr const char* kQuick[] = {"claude_code", "claude_desktop", "cursor"};
    const auto find = [&](std::string_view id) -> const connect::Client* {
        for (const connect::Client& c : all)
            if (id == c.id) return &c;
        return nullptr;
    };
    if (!find(connectClient_)) connectClient_ = kQuick[0];
    bool other = true;
    for (const char* id : kQuick) {
        if (const connect::Client* c = find(id)) {
            if (ImGui::RadioButton((std::string(c->name) + "##ai_tool." + id).c_str(), connectClient_ == id))
                connectClient_ = id;
            other &= connectClient_ != id;
            ImGui::SameLine();
        }
    }
    if (ImGui::RadioButton((std::string(tr("Other tools")) + "##ai_tool.other").c_str(), other) && !other)
        connectClient_ = "codex";
    other = std::find(std::begin(kQuick), std::end(kQuick), connectClient_) == std::end(kQuick);
    const connect::Client& c = *find(connectClient_);
    if (other) {
        ImGui::SetNextItemWidth(std::min(320.0f * s, ImGui::GetContentRegionAvail().x));
        if (ImGui::BeginCombo("##ai_tool.list", c.name)) {
            for (const connect::Client& o : all) {
                if (std::find(std::begin(kQuick), std::end(kQuick), std::string_view(o.id)) != std::end(kQuick)) continue;
                if (ImGui::Selectable(o.name, connectClient_ == o.id)) connectClient_ = o.id;
            }
            ImGui::EndCombo();
        }
    }
    ImGui::PushID(c.id);
    ImGui::TextWrapped("%s", tr(c.how));
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
    if (ImGui::SmallButton((std::string(ICON_LC_BOOK_OPEN " ") + tr("Their documentation")).c_str())) SDL_OpenURL(c.docs);
    if (const std::vector<const char*> notes = connect::planNotes(c.plan); !notes.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", tr("Plans and sign-in"));
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(420.0f * s);
            for (const char* note : notes) ImGui::TextWrapped("%s", tr(note));
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }
    ImGui::PopID();

    // 2. The live-mode switch itself (the same one as Preferences > AI).
    ImGui::SeparatorText(tr("2. Let it work in this window"));
    bool on = aiSettings_.liveControl;
    if (ImGui::Checkbox(tr("Let AI tools control this window##connect.ai_live"), &on)) setAiLiveControl(on);
    ImGui::SameLine();
    if (aiLinkStateValue() >= 3) ImGui::TextColored(theme::accent, tr("AI connected: %s"), aiLinkClient().c_str());
    else if (aiLink_) ImGui::TextDisabled("%s", tr("On: your AI tool can ask to connect."));
    else ImGui::TextDisabled("%s", tr("Off"));

    // 3. What the user will see, in the order it happens.
    ImGui::SeparatorText(tr("3. What happens next"));
    for (const char* line : {tr("Ask your AI tool for something (below). SculptForge shows a 6-digit code: click "
                                "Approve, or tell the AI the code."),
                             tr("While it works, the AI connected badge sits at the bottom right. Disconnect stops it."),
                             tr("Every step lands in Undo as \"AI: ...\", so you can step back."),
                             tr("It asks you first before files outside the project, scripts and the network."),
                             tr("It sees the viewport; what it sees goes to that tool's AI service.")}) {
        ImGui::Bullet();
        ImGui::TextWrapped("%s", line);
    }

    ImGui::SeparatorText(tr("Try asking"));
    const char* prompts[] = {tr("Block out a creature head with horns for me to sculpt."),
                             tr("Look at my model from all sides and tell me what to fix."),
                             tr("Make three rock variations I can refine."),
                             tr("Teach me: walk me through sculpting an ear, step by step.")};
    for (int i = 0; i < int(std::size(prompts)); ++i) {
        const std::string prompt = prompts[i];
        copyButton(status_, prompt, "prompt" + std::to_string(i));
        ImGui::SameLine();
        ImGui::TextWrapped("%s", prompt.c_str());
    }

    ImGui::Separator();
    if (ImGui::Button(tr("User guide: AI agents"))) guide_.open("scripting.md");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", tr("Turn this off in Preferences > AI."));
    ImGui::End();
}

} // namespace sf
