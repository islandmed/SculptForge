// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Supporting SculptForge: Help > Support SculptForge..., the first-run welcome, the supporters in Help > About and
// the one optional reminder after ten hours of sculpting. Each only opens kSupportUrl in the browser (Support.h);
// the app is the same for everyone, so nothing here checks, sends or stores anything else.
#include "app/App.h"
#include "app/Config.h"
#include "app/LookSchemes.h"
#include "app/Theme.h"

#include <IconsLucide.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
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
    status_ = SDL_OpenURL(kSupportUrl) ? std::string("Opened sculptforge.app/support.html in your browser")
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
        ImGui::TextDisabled("Be the first: sculptforge.app/support.html");
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
    ImGui::TextWrapped("%s", kFreeReleaseNote);
    if (ImGui::Button("Support SculptForge##about")) openSupportPage();
}

void App::chooseNewLook(bool tryIt) {
    newLookCard_ = false;
    newLookCardDone_ = true;
    if (tryIt) {
        uiPrefs_.look.look = int(looks::LookId::ForgeTerminal);
        uiPrefs_.look.scheme = "neutral";
        uiPrefs_.look.matchOmarchy = false;
        setShellLayout(1);
        lookDirty_ = true;
        status_ = "The new look: Neutral, SculptForge layout (Preferences > Interface > Look changes it)";
    } else {
        status_ = "Kept your layout (Preferences > Interface > Look has the new look)";
    }
    saveProjectPrefs();
}

// The first-run look picker (the welcome): Neutral (the default), Forge Terminal with Tokyo Night (0.10.7's
// default), or the Classic look with the ZBrush-style layout. A click applies it at once and remembers it.
void App::drawLookPicker() {
    struct Choice {
        const char* label;
        const char* name;
        int look;
        const char* scheme;
        int layout;
        const char* help;
    };
    static const Choice kChoices[] = {
        {"##welcome.look.neutral", "Neutral", int(looks::LookId::ForgeTerminal), "neutral", 1,
         "Neutral grey panels, so your model's colours read true, with one orange accent. The SculptForge layout."},
        {"##welcome.look.forge", "Forge Terminal", int(looks::LookId::ForgeTerminal), "tokyo_night", 1,
         "Terminal-style tiles in the Tokyo Night colours (blue-grey). Preferences > Interface > Look has more schemes."},
        {"##welcome.look.zbrush", "ZBrush-style layout", int(looks::LookId::Classic), "neutral", 0,
         "The Classic grey look with the alphabetical palette bar and the two-row top shelf, for ZBrush users."},
    };
    const float s = uiScale_, spacing = ImGui::GetStyle().ItemSpacing.x;
    const float w = std::floor((ImGui::GetContentRegionAvail().x - 2.0f * spacing) / 3.0f), h = 92.0f * s;
    for (int i = 0; i < 3; ++i) {
        const Choice& c = kChoices[i];
        if (i) ImGui::SameLine();
        const bool current = uiPrefs_.look.look == c.look && uiPrefs_.shellLayout == c.layout &&
                             (c.look != int(looks::LookId::ForgeTerminal) || uiPrefs_.look.scheme == c.scheme);
        const bool clicked = ImGui::InvisibleButton(c.label, ImVec2(w, h));
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const looks::SchemeInfo* scheme = looks::findScheme(c.scheme);
        const looks::Palette pal = c.look == int(looks::LookId::Classic) || !scheme ? config::classicPalette() : scheme->palette;
        config::drawLookMiniature(dl, a, b, pal, c.name, c.look == int(looks::LookId::Classic) ? fontInterSmall_ : fontMonoSmall_, s,
                                  c.layout);
        if (hovered && !current) dl->AddRectFilled(a, b, theme::u32(theme::textHover, 0.05f)); // (a faint lift, no border)
        if (current) dl->AddRect(a, b, theme::u32(theme::accent), 0.0f, 0, 2.0f * s);
        ImGui::LogText("%s", c.name);
        if (hovered) ImGui::SetTooltip("%s", c.help);
        if (clicked && !current) {
            uiPrefs_.look.look = c.look;
            uiPrefs_.look.scheme = c.scheme;
            uiPrefs_.look.matchOmarchy = false;
            setShellLayout(c.layout);
            lookDirty_ = true;
            saveProjectPrefs(); // remembered for the next start, also without Store Config
            status_ = std::string("Look: ") + c.name + " (Preferences > Interface > Look changes it)";
        }
    }
}

