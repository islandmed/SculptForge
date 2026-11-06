// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// File > Send to send/receive through the Bridge bridge, Bridge and STL files.
#include "app/App.h"

#include "io/BridgeProtocol.h"
#include "io/BridgeIO.h"

#include <filesystem>

namespace sf {

namespace {

const SDL_DialogFileFilter kBridgeFilters[] = {{"Bridge files", bridgefile::kDialogPattern}, {"All files", "*"}};

long long fileTime(const std::string& path) {
    if (path.empty()) return 0;
    std::error_code ec;
    auto t = std::filesystem::last_write_time(std::filesystem::path(std::u8string(path.begin(), path.end())), ec);
    return ec ? 0 : static_cast<long long>(t.time_since_epoch().count());
}

} // namespace

void SDLCALL App::onBridgeFileChosen(void* userdata, const char* const* files, int) {
    wakeMainLoop(); // a file dialog answered (maybe on another thread): redraw now
    if (!files || !files[0]) return;
    auto* req = static_cast<BridgeDialog*>(userdata);
    std::string path = files[0];
    if (req->kind != BridgeFile::Import &&
        std::filesystem::path(std::u8string(path.begin(), path.end())).extension().empty())
        path += req->kind == BridgeFile::ExportStl ? ".stl" : bridgefile::kExt;
    std::lock_guard lock(req->app->pendingMutex_);
    req->app->bridgePendingFiles_.push_back({req->kind, std::move(path)});
}

void App::runBridgeCommand(const std::string& name, const Args& args) {
    Args a = args;
    if (bridgeFolder_[0] && (name == "bridge_send" || name == "bridge_receive")) a["folder"] = std::string(bridgeFolder_);
    runCommand(name, a);
    // A new mesh arrived: frame it (runCommand does this for "open").
    if ((name == "bridge_receive" || name == "bridge_import") &&
        (status_.rfind("Received", 0) == 0 || status_.rfind("Opened", 0) == 0))
        camera_.frame(sceneBounds());
    if (name == "bridge_send") bridgeListTime_ = fileTime(bridgeObjectListPath(bridgeFolder_[0] ? bridgeFolder_ : bridgeDefaultRoot()));
}

void App::drawBridgeMenu() {
    if (!bridgeInitialised_) pollBridge(); // sets up the dialogs
    if (!ImGui::BeginMenu(tr("Bridge"))) return;
    const std::string root = bridgeFolder_[0] ? std::string(bridgeFolder_) : bridgeDefaultRoot();
    const bool bridge = !root.empty();
    if (ImGui::MenuItem(tr("Send"))) {
        runBridgeCommand("bridge_send");
        if (bridge) bridgeAutoReceive_ = true; // a round trip comes back the same way
    }
    if (!bridge && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", bridgeUnavailableMessage().c_str());
    if (ImGui::MenuItem(tr("Receive"))) runBridgeCommand("bridge_receive");
    if (!bridge && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", bridgeUnavailableMessage().c_str());
    if (ImGui::MenuItem(tr("Receive automatically"), nullptr, &bridgeAutoReceive_, bridge) && bridgeAutoReceive_)
        bridgeListTime_ = fileTime(bridgeObjectListPath(root));
    if (bridge) ImGui::TextDisabled(tr("Bridge folder: %s"), root.c_str());
    else ImGui::TextDisabled(tr("No default bridge folder on this system;\nBridge files still import and export."));
    ImGui::SetNextItemWidth(260.0f * uiScale_);
    ImGui::InputTextWithHint("##bridgefolder", tr("Custom bridge folder (optional)"), bridgeFolder_, sizeof(bridgeFolder_));
    ImGui::Separator();
    if (ImGui::MenuItem(tr("Import Bridge file...")))
        SDL_ShowOpenFileDialog(onBridgeFileChosen, &bridgeDialogs_[int(BridgeFile::Import)], window_, kBridgeFilters, 2, nullptr, false);
    if (ImGui::MenuItem(tr("Export Bridge file...")))
        SDL_ShowSaveFileDialog(onBridgeFileChosen, &bridgeDialogs_[int(BridgeFile::Export)], window_, kBridgeFilters, 2, nullptr);
    ImGui::EndMenu();
}

void App::pollBridge() {
    if (!bridgeInitialised_) {
        bridgeInitialised_ = true;
        for (int i = 0; i < int(BridgeFile::Count); ++i) bridgeDialogs_[i] = {this, BridgeFile(i)};
        // Register this executable Bridge target (BridgeApps/SculptForge/Bridge_Config.txt).
        if (const char* base = SDL_GetBasePath()) {
#ifdef _WIN32
            setBridgeApplicationPath(std::string(base) + "sculptforge.exe");
#else
            setBridgeApplicationPath(std::string(base) + "sculptforge");
#endif
        }
    }
    std::vector<std::pair<BridgeFile, std::string>> files;
    {
        std::lock_guard lock(pendingMutex_);
        files.swap(bridgePendingFiles_);
    }
    for (const auto& [kind, path] : files) {
        // Importing replaces the scene and its undo history, so ask about unsaved work first.
        if (kind == BridgeFile::Import)
            confirmDiscard([this, p = path] { runBridgeCommand("bridge_import", {{"path", p}}); });
        else runBridgeCommand(kind == BridgeFile::Export ? "bridge_export" : "export_stl", {{"path", path}});
    }

    // Auto-receive: the other application rewrites Bridge_ObjectList.txt on every Bridge send. Check once a second.
    if (!bridgeAutoReceive_ || doc_.sculptor.strokeActive()) return;
    const Uint64 now = SDL_GetTicksNS();
    if (now - bridgeLastPollNs_ < 1'000'000'000ull) return;
    bridgeLastPollNs_ = now;
    const std::string list = bridgeObjectListPath(bridgeFolder_[0] ? std::string(bridgeFolder_) : bridgeDefaultRoot());
    const long long t = fileTime(list);
    if (t == 0 || t == bridgeListTime_) return;
    bridgeListTime_ = t;
    runBridgeCommand("bridge_receive");
}

} // namespace sf
