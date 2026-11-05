// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// One-click update: the update banner's Update button (core/SelfUpdate.h says what is trusted and why). The
// download runs on a worker thread; installing happens on Restart now, or when the app quits after Later.
#include "app/App.h"
#include "app/I18n.h"
#include "app/Ui.h"
#include "ai/Http.h"
#include "ai/Process.h"
#include <imgui.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h> // GetCurrentProcessId
#endif

namespace sf {

namespace {

namespace fs = std::filesystem;

double nowSeconds() { return double(SDL_GetTicksNS()) * 1e-9; }
fs::path fsPath(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

bool releaseKey(uint8_t key[32]) { return update::parseHexKey(update::kUpdatePublicKeyHex, key); }

constexpr double kConfirmAfterSeconds = 5.0; // a new version that has run this long counts as started
constexpr double kRestartWaitSeconds = 90.0; // how long the old version waits for the new one to say so

// The release files over HTTPS, every hop checked (core/SelfUpdate.h).
update::FetchResult fetchRelease(const std::string& url, const std::function<bool(const char*, size_t)>& onData,
                                 const std::function<bool(uint64_t, uint64_t)>& onProgress) {
    const ai::HttpResponse r = ai::httpGetRestricted(url, update::allowedUpdateUrl, 60, onData, onProgress);
    return {r.status, r.error};
}

} // namespace

const update::Install& App::updateInstall() {
    if (updateInstallKnown_ && options_.uiTest.empty()) return updateInstall_; // (UI tests switch installs)
    updateInstallKnown_ = true;
    updateInstall_ = update::detectInstall(update::currentInstallProbe());
    // UI tests only: how the banner looks on other installs (nothing is downloaded in a UI test).
    const char* kind = options_.uiTest.empty() ? nullptr : std::getenv("SF_UPDATE_TEST_INSTALL");
    if (kind) {
        update::Install t;
        if (std::string(kind) == "appimage") {
            t.kind = update::InstallKind::AppImage;
            t.target = "/nonexistent/SculptForge.AppImage";
            t.asset = "SculptForge-linux-x86_64.AppImage";
        } else if (std::string(kind) == "pacman") {
            t.kind = update::InstallKind::PackageManager;
            t.manager = "pacman";
            t.command = "yay -Syu sculptforge";
        }
        updateInstall_ = t;
    }
    return updateInstall_;
}

bool App::selfUpdateOffered(const UpdateInfo& u) {
    uint8_t key[32];
    const bool keyed = releaseKey(key) || !options_.uiTest.empty(); // no key compiled in yet: Download only
    return keyed && update::selfUpdates(updateInstall().kind) && update::safeVersion(u.version);
}

void App::initSelfUpdate() {
    const std::string note = updateNotePath();
    if (note.empty() || !options_.uiTest.empty()) return;
    update::Pending p;
    std::string error;
    switch (update::checkPendingAtStart(note, appFullVersion(), p, error)) {
    case update::StartCheck::Counting:
        confirmStartAt_ = nowSeconds() + kConfirmAfterSeconds;
        break;
    case update::StartCheck::RolledBack: {
        // SculptForge p.version never got going: the previous version is back in place; start it instead.
        SDL_Log("update: %s didn't start %d times; rolled back to %s", p.version.c_str(), update::kMaxUnconfirmedStarts,
                p.from.c_str());
        ai::ChildProcess child;
        std::string err;
        if (child.start(p.target, {}, "", {}, err)) child.detach();
        running_ = false;
        break;
    }
    case update::StartCheck::Abandoned:
        if (!error.empty()) SDL_Log("update: %s", error.c_str());
        break;
    case update::StartCheck::Nothing: break;
    }
}

void App::tickSelfUpdate() {
    if (confirmStartAt_ > 0.0 && nowSeconds() >= confirmStartAt_) {
        confirmStartAt_ = 0.0;
        update::confirmStarted(updateNotePath()); // (the old version, watching, sees the note go and quits)
    }
    if (selfUpdateStage_ == SelfUpdateStage::Downloading && selfUpdate_ && selfUpdate_->done) {
        if (selfUpdate_->ok) {
            selfUpdateStage_ = SelfUpdateStage::Ready;
        } else {
            selfUpdateStage_ = selfUpdate_->error == "cancelled" ? SelfUpdateStage::Idle : SelfUpdateStage::Failed;
            selfUpdateError_ = selfUpdate_->error;
            selfUpdate_.reset();
        }
    }
    if (selfUpdateStage_ != SelfUpdateStage::Restarting || !restartChild_) return;
    // The new version is starting: it removes the note once it is up. Exiting before that is a failed start.
    std::error_code ec;
    if (!fs::exists(fsPath(updateNotePath()), ec) || nowSeconds() > restartDeadline_) {
        restartChild_->detach();
        restartChild_.reset();
        running_ = false;
        return;
    }
    if (restartChild_->running()) return;
    const int code = restartChild_->exitCode();
    restartChild_.reset();
    update::Pending p;
    std::string error;
    const bool restored = update::readPending(updateNotePath(), p) && update::rollback(p, updateNotePath(), error);
    SDL_Log("update: %s exited with %d before it was up; %s", selfUpdateVersion_.c_str(), code,
            restored ? "the previous version is back" : error.c_str());
    if (window_) SDL_ShowWindow(window_);
    selfUpdateStage_ = SelfUpdateStage::Failed;
    selfUpdateError_ = restored ? "SculptForge " + selfUpdateVersion_ + " didn't start, so this version was kept"
                                : "SculptForge " + selfUpdateVersion_ + " didn't start: " + error;
    selfUpdate_.reset();
}

void App::startSelfUpdate(const UpdateInfo& u) {
    if (selfUpdateStage_ == SelfUpdateStage::Downloading) return;
    selfUpdateVersion_ = u.version;
    selfUpdateError_.clear();
    installAtQuit_ = false;
    auto d = std::make_shared<update::Download>();
    selfUpdate_ = d;
    selfUpdateStage_ = SelfUpdateStage::Downloading;
    if (!options_.uiTest.empty()) return; // UI tests see the progress, never the network (Cancel ends it)
    uint8_t key[32];
    if (!releaseKey(key)) {
        d->error = "this build has no update key";
        d->done = true;
        return;
    }
    const update::Install install = updateInstall();
    const std::string version = u.version;
    std::array<uint8_t, 32> k;
    std::copy(key, key + 32, k.begin());
    // Detached like the update check: quitting never waits for it (the job is shared, so it outlives the app).
    std::thread([d, install, version, k] { update::downloadUpdate(version, install, k.data(), fetchRelease, *d); }).detach();
}

bool App::installSelfUpdate(bool relaunch, std::string& error) {
    if (!selfUpdate_ || !selfUpdate_->ok) {
        error = "nothing downloaded";
        return false;
    }
    const update::Install& install = updateInstall();
    if (install.kind == update::InstallKind::AppImage) {
        if (!update::installAppImage(install, *selfUpdate_, selfUpdateVersion_, appFullVersion(), updateNotePath(), error))
            return false;
        if (!relaunch) return true;
        auto child = std::make_shared<ai::ChildProcess>();
        if (!child->start(install.target, {}, "", {}, error)) {
            update::Pending p;
            std::string ignored;
            if (update::readPending(updateNotePath(), p)) update::rollback(p, updateNotePath(), ignored);
            return false;
        }
        restartChild_ = child;
        restartDeadline_ = nowSeconds() + kRestartWaitSeconds;
        return true;
    }
#if defined(_WIN32)
    if (install.kind == update::InstallKind::WindowsInstaller || install.kind == update::InstallKind::WindowsPortable) {
        // A helper finishes the job once this process has ended (Windows can't replace a running program).
        if (install.kind == update::InstallKind::WindowsPortable &&
            !update::writePending(updateNotePath(), {selfUpdateVersion_, appFullVersion(), install.target, install.target + ".old", 0})) {
            error = "can't write the update note";
            return false;
        }
        std::error_code ec;
        const fs::path script = fs::temp_directory_path(ec) / ("sculptforge-update-" + std::to_string(GetCurrentProcessId()) + ".ps1");
        {
            std::ofstream f(script, std::ios::binary | std::ios::trunc);
            f << update::windowsHelperScript(install, *selfUpdate_, GetCurrentProcessId(), relaunch);
            if (!f) {
                error = "can't write the update helper";
                return false;
            }
        }
        ai::ChildProcess helper;
        const std::u8string s = script.u8string();
        if (!helper.start("powershell.exe", {"-NoProfile", "-ExecutionPolicy", "Bypass", "-File", std::string(s.begin(), s.end())},
                          "", {}, error))
            return false;
        helper.detach();
        if (relaunch) running_ = false;
        return true;
    }
#endif
    error = "this install can't update itself";
    return false;
}

void App::restartIntoUpdate() {
    confirmDiscard([this] { // (unsaved changes: Save, Discard or Cancel first)
        saveProjectPrefs();
        std::string error;
        if (!installSelfUpdate(true, error)) {
            selfUpdateStage_ = SelfUpdateStage::Failed;
            selfUpdateError_ = error;
            selfUpdate_.reset();
            return;
        }
        selfUpdateStage_ = SelfUpdateStage::Restarting;
        if (restartChild_ && window_) SDL_HideWindow(window_); // (shown again if the new version doesn't start)
    });
}

void App::finishSelfUpdate() {
    if (!installAtQuit_ || selfUpdateStage_ != SelfUpdateStage::Ready || !options_.uiTest.empty()) return;
    std::string error;
    if (!installSelfUpdate(false, error)) SDL_Log("update: not installed: %s", error.c_str());
}

bool App::drawSelfUpdate(const UpdateInfo& u, bool& close) {
    const update::Install& install = updateInstall();
    const bool thisVersion = selfUpdateVersion_ == u.version;
    if (thisVersion && selfUpdateStage_ == SelfUpdateStage::Downloading && selfUpdate_) {
        const uint64_t got = selfUpdate_->received, total = selfUpdate_->total;
        char label[64];
        if (total) std::snprintf(label, sizeof label, "%.0f / %.0f MB", double(got) / 1e6, double(total) / 1e6);
        else std::snprintf(label, sizeof label, "%.0f MB", double(got) / 1e6);
        ImGui::TextUnformatted(tr("Downloading the update..."));
        ImGui::ProgressBar(total ? float(double(got) / double(total)) : 0.0f, ImVec2(320.0f * uiScale_, 0), label);
        ImGui::SameLine();
        if (ImGui::Button(tr("Cancel##update.cancel"))) {
            selfUpdate_->cancel = true;
            if (!options_.uiTest.empty()) selfUpdate_->error = "cancelled", selfUpdate_->done = true;
        }
        return true;
    }
    if (thisVersion && selfUpdateStage_ == SelfUpdateStage::Ready) {
        ImGui::Text(tr("SculptForge %s is downloaded and checked."), u.version.c_str());
        if (installAtQuit_) {
            ImGui::TextDisabled("%s", tr("It installs when you quit."));
            if (ImGui::Button(tr("Restart now##update.restart"))) restartIntoUpdate();
            ImGui::SameLine();
            if (ImGui::Button(tr("Hide##update.hide"))) close = true;
            return true;
        }
        if (ImGui::Button(tr("Restart now##update.restart"))) restartIntoUpdate();
        ImGui::SameLine();
        if (ImGui::Button(tr("Later##update.later"))) installAtQuit_ = true;
        ui::HelpText("Later", nullptr, "Install it when you quit SculptForge; the next start is the new version.",
                     "Update banner");
        return true;
    }
    if (thisVersion && selfUpdateStage_ == SelfUpdateStage::Restarting) {
        ImGui::Text(tr("Starting SculptForge %s..."), u.version.c_str());
        return true;
    }
    if (thisVersion && selfUpdateStage_ == SelfUpdateStage::Failed) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.55f, 0.45f, 1.0f));
        ImGui::TextWrapped(tr("The update didn't work: %s."), selfUpdateError_.c_str());
        ImGui::PopStyleColor();
        ImGui::TextDisabled("%s", tr("Nothing was changed. Download it from the website instead, or try again."));
    }
    if (install.kind == update::InstallKind::PackageManager) {
        // Never replaced from here: the package manager owns these files.
        ImGui::Text(tr("Update with your package manager (%s)"), install.manager.c_str());
        if (!install.command.empty()) {
            ImGui::SetNextItemWidth(320.0f * uiScale_);
            std::string cmd = install.command;
            ImGui::InputText("##update.command", cmd.data(), cmd.size() + 1, ImGuiInputTextFlags_ReadOnly);
            ImGui::SameLine();
            if (ImGui::Button(tr("Copy##update.copy"))) SDL_SetClipboardText(install.command.c_str());
        } else {
            ImGui::TextDisabled("%s", tr("Download the new package from the website and open it."));
        }
    }
    if (selfUpdateOffered(u)) {
        if (ImGui::Button((std::string(tr(selfUpdateStage_ == SelfUpdateStage::Failed && thisVersion ? "Try again" : "Update")) +
                           "##update.update").c_str()))
            startSelfUpdate(u);
        ui::HelpText("Update", nullptr,
                     "Download the new version from SculptForge's GitHub releases, check its signature and checksum, "
                     "and install it. The current version is kept until the new one has started.",
                     "Update banner");
        ImGui::SameLine();
    }
    return false;
}

} // namespace sf
