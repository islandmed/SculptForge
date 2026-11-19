// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Python scripting's interface (app/Python.h): Preferences > Scripts, and the Script palette's list of the Scripts
// folder (File > Script). A script in the list runs when it is clicked, never on its own.
#include "app/App.h"

#include "app/I18n.h"
#include "app/NativeDialog.h"
#include "app/Python.h"
#include "app/Theme.h"
#include "app/Ui.h"

#include <IconsLucide.h>
#include <imgui.h>

#include <cctype>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace sf {

namespace {

namespace fs = std::filesystem;

fs::path fsPath(const std::string& utf8) { return fs::path(std::u8string(utf8.begin(), utf8.end())); }

std::string fileUrl(std::string p) {
    for (char& c : p)
        if (c == '\\') c = '/';
    std::string url = p.rfind('/', 0) == 0 ? "file://" : "file:///";
    const char* hex = "0123456789ABCDEF";
    for (unsigned char c : p) {
        if (std::isalnum(c) || std::strchr("/:-_.~", c)) url += char(c);
        else url += {'%', hex[c >> 4], hex[c & 15]};
    }
    return url;
}

} // namespace

PythonScripting& App::python() {
    if (!python_) python_ = std::make_unique<PythonScripting>(*this);
    return *python_;
}

void App::runPythonFile(const std::string& path) {
    if (!layout_.consoleOpen) setConsoleOpen(true); // (what it prints, and its errors, show there)
    const std::u8string name = fsPath(path).filename().u8string();
    console_.print(Console::Kind::Command, "Running " + std::string(name.begin(), name.end()));
    const PythonScripting::Result r = python().runFile(path);
    if (r == PythonScripting::Result::Ok) status_ = "Ran " + std::string(name.begin(), name.end());
    else if (r == PythonScripting::Result::Error) status_ = std::string(name.begin(), name.end()) + " stopped with an error (see the console)";
}

void App::drawPythonPrefs() {
    PythonScripting& py = python();
    std::string why;
    if (!py.allowed(&why)) {
        ImGui::BeginDisabled();
        bool off = false;
        ImGui::Checkbox(tr("Enable Python scripts##prefs.python"), &off);
        ImGui::EndDisabled();
        ui::ManagedBadge();
        ImGui::TextDisabled("%s", tr(why.c_str()));
        return;
    }
    bool on = py.enabled();
    if (ImGui::Checkbox(tr("Enable Python scripts##prefs.python"), &on)) py.setEnabled(on);
    ui::HelpText("Enable Python scripts", nullptr,
                 "Lets the console and the Scripts folder run Python. A Python script is a program: it can do anything "
                 "you can on this computer (files, network, other programs). Only run scripts you trust. Off by default.",
                 "Preferences > Scripts");
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", tr("A Python script can do anything a program on this computer can. Only run scripts you trust. "
                                "Scripts run only when you run them, or at start-up if you tick one below; nothing you open can start one."));
    ImGui::PopStyleColor();
    if (py.loaded()) ImGui::TextDisabled("%s", (std::string(tr("Python")) + " " + py.version()).c_str());
    if (ImGui::Button(tr("Open Scripts folder##prefs.python"))) {
        const std::string dir = py.scriptsFolder();
        std::error_code ec;
        if (!dir.empty()) fs::create_directories(fsPath(dir), ec);
        if (dir.empty() || !sf::dialogs::openUrl(fileUrl(dir).c_str())) status_ = "The Scripts folder is " + dir;
    }
    ui::HelpText("Open Scripts folder", nullptr,
                 "The .py files in this folder are listed under File > Script. Packages you install into its "
                 "site-packages folder can be imported.",
                 "Preferences > Scripts");
    if (!py.enabled()) return;
    // Run at start-up: ticked per script, only scripts in the Scripts folder (and again after it changes; Python.h).
    // Ticked ones gone from the folder stay listed, so they can be turned off.
    std::vector<std::string> list = py.scripts();
    for (const std::string& p : py.startupScripts())
        if (std::find(list.begin(), list.end(), p) == list.end()) list.push_back(p);
    if (list.empty()) return;
    ImGui::SeparatorText(tr("Run at start-up"));
    for (const std::string& path : list) {
        const std::u8string name8 = fsPath(path).filename().u8string();
        const std::string name(name8.begin(), name8.end());
        bool on = py.runsAtStartup(path);
        if (ImGui::Checkbox((name + "##startup." + path).c_str(), &on) && !py.setRunAtStartup(path, on))
            status_ = name + " isn't in the Scripts folder: only scripts there can run at start-up";
        if (py.startupChanged(path)) { // (its own line: the narrow Preferences window would clip it)
            ImGui::Indent(ImGui::GetFrameHeight());
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(theme::danger, "%s", tr("changed, didn't run"));
            ImGui::SameLine();
            if (ImGui::SmallButton((std::string(tr("Trust this version")) + "##trust." + path).c_str())) {
                py.setRunAtStartup(path, true);
                status_ = name + " will run at start-up as it is now";
            }
            ui::HelpText("Trust this version", nullptr,
                         "The script changed since you chose to run it at start-up, so it didn't run. Look at what changed; "
                         "if you trust it, this runs it at start-up from now on (it doesn't run it now).",
                         "Preferences > Scripts");
            ImGui::Unindent(ImGui::GetFrameHeight());
        }
    }
    ui::HelpText("Run at start-up", nullptr,
                 "Ticked scripts run each time SculptForge starts (with Python on), so the buttons and panels they add are "
                 "there. A script that changed doesn't run until you tick it again.",
                 "Preferences > Scripts");
}

