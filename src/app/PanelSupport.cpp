// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Supporting SculptForge: Help > Support SculptForge..., the first-run welcome, the supporters in Help > About and
// the one optional reminder after ten hours of sculpting. Each only opens kSupportUrl in the browser (Support.h);
// the app is the same for everyone, so nothing here checks, sends or stores anything else.
#include "app/App.h"
#include "app/Theme.h"

#include <imgui.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace sf {

namespace assets {
extern const unsigned char supporters[]; // docs/supporters.txt (CMakeLists.txt)
extern const unsigned long long supporters_size;
} // namespace assets

namespace {

const std::vector<std::string>& supporterNames() {
    static const std::vector<std::string> names =
        parseSupporters(std::string_view(reinterpret_cast<const char*>(assets::supporters), size_t(assets::supporters_size)));
    return names;
}

} // namespace

void App::openSupportPage() {
    ++supportPageRequests_;
    if (!interactive()) { // UI tests, renders and demos never start a browser
        status_ = std::string("Support SculptForge: ") + kSupportUrl;
        return;
    }
    status_ = SDL_OpenURL(kSupportUrl) ? std::string("Opened sculptforge.app/donate.html in your browser")
                                       : std::string("Couldn't open ") + kSupportUrl + ": " + SDL_GetError();
}

void App::openDiscordPage() {
    ++discordRequests_;
    if (!interactive()) { // UI tests, renders and demos never start a browser
        status_ = std::string("Join the community on Discord: ") + kDiscordUrl;
        return;
    }
    status_ = SDL_OpenURL(kDiscordUrl) ? std::string("Opened the SculptForge Discord invite in your browser")
                                       : std::string("Couldn't open ") + kDiscordUrl + ": " + SDL_GetError();
}

void App::drawSupportersSection() {
    ImGui::SeparatorText("Supporters");
    const std::vector<std::string>& names = supporterNames();
    if (names.empty()) {
        ImGui::TextDisabled("Be the first: sculptforge.app/donate.html");
    } else {
        ImGui::TextWrapped("Thank you to everyone who supports SculptForge:");
        // Plain text only (TextUnformatted: no format strings, markup or links), sanitised by parseSupporters.
        const float rows = float(std::min<size_t>(names.size(), 8));
        if (ImGui::BeginChild("##supporters", ImVec2(0, rows * ImGui::GetTextLineHeightWithSpacing() + 4.0f * uiScale_),
                              ImGuiChildFlags_Borders)) {
            ImGuiListClipper clipper;
            clipper.Begin(int(names.size()));
            while (clipper.Step())
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) ImGui::TextUnformatted(names[size_t(i)].c_str());
        }
        ImGui::EndChild();
    }
    ImGui::TextWrapped("%s", kInsiderNote);
    if (ImGui::Button("Support SculptForge##about")) openSupportPage();
}

void App::drawSupportUi() {
    const float s = uiScale_;
    const CanvasRect c = canvasRect();
    if (showWelcome_) {
        // Not modal: the app works around it, and it doesn't come back once closed.
        ImGui::SetNextWindowPos(ImVec2(c.x + c.w * 0.5f, c.y + c.h * 0.3f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(440.0f * s, 0.0f), ImGuiCond_Appearing);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize;
        if (ImGui::Begin("Welcome to SculptForge", &showWelcome_, flags)) {
            ImGui::TextWrapped("Hold Ctrl over any control to see what it does. The User Guide (Shift+F1) explains every "
                               "tool, and Coming from ZBrush lists what is where.");
            // The keys to start with (Preferences > Input changes them later; Store Config keeps them).
            ImGui::Spacing();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Coming from:");
            struct Origin {
                const char* label;
                int style;
                const char* help;
            };
            static const Origin kOrigins[] = {
                {"ZBrush##welcome.from", 0, "ZBrush-style keys and gestures."},
                {"Blender##welcome.from", 2, "Blender's navigation and sculpt-mode keys."},
                {"Maya##welcome.from", 3, "Maya's Alt+drag view controls."},
                {"New to sculpting##welcome.from", 1, "SculptForge's own keys (the default)."},
            };
            for (const Origin& o : kOrigins) {
                ImGui::SameLine();
                const bool on = uiPrefs_.inputStyle == o.style;
                if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                if (ImGui::Button(o.label)) {
                    setInputStyle(o.style);
                    startInputStyle_ = o.style; // kept for the next start, also without Store Config
                    saveProjectPrefs();
                    status_ = std::string("Keys: ") + (o.style == 0 ? "ZBrush-style" : o.style == 2 ? "Blender" : o.style == 3 ? "Maya" : "SculptForge") +
                              " (Preferences > Input changes them)";
                }
                if (on) ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", o.help);
            }
            if (ImGui::Button("Open the User Guide##welcome")) {
                guide_.open();
                showWelcome_ = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Join the community on Discord##welcome")) openDiscordPage();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Questions, tips and work in progress: opens the invite in your browser.");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextWrapped("SculptForge is free. If it helps your work, consider supporting it. %s", kInsiderNote);
            if (ImGui::Button("Support SculptForge##welcome")) openSupportPage();
            ImGui::SameLine();
            if (ImGui::Button("Close##welcome")) showWelcome_ = false;
        }
        ImGui::End();
    }
    if (supportReminderOpen_) {
        // The one reminder: a small note along the bottom of the canvas, no focus taken, closed for good by any button.
        ImGui::SetNextWindowPos(ImVec2(c.x + c.w * 0.5f, c.y + c.h - 12.0f * s), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
        ImGui::SetNextWindowBgAlpha(0.94f);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                       ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
        if (ImGui::Begin("##support_reminder", nullptr, flags)) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("You've sculpted over ten hours in SculptForge. It's free; if it helps your work, "
                                   "consider supporting it.");
            ImGui::SameLine();
            if (ImGui::SmallButton("Support...##reminder")) {
                openSupportPage();
                supportReminderOpen_ = false;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Don't show again##reminder")) supportReminderOpen_ = false;
        }
        ImGui::End();
    }
}

} // namespace sf
