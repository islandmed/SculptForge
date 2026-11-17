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
                                "Scripts run only when you run them: nothing you open can start one."));
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
}

void App::drawPythonScripts() {
    PythonScripting& py = python();
    if (!py.allowed() || !py.enabled()) return;
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