// A script's panel (sf.ui.panel): a section of this palette, so My Panel can hold it like any other. Values live
// here; the script hears of a change once (a slider when let go, a check box when clicked).
void App::drawPythonPanel(python::UiPanel& panel) {
    const std::string section = std::string(PythonScripting::kActionPrefix) + panel.id;
    if (!subpalette(section.c_str(), panel.title.c_str())) return;
    ImGui::PushID(section.c_str());
    std::string run, changedId;
    double changed = 0.0;
    for (python::UiControl& c : panel.controls) {
        ImGui::PushID(c.id.empty() ? c.label.c_str() : c.id.c_str());
        switch (c.kind) {
        case python::UiControl::Kind::Label:
            ImGui::TextWrapped("%s", c.label.c_str());
            break;
        case python::UiControl::Kind::Separator:
            ImGui::Separator();
            break;
        case python::UiControl::Kind::Button: {
            const std::string id = PythonScripting::kActionPrefix + c.action;
            if (actions_.find(id)) ui::ActionButton(*this, id.c_str(), ImVec2(-1, 0), ui::Look::IconLabel);
            break;
        }
        case python::UiControl::Kind::Slider: {
            ImGui::SetNextItemWidth(-1);
            if (c.integer) {
                int v = int(std::lround(c.value));
                if (ImGui::SliderInt(("##" + c.id).c_str(), &v, int(c.min), int(c.max), (c.label + " %d").c_str())) c.value = v;
            } else {
                float v = float(c.value);
                if (ImGui::SliderFloat(("##" + c.id).c_str(), &v, float(c.min), float(c.max), (c.label + " %.3g").c_str())) c.value = v;
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) changedId = c.id, changed = c.value;
            break;
        }
        case python::UiControl::Kind::Checkbox: {
            bool v = c.value != 0.0;
            if (ImGui::Checkbox(c.label.c_str(), &v)) changedId = c.id, changed = v ? 1.0 : 0.0;
            break;
        }
        }
        if (!c.help.empty() && c.kind != python::UiControl::Kind::Button) ui::HelpText(c.label.c_str(), nullptr, c.help.c_str(), panel.title.c_str());
        ImGui::PopID();
    }
    ImGui::PopID();
    // (after the loop: the script may declare the panel again, which replaces this list)
    if (!changedId.empty()) python().setValue(panel.id, changedId, changed);
}

void App::drawPythonScripts() {
    PythonScripting& py = python();
    if (!py.allowed() || !py.enabled()) return;
    // The panels scripts declared (a callback that declares them again is applied after the loop: endDraw).
    py.beginDraw();
    for (python::UiPanel& p : py.panels()) drawPythonPanel(p);
    py.endDraw();
    const auto& scripts = py.scripts(); // (a folder listing at most every two seconds, only while this draws)
    ImGui::SeparatorText(tr("Python scripts"));
    if (scripts.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", tr("Put .py files in the Scripts folder (Preferences > Scripts) to list them here."));
        ImGui::PopStyleColor();
        return;
    }
    std::string run;
    for (const std::string& path : scripts) {
        const std::u8string stem = fsPath(path).stem().u8string();
        const std::string label = std::string(ICON_LC_FILE_CODE) + "  " + std::string(stem.begin(), stem.end()) + "##py." + path;
        if (ImGui::MenuItem(label.c_str())) run = path;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", path.c_str());
    }
    if (!run.empty()) runPythonFile(run);
}

} // namespace sf
