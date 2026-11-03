// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Project files in the app: Save / Save As (.sfproj), the unsaved-changes
// prompt (Save / Don't Save / Cancel) before new, open and quit, File > Open
// Recent and autosave with crash recovery. Prompts only appear in interactive
// sessions; --render, --record and demos run straight through.
#include "app/App.h"
#include "app/Support.h"
#include "app/LookSchemes.h"
#include "app/CrashLog.h"
#include "core/SplatCapture.h"
#include "app/Ui.h"
#include "app/I18n.h"

#include "ai/Http.h"

#include "core/Matcap.h"
#include "core/Project.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
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
// "matcaps/<name>.png" with the characters project attachment names allow.
std::string matcapAttachmentName(const std::string& name) {
    std::string safe;
    for (unsigned char c : name) safe += std::isalnum(c) || c == ' ' || c == '_' || c == '-' || c >= 0x80 ? char(c) : '_';
    return "matcaps/" + (safe.empty() ? std::string("MatCap") : safe) + ".png";
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
    if (char* pref = SDL_GetPrefPath("SculptForge", "SculptForge")) {
        prefDir_ = pref;
        SDL_free(pref);
    }
    if (prefDir_.empty()) return;
    std::ifstream f(fsPath(prefDir_ + "project.json"), std::ios::binary);
    Json prefs;
    std::string err;
    const bool hadPrefs = f && Json::parse(std::string(std::istreambuf_iterator<char>(f), {}), prefs, err);
    if (hadPrefs) {
        // An install from before SculptForge-style keys were the default, that never stored its key style: it had
        // the ZBrush-style keys, and keeps them (initActions applies the style).
        if (prefs["input_style"].isNumber()) startInputStyle_ = std::clamp(int(prefs["input_style"].asNumber(1)), 0, 3);
        if (!uiPrefs_.inputStyleSaved && !uiConfigDeterministic_) uiPrefs_.inputStyle = startInputStyle_ >= 0 ? startInputStyle_ : 0;
        for (const auto& p : prefs["recent_files"].items())
            if (p.isString()) recentFiles_.push_back(p.asString());
        autosaveMinutes_ = std::clamp(int(prefs["autosave_minutes"].asNumber(autosaveMinutes_)), 0, 120);
        // Updated since the last start (a new install has no prefs yet and gets the LightBox instead).
        const std::string lastSeen = prefs["last_seen_version"].isString() ? prefs["last_seen_version"].asString() : "";
        showWhatsNew_ = lastSeen != SCULPTFORGE_VERSION;
        checkUpdates_ = prefs["check_updates"].asBool(checkUpdates_);
        updateCheckedAt_ = int64_t(prefs["update_checked_at"].asNumber(0));
        if (prefs["update_skipped"].isString()) updateSkipped_ = prefs["update_skipped"].asString();
        sculptSeconds_ = addSculptTime(prefs["sculpt_seconds"].asNumber(0.0), 0.0);
        supportReminderDone_ = prefs["support_reminder_done"].asBool(false);
        supportCardPrefs_.enabled = prefs["support_card"].asBool(true);
        supportCardPrefs_.supporter = prefs["supporter"].asBool(false);
        supportCardPrefs_.shownAt = int64_t(prefs["support_card_shown_at"].asNumber(0));
        supportCardPrefs_.laterAt = int64_t(prefs["support_card_later_at"].asNumber(0));
        if (prefs["support_card_version"].isString()) supportCardPrefs_.shownVersion = prefs["support_card_version"].asString();
        // The look (0.10.7): what the last session used, unless ui.json names one. An install from before 0.10.7
        // remembers neither and keeps the Classic look with the ZBrush-style layout; the new-look card offers the
        // new default once (PanelSupport.cpp).
        newLookCardDone_ = prefs["new_look_card_done"].asBool(false);
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
    recentFiles_ = pruneRecentFiles(std::move(recentFiles_), fileExists);
    saveProjectPrefs(); // records this version, so What's New shows once
    // The support card: decided here, while starting, and never again this session (Support.h). Its picture is
    // decoded on a worker thread only if it shows.
    launchSupportCard(!hadPrefs,
                      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count(),
                      supportCardTestMode());

#if SF_UPDATE_CHECK
    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    if (checkUpdates_ && options_.uiTest.empty() && !std::getenv("SF_NO_UPDATE_CHECK") && updateCheckDue(updateCheckedAt_, now))
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
    prefs.set("sculpt_seconds", std::floor(sculptSeconds_)); // for the one support reminder (Support.h); stays here
    prefs.set("support_reminder_done", supportReminderDone_);
    prefs.set("support_card", supportCardPrefs_.enabled); // the support card at launch (Support.h)
    prefs.set("supporter", supportCardPrefs_.supporter);
    prefs.set("support_card_shown_at", double(supportCardPrefs_.shownAt));
    prefs.set("support_card_later_at", double(supportCardPrefs_.laterAt));
    if (!supportCardPrefs_.shownVersion.empty()) prefs.set("support_card_version", supportCardPrefs_.shownVersion);
    if (!uiConfigDeterministic_) {
        Json look = Json::object();
        lookToJson(uiPrefs_, look);
        prefs.set("ui_look", std::move(look));
    }
    prefs.set("new_look_card_done", newLookCardDone_);
    std::ofstream(fsPath(prefDir_ + "project.json"), std::ios::binary | std::ios::trunc) << prefs.dump(true);
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

void App::tickProject(float dt) {
    updateTitle();
    if (!interactive() || prefDir_.empty()) return;
    // Sculpting time, for the one optional support reminder (Support.h): counted here, kept in project.json.
    const bool stroking = doc_.sculptor.strokeActive();
    if (stroking) sculptSeconds_ = addSculptTime(sculptSeconds_, double(dt));
    if (supportReminderDue(sculptSeconds_, double(SDL_GetTicks()) * 1e-3, stroking, supportReminderDone_)) {
        supportReminderDone_ = true; // once only
        supportReminderOpen_ = true;
        saveProjectPrefs();
        redrawAt_ = double(SDL_GetTicksNS()) * 1e-9; // (a frame to show it, asleep or not)
    }
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
    if (saveProject(autosavePath(prefDir_), doc_, err)) {
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
    s.set("camera", std::move(cam));

    Json render = Json::object();
    render.set("wireframe", renderSettings_.wireframe);
    render.set("base_color", vec3Json(renderSettings_.baseColor));
    render.set("show_polypaint", renderSettings_.showPolypaint);
    render.set("color_tints_material", renderSettings_.colorTintsMaterial);
    render.set("show_mask", renderSettings_.showMask);
    render.set("show_polygroups", renderSettings_.showPolygroups);
    render.set("show_mesh", renderSettings_.showMesh);
    render.set("show_splats", renderSettings_.showSplats);
    render.set("splat_opacity", double(renderSettings_.splatOpacity));
    render.set("flat_shading", renderSettings_.flatShading);
    render.set("backface_culling", renderSettings_.backfaceCulling);
    render.set("background", std::string(renderSettings_.gradientBackground ? "gradient" : "flat"));
    render.set("background_color", vec3Json(renderSettings_.backgroundColor));
    render.set("matcap", renderSettings_.matcap < 0 ? std::string() : std::string(matcapName(renderSettings_.matcap)));
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
    // A custom MatCap travels inside the project (a PNG attachment), so the project opens with it on any machine.
    for (auto it = doc_.projectFiles.begin(); it != doc_.projectFiles.end();)
        it = it->first.rfind("matcaps/", 0) == 0 ? doc_.projectFiles.erase(it) : std::next(it);
    if (std::vector<uint8_t> png; isCustomMatcap(renderSettings_.matcap) && encodeMatcapPng(renderSettings_.matcap, png)) {
        const std::string file = matcapAttachmentName(matcapName(renderSettings_.matcap));
        doc_.projectFiles[file] = std::move(png);
        render.set("matcap_file", file);
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

    Json gizmo = Json::object(); // Gizmo 3D (Gizmo.cpp): where the pivot is and the options
    gizmo.set("pivot", vec3Json(gizmo_.pivot));
    gizmo.set("pivot_placed", gizmo_.pivotPlaced);
    gizmo.set("orient_surface", gizmo_.orientSurface);
    gizmo.set("axis_x", vec3Json(gizmo_.frame[0]));
    gizmo.set("axis_y", vec3Json(gizmo_.frame[1]));
    gizmo.set("use_mask", gizmo_.useMask);
    gizmo.set("all_subtools", gizmo_.all);
    gizmo.set("falloff", double(gizmo_.falloff));
    s.set("gizmo", std::move(gizmo));
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
        camera_.distance = std::max(camera_.distance, 1e-4f);
        camera_.fovY = std::clamp(camera_.fovY, 0.05f, 3.0f);
    }
    if (const Json& r = s["render"]; r.isObject()) {
        readBool(r, "wireframe", renderSettings_.wireframe);
        readVec3(r["base_color"], renderSettings_.baseColor);
        readBool(r, "show_polypaint", renderSettings_.showPolypaint);
        readBool(r, "color_tints_material", renderSettings_.colorTintsMaterial);
        readBool(r, "show_mask", renderSettings_.showMask);
        readBool(r, "show_polygroups", renderSettings_.showPolygroups);
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
        // The background (#21); projects from before it: the preferred one.
        renderSettings_.gradientBackground =
            r["background"].isString() ? r["background"].asString() != "flat" : uiPrefs_.gradientBackground;
        renderSettings_.backgroundColor = uiPrefs_.backgroundColor;
        readVec3(r["background_color"], renderSettings_.backgroundColor);
        renderSettings_.backgroundColor = glm::clamp(renderSettings_.backgroundColor, 0.0f, 1.0f);
        // Unknown, empty or missing (projects from before MatCaps): studio lighting, as they were saved.
        renderSettings_.matcap = -1;
        const std::string name = r["matcap"].isString() ? r["matcap"].asString() : std::string();
        for (int i = 0; i < matcapCount(); ++i)
            if (!name.empty() && name == matcapName(i)) renderSettings_.matcap = i;
        // A custom MatCap the project carries: the one with the same pixels if this machine has it, else added for
        // the session (renamed when a different MatCap here already has its name).
        const auto file = r["matcap_file"].isString() ? doc_.projectFiles.find(r["matcap_file"].asString())
                                                      : doc_.projectFiles.end();
        CustomMatcap m;
        std::string err;
        if (file != doc_.projectFiles.end() && decodeMatcapImage(file->second.data(), file->second.size(), m, err)) {
            int index = findMatcapByHash(m.hash);
            if (index < 0) {
                m.name = name.empty() ? std::string("Lit Sphere") : name;
                for (int n = 2; findMatcap(m.name) >= 0; ++n) m.name = name + " " + std::to_string(n);
                index = addCustomMatcap(std::move(m));
            }
            renderSettings_.matcap = index;
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
        readBool(g, "all_subtools", gizmo_.all);
        readFloat(g, "falloff", gizmo_.falloff);
        gizmo_.falloff = std::clamp(gizmo_.falloff, 0.0f, 1.0f);
        if (!gizmo_.pivotPlaced) updateGizmoPivot();
        gizmo_.undoSteps.clear();
        gizmo_.redoSteps.clear();
    }
}

void App::afterCommand(const std::string& name, const Args& args, const CommandResult& r) {
    if (!r.ok) return;
    if (name == "undo" || name == "redo") gizmoAfterUndo(name == "redo");
    const std::string path = argString(args, "path", doc_.path);
    const bool project = name == "open_project" || (name == "open" && isProjectPath(path));
    if (project) {
        if (!doc_.projectSettings["camera"].isObject()) camera_.frame(sceneBounds()); // e.g. saved by the CLI
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
    ImGui::SliderInt(tr("Autosave (min)"), &autosaveMinutes_, 0, 60, autosaveMinutes_ ? "%d" : "off");
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

void App::startUpdateCheck(bool manual) {
    if (updateJob_) return; // one at a time
    auto job = std::make_shared<UpdateJob>();
    job->manual = manual;
    updateJob_ = job;
    if (!manual) {
        updateCheckedAt_ = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        saveProjectPrefs();
    }
    const char* feed = std::getenv("SF_UPDATE_FEED");
    std::string source = feed && *feed ? feed : kUpdateFeedUrl;
    // Detached: a slow network never holds up quitting (the job is shared, so it outlives the app if it must).
    std::thread([job, source] {
        std::string body;
        if (source.rfind("https://", 0) == 0 || source.rfind("http://", 0) == 0) {
            ai::HttpResponse r = ai::httpRequest("GET", source, {"Accept: application/json"}, {}, 15);
            if (r.status != 200 && source == kUpdateFeedUrl)
                r = ai::httpRequest("GET", kUpdateFeedMirrorUrl, {"Accept: application/json"}, {}, 15);
            if (r.status == 200) body = r.body;
            else job->error = r.status ? "the server answered " + std::to_string(r.status) : (r.error.empty() ? "no connection" : r.error);
        } else {
            std::ifstream f(fsPath(source), std::ios::binary);
            if (f) body.assign(std::istreambuf_iterator<char>(f), {});
            else job->error = "can't read " + source;
        }
        if (job->error.empty()) job->ok = parseUpdateFeed(body, job->info, job->error);
        job->done = true;
    }).detach();
}

void App::pollUpdateCheck() {
    if (!updateJob_ || !updateJob_->done) return;
    const std::shared_ptr<UpdateJob> job = std::move(updateJob_);
    updateJob_.reset();
    if (!job->ok) {
        updateStatus_ = "Couldn't check for updates: " + job->error;
        if (job->manual) status_ = updateStatus_;
        return;
    }
    const UpdateInfo& u = job->info;
    if (compareVersions(u.version, SCULPTFORGE_VERSION) <= 0) {
        updateStatus_ = "SculptForge " SCULPTFORGE_VERSION " is the newest version";
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
        // Reading size (the guide's 17 px fonts), so the highlights are easy to read over the canvas.
        if (ImFont* title = guide_.bodyBoldFont()) ImGui::PushFont(title);
        ImGui::Text(tr("SculptForge %s is available"), u.version.c_str());
        if (guide_.bodyBoldFont()) ImGui::PopFont();
        if (ImFont* body = guide_.bodyFont()) ImGui::PushFont(body);
        ImGui::SameLine();
        ImGui::TextDisabled(tr("(you have %s)"), SCULPTFORGE_VERSION);
        for (const auto& h : u.highlights) ImGui::BulletText("%s", h.c_str());
        ImGui::TextDisabled("%s", kFreeReleaseNote); // memberships (Support.h): releases stay free for everyone
        if (guide_.bodyFont()) ImGui::PopFont();
        ImGui::Spacing();
        bool close = false;
        if (ImGui::Button(tr("What's new"))) SDL_OpenURL(u.notesUrl.c_str());
        ImGui::SameLine();
        if (ImGui::Button(tr("Download"))) {
            SDL_OpenURL(u.downloadUrl.c_str());
            close = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Become an Insider"))) openSupportPage();
        ImGui::SameLine();
        if (ImGui::Button(tr("Later"))) close = true;
        ImGui::SameLine();
        if (ImGui::Button(tr("Skip this version"))) {
            updateSkipped_ = u.version;
            saveProjectPrefs();
            close = true;
        }
        if (close) updateAvailable_.reset();
    }
    ImGui::End();
}

void App::drawUpdatesSection() {
#if SF_UPDATE_CHECK
    if (ImGui::Checkbox(tr("Check for updates"), &checkUpdates_)) saveProjectPrefs();
    ui::HelpText("Check for updates", nullptr,
                 "Once a day at startup, download a small file from the SculptForge website that names the newest "
                 "version, and show a banner when there is a newer one. Nothing about you or your computer is sent.",
                 "Preferences > Updates");
#else
    ImGui::TextWrapped("%s", tr("This build gets its updates from your package manager."));
#endif
    ui::ActionButton(*this, "help.check_updates", ImVec2(-1, 0));
    if (updateJob_) ImGui::TextDisabled("%s", tr("Checking..."));
    else if (!updateStatus_.empty()) ImGui::TextWrapped("%s", updateStatus_.c_str());
}

} // namespace sf
