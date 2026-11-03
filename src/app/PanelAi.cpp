// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Preferences > AI > Enable AI features (ai/AiPolicy.h). Off: the AI commands (brush_ai, image_to_3d,
// text_to_3d, comfyui_check) leave the command registry, so the command palette, scripts and the console
// can't reach them; AI Brush and Generate 3D leave the shelves and menus and their windows close; and every
// AI network entry point refuses. Saved in ai.json at once, so the CLI, Python and MCP server follow it.
// SF_AI=off (or --no-ai) locks it for studios.
#include "app/App.h"
#include "app/Ui.h"

#include "ai/AiCommands.h"

#include <IconsLucide.h>
#include <imgui.h>

namespace sf {

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
}

void App::setAiFeatures(bool on) {
    if (ai::aiPolicyLocked()) {
        status_ = ai::aiOffError();
        return;
    }
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
        showBrushLab_ = false;
        showImageTo3D_ = false;
    }
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
    if (locked) ImGui::TextDisabled("Set by this computer (SF_AI or --no-ai).");
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("AI Brush and Generate 3D. Off: they are hidden and SculptForge makes no AI network calls.");
    ImGui::TextWrapped("%s", ai::kPrivacyLine);
    ImGui::PopStyleColor();
    if (ImGui::SmallButton("AI services & privacy##prefs.ai"))
        SDL_OpenURL("https://github.com/islandmed/SculptForge/blob/main/docs/ai-services.md");
}

} // namespace sf
