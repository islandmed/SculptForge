// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// File > Maya (#42): the file-exchange bridge to the SculptForge shelf script for Maya (io/MayaBridge.h,
// integrations/maya). Off until the user picks an exchange folder with the native folder dialog; SculptForge
// then reads and writes only inside that folder, only when the user sends or gets, or (Get automatically, off
// by default) after a once-a-second modification-time check of that folder's one manifest. No sockets, no
// ports, and nothing from the folder is ever run.
#include "app/App.h"
#include "app/Ui.h"

#include "core/Json.h"
#include "io/MayaBridge.h"

#include <filesystem>
#include <fstream>
#include <iterator>

namespace sf {

namespace {

std::filesystem::path fsPath(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }

bool isFolder(const std::string& path) {
    std::error_code ec;
    return !path.empty() && std::filesystem::is_directory(fsPath(path), ec);
}

void menuAction(App& app, const char* id) {
    const Action* a = app.actions().find(id);
    if (!a) return;
    const std::string shortcut = app.actions().shortcutText(id);
    const std::string label = a->label + "##" + a->id;
    if (ImGui::MenuItem(label.c_str(), shortcut.empty() ? nullptr : shortcut.c_str(), false, !a->enabled || a->enabled()) &&
        a->run) {
        app.actions().touch(a->id);
        a->run();
    }
    ui::HelpAction(app, id);
}

// <prefs>/maya.json: {"folder": "...", "auto": false}. Read strictly: a small file, a string path to an existing
// folder (else the bridge stays off).
void loadMayaPrefs(const std::string& configDir, UiPrefs& p) {
    if (configDir.empty()) return;
    std::ifstream f(fsPath(configDir + "maya.json"), std::ios::binary);
    if (!f) return;
    std::string text(std::istreambuf_iterator<char>(f), {});
    Json j;
    std::string err;
    if (text.size() > 16384 || !Json::parse(text, j, err) || !j.isObject()) return;
    if (j["folder"].isString() && j["folder"].asString().size() < 4096 && isFolder(j["folder"].asString()))
        p.mayaFolder = j["folder"].asString();
    p.mayaAutoReceive = !p.mayaFolder.empty() && j["auto"].asBool(false);
}

void saveMayaPrefs(const std::string& configDir, const UiPrefs& p) {
    if (configDir.empty()) return;
    Json j = Json::object();
    j.set("folder", p.mayaFolder);
    j.set("auto", p.mayaAutoReceive);
    std::ofstream f(fsPath(configDir + "maya.json"), std::ios::binary | std::ios::trunc);
    f << j.dump(true) << "\n";
}

} // namespace

void App::setMayaFolder(const std::string& folder) {
    if (!isFolder(folder)) {
        status_ = "Not a folder: " + folder;
        return;
    }
    uiPrefs_.mayaFolder = folder;
    // What is already there is old news: Get from Maya loads it, Get automatically waits for the next send.
    const std::string inbox = mayaInbox(folder);
    mayaSeenRevision_ = mayaRevision(inbox);
    mayaManifestStamp_ = mayaManifestStamp(inbox);
    if (!uiConfigDeterministic_) saveMayaPrefs(configDir_, uiPrefs_);
    status_ = "Maya exchange folder: " + folder + " (choose the same one in Maya's SculptForge shelf)";
}

void App::sendToMaya() {
    if (uiPrefs_.mayaFolder.empty()) {
        status_ = "Choose the folder to exchange meshes with Maya through";
        chooseMayaFolder();
        return;
    }
    runCommand("maya_send", {{"folder", uiPrefs_.mayaFolder}});
}

void App::receiveFromMaya() {
    if (uiPrefs_.mayaFolder.empty()) {
        status_ = "Choose the folder to exchange meshes with Maya through";
        chooseMayaFolder();
        return;
    }
    const std::string inbox = mayaInbox(uiPrefs_.mayaFolder);
    mayaSeenRevision_ = mayaRevision(inbox); // a manual get counts as seen, so polling doesn't load it again
    mayaManifestStamp_ = mayaManifestStamp(inbox);
    runCommand("maya_receive", {{"folder", uiPrefs_.mayaFolder}});
    if (status_.rfind("Received", 0) == 0) camera_.frame(sceneBounds());
}

void App::drawMayaMenu() {
    if (!ImGui::BeginMenu("Maya")) return;
    menuAction(*this, "file.send_maya");
    menuAction(*this, "file.receive_maya");
    const bool hasFolder = !uiPrefs_.mayaFolder.empty();
    if (ImGui::MenuItem("Get automatically##maya", nullptr, &uiPrefs_.mayaAutoReceive, hasFolder)) {
        if (uiPrefs_.mayaAutoReceive) { // from now on: what is there already waits for Get from Maya
            const std::string inbox = mayaInbox(uiPrefs_.mayaFolder);
            mayaSeenRevision_ = mayaRevision(inbox);
            mayaManifestStamp_ = mayaManifestStamp(inbox);
        }
        if (!uiConfigDeterministic_) saveMayaPrefs(configDir_, uiPrefs_);
    }
    ui::HelpText("Get automatically", nullptr,
                 "Load what Maya sends as soon as it arrives: SculptForge checks the exchange folder's manifest once a "
                 "second (its modification time and size only). Off by default; off: use Get from Maya.",
                 "File > Maya");
    ImGui::Separator();
    menuAction(*this, "file.maya_folder");
    if (hasFolder) {
        ImGui::TextDisabled("Folder: %s", uiPrefs_.mayaFolder.c_str());
        if (ImGui::MenuItem("Turn the Maya bridge off##maya")) {
            uiPrefs_.mayaFolder.clear();
            uiPrefs_.mayaAutoReceive = false;
            if (!uiConfigDeterministic_) saveMayaPrefs(configDir_, uiPrefs_);
            status_ = "Maya bridge off: SculptForge no longer reads or writes the exchange folder";
        }
        ui::HelpText("Turn the Maya bridge off", nullptr, "Forget the exchange folder; nothing in it is deleted.",
                     "File > Maya");
    } else {
        ImGui::TextDisabled("Off: choose an exchange folder first.");
    }
    ImGui::TextDisabled("Needs the SculptForge shelf script in Maya\n(Help > User Guide > Working with Maya).");
    ImGui::EndMenu();
}

void App::pollMaya() {
    if (!mayaInitialised_) {
        mayaInitialised_ = true;
        if (!uiConfigDeterministic_) loadMayaPrefs(configDir_, uiPrefs_);
        if (!uiPrefs_.mayaFolder.empty()) { // what was sent before this start is old news
            const std::string inbox = mayaInbox(uiPrefs_.mayaFolder);
            mayaSeenRevision_ = mayaRevision(inbox);
            mayaManifestStamp_ = mayaManifestStamp(inbox);
        }
    }
    if (!uiPrefs_.mayaAutoReceive || uiPrefs_.mayaFolder.empty() || doc_.sculptor.strokeActive()) return;
    // UI tests check every frame (their frames are simulated); the app once a second.
    const Uint64 now = SDL_GetTicksNS();
    const Uint64 interval = options_.uiTest.empty() ? 1'000'000'000ull : 0;
    if (mayaLastPollNs_ != 0 && now - mayaLastPollNs_ < interval) return;
    mayaLastPollNs_ = now;
    // The cheap check first: the manifest's modification time and size. Only when they changed is the (small,
    // size-capped) manifest read for its revision.
    const std::string inbox = mayaInbox(uiPrefs_.mayaFolder);
    const long long t = mayaManifestStamp(inbox);
    if (t == 0 || t == mayaManifestStamp_) return;
    mayaManifestStamp_ = t;
    const int64_t revision = mayaRevision(inbox);
    if (revision >= 0 && revision != mayaSeenRevision_) receiveFromMaya();
}

} // namespace sf
