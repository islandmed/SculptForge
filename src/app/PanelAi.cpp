// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Preferences > AI > Enable AI features (ai/AiPolicy.h). Off: the AI commands (brush_ai, image_to_3d,
// text_to_3d, comfyui_check) leave the command registry, so the command palette, scripts and the console
// can't reach them; AI Brush and Generate 3D leave the shelves and menus and their windows close; and every
// AI network entry point refuses. Saved in ai.json at once, so the CLI, Python and MCP server follow it.
// SF_AI=off (or --no-ai) locks it for studios.
#include "app/App.h"
#include "app/I18n.h"
#include "app/Ui.h"

#include "ai/AiCommands.h"
#include "ai/Oidc.h"
#include "core/AuditLog.h"
#include "core/ManagedPolicy.h"

#include <IconsLucide.h>
#include <imgui.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

namespace sf {

void App::registerPolicyTestCommand() {
    // UI tests only (never registered otherwise, so no script or AI tool can change a studio's policy): a policy as
    // if IT had deployed it, for tests/ui/studio_policy.sfui. The next script starts without one.
    if (options_.uiTest.empty()) return;
    policy::resetPolicyForTests();
    commands_.add({"ui_policy", "UI tests: apply a studio policy (policy.json text; \"\" removes it).",
                   {{"json", ParamSpec::Type::String, "The policy file's text.", true}},
                   [this](Document&, const Args& a) -> CommandResult {
                       const std::string text = argString(a, "json");
                       if (text.empty()) {
                           policy::resetPolicyForTests();
                       } else { // through a file, as IT deploys it (a broken one fails closed with its error)
                           const std::filesystem::path path = audit::logPath() + ".policy.json"; // (the test's folder)
                           std::error_code ec;
                           std::filesystem::create_directories(path.parent_path(), ec);
                           std::ofstream(path, std::ios::binary) << text;
                           policy::setPolicyForTests(policy::loadPolicyFile(path.string(), "ui_policy"));
                       }
                       applyAiFeatures(ai::aiFeaturesAllowed(true), false); // (UI tests start with AI on)
                       applyUpdatePolicy();
                       return {true, policy::describe(policy::current())};
                   }});
}

void App::registerAiActions() {
    // The switch itself: always there, so the command palette finds "AI" to turn it back on.
    Action sw;
    sw.id = "prefs.ai_features";
    sw.label = "Enable AI features";
    sw.palette = "Preferences > AI";
    sw.description = std::string("AI Brush (describe a brush) and Generate 3D (image or text to a model). Off hides them "
                                 "and stops every AI network call. ") + ai::kPrivacyLine;
    sw.run = [this] { setAiFeatures(!ai::aiEnabled()); };
    sw.checked = [] { return ai::aiEnabled(); };
    sw.enabled = [] { return !ai::aiPolicyLocked(); };
    actions_.add(std::move(sw));
    if (!actions_.find("prefs.ai_live")) { // live mode (PanelAiLink.cpp): its switch and the kill switch
        Action live;
        live.id = "prefs.ai_live";
        live.label = "Let AI tools control this window";
        live.palette = "Preferences > AI";
        live.description = "Live mode: an AI tool on this computer (sculptforge-cli --mcp --attach) can ask to sculpt in this "
                           "window while you watch. You approve each connection; files outside the project, scripts and "
                           "the network ask first. Nothing listens on the network.";
        live.run = [this] { setAiLiveControl(!aiSettings_.liveControl); };
        live.checked = [this] { return aiSettings_.liveControl; };
        live.enabled = [] { return ai::aiEnabled() && policy::mcpLiveAllowed(); };
        actions_.add(std::move(live));
        Action cut;
        cut.id = "ai.live_disconnect";
        cut.label = "Disconnect AI tool";
        cut.palette = "Preferences > AI";
        cut.description = "Stop the AI tool controlling this window now (its steps stay in Undo).";
        cut.run = [this] { aiLinkDisconnect("AI tool disconnected"); };
        cut.enabled = [this] { return aiLinkStateValue() >= 2; };
        actions_.add(std::move(cut));
    }
    commands_.remove("ai_features"); // (called again when AI is turned back on)
    commands_.add({"ai_features", "Turn the AI features on or off (Preferences > AI > Enable AI features).",
                   {{"on", ParamSpec::Type::Bool, "true: on, false: off.", true}},
                   [this](Document&, const Args& a) -> CommandResult {
                       const bool on = argBool(a, "on", true);
                       if (ai::aiPolicyLocked() && on != ai::aiEnabled()) return {false, ai::aiOffError()};
                       setAiFeatures(on);
                       return {true, on ? "AI features on" : "AI features off"};
                   }});
    if (!ai::aiEnabled()) return;

    auto flag = [this](std::string id, std::string label, const char* icon, std::string palette, bool* value,
                       std::string description) {
        Action a;
        a.id = std::move(id);
        a.label = std::move(label);
        a.icon = icon;
        a.palette = std::move(palette);
        a.description = std::move(description);
        a.run = [value] { *value = !*value; };
        a.checked = [value] { return *value; };
        actions_.add(std::move(a));
    };
    flag("ui.brush_lab", "AI Brush", ICON_LC_SPARKLES, "Brush", &showBrushLab_,
         "Brush Lab: describe a brush in words and AI builds it; tweak the recipe with sliders.");
    flag("ui.generate3d", "Generate 3D", ICON_LC_WAND_SPARKLES, "Generate", &showImageTo3D_,
         "Turn an image or a description into a sculptable model (Hugging Face, fal.ai, Meshy, ComfyUI or offline).");
    // Help > Connect an AI tool (PanelConnectAi.cpp): not when a studio's policy turns AI tools (MCP) off.
    if (!policy::mcpAllowed()) return;
    Action connect;
    connect.id = "help.connect_ai";
    connect.label = "Connect an AI tool...";
    connect.icon = ICON_LC_PLUG;
    connect.palette = "Help";
    connect.description = "Use SculptForge from Claude, ChatGPT (Codex), Gemini, Grok, Cursor, VS Code and other AI "
                          "tools: the exact command or settings for this install, with Copy buttons. The tool starts "
                          "SculptForge on this computer; nothing listens on the network.";
    connect.run = [this] { showConnectAi_ = true; };
    actions_.add(std::move(connect));
}

void App::setAiFeatures(bool on) {
    if (ai::aiPolicyLocked()) {
        status_ = ai::aiOffError();
        return;
    }
    applyAiFeatures(on, true);
}

void App::applyAiFeatures(bool on, bool save) {
    const bool changed = on != ai::aiEnabled();
    aiSettings_.enabled = on;
    ai::setAiEnabled(on);
    if (changed && on) {
        ai::addAiFeatureCommands(commands_, [this] { return aiSettings_; });
        registerAiActions();
        actions_.registerCommands( // cmd.brush_ai and the others (shortcuts someone gave them are kept)
            commands_, [this](const std::string& name) { runCommand(name); },
            [this](const std::string& name) { consolePrefill(name + " "); });
    } else if (changed) {
        ai::removeAiFeatureCommands(commands_);
        for (const std::string& name : ai::aiCommandNames()) actions_.removePrefix("cmd." + name);
        actions_.removePrefix("ui.brush_lab");
        actions_.removePrefix("ui.generate3d");
        actions_.removePrefix("help.connect_ai");
        showConnectAi_ = false;
        showBrushLab_ = false;
        showImageTo3D_ = false;
    }
    syncAiLink(); // AI off: live mode stops and any AI tool is disconnected
    if (!save) return;
    std::string err;
    if (!uiConfigDeterministic_ && !aiSettingsPath_.empty() && !aiSettings_.save(aiSettingsPath_, err))
        status_ = "AI features " + std::string(on ? "on" : "off") + ", but " + err;
    else
        status_ = on ? "AI features on (Preferences > AI)" : "AI features off: nothing AI runs or connects (Preferences > AI)";
}

void App::drawAiPrefs() {
    const bool locked = ai::aiPolicyLocked();
    bool on = ai::aiEnabled();
    ImGui::BeginDisabled(locked);
    if (ImGui::Checkbox("Enable AI features##prefs.ai", &on)) setAiFeatures(on);
    ImGui::EndDisabled();
    ui::HelpAction(*this, "prefs.ai_features");
    if (ai::aiManaged()) ui::ManagedBadge();
    else if (locked) ImGui::TextDisabled("Set by this computer (SF_AI or --no-ai).");
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("AI Brush and Generate 3D. Off: they are hidden and SculptForge makes no AI network calls.");
    ImGui::TextWrapped("%s", ai::kPrivacyLine);
    ImGui::PopStyleColor();
    ImGui::Spacing();
    drawAiLinkPrefs(); // live mode (PanelAiLink.cpp)
    ImGui::Spacing();
    if (ImGui::SmallButton("AI services & privacy##prefs.ai"))
        SDL_OpenURL("https://github.com/islandmed/SculptForge/blob/main/docs/ai-services.md");
}

// The studio sign-in runs on a worker (ai/Oidc.h: discovery, the device code, polling); the panel shows the code.
struct StudioSignInJob {
    std::mutex mutex;
    ai::oidc::DeviceCode code; // (userCode and the URIs only are shown)
    bool haveCode = false;
    bool opened = false; // the sign-in page, opened once from the UI thread
    std::string error;
    std::atomic<bool> done{false}, ok{false}, cancel{false};
};

void App::drawAiStudioPrefs() {
    const policy::Policy& p = policy::current();
    auto hint = [](const std::string& text) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", text.c_str());
        ImGui::PopStyleColor();
    };
    if (p.present) {
        ImGui::TextUnformatted((std::string(ICON_LC_BUILDING_2 " ") + ui::ManagedByText()).c_str());
        if (p.broken) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.6f, 0.4f, 1));
            ImGui::TextWrapped("%s %s", tr("The studio policy can't be used, so AI, network features and AI tools are off:"),
                               p.error.c_str());
            ImGui::PopStyleColor();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", p.source.c_str());
    }
    // The studio sign-in (only when the policy names an identity provider).
    if (p.signInConfigured()) {
        ImGui::SeparatorText(tr("Studio sign-in"));
        const auto state = ai::oidc::state();
        if (signInJob_ && signInJob_->done) {
            if (!signInJob_->ok && !signInJob_->cancel) status_ = signInJob_->error;
            signInJob_.reset();
        }
        if (signInJob_) {
            std::lock_guard lock(signInJob_->mutex);
            if (signInJob_->haveCode) {
                const auto& c = signInJob_->code;
                if (!signInJob_->opened) {
                    SDL_OpenURL((c.verificationUriComplete.empty() ? c.verificationUri : c.verificationUriComplete).c_str());
                    signInJob_->opened = true;
                }
                ImGui::TextWrapped("%s", tr("In your browser, sign in and enter this code:"));
                ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), "%s", signInJob_->code.userCode.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton(tr("Copy code"))) ImGui::SetClipboardText(signInJob_->code.userCode.c_str());
                if (ImGui::SmallButton(tr("Open the sign-in page")))
                    SDL_OpenURL((signInJob_->code.verificationUriComplete.empty() ? signInJob_->code.verificationUri
                                                                                  : signInJob_->code.verificationUriComplete)
                                    .c_str());
                ImGui::SameLine();
                hint(signInJob_->code.verificationUri);
            } else {
                ImGui::TextDisabled("%s", tr("Connecting..."));
            }
            if (ImGui::Button(tr("Cancel"), ImVec2(-1, 0))) signInJob_->cancel = true;
        } else if (state == ai::oidc::State::SignedIn) {
            ImGui::TextWrapped("%s %s", tr("Signed in as"), ai::oidc::signedInAs().c_str());
            if (ImGui::Button(tr("Sign out"), ImVec2(-1, 0))) {
                ai::oidc::signOut(); // (a keychain erase: quick, and the artist asked for it)
                status_ = tr("Signed out of your studio account");
            }
        } else {
            if (ImGui::Button(tr("Sign in with your studio account"), ImVec2(-1, 0))) {
                auto job = std::make_shared<StudioSignInJob>();
                signInJob_ = job;
                std::thread([job] {
                    std::string error;
                    const bool ok = ai::oidc::signIn(
                        [job](const ai::oidc::DeviceCode& c) {
                            {
                                std::lock_guard lock(job->mutex);
                                job->code = c;
                                job->code.deviceCode.clear(); // (never shown, never kept by the panel)
                                job->haveCode = true;
                            }
                            return !job->cancel;
                        },
                        [job] { return job->cancel.load(); }, error);
                    {
                        std::lock_guard lock(job->mutex);
                        job->error = error;
                    }
                    job->ok = ok;
                    job->done = true;
                }).detach();
            }
            if (p.requireSignIn) hint(tr("Your studio requires the sign-in for AI features."));
        }
        hint(tr("Signs in through your studio's own login page in your browser. Nothing on this computer listens for it; "
                "the sign-in is kept in your system keychain."));
    }
    // What is recorded, and where (core/AuditLog.h).
    ImGui::SeparatorText(tr("Audit log"));
    hint(tr("Every AI action (AI tools, AI Brush, Generate 3D) is recorded on this computer: time, tool, command and "
            "settings, never your artwork or keys."));
    const std::string path = audit::logPath();
    ImGui::TextWrapped("%s", path.c_str());
    if (ImGui::SmallButton(tr("Copy path##audit"))) ImGui::SetClipboardText(path.c_str());
    if (!p.auditPath.empty()) {
        ImGui::SameLine();
        ui::ManagedBadge();
    }
    if (p.present && (p.mcp == policy::Lock::Off || !p.mcpCategories.empty() || !p.mcpLiveAllowed)) {
        ImGui::SeparatorText(tr("AI tools (MCP)"));
        hint(p.mcp == policy::Lock::Off ? tr("AI tools can't connect: your studio turned them off.")
             : !p.mcpLiveAllowed        ? tr("AI tools work on their own scene; your studio doesn't let them control this window.")
                                        : tr("AI tools may use only the kinds of command your studio allows."));
        ui::ManagedBadge();
    }
    hint(tr("SculptForge has no telemetry: nothing about you or your work is collected."));
}

} // namespace sf
