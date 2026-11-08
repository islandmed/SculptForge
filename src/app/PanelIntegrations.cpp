// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Preferences > Integrations > Show SculptForge on Discord (off by default; core/DiscordPresence.h): the icon, the
// name SculptForge and the elapsed time, and on request the monthly challenge's theme and the file's name. Only
// to the Discord app on this computer; it shows it to the user's friends. Tests, scripts, renders and CI never
// connect (supportCardTestMode), nor do runs by agents (discord::agentRun).
#include "app/App.h"
#include "app/Ui.h"
#include "core/NetworkPolicy.h"

#include <SDL3/SDL_video.h>
#include <imgui.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace sf {

void App::DiscordClientDelete::operator()(discord::Client* c) const { delete c; }

// Never in studio mode (core/NetworkPolicy.h): no Discord presence in a studio build.
bool App::discordPresenceAvailable() const { return net::localIpcAllowed() && discord::appIdConfigured(discord::kDiscordAppId); }

bool App::discordAgentRun() const {
    static const bool agent = discord::agentRun([](const char* n) { return std::getenv(n); }, SDL_GetCurrentVideoDriver());
    return agent;
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
        if (discordPrefs_.showChallenge) a.challenge = discordPrefs_.challenge;
        if (discordPrefs_.showFileName) a.fileName = doc_.path; // cut to its name (never the folder) when sent
    }
    discordClient_->set(a); // sends only when this changed
}

void App::drawIntegrationsPrefs() {
    const char* path = "Preferences > Integrations";
    bool changed = ImGui::Checkbox(tr("Show SculptForge on Discord##prefs.discord"), &discordPrefs_.enabled);
    ui::HelpText("Show SculptForge on Discord", nullptr,
                 "Your Discord profile shows SculptForge, its icon and how long you've been sculpting. Sent only to "
                 "the Discord app on this computer, which shows it to your friends. Off by default.",
                 path);
    ImGui::BeginDisabled(!discordPrefs_.enabled);
    changed |= ImGui::Checkbox(tr("Show the monthly challenge##prefs.discord"), &discordPrefs_.showChallenge);
    ui::HelpText("Show the monthly challenge", nullptr, "Adds \"Monthly challenge: <theme>\", with the theme you type below.", path);
    ImGui::BeginDisabled(!discordPrefs_.showChallenge);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##prefs.discord.challenge", tr("Theme (64 characters at most)"), discordChallengeBuf_,
                                 sizeof discordChallengeBuf_)) {
        discordPrefs_.challenge = discord::sanitize(discordChallengeBuf_, discord::kChallengeMax);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        std::snprintf(discordChallengeBuf_, sizeof discordChallengeBuf_, "%s", discordPrefs_.challenge.c_str());
        changed = true;
    }
    ImGui::EndDisabled();
    changed |= ImGui::Checkbox(tr("Show the file name##prefs.discord"), &discordPrefs_.showFileName);
    ui::HelpText("Show the file name", nullptr, "Adds the name of the file you're working on (never its folder). Off by default.",
                 path);
    ImGui::EndDisabled();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", tr("Nothing else is sent: no folders, no user name, no model details. Needs the Discord app running."));
    ImGui::PopStyleColor();
    if (changed) {
        saveProjectPrefs();
        updateDiscordPresence();
    }
}

} // namespace sf
