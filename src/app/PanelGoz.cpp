// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// File > ZBrush (GoZ): send/receive through the GoZ bridge, GoZ and STL files.
#include "app/App.h"

#include "io/GozIO.h"

#include <filesystem>

namespace sf {

namespace {

const SDL_DialogFileFilter kGozFilters[] = {{"ZBrush GoZ", "GoZ;goz"}, {"All files", "*"}};

long long fileTime(const std::string& path) {
    if (path.empty()) return 0;
    std::error_code ec;
    auto t = std::filesystem::last_write_time(std::filesystem::path(std::u8string(path.begin(), path.end())), ec);
    return ec ? 0 : static_cast<long long>(t.time_since_epoch().count());
}

} // namespace

void SDLCALL App::onGozFileChosen(void* userdata, const char* const* files, int) {
    if (!files || !files[0]) return;
    auto* req = static_cast<GozDialog*>(userdata);
    std::string path = files[0];
    if (req->kind != GozFile::Import &&
        std::filesystem::path(std::u8string(path.begin(), path.end())).extension().empty())
        path += req->kind == GozFile::ExportStl ? ".stl" : ".GoZ";
    std::lock_guard lock(req->app->pendingMutex_);
    req->app->gozPendingFiles_.push_back({req->kind, std::move(path)});
}

void App::runGozCommand(const std::string& name, const Args& args) {
    Args a = args;
    if (gozFolder_[0] && (name == "goz_send" || name == "goz_receive")) a["folder"] = std::string(gozFolder_);
    runCommand(name, a);
    // A new mesh arrived: frame it (runCommand does this for "open").
    if ((name == "goz_receive" || name == "goz_import") &&
        (status_.rfind("Received", 0) == 0 || status_.rfind("Opened", 0) == 0))
        camera_.frame(sceneBounds());
    if (name == "goz_send") gozListTime_ = fileTime(gozObjectListPath(gozFolder_[0] ? gozFolder_ : gozDefaultRoot()));
}

void App::drawGozMenu() {
    if (!gozInitialised_) pollGoz(); // sets up the dialogs
    if (!ImGui::BeginMenu("ZBrush (GoZ)")) return;
    const std::string root = gozFolder_[0] ? std::string(gozFolder_) : gozDefaultRoot();
    const bool bridge = !root.empty();
    if (ImGui::MenuItem("Send to ZBrush")) {
        runGozCommand("goz_send");
        if (bridge) gozAutoReceive_ = true; // a round trip comes back the same way
    }
    if (!bridge && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", gozUnavailableMessage().c_str());
    if (ImGui::MenuItem("Receive from ZBrush")) runGozCommand("goz_receive");
    if (!bridge && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", gozUnavailableMessage().c_str());
    if (ImGui::MenuItem("Receive automatically", nullptr, &gozAutoReceive_, bridge) && gozAutoReceive_)
        gozListTime_ = fileTime(gozObjectListPath(root));
    if (bridge) ImGui::TextDisabled("GoZ folder: %s", root.c_str());
    else ImGui::TextDisabled("ZBrush runs on Windows and macOS only;\nGoZ files still import and export.");
    ImGui::SetNextItemWidth(260.0f * uiScale_);
    ImGui::InputTextWithHint("##gozfolder", "Custom GoZ folder (optional)", gozFolder_, sizeof(gozFolder_));
    ImGui::Separator();
    if (ImGui::MenuItem("Import GoZ..."))
        SDL_ShowOpenFileDialog(onGozFileChosen, &gozDialogs_[int(GozFile::Import)], window_, kGozFilters, 2, nullptr, false);
    if (ImGui::MenuItem("Export GoZ..."))
        SDL_ShowSaveFileDialog(onGozFileChosen, &gozDialogs_[int(GozFile::Export)], window_, kGozFilters, 2, nullptr);
    ImGui::EndMenu();
}

void App::pollGoz() {
    if (!gozInitialised_) {
        gozInitialised_ = true;
        for (int i = 0; i < int(GozFile::Count); ++i) gozDialogs_[i] = {this, GozFile(i)};
        // Register this executable as ZBrush's GoZ target (GoZApps/SculptForge/GoZ_Config.txt).
        if (const char* base = SDL_GetBasePath()) {
#ifdef _WIN32
            setGozApplicationPath(std::string(base) + "sculptforge.exe");
#else
            setGozApplicationPath(std::string(base) + "sculptforge");
#endif
        }
    }
    std::vector<std::pair<GozFile, std::string>> files;
    {
        std::lock_guard lock(pendingMutex_);
        files.swap(gozPendingFiles_);
    }
    for (const auto& [kind, path] : files) {
        // Importing replaces the scene and its undo history, so ask about unsaved work first.
        if (kind == GozFile::Import)
            confirmDiscard([this, p = path] { runGozCommand("goz_import", {{"path", p}}); });
        else runGozCommand(kind == GozFile::Export ? "goz_export" : "export_stl", {{"path", path}});
    }

    // Auto-receive: ZBrush rewrites GoZ_ObjectList.txt on every GoZ send. Check once a second.
    if (!gozAutoReceive_ || doc_.sculptor.strokeActive()) return;
    const Uint64 now = SDL_GetTicksNS();
    if (now - gozLastPollNs_ < 1'000'000'000ull) return;
    gozLastPollNs_ = now;
    const std::string list = gozObjectListPath(gozFolder_[0] ? std::string(gozFolder_) : gozDefaultRoot());
    const long long t = fileTime(list);
    if (t == 0 || t == gozListTime_) return;
    gozListTime_ = t;
    runGozCommand("goz_receive");
}

} // namespace sf
