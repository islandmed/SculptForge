// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// One-click update: the update banner's Update button (core/SelfUpdate.h says what is trusted and why). The
// download runs on a worker thread; installing happens on Restart now, or when the app quits after Later.
#include "app/App.h"
#include "app/NativeDialog.h"
#include "app/I18n.h"
#include "app/Theme.h"
#include "app/Ui.h"
#include "ai/Http.h"
#include "ai/Process.h"
#include "core/Keychain.h"
#include "core/MemberBuilds.h"
#include <imgui.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h> // GetCurrentProcessId
#endif
#if defined(__linux__)
#include <csignal> // kill (endOldAppImage)
#include <unistd.h>
#endif

namespace sf {

// The member sign-in's tokens, shared with the worker threads (one refresh at a time: a refresh token works once).
struct App::MemberAuth {
    std::mutex mutex;
    member::Tokens tokens;
    bool loaded = false; // the keychain was read
};

// A sign-in in progress (device flow): the code to show, then the outcome.
struct App::MemberSignIn {
    std::mutex mutex;
    std::string userCode; // shown once GitHub has answered
    int64_t expiresAt = 0;
    std::atomic<bool> cancel{false}, done{false};
    bool ok = false, saved = false;
    std::string login, error;
};

namespace {

namespace fs = std::filesystem;

int64_t unixNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// GET a member API URL with the token (and nothing else); the body when it answered 200.
ai::HttpResponse memberGet(const std::string& url, const std::string& token, size_t cap) {
    std::string body;
    ai::HttpResponse r = ai::httpGetRestricted(
        url, member::isMemberApiUrl, [&token](const std::string& u) { return member::headersFor(u, token); }, 30,
        [&](const char* d, size_t n) {
            if (body.size() + n > cap) return false;
            body.append(d, n);
            return true;
        });
    r.body = std::move(body);
    return r;
}

ai::HttpResponse postGitHub(const char* url, const std::string& body) {
    return ai::httpPostNoRedirect(url, {"Accept: application/json", "Content-Type: application/x-www-form-urlencoded"},
                                  body, 30);
}

double nowSeconds() { return double(SDL_GetTicksNS()) * 1e-9; }
fs::path fsPath(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

// The main action's button: the accent fill, as an active mode's button has (theme::accent, not ButtonActive,
// which is a grey in some looks).
bool accentButton(const char* label) {
    ImGui::PushStyleColor(ImGuiCol_Button, theme::accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::accentHover);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::textOnAccent);
    const bool clicked = ImGui::Button(label);
    ImGui::PopStyleColor(3);
    return clicked;
}

bool releaseKey(uint8_t key[32]) { return update::parseHexKey(update::kUpdatePublicKeyHex, key); }

constexpr double kConfirmAfterSeconds = 5.0; // a new version that has run this long counts as started
constexpr double kRestartWaitSeconds = 90.0; // how long the old version waits for the new one to say so

// The release files over HTTPS, every hop checked (core/SelfUpdate.h); a member build's token goes only to the
// member API URLs, never to the asset host a redirect points at.
update::Fetch releaseFetcher(std::string token) {
    return [token](const std::string& url, const std::function<bool(const char*, size_t)>& onData,
                   const std::function<bool(uint64_t, uint64_t)>& onProgress) {
        const ai::HttpResponse r = ai::httpGetRestricted(
            url, update::allowedUpdateUrl, [&token](const std::string& u) { return member::headersFor(u, token); }, 60,
            onData, onProgress);
        return update::FetchResult{r.status, r.error};
    };
}

} // namespace

const update::Install& App::updateInstall() {
    if (updateInstallKnown_ && options_.uiTest.empty()) return updateInstall_; // (UI tests switch installs)
    updateInstallKnown_ = true;
    updateInstall_ = update::detectInstall(update::currentInstallProbe());
    // Studio mode: no one-click update (it downloads only public releases); the studio's IT installs its own builds.
    if (studioMode().active) updateInstall_ = update::Install{};
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
    if (!memberAuth_) memberAuth_ = std::make_shared<MemberAuth>();
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

// Linux AppImage, after Restart to update: SculptForge 0.11.10 and earlier, hidden while waiting, never noticed the
// new version start and kept running (tickSelfUpdate ran only on drawn frames). The new version ends it: its parent
// is that version (AppRun is the program itself) when it runs our same AppImage file (update::isOldAppImage).
void App::endOldAppImage() {
#if defined(__linux__)
    const char* appImage = std::getenv("APPIMAGE");
    const pid_t parent = getppid();
    if (!appImage || !*appImage || parent <= 1 || !update::isOldAppImage("/proc/" + std::to_string(parent), appImage)) return;
    SDL_Log("update: the previous version (process %d) is still running; ending it", int(parent));
    kill(parent, SIGTERM); // SDL: a quit, as if its window were closed (its changes were saved before the restart)
#endif
}

void App::tickSelfUpdate() {
    if (memberSignIn_ && memberSignIn_->done) {
        const std::shared_ptr<MemberSignIn> job = std::move(memberSignIn_);
        if (job->ok) {
            memberBuilds_ = true;
            memberSessionOnly_ = !job->saved; // (no keychain: signed in until quitting, nothing saved)
            memberLogin_ = job->login;
            memberStatus_ = job->saved ? "" : "Signed in until you quit: this system has no keychain to keep it in.";
            saveProjectPrefs();
            startUpdateCheck(true); // what's there for members now
        } else if (!job->cancel) {
            memberStatus_ = "Couldn't sign in: " + job->error;
        }
        if (auto done = std::move(memberSignInDone_)) {
            memberSignInDone_ = nullptr;
            done(job->ok);
        }
    }
    if (confirmStartAt_ > 0.0 && nowSeconds() >= confirmStartAt_) {
        confirmStartAt_ = 0.0;
        update::confirmStarted(updateNotePath()); // (the old version, watching, sees the note go and quits)
        endOldAppImage();
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
    discordClosed_ = false; // this version carries on: back on Discord, if it's on
    updateDiscordPresence();
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
    const std::map<std::string, std::string> files = u.member ? u.files : std::map<std::string, std::string>{};
    const std::shared_ptr<MemberAuth> auth = u.member ? memberAuth_ : nullptr;
    std::array<uint8_t, 32> k;
    std::copy(key, key + 32, k.begin());
    // Detached like the update check: quitting never waits for it (the job is shared, so it outlives the app).
    std::thread([d, install, version, k, files, auth] {
        std::string token;
        if (auth) {
            bool signedOut = false;
            if (!memberAccessToken(*auth, token, d->error, signedOut) || files.empty()) {
                if (d->error.empty()) d->error = "the member build has no files";
                d->done = true;
                return;
            }
        }
        update::downloadUpdate(version, install, k.data(), releaseFetcher(token), *d, files);
    }).detach();
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
        if (restartChild_) { // hidden: the new version shows itself on Discord (if it's on), never both
            discordClosed_ = true;
            discordClient_.reset();
        }
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
        if (accentButton(tr("Restart now##update.restart"))) restartIntoUpdate();
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
        if (accentButton((std::string(tr(selfUpdateStage_ == SelfUpdateStage::Failed && thisVersion ? "Try again" : "Update")) +
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

// --- Member builds ---

bool App::memberAccessToken(MemberAuth& auth, std::string& token, std::string& error, bool& signedOut) {
    std::lock_guard<std::mutex> lock(auth.mutex);
    if (!auth.loaded) {
        auth.loaded = true;
        std::string secret;
        if (keychain::load(member::kKeychainAccount, secret)) member::tokensFromSecret(secret, auth.tokens);
    }
    const int64_t now = unixNow();
    if (!auth.tokens.usable(now)) {
        if (!auth.tokens.refreshable(now)) {
            signedOut = true;
            auth.tokens = {};
            keychain::erase(member::kKeychainAccount);
            error = "signed out";
            return false;
        }
        const ai::HttpResponse r = postGitHub(member::kTokenUrl, member::refreshBody(auth.tokens.refresh));
        if (!r.error.empty() || r.status != 200) { // (no answer: try again next time; the refresh token is unused)
            error = r.error.empty() ? "GitHub answered " + std::to_string(r.status) : r.error;
            return false;
        }
        member::Tokens fresh;
        switch (member::parseTokenReply(r.body, now, fresh, error)) {
        case member::TokenReply::Ok:
            auth.tokens = fresh;
            keychain::store(member::kKeychainAccount, member::tokensToSecret(fresh)); // (the old refresh token is spent)
            break;
        case member::TokenReply::Expired:
        case member::TokenReply::Denied:
            signedOut = true;
            auth.tokens = {};
            keychain::erase(member::kKeychainAccount);
            error = "signed out";
            return false;
        default:
            if (error.empty()) error = "GitHub didn't renew the sign-in";
            return false;
        }
    }
    token = auth.tokens.access;
    return true;
}

void App::checkMemberBuilds(MemberAuth& auth, UpdateJob& job) {
    std::string token, error;
    bool signedOut = false;
    if (!memberAccessToken(auth, token, error, signedOut)) {
        job.memberSignedOut = signedOut;
        if (!signedOut) job.memberError = error;
        return;
    }
    const ai::HttpResponse r = memberGet(member::kReleasesUrl, token, 4u << 20);
    if (r.status == 401) { // revoked (or the app was uninstalled from the repository)
        std::lock_guard<std::mutex> lock(auth.mutex);
        auth.tokens = {};
        keychain::erase(member::kKeychainAccount);
        job.memberSignedOut = true;
        return;
    }
    if (r.status != 200) {
        job.memberError = r.status == 404 || r.status == 403 ? "this GitHub account has no access to member builds"
                          : r.error.empty() ? "GitHub answered " + std::to_string(r.status) : r.error;
        return;
    }
    UpdateInfo best;
    if (!member::parseMemberReleases(r.body, best, error)) return; // (no member build yet: nothing to offer)
    if (!job.ok || compareVersions(best.version, job.info.version) > 0) {
        job.info = std::move(best);
        job.ok = true;
        job.error.clear();
    }
}

void App::startMemberSignIn(std::function<void(bool member)> onDone) {
    if (memberSignIn_ && !memberSignIn_->done) { // one at a time: a second caller hears the same outcome
        if (onDone) {
            auto first = std::move(memberSignInDone_);
            memberSignInDone_ = [first, onDone](bool member) {
                if (first) first(member);
                onDone(member);
            };
        }
        return;
    }
    memberSignInDone_ = std::move(onDone);
    if (!memberAuth_) memberAuth_ = std::make_shared<MemberAuth>();
    auto job = std::make_shared<MemberSignIn>();
    memberSignIn_ = job;
    memberStatus_.clear();
    if (!options_.uiTest.empty()) { // UI tests: the code screen, no network
        job->userCode = "TEST-CODE";
        job->expiresAt = unixNow() + 900;
        return;
    }
    const std::shared_ptr<MemberAuth> auth = memberAuth_;
    std::thread([job, auth] {
        struct Done {
            MemberSignIn& j;
            ~Done() { j.done = true; }
        } done{*job};
        ai::HttpResponse r = postGitHub(member::kDeviceCodeUrl, member::deviceCodeBody());
        member::DeviceCode code;
        if (r.status != 200 || !member::parseDeviceCode(r.body, unixNow(), code, job->error)) {
            if (job->error.empty()) job->error = r.error.empty() ? "GitHub answered " + std::to_string(r.status) : r.error;
            return;
        }
        {
            std::lock_guard<std::mutex> lock(job->mutex);
            job->userCode = code.userCode;
            job->expiresAt = code.expiresAt;
        }
        int interval = code.interval;
        member::Tokens tokens;
        while (true) { // GitHub's pace: one poll per interval until the user has entered the code (or not)
            for (int i = 0; i < interval * 10; ++i) {
                if (job->cancel) return;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (unixNow() >= code.expiresAt) {
                job->error = "the code ran out: sign in again";
                return;
            }
            r = postGitHub(member::kTokenUrl, member::pollBody(code.deviceCode));
            if (!r.error.empty()) continue; // (a dropped connection: the next poll)
            const member::TokenReply reply = member::parseTokenReply(r.body, unixNow(), tokens, job->error);
            if (reply == member::TokenReply::Ok) break;
            if (reply == member::TokenReply::Pending) continue;
            if (reply == member::TokenReply::SlowDown) {
                interval += 5;
                continue;
            }
            if (reply == member::TokenReply::Expired) job->error = "the code ran out: sign in again";
            else if (reply == member::TokenReply::Denied) job->error = "the sign-in was declined on GitHub";
            return;
        }
        // Signed in to GitHub; a member is one who can see the member repository's releases.
        r = memberGet(member::kReleasesUrl, tokens.access, 4u << 20);
        if (r.status != 200) {
            job->error = r.status == 404 || r.status == 403
                             ? "this GitHub account has no access to member builds (Member or Studio membership, "
                               "with the invitation to the member repository accepted)"
                             : r.error.empty() ? "GitHub answered " + std::to_string(r.status) : r.error;
            return; // (the token is dropped)
        }
        r = memberGet(member::kUserUrl, tokens.access, 64u << 10);
        if (r.status == 200) job->login = member::parseLogin(r.body);
        job->saved = keychain::store(member::kKeychainAccount, member::tokensToSecret(tokens));
        {
            std::lock_guard<std::mutex> lock(auth->mutex);
            auth->tokens = tokens;
            auth->loaded = true;
        }
        job->ok = true;
    }).detach();
}

void App::finishMemberSignInForTest(bool ok) {
    if (!memberSignIn_ || options_.uiTest.empty()) return;
    memberSignIn_->ok = ok;
    memberSignIn_->login = ok ? "test-member" : "";
    memberSignIn_->error = ok ? "" : "this GitHub account has no access to member builds";
    memberSignIn_->done = true;
}

void App::memberSignOut() {
    if (memberSignIn_) memberSignIn_->cancel = true;
    memberSignIn_.reset();
    memberBuilds_ = memberSessionOnly_ = false;
    memberLogin_.clear();
    memberStatus_ = "Signed out.";
    saveProjectPrefs();
    const std::shared_ptr<MemberAuth> auth = memberAuth_;
    if (auth) {
        std::lock_guard<std::mutex> lock(auth->mutex);
        auth->tokens = {};
        auth->loaded = true; // (nothing more to read)
    }
    if (options_.uiTest.empty()) std::thread([] { keychain::erase(member::kKeychainAccount); }).detach();
}

// A sign-in in progress: the code to enter on GitHub, Copy, the page, Cancel (Preferences > Updates and the
// membership card's "I'm already a member").
void App::drawMemberSignInSteps() {
    if (!memberSignIn_) return;
    std::string code;
    {
        std::lock_guard<std::mutex> lock(memberSignIn_->mutex);
        code = memberSignIn_->userCode;
    }
    if (code.empty()) {
        ImGui::TextDisabled("%s", tr("Asking GitHub..."));
    } else {
        ImGui::TextWrapped("%s", tr("On GitHub, enter this code:"));
        if (ImFont* big = guide_.bodyBoldFont()) ImGui::PushFont(big);
        ImGui::TextUnformatted(code.c_str());
        if (guide_.bodyBoldFont()) ImGui::PopFont();
        ImGui::SameLine();
        if (ImGui::Button(tr("Copy##member.copy"))) SDL_SetClipboardText(code.c_str());
        if (accentButton(tr("Open github.com/login/device##member.open"))) // the next step
            sf::dialogs::openUrl(member::kVerifyUrl); // (always this page, whatever GitHub's answer said)
        ImGui::TextDisabled("%s", tr("Waiting for GitHub..."));
    }
    if (ImGui::Button(tr("Cancel##member.cancel"))) {
        memberSignIn_->cancel = true;
        if (!options_.uiTest.empty()) memberSignIn_->done = true;
    }
}

void App::drawMemberBuildsSection() {
    ImGui::Spacing();
    ImGui::SeparatorText(tr("Member builds"));
    ui::HelpText("Member builds", nullptr,
                 "Member and Studio members get each release early. Sign in with the GitHub account that has the "
                 "invitation to the member repository: SculptForge then also offers the member builds, checked with "
                 "the same signature as every release. The sign-in can only read that repository's releases; it is "
                 "kept in your system's keychain and Sign out removes it.",
                 "Preferences > Updates");
    const bool signedIn = memberBuilds_;
    if (memberSignIn_) {
        drawMemberSignInSteps();
    } else if (signedIn) {
        if (memberLogin_.empty()) ImGui::TextUnformatted(tr("Signed in with GitHub."));
        else ImGui::Text(tr("Signed in as %s on GitHub."), memberLogin_.c_str());
        if (ImGui::Button(tr("Sign out##member.sign_out"))) memberSignOut();
    } else {
        if (ImGui::Button(tr("Sign in with GitHub##member.sign_in"))) startMemberSignIn();
    }
    if (!memberStatus_.empty()) ImGui::TextWrapped("%s", memberStatus_.c_str());
}

} // namespace sf
