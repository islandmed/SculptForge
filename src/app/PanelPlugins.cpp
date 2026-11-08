// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sandboxed plugins in the app (core/Plugin.h): the plugin window (its parameters, drawn by us from its manifest;
// Apply / Add Part; Stop while it runs), the Plugins group under Tool > Reshape, and the rows in a pack's
// details. A run copies the mesh on the UI thread, runs on a worker and is committed back on the UI thread.
#include "app/App.h"
#include "app/I18n.h"
#include "app/Palettes.h"
#include "app/Ui.h"

#include <IconsLucide.h>
#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace sf {

namespace {
std::filesystem::path fsPath(const std::string& p) { return std::filesystem::path(std::u8string(p.begin(), p.end())); }
} // namespace

void App::refreshPlugins() {
    std::vector<std::string> messages;
    plugins_.list = installedPlugins(&messages);
    for (const std::string& m : messages) SDL_Log("%s", m.c_str());
    if (!plugins_.open.empty() && !findPluginUi(plugins_.open)) plugins_.open.clear();
}

const InstalledPlugin* App::findPluginUi(const std::string& key) const {
    for (const InstalledPlugin& p : plugins_.list)
        if (p.key == key) return &p;
    return nullptr;
}

void App::openPlugin(const std::string& key) {
    const InstalledPlugin* p = findPluginUi(key);
    if (!p) return;
    plugins_.open = key;
    std::vector<double>& v = plugins_.values[key];
    if (v.size() != p->manifest.params.size()) {
        v.clear();
        for (const PluginParam& q : p->manifest.params) v.push_back(q.def);
    }
}

void App::startPluginRun(const InstalledPlugin& p) {
    if (plugins_.running) return;
    std::string err;
    if (p.off) {
        status_ = p.manifest.name + " is turned off: " + p.offWhy;
        return;
    }
    if (!pluginCanRunOn(doc_, p, err)) {
        status_ = err;
        return;
    }
    std::vector<uint8_t> wasm;
    {
        std::ifstream f(fsPath(p.file), std::ios::binary);
        wasm.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }
    if (wasm.empty() || wasm.size() != p.bytes) {
        status_ = p.manifest.name + ": its file changed or can't be read; reinstall its pack";
        return;
    }
    if (plugins_.worker.joinable()) plugins_.worker.join();
    if (doc_.sculptor.strokeActive()) endStroke();
    doc_.syncBvh();
    plugins_.runKey = p.key;
    plugins_.input = pluginInputFrom(doc_.mesh);
    plugins_.output = {};
    plugins_.error.clear();
    plugins_.cancel = false;
    plugins_.done = false;
    plugins_.running = true;
    plugins_.started = std::chrono::steady_clock::now();
    PluginRunOptions opts;
    opts.params = plugins_.values[p.key];
    opts.cancel = &plugins_.cancel;
    plugins_.worker = std::thread([this, wasm = std::move(wasm), manifest = p.manifest, opts] {
        plugins_.failure = runPlugin(wasm, manifest, plugins_.input, opts, plugins_.output, plugins_.error);
        plugins_.done = true;
    });
}

void App::pollPluginRun() {
    if (!plugins_.running || !plugins_.done) return;
    plugins_.worker.join();
    plugins_.running = false;
    const InstalledPlugin* p = findPluginUi(plugins_.runKey);
    if (!p) return;
    const std::string name = p->manifest.name;
    if (plugins_.failure == PluginFailure::Cancelled) {
        status_ = name + ": stopped. The mesh is unchanged.";
    } else if (plugins_.failure != PluginFailure::None) {
        status_ = name + ": " + plugins_.error + ". The mesh is unchanged.";
        if (pluginFailureDisables(plugins_.failure)) {
            std::string err;
            setPluginOff(*p, true, plugins_.error, err);
            status_ += " It is turned off until you turn it on again.";
            refreshPlugins();
        }
        SDL_Log("Plugin %s: %s", plugins_.runKey.c_str(), plugins_.error.c_str());
    } else {
        std::string msg;
        const bool ok = commitPluginOutput(doc_, *p, plugins_.input, std::move(plugins_.output), msg);
        status_ = msg;
        if (ok && p->manifest.kind == PluginKind::Generator && !onScreen(doc_.partBounds(doc_.activePart)))
            camera_.frame(sceneBounds());
        updateTitle();
    }
    plugins_.input = {};
    plugins_.output = {};
}