void App::drawSupportUi() {
    const float s = uiScale_;
    const CanvasRect c = canvasRect();
    if (showWelcome_) {
        // Not modal: the app works around it, and it doesn't come back once closed.
        ImGui::SetNextWindowPos(ImVec2(c.x + c.w * 0.5f, c.y + c.h * 0.3f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(500.0f * s, 0.0f), ImGuiCond_Appearing);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize;
        if (ImGui::Begin("Welcome to SculptForge", &showWelcome_, flags)) {
            // New to sculpting: the lesson first, big (docs/guide/first-sculpt.md).
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::Button(ICON_LC_GRADUATION_CAP "  Start the 10-minute lesson##welcome.lesson", ImVec2(-1, 40.0f * s))) {
                guide_.open("first-sculpt.md");
                showWelcome_ = false;
            }
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Your first sculpt: a little mushroom house from this sphere, step by step, in about ten minutes.");
            ImGui::Spacing();
            ImGui::TextWrapped("Hold Ctrl over any control to see what it does. The User Guide (Shift+F1) explains every "
                               "tool, and Coming from ZBrush lists what is where.");
            // The look (0.10.9, #U6): three cards, the first the default. Existing installs never see this window
            // unless they open Help > Welcome, so nobody's look changes by itself.
            ImGui::Spacing();
            ImGui::TextUnformatted("Pick a look:");
            drawLookPicker();
            // Where you come from sets the keys and the layout together (Preferences > Input and > Interface > Look
            // change them later; both are remembered without Store Config).
            ImGui::Spacing();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Coming from:");
            struct Origin {
                const char* label;
                int style, layout;
                const char* help;
            };
            static const Origin kOrigins[] = {
                {"ZBrush##welcome.from", 0, 0, "ZBrush-style keys and gestures, and the ZBrush-style layout."},
                {"Blender##welcome.from", 2, 1, "Blender's navigation and sculpt-mode keys, the SculptForge layout."},
                {"Maya##welcome.from", 3, 1, "Maya's Alt+drag view controls, the SculptForge layout."},
                {"New to sculpting##welcome.from", 1, 1, "SculptForge's own keys and layout (the default)."},
            };
            for (const Origin& o : kOrigins) {
                ImGui::SameLine();
                const bool on = uiPrefs_.inputStyle == o.style && uiPrefs_.shellLayout == o.layout;
                if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                if (ImGui::Button(o.label)) {
                    setInputStyle(o.style);
                    startInputStyle_ = o.style; // kept for the next start, also without Store Config
                    setShellLayout(o.layout);
                    lookDirty_ = true;
                    saveProjectPrefs();
                    status_ = std::string("Keys: ") + (o.style == 0 ? "ZBrush-style" : o.style == 2 ? "Blender" : o.style == 3 ? "Maya" : "SculptForge") +
                              (o.layout == 0 ? ", ZBrush-style layout" : ", SculptForge layout") + " (Preferences changes them)";
                }
                if (on) ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", o.help);
            }
            // A base mesh to start from (core/StarterMeshes.h), instead of the sphere.
            ImGui::Spacing();
            if (ImGui::Button(ICON_LC_USER_ROUND "  Start from a head##welcome")) {
                confirmDiscard([this] { runCommand("new_starter_mesh", {{"name", std::string("human-head")}}); });
                showWelcome_ = false;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("A neutral human head with clean quads and eye and mouth loops, ready for a portrait.");
            ImGui::SameLine();
            if (ImGui::Button("More starter meshes##welcome")) {
                showStarterMeshes();
                showWelcome_ = false;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Bust, full body, hand, foot, ear, eye, stylised and creature heads, torso and "
                                  "bevelled primitives: Library > Tool > Starter meshes.");
            // AI (Preferences > AI changes it later; saved at once).
            ImGui::Spacing();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("AI features:");
            for (const bool choice : {true, false}) {
                ImGui::SameLine();
                const bool on = ai::aiEnabled() == choice;
                if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                ImGui::BeginDisabled(ai::aiPolicyLocked());
                if (ImGui::Button(choice ? "On##welcome.ai" : "Off##welcome.ai")) setAiFeatures(choice);
                ImGui::EndDisabled();
                if (on) ImGui::PopStyleColor();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("%s", choice ? "AI Brush (describe a brush) and Generate 3D (image or text to a model)."
                                                   : "Hides them; SculptForge makes no AI network calls.");
            }
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s", ai::kPrivacyLine);
            ImGui::PopStyleColor();
            ImGui::Spacing();
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
            ImGui::TextWrapped("SculptForge is free. Memberships add credits, a monthly pack, a feature vote and, for Insiders, daily drops and preview builds. %s", kFreeReleaseNote);
            if (ImGui::Button("Support SculptForge##welcome")) showSupportCard(SupportCardReason::Weekly); // the memberships
            ImGui::SameLine();
            if (ImGui::Button("Close##welcome")) showWelcome_ = false;
        }
        ImGui::End();
    }
    if (newLookCard_ && !showWelcome_ && !supportCardOpen() && !whatsNew_ && !showWhatsNew_) {
        // Once, after updating from before 0.10.7 (PanelProject.cpp): the new default look, never forced.
        ImGui::SetNextWindowPos(ImVec2(c.x + c.w * 0.5f, c.y + c.h - 16.0f * s), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
        ImGui::SetNextWindowSize(ImVec2(420.0f * s, 0.0f), ImGuiCond_Always);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing;
        bool open = true;
        if (ImGui::Begin("SculptForge has a new look##newlook", &open, flags)) {
            ImGui::TextWrapped("Neutral grey panels with one orange accent, and the SculptForge layout: menus by task, a "
                               "status bar and a one-row tool shelf. Your layout stays as it is unless you try it.");
            ImGui::Spacing();
            if (ImGui::Button("Try it##newlook")) chooseNewLook(true);
            ImGui::SameLine();
            if (ImGui::Button("Keep my layout##newlook")) chooseNewLook(false);
            ImGui::TextDisabled("Either way: Preferences > Interface > Look changes it later.");
        }
        ImGui::End();
        if (!open) chooseNewLook(false);
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
            ImGui::TextUnformatted("You've sculpted over ten hours in SculptForge. Memberships add credits, a monthly "
                                   "brush pack and a roadmap vote.");
            ImGui::SameLine();
            if (ImGui::SmallButton("Membership...##reminder")) {
                showSupportCard(SupportCardReason::Weekly); // the memberships, each a click from its checkout
                supportReminderOpen_ = false;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Don't show again##reminder")) supportReminderOpen_ = false;
        }
        ImGui::End();
    }
}

} // namespace sf
