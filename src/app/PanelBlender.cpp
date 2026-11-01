// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// File > Blender: the bridge to the SculptForge add-on for Blender (io/BlenderBridge.h, addons/blender).
// Blender writes to_sculptforge/manifest.json; this polls it once a second and loads each new revision.
#include "app/App.h"
#include "app/I18n.h"
#include "app/Ui.h"

#include "io/BlenderBridge.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace sf {

namespace {

// What the add-on should start: the AppImage itself (not its temporary mount), the .app bundle on macOS, else
// this executable.
std::string bridgeExecutable() {
    if (const char* appimage = std::getenv("APPIMAGE"); appimage && *appimage) return appimage;
#if !defined(_WIN32) && !defined(__APPLE__)
    // A Flatpak: /app/bin/sculptforge exists only inside the sandbox; Blender starts the launcher Flatpak exports.
    if (const char* id = std::getenv("FLATPAK_ID"); id && *id) {
        std::ifstream f("/.flatpak-info");
        const std::string launcher =
            flatpakHostLauncher(std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()));
        if (!launcher.empty()) return launcher;
    }
#endif
    const char* base = SDL_GetBasePath();
    if (!base) return {};
    std::string dir = base;
#ifdef __APPLE__
    if (const size_t app = dir.find(".app/"); app != std::string::npos) return dir.substr(0, app + 4);
#endif
#ifdef _WIN32
    return dir + "sculptforge.exe";
#else
    return dir + "sculptforge";
#endif
}

// A menu row for an action, as in the palettes (label, shortcut, Ctrl-hover help).
void menuAction(App& app, const char* id) {
    const Action* a = app.actions().find(id);
    if (!a) return;
    const std::string shortcut = app.actions().shortcutText(id);
    const std::string label = std::string(trAction(a->id, a->label)) + "##" + a->id;
    if (ImGui::MenuItem(label.c_str(), shortcut.empty() ? nullptr : shortcut.c_str(), false, !a->enabled || a->enabled()) &&
        a->run) {
        app.actions().touch(a->id);
        a->run();
    }
    ui::HelpAction(app, id);
}

} // namespace

void App::receiveFromBlender() {
    const std::string root = blenderExchangeRoot();
    if (!root.empty()) { // a manual receive counts as seen, so polling doesn't load it again
        blenderSeenRevision_ = bridgeRevision(blenderInbox(root));
    }
    runCommand("blender_receive");
    if (status_.rfind("Received", 0) == 0) camera_.frame(sceneBounds());
}

void App::drawBlenderMenu() {
    if (!ImGui::BeginMenu("Blender")) return;
    menuAction(*this, "file.send_blender");
    menuAction(*this, "file.receive_blender");
    ImGui::MenuItem("Receive automatically##blender", nullptr, &blenderAutoReceive_);
    ui::HelpText("Receive automatically", nullptr,
                 "Load what Blender sends as soon as it arrives (checked once a second). Off: use Receive from "
                 "Blender.",
                 "File > Blender");
    const std::string root = blenderExchangeRoot();
    ImGui::TextDisabled("Needs the SculptForge add-on in Blender\n(Help > User Guide > Working with Blender).");
    if (ImGui::IsItemHovered() && !root.empty()) ImGui::SetTooltip("Exchange folder: %s", root.c_str());
    ImGui::EndMenu();
}

void App::pollBlender() {
    const std::string root = blenderExchangeRoot();
    if (root.empty()) return;
    const std::string inbox = blenderInbox(root);
    if (!blenderInitialised_) {
        blenderInitialised_ = true;
        writeBridgeAppInfo(root, bridgeExecutable(), SCULPTFORGE_VERSION);
        // What was sent before this start is old news, unless the add-on started us to load it.
        blenderSeenRevision_ = bridgeRevision(inbox);
        if (options_.blender && blenderSeenRevision_ >= 0) receiveFromBlender();
    }
    // UI tests check every frame (their frames are simulated); the app once a second, and at once when a heartbeat
    // is due (at start, and after Send to Blender).
    const Uint64 now = SDL_GetTicksNS();
    const Uint64 interval = options_.uiTest.empty() ? 1'000'000'000ull : 0;
    if (blenderLastBeatNs_ != 0 && blenderLastPollNs_ != 0 && now - blenderLastPollNs_ < interval) return;
    blenderLastPollNs_ = now;

    std::error_code ec;
    if (!std::filesystem::is_directory(std::filesystem::path(std::u8string(inbox.begin(), inbox.end())), ec))
    {
        blenderLastBeatNs_ = std::max<Uint64>(blenderLastBeatNs_, 1); // nothing due: back to once a second
        return; // the add-on has never sent anything: no heartbeat either
    }
    if (blenderLastBeatNs_ == 0 || now - blenderLastBeatNs_ >= 2'000'000'000ull) { // at once, then every 2 s
        blenderLastBeatNs_ = now;
        writeBridgeHeartbeat(root);
    }
    if (!blenderAutoReceive_ || doc_.sculptor.strokeActive()) return;
    // The revision, not the file time: a copied or quickly rewritten file can keep its time (Windows copies keep
    // it; FAT and network drives store it coarsely). The manifest is a few hundred bytes.
    const int64_t revision = bridgeRevision(inbox);
    if (revision >= 0 && revision != blenderSeenRevision_) receiveFromBlender();
}

} // namespace sf