// One row per installed plugin (of one pack when PACKID is given): its name opens its window.
void App::drawPluginButtons(const std::string& packId, bool endLine) {
    for (const InstalledPlugin& p : plugins_.list) {
        if (!packId.empty() && p.packId != packId) continue;
        const std::string label = std::string(p.manifest.kind == PluginKind::Generator ? ICON_LC_BOX "  " : ICON_LC_WAND_SPARKLES "  ") +
                                  p.manifest.name + (p.off ? std::string(" (") + tr("off") + ")" : "") + "###plugin." + p.packId + "." +
                                  p.manifest.id;
        if (ImGui::SmallButton(label.c_str())) openPlugin(p.key);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s %s, %s - %s", pluginKindLabel(p.manifest.kind), p.manifest.version.c_str(),
                              p.manifest.author.c_str(), p.packName.c_str());
        ImGui::SameLine();
    }
    if (endLine) ImGui::NewLine();
}

// Tool > Reshape > Plugins (only when a pack brought some).
void App::drawDeformPlugins() {
    if (plugins_.list.empty()) return;
    tray::GroupLabel(*this, "Plugins");
    drawPluginButtons("");
}

void App::drawPluginWindow() {
    pollPluginRun();
    if (plugins_.open.empty()) return;
    const InstalledPlugin* p = findPluginUi(plugins_.open);
    if (!p) {
        plugins_.open.clear();
        return;
    }
    const PluginManifest& m = p->manifest;
    const float s = uiScale_;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.45f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(380 * s, 0), ImGuiCond_Appearing);
    bool open = true;
    const std::string title = m.name + "###pluginwindow";
    if (!ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                                                ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        if (!open && !plugins_.running) plugins_.open.clear();
        return;
    }
    ImGui::TextDisabled(tr("%s %s by %s, from %s"), tr(pluginKindLabel(m.kind)), m.version.c_str(), m.author.c_str(),
                        p->packName.c_str());
    if (!m.description.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 360 * s);
        ImGui::TextUnformatted(m.description.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::Separator();
    std::vector<double>& v = plugins_.values[p->key];
    v.resize(m.params.size());
    ImGui::BeginDisabled(plugins_.running);
    for (size_t i = 0; i < m.params.size(); ++i) {
        const PluginParam& q = m.params[i];
        const std::string id = q.label + "##pp." + q.id;
        if (q.type == PluginParam::Type::Bool) {
            bool b = v[i] != 0.0;
            if (ui::Checkbox(id.c_str(), &b)) v[i] = b ? 1.0 : 0.0;
        } else if (q.type == PluginParam::Type::Int) {
            int k = int(v[i]);
            ImGui::SetNextItemWidth(220 * s);
            if (ImGui::SliderInt(id.c_str(), &k, int(q.min), int(q.max))) v[i] = k;
        } else {
            double x = v[i];
            ImGui::SetNextItemWidth(220 * s);
            if (ImGui::SliderScalar(id.c_str(), ImGuiDataType_Double, &x, &q.min, &q.max, "%.3f")) v[i] = std::clamp(x, q.min, q.max);
        }
        if (!q.help.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", q.help.c_str());
    }
    if (!m.params.empty() && ImGui::SmallButton(tr("Defaults##plugin")))
        for (size_t i = 0; i < m.params.size(); ++i) v[i] = m.params[i].def;
    ImGui::EndDisabled();
    ImGui::Spacing();
    if (p->off) {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(240, 110, 100, 255));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 360 * s);
        ImGui::Text("%s %s", tr("Turned off:"), p->offWhy.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        if (ImGui::Button(tr("Turn on again##plugin"), ImVec2(160 * s, 0))) {
            std::string err;
            status_ = setPluginOff(*p, false, "", err) ? "Turned on plugin " + m.name : err;
            refreshPlugins();
        }
    } else if (plugins_.running) {
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - plugins_.started).count();
        ImGui::Text(tr("Running... %.1f s"), secs);
        ImGui::SameLine();
        if (ImGui::Button(tr("Stop##plugin"), ImVec2(100 * s, 0))) plugins_.cancel = true;
    } else {
        const char* run = m.kind == PluginKind::Generator ? tr("Add Part##plugin") : tr("Apply##plugin");
        if (ImGui::Button(run, ImVec2(140 * s, 0))) startPluginRun(*p);
        ui::HelpText(m.kind == PluginKind::Generator ? "Add Part" : "Apply", nullptr,
                     m.kind == PluginKind::Generator
                         ? "Runs the plugin on a copy of the active Part and adds what it makes as a new Part."
                         : "Runs the plugin on a copy of the active Part and applies the result as one undo step. "
                           "The mask and hidden faces hold their vertices.",
                     "Tool > Reshape > Plugins");
    }
    ImGui::TextDisabled("%s", tr("Sandboxed: no files, network or system access; time and memory limits."));
    ImGui::End();
    if (!open && !plugins_.running) plugins_.open.clear();
}

} // namespace sf
