// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Project files in the app: Save / Save As (.sfproj), the unsaved-changes
// prompt (Save / Don't Save / Cancel) before new, open and quit, File > Open
// Recent and autosave with crash recovery. Prompts only appear in interactive
// sessions; --render, --record and demos run straight through.
#include "app/App.h"
#include "app/Support.h"
#include "app/BigMeshTip.h"
#include "app/LookSchemes.h"
#include "core/ManagedPolicy.h"
#include "app/CrashLog.h"
#include "core/LegacyNames.h"
#include "core/SplatCapture.h"
#include "app/Ui.h"
#include "app/I18n.h"
#include "app/UserData.h"

#include "ai/Http.h"

#include "core/LitSphere.h"
#include "core/Project.h"

#include <imgui.h>
#include <imgui_internal.h> // (BringWindowToDisplayFront: the update banner)

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <thread>

namespace sf {

namespace {

const SDL_DialogFileFilter kProjectFilters[] = {{"SculptForge project", "sfproj"}, {"All files", "*"}};
constexpr const char* kUnsavedPopup = "Unsaved Changes";
constexpr const char* kRestorePopup = "Restore Autosave?";

std::filesystem::path fsPath(const std::string& p) { return std::filesystem::path(std::u8string(p.begin(), p.end())); }
std::string utf8(const std::filesystem::path& p) {
    auto s = p.u8string();
    return std::string(s.begin(), s.end());
}
std::string autosavePath(const std::string& prefDir) { return prefDir + "autosave.sfproj"; }
bool fileExists(const std::string& p) {
    std::error_code ec;
    return std::filesystem::is_regular_file(fsPath(p), ec);
}

Json vec3Json(const glm::vec3& v) {
    Json a = Json::array();
    for (int i = 0; i < 3; ++i) a.push(double(v[i]));
    return a;
}
void readVec3(const Json& a, glm::vec3& v) {
    if (a.size() != 3) return;
    for (int i = 0; i < 3; ++i) v[i] = float(a[size_t(i)].asNumber(v[i]));
}
void readFloat(const Json& j, const char* key, float& v) { v = float(j[key].asNumber(v)); }
void readBool(const Json& j, const char* key, bool& v) { v = j[key].asBool(v); }

// Save As dialog: a cancelled dialog is reported as an empty path so a pending
// "save, then quit/open/new" is dropped instead of firing on a later save.
// "lit spheres/<name>.png" with the characters project attachment names allow.
std::string litsphereAttachmentName(const std::string& name) {
    std::string safe;
    for (unsigned char c : name) safe += std::isalnum(c) || c == ' ' || c == '_' || c == '-' || c >= 0x80 ? char(c) : '_';
    return "litspheres/" + (safe.empty() ? std::string("LitSphere") : safe) + ".png";
}

void SDLCALL onProjectChosen(void* userdata, const char* const* files, int) {
    wakeMainLoop(); // a file dialog answered (maybe on another thread): redraw now
    auto* req = static_cast<App::DialogRequest*>(userdata);
    req->app->queueFile(req->action, files && files[0] ? std::string(files[0]) : std::string());
}

} // namespace

bool App::interactive() const { return options_.renderPath.empty() && options_.recordPath.empty() && !demo_; }

void App::initProject() {
    if (!interactive()) return;
    prefDir_ = userDataDir();
    setProjectAutosaveFolder(prefDir_); // (its autosave may name a streamed level's file by path: core/Project.h)
    if (prefDir_.empty()) return;
    std::ifstream f(fsPath(prefDir_ + "project.json"), std::ios::binary);
    Json prefs;
    std::string err;
    const bool hadPrefs = f && Json::parse(std::string(std::istreambuf_iterator<char>(f), {}), prefs, err);
    if (hadPrefs) {
        // An install from before SculptForge-style keys were the default, that never stored its key style: it had
        // the Classic sculpting keys, and keeps them (initActions applies the style).
        if (prefs["input_style"].isNumber()) startInputStyle_ = std::clamp(int(prefs["input_style"].asNumber(1)), 0, 3);
        if (!uiPrefs_.inputStyleSaved && !uiConfigDeterministic_) uiPrefs_.inputStyle = startInputStyle_ >= 0 ? startInputStyle_ : 0;
        for (const auto& p : prefs["recent_files"].items())
            if (p.isString()) recentFiles_.push_back(p.asString());
        autosaveMinutes_ = std::clamp(int(prefs["autosave_minutes"].asNumber(autosaveMinutes_)), 0, 120);
        // Updated since the last start (a new install has no prefs yet and gets the Library instead).
        const std::string lastSeen = prefs["last_seen_version"].isString() ? prefs["last_seen_version"].asString() : "";
        showWhatsNew_ = lastSeen != SCULPTFORGE_VERSION;
        // (the membership card's update moment: a newer version than the last start's)
        updatedThisStart_ = !lastSeen.empty() && compareVersions(SCULPTFORGE_VERSION, lastSeen) > 0;
        checkUpdates_ = prefs["check_updates"].asBool(checkUpdates_);
        updateCheckedAt_ = int64_t(prefs["update_checked_at"].asNumber(0));
        if (prefs["update_skipped"].isString()) updateSkipped_ = prefs["update_skipped"].asString();
        memberBuilds_ = prefs["member_builds"].asBool(false); // (the token itself is in the keychain only)
        if (prefs["member_login"].isString()) memberLogin_ = prefs["member_login"].asString().substr(0, 39);
        if (prefs["update_channel"].isString())
            updateChannel_ = prefs["update_channel"].asString() == "preview" ? UpdateChannel::Preview : UpdateChannel::Stable;
        applyUpdatePolicy();
        // The membership cards (Support.h). ("support_card": false, the old switch, and "supporter" from before
        // 0.11.1 are honoured as they were: a member said so.)
        supportCardPrefs_.supporter = prefs["supporter"].asBool(false) || !prefs["support_card"].asBool(true);
        bigMeshTipDone_ = prefs["big_mesh_tip_done"].asBool(false);
        supportCardPrefs_.shownAt = int64_t(prefs["support_card_shown_at"].asNumber(0));
        supportCardPrefs_.hiddenUntil = int64_t(prefs["member_card_hidden_until"].asNumber(0));
        if (prefs["member_card_challenge"].isString())
            supportCardPrefs_.challengeSeen = prefs["member_card_challenge"].asString().substr(0, 32);
        { // the backoff and the milestones' counts (this computer only: never sent)
            auto count = [&](const char* key) { return int(std::clamp(prefs[key].asNumber(0), 0.0, 1e8)); };
            supportCardPrefs_.notNow = count("member_card_not_now");
            supportCardPrefs_.shownCount = count("member_card_shown");
            supportCardPrefs_.starts = count("member_card_starts");
            supportCardPrefs_.saves = count("member_card_saves");
            supportCardPrefs_.milestonesShown = unsigned(count("member_card_milestones")) & 7u;
            supportCardPrefs_.sculptSeconds = std::clamp(prefs["member_card_sculpt_seconds"].asNumber(0), 0.0, 1e9);
            // The last update check's "support" block (core/UpdateCheck.h), for the card's honest line.
            if (const Json& f = prefs["member_card_feed"]; f.isObject()) {
                SupportFeed& feed = supportCardPrefs_.feed;
                feed.present = true;
                for (const Json& n : f["funded"].items())
                    if (n.isString() && feed.funded.size() < 5) feed.funded.push_back(sanitizeSupporterName(n.asString()));
                feed.foundingTaken = int(std::clamp(f["founding_taken"].asNumber(-1), -1.0, 1e5));
                feed.foundingLimit = int(std::clamp(f["founding_limit"].asNumber(-1), -1.0, 1e5));
                feed.memberRequests = int(std::clamp(f["member_requests"].asNumber(-1), -1.0, 1e5));
            }
        }
        discordPrefs_.enabled = prefs["discord_presence"].asBool(false); // Preferences > Integrations (PanelIntegrations.cpp)
        discordPrefs_.showChallenge = prefs["discord_challenge_shown"].asBool(false);
        discordPrefs_.showFileName = prefs["discord_file_name_shown"].asBool(false);
        if (prefs["discord_challenge"].isString())
            discordPrefs_.challenge = discord::sanitize(prefs["discord_challenge"].asString(), discord::kChallengeMax);
        std::snprintf(discordChallengeBuf_, sizeof discordChallengeBuf_, "%s", discordPrefs_.challenge.c_str());
        // The look (0.10.7): what the last session used, unless ui.json names one. An install from before 0.10.7
        // remembers neither and keeps the Classic look with the Classic layout; the new-look card offers the
        // new default once (PanelSupport.cpp).
        newLookCardDone_ = prefs["new_look_card_done"].asBool(false);
        quickTourDone_ = prefs["quick_tour_done"].asBool(false);
        uiPrefs_.confirmSubdivide = prefs["confirm_subdivide"].asBool(uiPrefs_.confirmSubdivide); // (Don't ask again)
        recentColors_.clear();
        for (const auto& c : prefs["recent_colors"].items()) {
            if (recentColors_.size() >= kRecentColorsMax || !c.isArray()) break;
            glm::vec3 v(0.0f);
            readVec3(c, v);
            recentColors_.push_back(glm::clamp(v, glm::vec3(0.0f), glm::vec3(1.0f)));
        }
        if (!uiPrefs_.lookSaved && !uiConfigDeterministic_) {
            UiPrefs saved = uiPrefs_;
            if (!lookFromJson(prefs["ui_look"], saved)) {
                saved.look.look = int(looks::LookId::Classic);
                saved.shellLayout = 0;
            }
            uiPrefs_.look = saved.look;
            setShellLayout(saved.shellLayout);
            lookDirty_ = true;
        }
        const bool onNewLook = uiPrefs_.look.look == int(looks::LookId::ForgeTerminal) && uiPrefs_.shellLayout == 1;
        newLookCard_ = !newLookCardDone_ && !onNewLook && !uiConfigDeterministic_;
    } else {
        showWelcome_ = options_.uiTest.empty() && options_.demo.empty() && !options_.benchmark; // the first start: no prefs yet
        startInputStyle_ = uiPrefs_.inputStyle; // a new install: SculptForge's keys until the welcome's choice
        newLookCardDone_ = true; // a new install starts in the new look (or ui.json's): nothing to announce
    }
    initSelfUpdate(); // a just-installed version counts its start (PanelUpdate.cpp)
    recentFiles_ = pruneRecentFiles(std::move(recentFiles_), fileExists);
    // (the starts milestone: counted on this computer only, never sent)
    if (!supportCardTestMode()) supportCardPrefs_.starts = std::min(supportCardPrefs_.starts + 1, 100000000);
    saveProjectPrefs(); // records this version, so What's New shows once
    updateDiscordPresence(); // on if it was left on (PanelIntegrations.cpp)
    // The full membership card on the first start (Support.h); after an update, the small card's moment; then a
    // milestone due (the starts are counted above).
    if (updatedThisStart_) memberMoment(MemberMoment::Updated);
    launchSupportCard(!hadPrefs,
                      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count(),
                      supportCardTestMode());
    memberMilestoneCheck();

#if SF_UPDATE_CHECK
    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    applyUpdatePolicy();
    if (checkUpdates_ && options_.uiTest.empty() && options_.benchmarkScript.empty() && // (a take: local only)
        !std::getenv("SF_NO_UPDATE_CHECK") && updateCheckDue(updateCheckedAt_, now))
        startUpdateCheck(false);
#endif

    // An autosave newer than the last clean exit means the previous session ended unexpectedly.
    std::error_code ec;
    const auto autosave = fsPath(autosavePath(prefDir_)), marker = fsPath(prefDir_ + "clean_exit");
    if (std::filesystem::is_regular_file(autosave, ec)) {
        const auto saved = std::filesystem::last_write_time(autosave, ec);
        std::error_code ec2;
        const auto exited = std::filesystem::last_write_time(marker, ec2);
        offerRestore_ = !ec && (ec2 || saved > exited);
    }
}

void App::finishProject() {
    if (!interactive() || prefDir_.empty()) return;
    finishSelfUpdate(); // an update left for Later installs now
    saveProjectPrefs();
    std::ofstream(fsPath(prefDir_ + "clean_exit"), std::ios::binary | std::ios::trunc) << "SculptForge exited normally\n";
}

void App::saveProjectPrefs() {
    if (prefDir_.empty()) return;
    Json prefs = Json::object(), recent = Json::array();
    for (const auto& p : recentFiles_) recent.push(p);
    prefs.set("recent_files", std::move(recent));
    prefs.set("autosave_minutes", autosaveMinutes_);
    if (startInputStyle_ >= 0) prefs.set("input_style", startInputStyle_);
    prefs.set("last_seen_version", std::string(SCULPTFORGE_VERSION));
    prefs.set("check_updates", checkUpdates_);
    prefs.set("update_checked_at", double(updateCheckedAt_));
    if (!updateSkipped_.empty()) prefs.set("update_skipped", updateSkipped_);
    prefs.set("update_channel", std::string(updateChannel_ == UpdateChannel::Preview ? "preview" : "stable"));
    prefs.set("member_builds", memberBuilds_ && !memberSessionOnly_);
    if (memberBuilds_ && !memberSessionOnly_ && !memberLogin_.empty()) prefs.set("member_login", memberLogin_);
    prefs.set("supporter", supportCardPrefs_.supporter); // the membership cards (Support.h)
    prefs.set("big_mesh_tip_done", bigMeshTipDone_); // the one big-mesh tip (BigMeshTip.h)
    prefs.set("support_card_shown_at", double(supportCardPrefs_.shownAt));
    prefs.set("member_card_hidden_until", double(supportCardPrefs_.hiddenUntil));
    if (!supportCardPrefs_.challengeSeen.empty()) prefs.set("member_card_challenge", supportCardPrefs_.challengeSeen);
    {
        const SupportCardPrefs& c = supportCardPrefs_;
        prefs.set("member_card_not_now", c.notNow);
        prefs.set("member_card_shown", c.shownCount);
        prefs.set("member_card_starts", c.starts);
        prefs.set("member_card_saves", c.saves);
        prefs.set("member_card_milestones", int(c.milestonesShown));
        prefs.set("member_card_sculpt_seconds", std::floor(c.sculptSeconds));
        if (c.feed.present) {
            Json f = Json::object(), funded = Json::array();
            for (const auto& n : c.feed.funded) funded.push(n);
            f.set("funded", std::move(funded));
            f.set("founding_taken", c.feed.foundingTaken);
            f.set("founding_limit", c.feed.foundingLimit);
            f.set("member_requests", c.feed.memberRequests);
            prefs.set("member_card_feed", std::move(f));
        }
    }
    prefs.set("discord_presence", discordPrefs_.enabled); // Preferences > Integrations (PanelIntegrations.cpp)
    prefs.set("discord_challenge_shown", discordPrefs_.showChallenge);
    prefs.set("discord_file_name_shown", discordPrefs_.showFileName);
    if (!discordPrefs_.challenge.empty()) prefs.set("discord_challenge", discordPrefs_.challenge);
    if (!uiConfigDeterministic_) {
        Json look = Json::object();
        lookToJson(uiPrefs_, look);
        prefs.set("ui_look", std::move(look));
    }
    prefs.set("new_look_card_done", newLookCardDone_);
    prefs.set("quick_tour_done", quickTourDone_);
    prefs.set("confirm_subdivide", uiPrefs_.confirmSubdivide);
    Json colours = Json::array(); // the left shelf's recent colours, newest first
    for (const glm::vec3& c : recentColors_) colours.push(vec3Json(c));
    prefs.set("recent_colors", std::move(colours));
    std::string err; // (temp file, then rename: a quit that ends mid-write leaves the last one whole)
    if (!writeJsonAtomic(prefDir_ + "project.json", prefs, err)) SDL_Log("%s", err.c_str());
}

void App::rememberPaintColor(const glm::vec3& linear) {
    // Already the newest: nothing to move or save (each stroke with the same colour lands here).
    auto same = [&](const glm::vec3& c) { return glm::all(glm::lessThan(glm::abs(c - linear), glm::vec3(0.5f / 255.0f))); };
    if (!recentColors_.empty() && same(recentColors_.front())) return;
    recentColors_.erase(std::remove_if(recentColors_.begin(), recentColors_.end(), same), recentColors_.end());
    recentColors_.insert(recentColors_.begin(), linear);
    if (recentColors_.size() > kRecentColorsMax) recentColors_.resize(kRecentColorsMax);
    saveProjectPrefs();
}

void App::rememberRecent(const std::string& path) {
    if (path.empty() || prefDir_.empty()) return;
    std::error_code ec;
    std::string full = utf8(std::filesystem::absolute(fsPath(path), ec).lexically_normal());
    if (ec) full = path;
    if (full == utf8(fsPath(autosavePath(prefDir_)).lexically_normal())) return;
    recentFiles_ = addRecentFile(std::move(recentFiles_), full);
    saveProjectPrefs();
}

void App::tickBigMeshTip() {
    // Not in renders, recordings, demos or benchmarks; UI tests (a demo script) only when they set the threshold
    // (tests/ui/big_mesh_tip.sfui), so it never sits over another test's clicks.
    const char* testThreshold = std::getenv("SF_BIG_MESH_TIP_TRIANGLES");
    const bool uiTest = !options_.uiTest.empty();
    if (!options_.renderPath.empty() || !options_.recordPath.empty() || (demo_ && !uiTest) ||
        (uiTest && !(testThreshold && *testThreshold)) || options_.benchmark || proof_)
        return;
    // It waits for a quiet moment (no stroke, no card or tour), then shows once.
    const bool busy = doc_.sculptor.strokeActive() || showWelcome_ || showWhatsNew_ || whatsNewOpen() || supportCardOpen() ||
                      quickTourOpen_;
    if (bigMeshTipDue(sceneTrianglesWithMicro(), bigMeshTipTriangles(), bigMeshTipDone_, busy)) {
        bigMeshTipDone_ = true; // once only, written now: a crash or a forced quit doesn't bring it back
        bigMeshTipOpen_ = true;
        saveProjectPrefs();
        redrawAt_ = double(SDL_GetTicksNS()) * 1e-9; // (a frame to show it, asleep or not)
    }
}

void App::tickProject(float dt) {
    updateTitle();
    if (!interactive() || prefDir_.empty()) return;
    if (!doc_.dirty()) {
        autosaveTimer_ = 0.0f;
        if (autosaveWritten_) { // nothing left to recover
            std::error_code ec;
            std::filesystem::remove(fsPath(autosavePath(prefDir_)), ec);
            autosaveWritten_ = false;
            autosaveRevision_ = ~uint64_t(0);
        }
        return;
    }
    autosaveTimer_ += dt;
    const uint64_t revision = doc_.undo.revision() * 1000003u + doc_.splatsVersion;
    // Edits only `modified` tracks (layer weights, Python setters) leave the revision alone, so
    // while it is set autosave every interval rather than trusting an unchanged revision.
    if (autosaveMinutes_ <= 0 || autosaveTimer_ < float(autosaveMinutes_) * 60.0f || doc_.sculptor.strokeActive() ||
        (revision == autosaveRevision_ && !doc_.modified))
        return;
    autosaveTimer_ = 0.0f;
    storeProjectSettings();
    std::string err;
    if (saveProject(autosavePath(prefDir_), doc_, err, /*autosave*/ true)) {
        autosaveRevision_ = revision;
        autosaveWritten_ = true;
        SDL_Log("Autosaved to %s", autosavePath(prefDir_).c_str());
    } else {
        status_ = "Autosave failed: " + err;
    }
}

void App::saveProjectAs(bool forceDialog) {
    if (!forceDialog && isProjectPath(doc_.path)) {
        saveProjectTo(doc_.path);
        return;
    }
    std::string suggestion;
    if (!doc_.path.empty()) suggestion = utf8(fsPath(doc_.path).replace_extension(kProjectExtension));
    SDL_ShowSaveFileDialog(onProjectChosen, &dialogRequests_[int(FileAction::SaveProject)], window_, kProjectFilters, 2,
                           suggestion.empty() ? nullptr : suggestion.c_str());
}

void App::saveProjectTo(const std::string& path) {
    if (path.empty()) { // dialog cancelled
        continueAfterSave_ = false;
        discardAction_ = nullptr;
        return;
    }
    runCommand("save_project", {{"path", path}});
    if (lastCommandOk_) countMemberSave(); // (the 100th save milestone: counted on this computer only)
    if (continueAfterSave_) {
        continueAfterSave_ = false;
        auto action = std::move(discardAction_);
        discardAction_ = nullptr;
        if (!doc_.dirty() && action) action(); // only once the work is really saved
    }
}

void App::storeProjectSettings() {
    Json& s = doc_.projectSettings;
    if (!s.isObject()) s = Json::object();
    Json cam = Json::object();
    cam.set("target", vec3Json(camera_.target));
    cam.set("distance", double(camera_.distance));
    cam.set("yaw", double(camera_.yaw));
    cam.set("pitch", double(camera_.pitch));
    cam.set("roll", double(camera_.roll));
    cam.set("fov_y", double(camera_.fovY));
    cam.set("shift_x", double(camera_.shift.x));
    cam.set("shift_y", double(camera_.shift.y));
    s.set("camera", std::move(cam));
    Json views = Json::array();
    for (const SavedView& v : savedViews_) {
        Json o = Json::object();
        o.set("name", v.name);
        o.set("target", vec3Json(v.camera.target));
        o.set("distance", double(v.camera.distance));
        o.set("yaw", double(v.camera.yaw));
        o.set("pitch", double(v.camera.pitch));
        o.set("roll", double(v.camera.roll));
        o.set("fov_y", double(v.camera.fovY));
        o.set("ortho", v.camera.ortho);
        o.set("shift_x", double(v.camera.shift.x));
        o.set("shift_y", double(v.camera.shift.y));
        views.push(std::move(o));
    }
    s.set("views", std::move(views));

    Json render = Json::object();
    render.set("wireframe", renderSettings_.wireframe);
    render.set("base_color", vec3Json(renderSettings_.baseColor));
    render.set("show_vertexpaint", renderSettings_.showVertexPaint);
    render.set("color_tints_material", renderSettings_.colorTintsMaterial);
    render.set("show_mask", renderSettings_.showMask);
    render.set("show_facegroups", renderSettings_.showFaceGroups);
    render.set("show_mesh", renderSettings_.showMesh);
    render.set("show_splats", renderSettings_.showSplats);
    render.set("splat_opacity", double(renderSettings_.splatOpacity));
    render.set("flat_shading", renderSettings_.flatShading);
    render.set("backface_culling", renderSettings_.backfaceCulling);
    render.set("background", std::string(renderSettings_.gradientBackground ? "gradient" : "flat"));
    render.set("background_color", vec3Json(renderSettings_.backgroundColor));
    {
        Json light = Json::object(); // the Lit Sphere / studio light (#80)
        light.set("yaw", double(renderSettings_.lightYaw));
        light.set("pitch", double(renderSettings_.lightPitch));
        light.set("intensity", double(renderSettings_.lightIntensity));
        light.set("ambient", double(renderSettings_.lightAmbient));
        render.set("light", light);
    }
    render.set("litsphere", renderSettings_.litsphere < 0 ? std::string() : std::string(litsphereName(renderSettings_.litsphere)));
    // Viewport shading (Material > Viewport Shading): Lit Sphere or PBR with its environment.
    {
        const RenderSettings& v = renderSettings_;
        Json pbr = Json::object();
        pbr.set("shading", std::string(v.pbr() ? "pbr" : "lit_sphere"));
        pbr.set("environment", environments()[size_t(std::clamp(v.environment, 0, environmentCount() - 1))].id);
        pbr.set("rotation", double(v.environmentRotation));
        pbr.set("exposure", double(v.exposure));
        pbr.set("tone_map", std::string(toneMapId(v.toneMap)));
        pbr.set("background", v.environmentBackground);
        pbr.set("background_blur", double(v.backgroundBlur));
        pbr.set("roughness", double(v.pbrRoughness));
        pbr.set("metallic", double(v.pbrMetallic));
        pbr.set("cavity", double(v.pbrCavity));
        render.set("viewport", std::move(pbr));
    }
    // A custom Lit Sphere travels inside the project (a PNG attachment), so the project opens with it on any machine.
    for (auto it = doc_.projectFiles.begin(); it != doc_.projectFiles.end();)
        it = it->first.rfind("litspheres/", 0) == 0 ? doc_.projectFiles.erase(it) : std::next(it);
    if (std::vector<uint8_t> png; isCustomLitSphere(renderSettings_.litsphere) && encodeLitSpherePng(renderSettings_.litsphere, png)) {
        const std::string file = litsphereAttachmentName(litsphereName(renderSettings_.litsphere));
        doc_.projectFiles[file] = std::move(png);
        render.set("litsphere_file", file);
    }
    s.set("render", std::move(render));

    Json brush = Json::object();
    brush.set("type", brushName(brush_));
    brush.set("radius_px", double(radiusPx_));
    brush.set("strength", double(strength_));
    brush.set("smooth_strength", double(smoothStrength_));
    brush.set("smooth_keep_volume", smoothKeepVolume_);
    brush.set("smooth_group_borders", smoothGroupBorders_);
    brush.set("rgb_intensity", double(rgbIntensity_));
    brush.set("focal_shift", double(focalShift_));
    brush.set("invert", invertToggle_);
    brush.set("front_faces_only", frontFacesOnly_);
    brush.set("connected_only", uiPrefs_.connectedOnly);
    brush.set("view_falloff", uiPrefs_.viewFalloff);
    brush.set("pressure_strength", pressureStrength_);
    brush.set("pressure_size", pressureSize_);
    brush.set("paint_color", vec3Json(doc_.paintColor));
    brush.set("dynamic", doc_.sculptor.dyntopo.enabled); // dynamic topology (core/Dyntopo.h)
    brush.set("dynamic_detail", double(doc_.sculptor.dyntopo.detail));
    brush.set("dynamic_smooth_remesh", double(doc_.sculptor.dyntopo.smoothRemesh));
    brush.set("vdm", doc_.brushVdm ? doc_.brushVdm->name : std::string()); // by name: the map stays in the library
    s.set("brush", std::move(brush));

    Json sym = Json::object();
    sym.set("x", symmetry_.x);
    sym.set("y", symmetry_.y);
    sym.set("z", symmetry_.z);
    sym.set("radial", symmetry_.radialEnabled); // radial symmetry (#63): on, its count and axis
    sym.set("radial_count", double(symmetry_.radial));
    sym.set("radial_axis", double(symmetry_.radialAxis));
    sym.set("local", symmetry_.local); // around the active Part's centre
    s.set("symmetry", std::move(sym));

    Json gizmo = Json::object(); // Transform gizmo (Gizmo.cpp): where the pivot is and the options
    gizmo.set("pivot", vec3Json(gizmo_.pivot));
    gizmo.set("pivot_placed", gizmo_.pivotPlaced);
    gizmo.set("orient_surface", gizmo_.orientSurface);
    gizmo.set("axis_x", vec3Json(gizmo_.frame[0]));
    gizmo.set("axis_y", vec3Json(gizmo_.frame[1]));
    gizmo.set("use_mask", gizmo_.useMask);
    gizmo.set("all_parts", gizmo_.all);
    gizmo.set("folder_parts", gizmo_.folder);
    gizmo.set("falloff", double(gizmo_.falloff));
    s.set("gizmo", std::move(gizmo));

    Json uv = Json::object(); // UV Editor > Pack: the UDIM grid (#135)
    uv.set("pack_columns", double(uvPackColumns_));
    uv.set("pack_rows", double(uvPackRows_));
    s.set("uv_editor", std::move(uv));
}

// Lenient: anything missing keeps its current value.
void App::applyProjectSettings() {
    const Json& s = doc_.projectSettings;
    if (const Json& cam = s["camera"]; cam.isObject()) {
        readVec3(cam["target"], camera_.target);
        readFloat(cam, "distance", camera_.distance);
        readFloat(cam, "yaw", camera_.yaw);
        readFloat(cam, "pitch", camera_.pitch);
        camera_.roll = 0.0f; // projects from before free rotation have none
        readFloat(cam, "roll", camera_.roll);
        readFloat(cam, "fov_y", camera_.fovY);
        camera_.shift = glm::vec2(0.0f); // projects from before the lens shift have none
        readFloat(cam, "shift_x", camera_.shift.x);
        readFloat(cam, "shift_y", camera_.shift.y);
        camera_.shift = glm::clamp(camera_.shift, glm::vec2(-1.0f), glm::vec2(1.0f));
        camera_.distance = std::max(camera_.distance, 1e-4f);
        camera_.fovY = std::clamp(camera_.fovY, 0.05f, 3.0f);
    }
    savedViews_.clear();
    lastSavedView_ = -1;
    if (const Json& views = s["views"]; views.isArray())
        for (size_t i = 0; i < views.size(); ++i) {
            const Json& o = views[i];
            if (!o.isObject() || !o["name"].isString()) continue;
            SavedView v{o["name"].asString(), camera_};
            readVec3(o["target"], v.camera.target);
            readFloat(o, "distance", v.camera.distance);
            readFloat(o, "yaw", v.camera.yaw);
            readFloat(o, "pitch", v.camera.pitch);
            readFloat(o, "roll", v.camera.roll);
            readFloat(o, "fov_y", v.camera.fovY);
            readBool(o, "ortho", v.camera.ortho);
            v.camera.shift = glm::vec2(0.0f);
            readFloat(o, "shift_x", v.camera.shift.x);
            readFloat(o, "shift_y", v.camera.shift.y);
            v.camera.shift = glm::clamp(v.camera.shift, glm::vec2(-1.0f), glm::vec2(1.0f));
            v.camera.distance = std::max(v.camera.distance, 1e-4f);
            v.camera.fovY = std::clamp(v.camera.fovY, 0.05f, 3.0f);
            savedViews_.push_back(std::move(v));
        }
    if (const Json& r = s["render"]; r.isObject()) {
        readBool(r, "wireframe", renderSettings_.wireframe);
        readVec3(r["base_color"], renderSettings_.baseColor);
        readBool(r, "show_vertexpaint", renderSettings_.showVertexPaint);
        readBool(r, "color_tints_material", renderSettings_.colorTintsMaterial);
        readBool(r, "show_mask", renderSettings_.showMask);
        readBool(r, "show_facegroups", renderSettings_.showFaceGroups);
        readBool(r, "show_mesh", renderSettings_.showMesh);
        readBool(r, "show_splats", renderSettings_.showSplats);
        readFloat(r, "splat_opacity", renderSettings_.splatOpacity);
        // Projects from before the setting: the preferred shading.
        renderSettings_.flatShading = r["flat_shading"].asBool(uiPrefs_.flatShading);
        renderSettings_.backfaceCulling = r["backface_culling"].asBool(uiPrefs_.backfaceCulling);
        // Viewport shading; projects from before it: Lit Sphere.
        {
            RenderSettings& v = renderSettings_;
            const RenderSettings defaults;
            const Json& pbr = r["viewport"];
            auto num = [&](const char* key, float def, float lo, float hi) {
                return pbr.isObject() && pbr[key].isNumber() ? std::clamp(float(pbr[key].asNumber()), lo, hi) : def;
            };
            v.shading = pbr.isObject() && pbr["shading"].isString() && pbr["shading"].asString() == "pbr"
                            ? RenderSettings::Shading::Pbr
                            : RenderSettings::Shading::LitSphere;
            const int env = pbr.isObject() && pbr["environment"].isString() ? findEnvironment(pbr["environment"].asString()) : -1;
            v.environment = env >= 0 ? env : 0;
            v.environmentRotation = num("rotation", defaults.environmentRotation, -360.0f, 360.0f);
            v.exposure = num("exposure", defaults.exposure, -10.0f, 10.0f);
            v.toneMap = defaults.toneMap;
            if (pbr.isObject() && pbr["tone_map"].isString()) parseToneMap(pbr["tone_map"].asString(), v.toneMap);
            v.environmentBackground = pbr.isObject() && pbr["background"].isBool() ? pbr["background"].asBool() : defaults.environmentBackground;
            v.backgroundBlur = num("background_blur", defaults.backgroundBlur, 0.0f, 1.0f);
            v.pbrRoughness = num("roughness", defaults.pbrRoughness, 0.0f, 1.0f);
            v.pbrMetallic = num("metallic", defaults.pbrMetallic, 0.0f, 1.0f);
            v.pbrCavity = num("cavity", defaults.pbrCavity, 0.0f, 1.0f);
        }
        // The light (#80); projects from before it: the material's own.
        {
            RenderSettings& v = renderSettings_;
            const RenderSettings defaults;
            const Json& light = r["light"];
            auto num = [&](const char* key, float def, float lo, float hi) {
                return light.isObject() && light[key].isNumber() ? std::clamp(float(light[key].asNumber()), lo, hi) : def;
            };
            v.lightYaw = num("yaw", defaults.lightYaw, -180.0f, 180.0f);
            v.lightPitch = num("pitch", defaults.lightPitch, -90.0f, 90.0f);
            v.lightIntensity = num("intensity", defaults.lightIntensity, 0.0f, 3.0f);
            v.lightAmbient = num("ambient", defaults.lightAmbient, 0.0f, 1.0f);
        }
        // The background (#21); projects from before it: the preferred one.
        renderSettings_.gradientBackground =
            r["background"].isString() ? r["background"].asString() != "flat" : uiPrefs_.gradientBackground;
        renderSettings_.backgroundColor = uiPrefs_.backgroundColor;
        readVec3(r["background_color"], renderSettings_.backgroundColor);
        renderSettings_.backgroundColor = glm::clamp(renderSettings_.backgroundColor, 0.0f, 1.0f);
        // Unknown, empty or missing (projects from before Lit Spheres): studio lighting, as they were saved.
        renderSettings_.litsphere = -1;
        const std::string name = r["litsphere"].isString() ? r["litsphere"].asString() : std::string();
        for (int i = 0; i < litsphereCount(); ++i)
            if (!name.empty() && name == litsphereName(i)) renderSettings_.litsphere = i;
        // A custom Lit Sphere the project carries: the one with the same pixels if this machine has it, else added for
        // the session (renamed when a different Lit Sphere here already has its name).
        const auto file = r["litsphere_file"].isString() ? doc_.projectFiles.find(legacy::modernize(r["litsphere_file"].asString()))
                                                      : doc_.projectFiles.end();
        CustomLitSphere m;
        std::string err;
        if (file != doc_.projectFiles.end() && decodeLitSphereImage(file->second.data(), file->second.size(), m, err)) {
            int index = findLitSphereByHash(m.hash);
            if (index < 0) {
                m.name = name.empty() ? std::string("Lit Sphere") : name;
                for (int n = 2; findLitSphere(m.name) >= 0; ++n) m.name = name + " " + std::to_string(n);
                index = addCustomLitSphere(std::move(m));
            }
            renderSettings_.litsphere = index;
        }
    }
    if (const Json& b = s["brush"]; b.isObject()) {
        for (int i = 0; i < int(BrushType::Count); ++i)
            if (b["type"].asString() == brushName(BrushType(i))) brush_ = BrushType(i);
        readFloat(b, "radius_px", radiusPx_);
        readFloat(b, "strength", strength_);
        readFloat(b, "smooth_strength", smoothStrength_);
        readFloat(b, "rgb_intensity", rgbIntensity_);
        readFloat(b, "focal_shift", focalShift_);
        radiusPx_ = clampDrawSize(radiusPx_);
        strength_ = std::clamp(strength_, 0.0f, 1.0f);
        smoothStrength_ = std::clamp(smoothStrength_, 0.0f, 1.0f);
        rgbIntensity_ = std::clamp(rgbIntensity_, 0.0f, 1.0f);
        focalShift_ = std::clamp(focalShift_, -1.0f, 1.0f);
        readBool(b, "invert", invertToggle_);
        readBool(b, "smooth_keep_volume", smoothKeepVolume_);
        readBool(b, "smooth_group_borders", smoothGroupBorders_);
        readBool(b, "front_faces_only", frontFacesOnly_);
        readBool(b, "connected_only", uiPrefs_.connectedOnly);
        readBool(b, "view_falloff", uiPrefs_.viewFalloff);
        readBool(b, "pressure_strength", pressureStrength_);
        readVec3(b["paint_color"], doc_.paintColor);
        readBool(b, "pressure_size", pressureSize_);
        readBool(b, "dynamic", doc_.sculptor.dyntopo.enabled);
        readFloat(b, "dynamic_detail", doc_.sculptor.dyntopo.detail);
        doc_.sculptor.dyntopo.detail = std::clamp(doc_.sculptor.dyntopo.detail, 0.0f, 1.0f);
        readFloat(b, "dynamic_smooth_remesh", doc_.sculptor.dyntopo.smoothRemesh);
        doc_.sculptor.dyntopo.smoothRemesh = std::clamp(doc_.sculptor.dyntopo.smoothRemesh, 0.0f, 1.0f);
        if (const int vdm = b["vdm"].isString() ? findVdm(doc_, b["vdm"].asString()) : -1; vdm >= 0)
            doc_.brushVdm = doc_.vdms[size_t(vdm)];
        brushSettingsKey_ = currentBrushKey(); // the project's values belong to its brush (#78), nothing to swap
    }
    if (const Json& sym = s["symmetry"]; sym.isObject()) {
        readBool(sym, "x", symmetry_.x);
        readBool(sym, "y", symmetry_.y);
        readBool(sym, "z", symmetry_.z);
        symmetry_.radialEnabled = false; // (older projects have none)
        symmetry_.local = false;
        readBool(sym, "radial", symmetry_.radialEnabled);
        readBool(sym, "local", symmetry_.local);
        float count = float(symmetry_.radial), axis = float(symmetry_.radialAxis);
        readFloat(sym, "radial_count", count);
        readFloat(sym, "radial_axis", axis);
        symmetry_.radial = std::clamp(int(std::lround(count)), 2, Symmetry::kMaxRadial);
        symmetry_.radialAxis = std::clamp(int(std::lround(axis)), 0, 2);
        symmetryPivotKey_ = ~uint64_t(0);
    }
    if (const Json& g = s["gizmo"]; g.isObject()) {
        gizmo_.centreKey = ~uint64_t(0);
        updateGizmoPivot(); // the opened scene's centre, then the saved pivot over it
        glm::vec3 x = gizmo_.frame[0], y = gizmo_.frame[1];
        readVec3(g["pivot"], gizmo_.pivot);
        readBool(g, "pivot_placed", gizmo_.pivotPlaced);
        readBool(g, "orient_surface", gizmo_.orientSurface);
        readVec3(g["axis_x"], x);
        readVec3(g["axis_y"], y);
        if (glm::length(glm::cross(x, y)) > 1e-3f) { // orthonormal again, whatever the file holds
            x = glm::normalize(x);
            const glm::vec3 z = glm::normalize(glm::cross(x, y));
            gizmo_.frame = glm::mat3(x, glm::cross(z, x), z);
        }
        readBool(g, "use_mask", gizmo_.useMask);
        readBool(g, "all_parts", gizmo_.all);
        readBool(g, "folder_parts", gizmo_.folder);
        readFloat(g, "falloff", gizmo_.falloff);
        gizmo_.falloff = std::clamp(gizmo_.falloff, 0.0f, 1.0f);
        if (!gizmo_.pivotPlaced) updateGizmoPivot();
        gizmo_.undoSteps.clear();
        gizmo_.redoSteps.clear();
    }
    // Projects from before the grid: the preference.
    const Json& uv = s["uv_editor"];
    uvPackColumns_ = std::clamp(int(uv["pack_columns"].asNumber(uiPrefs_.uvPackColumns)), 1, 10);
    uvPackRows_ = std::clamp(int(uv["pack_rows"].asNumber(uiPrefs_.uvPackRows)), 1, 10);
}

void App::afterCommand(const std::string& name, const Args& args, const CommandResult& r) {
    if (!r.ok) return;
    if (name == "undo" || name == "redo") gizmoAfterUndo(name == "redo");
    if (name == "uv_pack" || name == "uv_unwrap") uvPackGridUsed(args);
    const std::string path = argString(args, "path", doc_.path);
    const bool project = name == "open_project" || (name == "open" && isProjectPath(path));
    if (project) {
        if (!doc_.projectSettings["camera"].isObject()) frameView(sceneBounds()); // e.g. saved by the CLI
        applyProjectSettings(); // after runCommand framed the camera, so the saved view wins
    }
    if (project || name == "open" || name == "open_obj" || name == "save_project" || name == "save_obj")
        rememberRecent(path);
}

void App::confirmDiscard(std::function<void()> action) {
    if (!interactive() || !doc_.dirty()) {
        action();
        return;
    }
    discardAction_ = std::move(action);
    continueAfterSave_ = false;
    openUnsavedPopup_ = true;
}

void App::requestQuit() {
    confirmDiscard([this] { running_ = false; });
}

void App::openPathConfirmed(const std::string& path) {
    // Images go to the Image to 3D panel and brushes into the library: no scene change.
    std::string ext = fsPath(path).extension().string();
    for (auto& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
    if (capture::isVideoFile(path)) { // a phone video of an object: train a splat from it (PanelSplatTrain.cpp)
        runCommand("splat_train", {{"source", path}});
        return;
    }
    if (Image::isImagePath(path) || ext == ".sfbrush" || ext == ".exr" || ext == ".sfvdm" || ext == kPackExtension)
        openPath(path); // (a pack opens its review screen: the scene stays)
    else confirmDiscard([this, path] { openPath(path); });
}

void App::drawFileMenuProjectItems() {
    if (ImGui::BeginMenu(tr("Open Recent"), !recentFiles_.empty())) {
        const size_t before = recentFiles_.size();
        recentFiles_ = pruneRecentFiles(std::move(recentFiles_), fileExists);
        if (recentFiles_.size() != before) saveProjectPrefs();
        std::string chosen;
        for (size_t i = 0; i < recentFiles_.size(); ++i) {
            const std::string label = utf8(fsPath(recentFiles_[i]).filename()) + "##recent" + std::to_string(i);
            if (ImGui::MenuItem(label.c_str())) chosen = recentFiles_[i];
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", recentFiles_[i].c_str());
        }
        ImGui::Separator();
        if (ImGui::MenuItem(tr("Clear Recent"))) {
            recentFiles_.clear();
            saveProjectPrefs();
        }
        ImGui::EndMenu();
        if (!chosen.empty()) openPathConfirmed(chosen);
    }
    if (ImGui::MenuItem(tr("Save"), "Ctrl+S")) saveProjectAs(false);
    if (ImGui::MenuItem(tr("Save As..."), "Ctrl+Shift+S")) saveProjectAs(true);
    ImGui::SetNextItemWidth(120.0f * uiScale_);
    ImGui::SliderInt(tr("Autosave (min)"), &autosaveMinutes_, 0, 60, tr(autosaveMinutes_ ? "%d" : "off"));
    if (ImGui::IsItemDeactivatedAfterEdit()) saveProjectPrefs();
}

void App::drawProjectPopups() {
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    if (openUnsavedPopup_) {
        ImGui::OpenPopup(kUnsavedPopup);
        openUnsavedPopup_ = false;
    }
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(kUnsavedPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const std::string name = doc_.path.empty() ? std::string("Untitled") : utf8(fsPath(doc_.path).filename());
        ImGui::Text(tr("Save changes to %s before continuing?"), name.c_str());
        ImGui::TextDisabled("%s", tr("Your changes will be lost if you don't save them."));
        ImGui::Spacing();
        const float w = 110.0f * uiScale_;
        if (ImGui::Button(tr("Save"), ImVec2(w, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            ImGui::CloseCurrentPopup();
            if (isProjectPath(doc_.path)) {
                saveProjectTo(doc_.path);
                auto action = std::move(discardAction_);
                discardAction_ = nullptr;
                if (!doc_.dirty() && action) action();
            } else {
                continueAfterSave_ = true;
                saveProjectAs(true);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Don't Save"), ImVec2(w, 0))) {
            ImGui::CloseCurrentPopup();
            auto action = std::move(discardAction_);
            discardAction_ = nullptr;
            if (action) action();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Cancel"), ImVec2(w, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
            discardAction_ = nullptr;
        }
        ImGui::EndPopup();
    }

    if (offerRestore_) {
        ImGui::OpenPopup(kRestorePopup);
        offerRestore_ = false;
    }
    if (showWhatsNew_ && !supportCard_ && !showWelcome_) { // after the membership card, if it showed
        showWhatsNew();
        showWhatsNew_ = false;
    }
    drawUpdateBanner();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(kRestorePopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(tr("SculptForge did not close normally last time."));
        ImGui::TextUnformatted(tr("Restore the autosaved work?"));
        if (crashlog::previousCrashed()) {
            ImGui::Spacing();
            ImGui::TextDisabled("%s", tr("What happened is in sculptforge-previous.log (Help > Open log folder);"));
            ImGui::TextDisabled("%s", tr("please attach it if you report the problem."));
        }
        ImGui::Spacing();
        const float w = 110.0f * uiScale_;
        const std::string autosave = autosavePath(prefDir_);
        if (ImGui::Button(tr("Restore"), ImVec2(w, 0))) {
            ImGui::CloseCurrentPopup();
            runCommand("open_project", {{"path", autosave}});
            if (doc_.path == autosave) {
                doc_.path.clear();     // Save asks where to put it
                doc_.modified = true;  // still unsaved work
                autosaveWritten_ = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Discard"), ImVec2(w, 0))) {
            ImGui::CloseCurrentPopup();
            std::error_code ec;
            std::filesystem::remove(fsPath(autosave), ec);
        }
        ImGui::EndPopup();
    }
}

// --- Update check ---

#ifndef SCULPTFORGE_PRERELEASE
#define SCULPTFORGE_PRERELEASE ""
#endif
#ifndef SF_DEV_COMMIT // a developer's build: the commit it was configured at (CMakeLists.txt, SCULPTFORGE_DEV_BUILD)
#define SF_DEV_COMMIT ""
#endif

const char* App::appPrerelease() { return SCULPTFORGE_PRERELEASE; }
std::string App::appFullVersion() {
    return std::string(SCULPTFORGE_VERSION) + (*SCULPTFORGE_PRERELEASE ? std::string("-") + SCULPTFORGE_PRERELEASE : "");
}
std::string App::appVersionLabel() { return versionLabel(SCULPTFORGE_VERSION, SCULPTFORGE_PRERELEASE); }
ShownVersion App::appShownVersion() { return shownVersion(SCULPTFORGE_VERSION, SCULPTFORGE_PRERELEASE, SF_DEV_COMMIT); }

void App::startUpdateCheck(bool manual) {
    if (updateJob_) return; // one at a time
    auto job = std::make_shared<UpdateJob>();
    job->manual = manual;
    updateJob_ = job;
    if (!manual) {
        updateCheckedAt_ = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        saveProjectPrefs();
    }
    // Studio mode: only the studio's own channel, then its mirror (core/StudioProfile.h); else the public feed.
    std::string fallback;
    const std::string source = updateFeedSource(studioMode(), std::getenv("SF_UPDATE_FEED"), fallback);
    if (source.empty()) { // a studio without a channel: nothing to ask
        job->error = "this studio build has no update channel";
        job->done = true;
        return;
    }
    // Detached: a slow network never holds up quitting (the job is shared, so it outlives the app if it must).
    const UpdateChannel channel = updateChannel_;
    // Member builds only when signed in, never in UI tests (the network) and never in studio mode (its own channel).
    const std::shared_ptr<MemberAuth> member =
        memberBuilds_ && options_.uiTest.empty() && !studioMode().active ? memberAuth_ : nullptr;
    std::thread([job, source, fallback, channel, member] {
        std::string body;
        // A URL or a local file (a studio's mirror on a share; UI tests).
        auto fetch = [&](const std::string& from) {
            job->error.clear();
            if (from.rfind("https://", 0) == 0 || from.rfind("http://", 0) == 0) {
                ai::HttpResponse r = ai::httpRequest("GET", from, {"Accept: application/json"}, {}, 15);
                if (r.status == 200) body = r.body;
                else job->error = r.status ? "the server answered " + std::to_string(r.status) : (r.error.empty() ? "no connection" : r.error);
            } else {
                std::ifstream f(fsPath(from), std::ios::binary);
                if (f) body.assign(std::istreambuf_iterator<char>(f), {});
                else job->error = "can't read " + from;
            }
            return job->error.empty();
        };
        if (!fetch(source) && !fallback.empty()) fetch(fallback);
        if (job->error.empty()) job->ok = parseUpdateFeed(body, job->info, job->error, channel);
        if (member) checkMemberBuilds(*member, *job); // (PanelUpdate.cpp)
        job->done = true;
    }).detach();
}

void App::pollUpdateCheck() {
    if (!updateJob_ || !updateJob_->done) return;
    const std::shared_ptr<UpdateJob> job = std::move(updateJob_);
    updateJob_.reset();
    if (job->memberSignedOut) { // (the token is already gone from the keychain)
        memberBuilds_ = false;
        memberStatus_ = "Your member sign-in has run out: sign in again for member builds.";
        saveProjectPrefs();
    } else if (!job->memberError.empty()) {
        memberStatus_ = "Couldn't check member builds: " + job->memberError;
    }
    if (!job->ok) {
        updateStatus_ = "Couldn't check for updates: " + job->error;
        if (job->manual) status_ = updateStatus_;
        return;
    }
    const UpdateInfo& u = job->info;
    // The feed's "support" block (the card's honest line): kept until the next check, gone when the feed drops it.
    if (u.support.present || supportCardPrefs_.feed.present) {
        supportCardPrefs_.feed = u.support;
        saveProjectPrefs();
    }
    // The monthly challenge's results (latest.json's `challenge`): once per challenge, the membership card's moment.
    if (!u.challengeId.empty() && u.challengeId != supportCardPrefs_.challengeSeen) {
        supportCardPrefs_.challengeSeen = u.challengeId;
        challengeTitle_ = u.challengeTitle;
        saveProjectPrefs();
        memberMoment(MemberMoment::Challenge);
    }
    if (compareVersions(u.version, appFullVersion()) <= 0) {
        updateStatus_ = "SculptForge " + appVersionLabel() + " is the newest version";
        if (job->manual) status_ = updateStatus_;
        return;
    }
    updateStatus_ = "SculptForge " + u.version + " is available";
    // An automatic check stays quiet about a version the user chose to skip; asking again shows it.
    if (job->manual || u.version != updateSkipped_) updateAvailable_ = u;
    if (job->manual) status_ = updateStatus_;
}

void App::drawUpdateBanner() {
    pollUpdateCheck(); // (here, not in tickProject: this runs every frame, UI tests included)
    tickSelfUpdate();  // (and in run() on the passes that draw nothing: asleep, hidden for a restart)
    if (!updateAvailable_) return;
    const UpdateInfo& u = *updateAvailable_;
    const CanvasRect c = canvasRect(); // top centre of the 3D view, clear of the shelves
    ImGui::SetNextWindowPos(ImVec2(c.x + c.w * 0.5f, c.y + timelineTopInset_ + 12.0f * uiScale_), ImGuiCond_Always,
                            ImVec2(0.5f, 0.0f)); // (below the timeline strip when it is at the top)
    ImGui::SetNextWindowBgAlpha(0.96f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
    if (ImGui::Begin("##update_banner", nullptr, flags)) {
        // Above the floating panels (a Preferences window open over the canvas's top would hide its buttons), but
        // never over a modal dialog.
        if (!ImGui::GetTopMostPopupModal()) ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        // Reading size (the guide's 17 px fonts), so the highlights are easy to read over the canvas.
        if (ImFont* title = guide_.bodyBoldFont()) ImGui::PushFont(title);
        ImGui::Text(tr("SculptForge %s is available"), u.version.c_str());
        if (guide_.bodyBoldFont()) ImGui::PopFont();
        if (ImFont* body = guide_.bodyFont()) ImGui::PushFont(body);
        ImGui::SameLine();
        ImGui::TextDisabled(tr("(you have %s)"), appVersionLabel().c_str());
        for (const auto& h : u.highlights) ImGui::BulletText("%s", h.c_str());
        ImGui::TextDisabled("%s %s", kFreeReleaseNote, tr("Members get betas early.")); // memberships (Support.h)
        if (guide_.bodyFont()) ImGui::PopFont();
        ImGui::Spacing();
        bool close = false;
        if (!drawSelfUpdate(u, close)) { // (PanelUpdate.cpp: Update, or the package manager's command)
            if (ImGui::Button(tr("What's new"))) SDL_OpenURL(u.notesUrl.c_str());
            ImGui::SameLine();
            if (ImGui::Button(tr("Download"))) {
                SDL_OpenURL(u.downloadUrl.c_str());
                close = true;
            }
            ImGui::SameLine();
            if (ImGui::Button(tr("Become a member"))) openSupportPage();
            ImGui::SameLine();
            if (ImGui::Button(tr("Later"))) close = true;
            ImGui::SameLine();
            if (ImGui::Button(tr("Skip this version"))) {
                updateSkipped_ = u.version;
                saveProjectPrefs();
                close = true;
            }
        }
        if (close) updateAvailable_.reset();
    }
    ImGui::End();
}

void App::applyUpdatePolicy() {
    // A studio's policy (core/ManagedPolicy.h "updates") locks these.
    const policy::Policy& p = policy::current();
    if (p.updateCheck != policy::Lock::Unset) checkUpdates_ = p.updateCheck == policy::Lock::On;
    if (!p.updateChannel.empty()) updateChannel_ = p.updateChannel == "preview" ? UpdateChannel::Preview : UpdateChannel::Stable;
}

void App::drawUpdatesSection() {
#if SF_UPDATE_CHECK
    const policy::Policy& managed = policy::current();
    const bool checkLocked = managed.updateCheck != policy::Lock::Unset, channelLocked = !managed.updateChannel.empty();
    ImGui::BeginDisabled(checkLocked);
    if (ImGui::Checkbox(tr("Check for updates"), &checkUpdates_)) saveProjectPrefs();
    ImGui::EndDisabled();
    if (checkLocked) ui::ManagedBadge();
    ui::HelpText("Check for updates", nullptr,
                 "Once a day at startup, download a small file from the SculptForge website that names the newest "
                 "version, and show a banner when there is a newer one. Nothing about you or your computer is sent.",
                 "Preferences > Updates");
    ImGui::TextUnformatted(tr("Update channel"));
    int channel = updateChannel_ == UpdateChannel::Preview ? 1 : 0;
    ImGui::BeginDisabled(channelLocked);
    bool changed = ImGui::RadioButton((std::string(tr("Stable")) + "##prefs.update_stable").c_str(), &channel, 0);
    ImGui::SameLine();
    changed |= ImGui::RadioButton((std::string(tr("Preview (betas)")) + "##prefs.update_preview").c_str(), &channel, 1);
    ImGui::EndDisabled();
    if (channelLocked) ui::ManagedBadge();
    ui::HelpText("Update channel", nullptr,
                 "Stable offers only finished releases. Preview also offers the betas the update feed names. Betas "
                 "are a member perk: members get them from the private members' repository and the #members channel "
                 "on Discord. When the stable release passes a beta, Preview offers that.",
                 "Preferences > Updates");
    if (changed) {
        updateChannel_ = channel ? UpdateChannel::Preview : UpdateChannel::Stable;
        updateSkipped_.clear();
        saveProjectPrefs();
    }
#else
    ImGui::TextWrapped("%s", tr("This build gets its updates from your package manager."));
#endif
    ui::ActionButton(*this, "help.check_updates", ImVec2(-1, 0));
    if (updateJob_) ImGui::TextDisabled("%s", tr("Checking..."));
    else if (!updateStatus_.empty()) ImGui::TextWrapped("%s", updateStatus_.c_str());
#if SF_UPDATE_CHECK
    drawMemberBuildsSection(); // (PanelUpdate.cpp)
#endif
}

} // namespace sf
