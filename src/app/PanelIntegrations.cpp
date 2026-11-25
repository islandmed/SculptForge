// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Preferences > Integrations > Show SculptForge on Discord (off by default; core/DiscordPresence.h): the icon, the
// name SculptForge, the elapsed time and one word for the work, Sculpting, Painting or Idle. Nothing from the scene
// (the owner's conditions, 11-23: no file names, Part names, paths or user names; the file-name and challenge
// options of 0.11 to 0.14.0 are gone). Only to the Discord app on this computer; it shows it to the user's friends.
// Tests, scripts, renders and CI never connect (supportCardTestMode), nor do runs by agents (discord::agentRun).
#include "app/App.h"
#include "app/Ui.h"
#include "core/NetworkPolicy.h"

#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>
#include <imgui.h>

#include <chrono>
#include <cstdlib>

namespace sf {

namespace {
constexpr double kDiscordIdleSeconds = 5 * 60.0; // no stroke for this long: Idle
}

void App::DiscordClientDelete::operator()(discord::Client* c) const { delete c; }

// Never in studio mode (core/NetworkPolicy.h): no Discord presence in a studio build.
bool App::discordPresenceAvailable() const { return net::localIpcAllowed() && discord::appIdConfigured(discord::kDiscordAppId); }

bool App::discordAgentRun() const {
    static const bool agent = discord::agentRun([](const char* n) { return std::getenv(n); }, SDL_GetCurrentVideoDriver());
    return agent;
}

discord::Mode App::discordMode() const {
    const double now = double(SDL_GetTicksNS()) * 1e-9;
    if (now - discordStrokeAt_ > kDiscordIdleSeconds) return discord::Mode::Idle;
    return discordStrokePaint_ ? discord::Mode::Painting : discord::Mode::Sculpting;
}

void App::updateDiscordPresence() {
    // Scripted and agent runs (benchmarks, offscreen checks, the Windows PC's runs) use the user's preferences too:
    // they never show up on the user's Discord.
    if (!discordPresenceAvailable() || supportCardTestMode() || discordAgentRun() || discordClosed_) return;
    if (!discordPrefs_.enabled && !discordClient_) return; // never switched on: no thread, no connection
    if (!discordClient_) discordClient_.reset(new discord::Client(discord::kDiscordAppId));
    if (discordStart_ == 0)
        discordStart_ = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    discord::Activity a;
    a.show = discordPrefs_.enabled;
    if (a.show) {
        a.start = discordStart_;
        a.mode = discordMode();
        // The flip to Idle must go out while the window sleeps too: one wake-up when it is due.
        if (a.mode != discord::Mode::Idle) redrawIn(kDiscordIdleSeconds - (double(SDL_GetTicksNS()) * 1e-9 - discordStrokeAt_) + 0.1);
    }
    discordClient_->set(a); // sends only when this changed (one activity every 4 s at most)
}

void App::drawIntegrationsPrefs() {
    const char* path = "Preferences > Integrations";
    bool changed = ImGui::Checkbox(tr("Show SculptForge on Discord##prefs.discord"), &discordPrefs_.enabled);
    ui::HelpText("Show SculptForge on Discord", nullptr,
                 "Your Discord profile shows SculptForge, its icon, how long you've been at it and one word for what "
                 "you're doing: Sculpting, Painting or Idle. Sent only to the Discord app on this computer, which shows "
                 "it to your friends. Off by default.",
                 path);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", tr("Shared: the name SculptForge, its icon, your time in the app and Sculpting, Painting or Idle. "
                                "Nothing else: no file names, folders, Part names, user name or model details. Needs the Discord "
                                "app running on this computer."));
    ImGui::PopStyleColor();
    if (discordPrefs_.optionsNote) { // had Show the file name or Show the monthly challenge on up to 0.14.0
        ImGui::Spacing();
        ImGui::TextWrapped("%s", tr("Discord now shows only SculptForge and what you're doing, never the file name or a challenge "
                                    "theme. Those two options are gone."));
        if (ImGui::Button(tr("Got it##prefs.discord.note"))) {
            discordPrefs_.optionsNote = false;
            changed = true;
        }
    }
    if (changed) {
        saveProjectPrefs();
        updateDiscordPresence();
    }
}

} // namespace sf
