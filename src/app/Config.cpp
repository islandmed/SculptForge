// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Default actions and shortcuts, the preferences' autosave, Save Layout / Revert / Export / Import Interface,
// the Preferences > Saving & Layouts / Interface / Hotkeys / Library sections, the F1
// Controls sheet (generated from the action registry) and classic
// shortcut assignment.
#include "app/IconsUsed.h"
#include "app/UiScale.h"
#include "app/NativeDialog.h"
#include "app/Config.h"

#include "app/App.h"
#include "app/UiServe.h"
#include "app/CrashLog.h"
#include "core/PerfTrace.h"
#include "app/LookSchemes.h"
#include "app/Palettes.h"
#include "app/Ui.h"
#include "app/I18n.h"
#include "app/UserData.h"
#include "core/LegacyNames.h"
#include "core/Bake.h"
#include "core/ListSort.h"
#include "core/Micro.h"
#include "core/FigurePose.h"
#include "core/Multires.h"
#include "core/Script.h"
#include "core/Units.h"

#include <IconsLucide.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <span>

namespace sf {

namespace assets {
extern const unsigned char fontSemiBold[];
extern const unsigned long long fontSemiBold_size;
extern const unsigned char fontLucide[];
extern const unsigned long long fontLucide_size;
} // namespace assets

void lookToJson(const UiPrefs& p, Json& j) {
    Json look = Json::object();
    look.set("name", std::string(looks::lookKey(looks::LookId(p.look.look))));
    look.set("scheme", p.look.scheme);
    look.set("matchOmarchy", p.look.matchOmarchy);
    look.set("phosphor", std::string(p.look.amber ? "amber" : "green"));
    look.set("effects", p.look.effects);
    look.set("outlines", std::string(p.look.outlines == 0 ? "off" : p.look.outlines == 2 ? "on" : "subtle"));
    look.set("outlineOpacity", double(p.look.outlineOpacity));
    j.set("look", std::move(look));
    j.set("layout", std::string(p.shellLayout == 1 ? "sculptforge" : "classic"));
}

bool lookFromJson(const Json& j, UiPrefs& p) {
    if (!j.isObject()) return false;
    if (const Json& look = j["look"]; look.isObject()) {
        if (look["name"].isString()) p.look.look = int(looks::lookFromKey(look["name"].asString()));
        if (look["scheme"].isString() && looks::findScheme(look["scheme"].asString())) p.look.scheme = look["scheme"].asString();
        p.look.matchOmarchy = look["matchOmarchy"].asBool(p.look.matchOmarchy);
        if (look["phosphor"].isString()) p.look.amber = look["phosphor"].asString() == "amber";
        p.look.effects = look["effects"].asBool(p.look.effects);
        if (look["outlines"].isString()) {
            const std::string o = look["outlines"].asString();
            p.look.outlines = o == "off" ? 0 : o == "on" ? 2 : 1;
        }
        p.look.outlineOpacity = std::clamp(float(look["outlineOpacity"].asNumber(p.look.outlineOpacity)), 0.0f, 1.0f);
    }
    if (j["layout"].isString()) p.shellLayout = j["layout"].asString() == "sculptforge" ? 1 : 0;
    return j["look"].isObject() || j["layout"].isString();
}

Json toJson(const UiPrefs& p) {
    Json j = Json::object();
    j.set("popupInfo", p.popupInfo);
    j.set("helpOpacity", double(p.helpOpacity));
    j.set("tooltipDelay", double(p.tooltipDelay));
    j.set("reduceMotion", p.reduceMotion);
    j.set("oneSubpalette", p.oneSubpalette);
    j.set("autoCollapseLeft", p.autoCollapseLeft);
    j.set("autoCollapseRight", p.autoCollapseRight);
    j.set("uiScale", double(p.uiScale));
    j.set("libraryAtStartup", p.libraryAtStartup);
    j.set("startupSplash", p.startupSplash);
    j.set("libraryFolder", p.libraryFolder);
    j.set("cameraNotch", double(p.cameraNotch));
    j.set("orbitLock", std::string(orbitLockName(p.orbitLock)));
    j.set("turntable", p.orbitLock == 1);
    j.set("inputStyle", std::string(p.inputStyle == 3 ? "maya" : p.inputStyle == 2 ? "blender" : p.inputStyle == 1 ? "sculptforge" : "classic"));
    j.set("emulateNumpad", p.emulateNumpad);
    j.set("frameDoubleTapMs", double(p.frameDoubleTapMs));
    j.set("rightClickQuickMenu", p.rightClickQuickMenu);
    j.set("brushSort", p.brushSort);
    j.set("alphaSort", p.alphaSort);
    j.set("materialSort", p.materialSort);
    j.set("librarySort", p.librarySort);
    if (!p.brushSet.empty()) j.set("brushSet", p.brushSet);
    j.set("spaceCamera", p.spaceCamera);
    j.set("emulate3Button", p.emulate3Button);
    j.set("mayaZoomHorizontal", p.mayaZoomHorizontal);
    j.set("wheelZoomToPointer", p.wheelZoomToPointer);
    j.set("invertZoom", p.invertZoom);
    j.set("keyNavSpeed", p.keyNavSpeed);
    j.set("quickRemesh", p.quickRemesh);
    j.set("remeshAfterStroke", p.remeshAfterStroke);
    j.set("alongNormal", std::string(p.alongNormal == 1 ? "always" : p.alongNormal == 2 ? "never" : "invert_key"));
    j.set("connectedOnly", p.connectedOnly);
    j.set("viewFalloff", p.viewFalloff);
    j.set("dyntopoSmoothRemesh", double(p.dyntopoSmoothRemesh));
    j.set("airStrokes", p.airStrokes);
    j.set("slideRotateKey", std::string(p.slideRotateKey == 1 ? "ctrl" : p.slideRotateKey == 2 ? "alt" : p.slideRotateKey == 3 ? "off" : "shift"));
    j.set("cursorFollowsSurface", p.cursorFollowsSurface);
    j.set("hideRingWhileStroking", p.hideRingWhileStroking);
    j.set("timelinePosition", std::string(p.timelineTop ? "top" : "bottom"));
    j.set("showAxes", p.showAxes);
    j.set("axisGizmoSize", std::string(p.axisGizmoSize == 0 ? "small" : p.axisGizmoSize == 2 ? "large" : "medium"));
    j.set("showViewFigure", p.showViewFigure);
    j.set("alphabeticalPalettes", p.alphabeticalPalettes);
    lookToJson(p, j);
    j.set("language", p.language);
    j.set("lengthUnit", p.lengthUnit);
    j.set("fileUnit", p.fileUnit);
    j.set("tablet", toJson(p.tablet));
    j.set("spaceMouse", toJson(p.spaceMouse));
    j.set("antialiasing", double(p.antialiasing));
    j.set("fastNavigation", p.fastNavigation);
    j.set("viewResolution", double(p.viewResolution));
    j.set("multithreading", p.multithreading);
    j.set("predictiveStrokes", p.predictiveStrokes);
    j.set("draftStrokes", p.draftStrokes);
    j.set("liveMove", p.liveMove);
    j.set("backgroundPreview", p.backgroundPreview);
    j.set("fastUpload", p.fastUpload);
    j.set("fastBigBrushes", p.fastBigBrushes);
    j.set("dyntopoConstantDetail", p.dyntopoConstantDetail);
    j.set("flatShading", p.flatShading);
    j.set("viewportCursor", double(p.viewportCursor));
    j.set("backfaceCulling", p.backfaceCulling);
    j.set("floorGrid", p.floorGrid);
    j.set("uvPackColumns", double(p.uvPackColumns));
    j.set("uvPackRows", double(p.uvPackRows));
    j.set("maskDarkness", double(p.maskDarkness));
    j.set("confirmSubdivide", p.confirmSubdivide);
    j.set("confirmRemeshLevels", p.confirmRemeshLevels);
    j.set("background", std::string(p.gradientBackground ? "gradient" : "flat"));
    Json bg = Json::array();
    for (int i = 0; i < 3; ++i) bg.push(double(p.backgroundColor[i]));
    j.set("backgroundColor", std::move(bg));
    j.set("showHints", p.showHints);
    Json hints = Json::array();
    for (const std::string& h : p.hiddenHints) hints.push(h);
    j.set("hiddenHints", std::move(hints));
    return j;
}

const char* orbitLockName(int lock) {
    static constexpr const char* names[4] = {"free", "y", "x", "z"};
    return names[std::clamp(lock, 0, 3)];
}

int parseOrbitLock(std::string_view name) {
    for (int i = 0; i < 4; ++i)
        if (name == orbitLockName(i)) return i;
    return name == "turntable" ? 1 : -1;
}

void fromJson(const Json& j, UiPrefs& p) {
    if (!j.isObject()) return;
    p.popupInfo = j["popupInfo"].asBool(p.popupInfo);
    p.helpOpacity = std::clamp(float(j["helpOpacity"].asNumber(p.helpOpacity)), 0.3f, 1.0f);
    p.tooltipDelay = std::clamp(float(j["tooltipDelay"].asNumber(p.tooltipDelay)), 0.0f, 3.0f);
    p.reduceMotion = j["reduceMotion"].asBool(p.reduceMotion);
    p.oneSubpalette = j["oneSubpalette"].asBool(p.oneSubpalette);
    p.autoCollapseLeft = j["autoCollapseLeft"].asBool(p.autoCollapseLeft);
    p.autoCollapseRight = j["autoCollapseRight"].asBool(p.autoCollapseRight);
    p.uiScale = std::clamp(float(j["uiScale"].asNumber(p.uiScale)), 0.0f, 3.0f);
    p.libraryAtStartup = j["libraryAtStartup"].asBool(p.libraryAtStartup);
    p.startupSplash = j["startupSplash"].asBool(p.startupSplash);
    if (j["libraryFolder"].isString()) p.libraryFolder = j["libraryFolder"].asString();
    p.cameraNotch = std::clamp(int(j["cameraNotch"].asNumber(p.cameraNotch)), 0, 2);
    if (j["turntable"].asBool(false)) p.orbitLock = 1;
    if (j["orbitLock"].isString()) p.orbitLock = std::max(parseOrbitLock(j["orbitLock"].asString()), 0);
    if (j["inputStyle"].isString()) {
        p.inputStyleSaved = true;
        const std::string style = j["inputStyle"].asString();
        p.inputStyle = style == "maya" ? 3 : style == "blender" ? 2 : style == "sculptforge" ? 1 : 0;
    }
    p.emulateNumpad = j["emulateNumpad"].asBool(p.emulateNumpad);
    p.frameDoubleTapMs = std::clamp(int(j["frameDoubleTapMs"].asNumber(p.frameDoubleTapMs)), 0, 1000);
    p.rightClickQuickMenu = j["rightClickQuickMenu"].asBool(p.rightClickQuickMenu);
    for (auto [key, v] : {std::pair{"brushSort", &p.brushSort}, std::pair{"alphaSort", &p.alphaSort},
                          std::pair{"materialSort", &p.materialSort}, std::pair{"librarySort", &p.librarySort}})
        if (j[key].isString()) *v = listSortId(listSortFromId(j[key].asString()));
    if (j["brushSet"].isString() && j["brushSet"].asString().size() <= 64) p.brushSet = j["brushSet"].asString();
    p.spaceCamera = j["spaceCamera"].asBool(p.spaceCamera);
    p.emulate3Button = j["emulate3Button"].asBool(p.emulate3Button);
    p.mayaZoomHorizontal = j["mayaZoomHorizontal"].asBool(p.mayaZoomHorizontal);
    p.wheelZoomToPointer = j["wheelZoomToPointer"].asBool(p.inputStyle == 3); // (inputStyle is read above)
    p.invertZoom = j["invertZoom"].asBool(p.invertZoom);
    p.keyNavSpeed = std::clamp(float(j["keyNavSpeed"].asNumber(p.keyNavSpeed)), 0.1f, 5.0f);
    p.quickRemesh = j["quickRemesh"].asBool(p.quickRemesh);
    p.remeshAfterStroke = j["remeshAfterStroke"].asBool(p.remeshAfterStroke);
    if (j["alongNormal"].isString()) {
        const std::string a = j["alongNormal"].asString();
        p.alongNormal = a == "always" ? 1 : a == "never" ? 2 : 0;
    }
    p.connectedOnly = j["connectedOnly"].asBool(p.connectedOnly);
    p.viewFalloff = j["viewFalloff"].asBool(p.viewFalloff);
    p.dyntopoSmoothRemesh = std::clamp(float(j["dyntopoSmoothRemesh"].asNumber(p.dyntopoSmoothRemesh)), 0.0f, 1.0f);
    p.airStrokes = j["airStrokes"].asBool(p.airStrokes);
    if (j["slideRotateKey"].isString()) {
        const std::string k = j["slideRotateKey"].asString();
        p.slideRotateKey = k == "ctrl" ? 1 : k == "alt" ? 2 : k == "off" ? 3 : 0;
    }
    p.cursorFollowsSurface = j["cursorFollowsSurface"].asBool(p.cursorFollowsSurface);
    p.hideRingWhileStroking = j["hideRingWhileStroking"].asBool(p.hideRingWhileStroking);
    if (j["timelinePosition"].isString()) p.timelineTop = j["timelinePosition"].asString() != "bottom";
    p.showAxes = j["showAxes"].asBool(p.showAxes);
    if (j["axisGizmoSize"].isString()) {
        const std::string size = j["axisGizmoSize"].asString();
        p.axisGizmoSize = size == "small" ? 0 : size == "large" ? 2 : 1;
    }
    p.showViewFigure = j["showViewFigure"].asBool(p.showViewFigure);
    p.alphabeticalPalettes = j["alphabeticalPalettes"].asBool(p.alphabeticalPalettes);
    // A ui.json from before 0.10.7 names neither: it was stored with the Classic look and the Classic layout,
    // and keeps them (the new-look card offers the new default once, PanelSupport.cpp).
    p.lookSaved = lookFromJson(j, p);
    if (!p.lookSaved) {
        p.look.look = int(looks::LookId::Classic);
        p.shellLayout = 0;
    }
    if (j["language"].isString()) p.language = languageCode(languageFromCode(j["language"].asString()));
    if (const LengthUnit* u = findLengthUnit(j["lengthUnit"].asString())) p.lengthUnit = u->id;
    if (const LengthUnit* u = findLengthUnit(j["fileUnit"].asString())) p.fileUnit = u->id;
    fromJson(j["tablet"], p.tablet);
    fromJson(j["spaceMouse"], p.spaceMouse);
    const int aa = int(j["antialiasing"].asNumber(p.antialiasing));
    p.antialiasing = aa >= 8 ? 8 : aa >= 4 ? 4 : aa >= 2 ? 2 : 1;
    p.fastNavigation = j["fastNavigation"].asBool(p.fastNavigation);
    {
        const int r = int(j["viewResolution"].asNumber(p.viewResolution));
        p.viewResolution = r <= 50 ? 50 : r <= 75 ? 75 : 100;
    }
    p.multithreading = j["multithreading"].asBool(p.multithreading);
    p.predictiveStrokes = j["predictiveStrokes"].asBool(p.predictiveStrokes);
    p.draftStrokes = j["draftStrokes"].asBool(p.draftStrokes);
    p.liveMove = j["liveMove"].asBool(p.liveMove);
    p.backgroundPreview = j["backgroundPreview"].asBool(p.backgroundPreview);
    p.fastUpload = j["fastUpload"].asBool(p.fastUpload);
    p.fastBigBrushes = j["fastBigBrushes"].asBool(p.fastBigBrushes);
    p.dyntopoConstantDetail = j["dyntopoConstantDetail"].asBool(p.dyntopoConstantDetail);
    p.flatShading = j["flatShading"].asBool(p.flatShading);
    p.viewportCursor = std::clamp(int(j["viewportCursor"].asNumber(p.viewportCursor)), 0, 1);
    p.backfaceCulling = j["backfaceCulling"].asBool(p.backfaceCulling);
    p.floorGrid = j["floorGrid"].asBool(p.floorGrid);
    p.uvPackColumns = std::clamp(int(j["uvPackColumns"].asNumber(p.uvPackColumns)), 1, 10);
    p.uvPackRows = std::clamp(int(j["uvPackRows"].asNumber(p.uvPackRows)), 1, 10);
    p.maskDarkness = std::clamp(float(j["maskDarkness"].asNumber(p.maskDarkness)), 0.0f, 1.0f);
    p.confirmSubdivide = j["confirmSubdivide"].asBool(p.confirmSubdivide);
    p.confirmRemeshLevels = j["confirmRemeshLevels"].asBool(p.confirmRemeshLevels);
    if (j["background"].isString()) p.gradientBackground = j["background"].asString() != "flat";
    p.showHints = j["showHints"].asBool(p.showHints);
    if (const Json& hints = j["hiddenHints"]; hints.size() > 0) {
        p.hiddenHints.clear();
        for (size_t i = 0; i < hints.size() && i < 64; ++i)
            if (hints[i].isString() && hints[i].asString().size() < 64) p.hiddenHints.insert(hints[i].asString());
    }
    if (const Json& bg = j["backgroundColor"]; bg.size() == 3)
        for (int i = 0; i < 3; ++i) p.backgroundColor[i] = std::clamp(float(bg[size_t(i)].asNumber(p.backgroundColor[i])), 0.0f, 1.0f);
}

namespace {

enum UiFileKind { kSaveUi, kLoadUi, kRunScript, kLibraryFolder, kBlenderKeymap, kRadialImport, kRadialExport, kMayaFolder, kUiFileKinds };

struct UiFileRequest {
    App* app = nullptr;
    int kind = 0;
};
UiFileRequest g_requests[kUiFileKinds];

void SDLCALL onUiFileChosen(void* userdata, const char* const* files, int) {
    wakeMainLoop(); // a file dialog answered (maybe on another thread): redraw now
    if (!files || !files[0]) return;
    auto* req = static_cast<UiFileRequest*>(userdata);
    req->app->queueUiFile(req->kind, files[0]);
}

const SDL_DialogFileFilter kUiFilters[] = {{"SculptForge UI layout", "json"}};
const SDL_DialogFileFilter kBlenderKeymapFilters[] = {{"Blender keymap", "py"}, {"All files", "*"}};
const SDL_DialogFileFilter kRadialFilters[] = {{"SculptForge radial menu", "json"}};
const SDL_DialogFileFilter kScriptFilters[] = {{"SculptForge script", "sfscript;txt"}, {"All files", "*"}};

std::filesystem::path fsPath(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }

bool fileExists(const std::string& path) {
    std::error_code ec;
    return !path.empty() && std::filesystem::is_regular_file(fsPath(path), ec);
}

std::string lower(std::string s) {
    for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

ImVec4 col(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

// The mouse and pen gestures (not actions, so not in the registry).
struct Gesture {
    const char* what;
    const char* how;
};
constexpr Gesture kGestures[] = {
    {"Sculpt", "Left drag on the model"},
    {"Invert (subtract)", "Hold Alt while sculpting"},
    {"Smooth", "Hold Shift while sculpting"},
    {"Rotate", "Left drag on the background, or right drag"},
    {"Pan", "Alt + left drag on the background, or middle drag"},
    {"Zoom", "Mouse wheel, Ctrl + middle drag, or Ctrl + left drag on the background (nothing masked)"},
    {"Paint mask", "Hold Ctrl while stroking (Ctrl+Alt erases)"},
    {"Invert mask", "Ctrl + click the background"},
    {"Clear mask", "Ctrl + drag on the background (Mask Pen)"},
    {"Mask a lasso or rectangle", "Ctrl + drag with Lasso Mask / Rectangle Mask (Tool > Masking; Ctrl+Alt unmasks)"},
    {"Isolate face group", "Ctrl+Shift + click it (again: invert)"},
    {"Hide face group", "Ctrl+Shift+Alt + click it"},
    {"Show all", "Ctrl+Shift + click the background"},
    {"Hide outside a rectangle or lasso", "Ctrl+Shift + drag (add Alt: inside; shape in Tool > Visibility)"},
    {"Select a Part", "Alt + click it"},
    {"Help on any control", "Hold Ctrl and point at it (Ctrl+Alt: scripting parameters)"},
    {"Assign a shortcut", "Ctrl+Alt + click any button, then press the key"},
    {"Tablet pressure", "Used automatically when a pen is detected (Preferences > Tablet: curve, test area)"},
    {"Pen eraser end", "Inverts the brush (Ctrl + eraser erases mask); Preferences > Tablet"},
    {"Pen side buttons", "Lower: right-click (drag to rotate), upper: Alt (invert); Preferences > Tablet"},
    {"Open files", "Drop a model (.obj, .glb ...), brush (.sfbrush) or image onto the window"},
};

// The same in the Blender style (Preferences > Input).
constexpr Gesture kBlenderGestures[] = {
    {"Sculpt", "Left drag on the model"},
    {"Invert (subtract)", "Hold Ctrl while sculpting"},
    {"Smooth", "Hold Shift while sculpting"},
    {"Rotate", "Middle drag, Alt + left drag (pen, trackpad), or left drag beside the model"},
    {"Pan", "Shift + middle drag, or Alt+Shift + left drag"},
    {"Zoom", "Mouse wheel, Ctrl + middle drag, or Alt+Ctrl + left drag"},
    {"Views", "Numpad 1 front, 3 right, 7 top (Ctrl: the opposite side), 5 orthographic, . frame the Part, Home "
              "frame everything; Emulate Numpad (Preferences > Input) for the number row"},
    {"Paint mask", "M for the Mask brush, then drag (Ctrl+drag unmasks); M again goes back"},
    {"Invert / clear mask", "Ctrl+I / Alt+M"},
    {"Select a Part", "Alt+Q over it (Alt+click masks in this style), or triple-click it"},
    {"Hide face group", "H over it (Shift+H: show only it, again: everything; Alt+H shows all)"},
    {"Hide outside a shape", "Ctrl+Shift + drag a rectangle or lasso (add Alt: inside; shape in Tool > Visibility); "
                             "click: isolate the face group, the background shows all"},
    {"Cut", "Pick a Cut shape (Ctrl+Shift + click the brush tile, or Ctrl+Shift+B), then Ctrl+Shift + drag"},
    {"Brush radius / strength", "F / Shift+F, then drag or type a number"},
    {"Help on any control", "Hold Ctrl and point at it (Ctrl+Alt: scripting parameters)"},
    {"Assign a shortcut", "Ctrl+Alt + click any button, then press the key"},
    {"Tablet pressure", "Used automatically when a pen is detected (Preferences > Tablet: curve, test area)"},
    {"Pen side buttons", "Lower: right-click (drag to rotate), upper: invert; Preferences > Tablet"},
    {"Open files", "Drop a model (.obj, .glb ...), brush (.sfbrush) or image onto the window"},
};

} // namespace

// ---------------------------------------------------------------------------
// Startup
// ---------------------------------------------------------------------------

void App::initUiConfig(bool deterministic) {
    uiConfigDeterministic_ = deterministic;
    configDir_ = userDataDir();
    layout_ = standardLayout(uiPrefs_.shellLayout);
    if (deterministic) return; // tests, renders and recordings always use the standard layout
    std::string err;
    if (fileExists(uiConfigPath())) {
        if (!readUiConfig(uiConfigPath(), err, true)) SDL_Log("%s", err.c_str());
    } else if (const std::string& studio = studioMode().profile.preferences; !studio.empty() && fileExists(studio)) {
        // A studio build's first start: the studio's layout, keys and scale (core/StudioProfile.h); saved as the
        // artist's own from the first change on.
        if (!readUiConfig(studio, err)) SDL_Log("Studio preferences: %s", err.c_str());
    }
}

void App::addHelpFonts(float scale) {
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;
    cfg.OversampleH = 2;
    fontHelpTitle_ = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(assets::fontSemiBold),
                                                    int(assets::fontSemiBold_size), 15.0f * scale, &cfg);
        ImFontConfig icons;
    icons.MergeMode = true;
    icons.PixelSnapH = true;
    icons.FontDataOwnedByAtlas = false;
    icons.GlyphMinAdvanceX = 15.0f * scale;
    icons.GlyphOffset = ImVec2(0, 2.0f * scale);
    io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(assets::fontLucide), int(assets::fontLucide_size), 15.0f * scale,
                                   &icons, kUsedIconRanges);
    mergeCjkFont(15.0f * scale);
    // ProggyClean (ships inside Dear ImGui) is a pixel font: keep it at whole multiples of 13 px.
    ImFontConfig mono;
    mono.SizePixels = 13.0f * std::max(1.0f, std::floor(scale + 0.25f));
    fontConsole_ = io.Fonts->AddFontDefault(&mono);
}

void App::initActions() {
    for (int i = 0; i < kUiFileKinds; ++i) g_requests[i] = {this, i};
    auto add = [&](const char* id, const char* label, const char* palette, const char* shortcut, std::function<void()> run,
                   const char* icon = nullptr, const char* command = nullptr, std::function<bool()> checked = nullptr,
                   std::function<bool()> enabled = nullptr, const char* description = nullptr) {
        Action a;
        a.id = id;
        a.label = label;
        a.palette = palette;
        a.shortcut = shortcut ? shortcut : "";
        a.run = std::move(run);
        a.icon = icon;
        a.command = command ? command : "";
        a.checked = std::move(checked);
        a.enabled = std::move(enabled);
        if (description) a.description = description;
        else if (const Command* c = a.command.empty() ? nullptr : commands_.find(a.command)) a.description = c->description;
        actions_.add(std::move(a));
    };

    // --- The shortcuts SculptForge always had (unchanged meaning) ---
    add("edit.undo", "Undo", "Edit", "Ctrl+Z", [this] { runCommand("undo"); }, ICON_LC_UNDO_2, "undo", nullptr,
        [this] { return doc_.undo.canUndo(); });
    add("edit.redo", "Redo", "Edit", "Ctrl+Shift+Z / Ctrl+Y", [this] { runCommand("redo"); }, ICON_LC_REDO_2, "redo", nullptr,
        [this] { return doc_.undo.canRedo(); });
    // Subdivide bakes: it keeps no levels, and on a Part with levels it removes them. Named and placed apart from
    // Add Level, and it asks first on a Part with levels (Discord 11-02: "I keep pressing the wrong one").
    add("geometry.subdivide", "Subdivide (removes levels)", "Tool > Geometry", nullptr,
        [this] {
            if (doc_.mesh.multires && uiPrefs_.confirmSubdivide) openSubdivideConfirm_ = true;
            else runCommand("subdivide");
        },
        nullptr, "subdivide", nullptr, nullptr,
        "Destructive: four times the polygons as one plain mesh, with no lower levels to step back to. On a Part "
        "with subdivision levels it removes them (it asks first; Ctrl+Z brings them back). To keep levels, use "
        "Add Level instead.");
    // Tool > Geometry > Cleanup > Find Problems (PanelMeshCheck.cpp, its Check button): here so F3 finds it.
    add("geometry.mesh_check", "Find Problems", "Tool > Geometry", nullptr,
        [this] {
            runMeshCheck();
            status_ = meshReport_.summary();
        },
        nullptr, "mesh_check", nullptr, [this] { return doc_.mesh.triangleCount() > 0; },
        "Count what makes the active Part hard to sculpt, remesh, print or export: non-manifold edges, holes, "
        "duplicate, degenerate and flipped triangles. Changes nothing; Tool > Geometry > Find Problems lists each "
        "with its Fix.");
    add("geometry.close_holes", "Fill Holes", "Tool > Geometry", nullptr, [this] { runCommand("close_holes"); },
        ICON_LC_CIRCLE_DOT, "close_holes", nullptr, nullptr,
        "Cap every open boundary of the active Part (a hole, an open bottom, the rim of an extracted shell) with a "
        "new patch sized like the polygons around it, so remeshing and sculpting see a closed surface. The rest of "
        "the Part stays as it is; the patches get a face group of their own (#103).");
    // Subdivision levels (Tool > Geometry; core/Multires.h).
    add("geometry.divide", "Add Level", "Tool > Geometry", "Ctrl+D", [this] { runCommand("subdiv_divide", {{"smooth", smt_}}); },
        ICON_LC_GRID_2X2, "subdiv_divide", nullptr, nullptr,
        "Non-destructive: add a subdivision level: four times the polygons, and the lower levels are kept, so you can step down to "
        "shape big forms and back up to the detail. Quads stay quads (Catmull-Clark). Smooth off divides without smoothing.");
    add("geometry.smt", "Smooth", "Tool > Geometry", nullptr, [this] { smt_ = !smt_; }, nullptr, nullptr,
        [this] { return smt_; }, nullptr,
        "Smooth the next new level (on by default). Off: the new level keeps the hard edges and flat faces of the one below.");
    add("geometry.lower_res", "Lower Level", "Tool > Geometry", "Shift+D", [this] { runCommand("subdiv_lower"); },
        ICON_LC_CHEVRON_DOWN, "subdiv_lower", nullptr, [this] { return subdivCurrentLevel(doc_.mesh) > 0; },
        "Step one subdivision level down (fewer polygons, for big forms). The detail of the higher levels is kept.");
    add("geometry.higher_res", "Higher Level", "Tool > Geometry", "D", [this] { runCommand("subdiv_higher"); },
        ICON_LC_CHEVRON_UP, "subdiv_higher", nullptr,
        [this] { return subdivCurrentLevel(doc_.mesh) + 1 < subdivLevelCount(doc_.mesh); },
        "Step one subdivision level up (more polygons, for detail). Edits made below carry the detail along.");
    add("geometry.del_lower", "Remove Below", "Tool > Geometry", nullptr, [this] { confirmLevelDelete("subdiv_delete_lower"); }, nullptr,
        "subdiv_delete_lower", nullptr, [this] { return subdivCurrentLevel(doc_.mesh) > 0; });
    add("geometry.del_higher", "Remove Above", "Tool > Geometry", nullptr, [this] { confirmLevelDelete("subdiv_delete_higher"); }, nullptr,
        "subdiv_delete_higher", nullptr,
        [this] { return subdivCurrentLevel(doc_.mesh) + 1 < subdivLevelCount(doc_.mesh); });
    add("geometry.reconstruct", "Rebuild Lower Levels", "Tool > Geometry", nullptr, [this] { runCommand("subdiv_reconstruct"); },
        nullptr, "subdiv_reconstruct", nullptr, [this] { return subdivCurrentLevel(doc_.mesh) == 0; });
    // Micro-displacement levels (Tool > Geometry > Micro; core/Micro.h).
    add("geometry.micro_divide", "Add Micro", "Tool > Geometry", nullptr,
        [this] { runCommand("micro_divide", {{"level", double(microSlider_)}}); }, ICON_LC_GRID_3X3, "micro_divide", nullptr,
        [this] { return !hasMicro(doc_.mesh) && subdivCurrentLevel(doc_.mesh) + 1 >= subdivLevelCount(doc_.mesh); },
        "Store the surface above the highest level as micro displacements: hundreds of millions of sculptable "
        "triangles in a fraction of the memory (300M take about 0.4 GB). The Micro slider sets how fine.");
    add("geometry.micro_delete", "Delete Micro", "Tool > Geometry", nullptr, [this] { runCommand("micro_delete"); }, nullptr,
        "micro_delete", nullptr, [this] { return hasMicro(doc_.mesh); });
    add("geometry.micro_bake", "Bake Micro", "Tool > Geometry", nullptr, [this] { runCommand("micro_bake"); }, nullptr, "micro_bake",
        nullptr, [this] { return hasMicro(doc_.mesh); },
        "Turn the micro level into a regular mesh in a new Part (for export or tools that need real geometry).");
    add("geometry.micro_export", "Export Displacement", "Tool > Geometry", nullptr,
        [this] {
            const std::string path = (std::filesystem::path(defaultBakeFolder(doc_)) / (doc_.assetName() + "_micro_displacement.exr")).string();
            runCommand("micro_export_displacement", {{"path", path}});
        },
        nullptr, "micro_export_displacement", nullptr, [this] { return hasMicro(doc_.mesh) && doc_.mesh.hasUvs(); },
        "Write the micro level as a 32-bit EXR displacement map over the base UVs (next to the project).");
    add("geometry.micro_noise", "Micro Surface Noise", "Tool > Geometry", nullptr, [this] { runCommand("micro_noise"); }, nullptr,
        "micro_noise", nullptr, [this] { return hasMicro(doc_.mesh); });
    add("part.duplicate", "Duplicate Part", "Tool > Parts", "Ctrl+Shift+D", [this] { runCommand("part_duplicate"); },
        ICON_LC_COPY, "part_duplicate", nullptr, nullptr,
        "Copy the active Part, with its layers and attributes, and make the copy active (also in the Parts list's "
        "right-click menu).");
    add("part.split_hidden", "Split Hidden", "Tool > Parts", nullptr, [this] { runCommand("part_split_hidden"); }, nullptr,
        "part_split_hidden", nullptr, [this] { return doc_.mesh.hasHidden(); },
        "Move the hidden triangles of the active Part into a new Part just below it, shown. One undo step.");
    add("part.split_masked", "Split Masked", "Tool > Parts", nullptr, [this] { runCommand("part_split_masked"); }, nullptr,
        "part_split_masked", nullptr, [this] { return doc_.mesh.hasMask(); },
        "Move the masked triangles (the average mask of their corners above 50%) into a new, unmasked Part just "
        "below. One undo step.");
    add("part.split_unmasked", "Split Unmasked", "Tool > Parts", nullptr, [this] { runCommand("part_split_unmasked"); },
        nullptr, "part_split_unmasked", nullptr, [this] { return doc_.mesh.hasMask(); },
        "Move the triangles that are not masked into a new Part just below; the masked ones stay. One undo step.");
    add("geometry.delete_masked", "Delete Masked", "Tool > Visibility", nullptr, [this] { runCommand("delete_masked"); },
        nullptr, "delete_masked", nullptr, [this] { return doc_.mesh.hasMask(); },
        "Delete the masked triangles (the average mask of their corners above 50%) of the active Part. Ctrl+Z "
        "brings them back.");
    add("part.sculpt_under_cursor", "Sculpt the Part under the cursor", "Tool > Parts", "Alt+Q",
        [this] { sculptPartUnderCursor(); }, ICON_LC_MOUSE_POINTER_CLICK, nullptr, nullptr, nullptr,
        "Make the Part under the pointer the one you sculpt, without moving the view (each Part keeps its mask). "
        "The same key as Blender's; Preferences > Hotkeys changes it.");
    add("part.duplicate_linked", "Linked Duplicate", "Tool > Parts", nullptr, [this] { runCommand("part_duplicate_linked"); },
        ICON_LC_LINK, "part_duplicate_linked", nullptr, [this] { return doc_.mesh.triangleCount() > 0; },
        "A copy that shares the active Part's mesh, beside it: sculpt, paint or mask any of them and all change. Move "
        "each with the Gizmo; Make Unique gives one its own mesh again. Hundreds of copies cost about one mesh.");
    add("part.make_unique", "Make Unique", "Tool > Parts", nullptr, [this] { runCommand("part_make_unique"); },
        ICON_LC_UNLINK, "part_make_unique", nullptr, [this] { return doc_.linkCount(doc_.activePart) > 1; },
        "Give the active linked Part its own copy of the mesh: it stays where it is and no longer changes with the "
        "others.");
    // Tool > Part > Extract, Tool > Geometry > Symmetry (Make Symmetric, Mirror), Tool > Reshape (PanelExtract.cpp).
    add("part.extract", "Make Shell", "Tool > Parts", nullptr, [this] { runExtract(); }, nullptr, "extract", nullptr,
        [this] { return doc_.mesh.hasMask(); },
        "Make a shell from the masked part of the Part (a helmet, a strap, clothing), shown as a preview below it. "
        "Change Thickness, Edge Smooth, Both Sides, Corners or Border and it updates; Accept keeps it as a new Part.");
    add("part.extract_accept", "Accept", "Tool > Parts", nullptr, [this] { runCommand("extract_accept"); }, nullptr,
        "extract_accept", nullptr, [this] { return doc_.extractPreviewCurrent(); },
        "Keep the Extract preview as a new Part below this one. Ctrl+Z takes it away again.");
    // Figure Pose (core/FigurePose.h, PanelParts.cpp).
    add("part.figure_pose", "Pose all Parts", "Tool > Parts", nullptr, [this] { runCommand("figure_pose_begin"); }, nullptr,
        "figure_pose_begin", nullptr, [this] { return !figurePoseActive(doc_); },
        "Merge the visible Parts into one light Pose Mesh and pose it with the Gizmo and masks; Apply Pose gives "
        "every Part the pose at full resolution.");
    add("part.figure_pose_apply", "Apply Pose", "Tool > Parts", nullptr, [this] { runCommand("figure_pose_apply"); },
        nullptr, "figure_pose_apply", nullptr, [this] { return figurePoseActive(doc_); },
        "Give every Part the Pose Mesh's moves at full resolution, as one undo step, and remove the Pose Mesh.");
    add("part.figure_pose_cancel", "Cancel Pose", "Tool > Parts", nullptr, [this] { runCommand("figure_pose_cancel"); },
        nullptr, "figure_pose_cancel", nullptr, [this] { return figurePoseActive(doc_); },
        "Remove the Pose Mesh and show the Parts again; nothing changes.");
    add("part.extract_double", "Both Sides", "Tool > Parts", nullptr,
        [this] {
            extractDouble_ = !extractDouble_;
            if (doc_.extractPreviewCurrent()) runExtract();
        },
        nullptr, nullptr, [this] { return extractDouble_; }, nullptr,
        "Grow the extracted shell both inward and outward from the surface, instead of outward only.");
    add("part.extract_triangle_corners", "Corners", "Tool > Parts", nullptr,
        [this] {
            extractTriCorners_ = !extractTriCorners_;
            if (doc_.extractPreviewCurrent()) runExtract();
        },
        nullptr, nullptr, [this] { return extractTriCorners_; }, nullptr,
        "Triangle corners: where the extracted edge turns sharply, the rim is made of triangles instead of quads.");
    add("part.extract_triangle_border", "Border", "Tool > Parts", nullptr,
        [this] {
            extractTriBorder_ = !extractTriBorder_;
            if (doc_.extractPreviewCurrent()) runExtract();
        },
        nullptr, nullptr, [this] { return extractTriBorder_; }, nullptr,
        "Triangle border: the whole rim of the extracted shell is made of triangles instead of quads.");
    add("geometry.mirror_weld", "Make Symmetric", "Tool > Geometry", nullptr,
        [this] { runCommand("mirror_and_weld", {{"axis", axesText(mirrorWeldAxes_)}}); }, nullptr, "mirror_and_weld",
        nullptr, [this] { return !axesText(mirrorWeldAxes_).empty() && doc_.mesh.triangleCount() > 0; },
        "Mirror the + side of the Part onto the - side across the chosen axis and weld the seam into one "
        "watertight mesh. Face Groups and vertex paint come along. To keep the other side, Symmetry > Mirror first.");
    add("deformation.mirror", "Mirror", "Tool > Geometry", nullptr,
        [this] { runCommand("mirror", {{"axis", axesText(mirrorAxes_)}}); }, nullptr, "mirror", nullptr,
        [this] { return !axesText(mirrorAxes_).empty() && doc_.mesh.vertexCount() > 0; },
        "Flip the Part across the chosen axis, through the origin (the plane symmetry and Make Symmetric use).");
    for (int k = 0; k < 3; ++k) {
        static const char* const kWeldIds[3] = {"geometry.mirror_weld_x", "geometry.mirror_weld_y", "geometry.mirror_weld_z"};
        static const char* const kMirrorIds[3] = {"deformation.mirror_x", "deformation.mirror_y", "deformation.mirror_z"};
        static const char* const kNames[3] = {"X", "Y", "Z"};
        add(kWeldIds[k], kNames[k], "Tool > Geometry", nullptr, [this, k] { mirrorWeldAxes_[k] = !mirrorWeldAxes_[k]; },
            nullptr, nullptr, [this, k] { return mirrorWeldAxes_[k]; }, nullptr,
            "An axis Make Symmetric mirrors across (several: one after the other).");
        add(kMirrorIds[k], kNames[k], "Tool > Geometry", nullptr, [this, k] { mirrorAxes_[k] = !mirrorAxes_[k]; }, nullptr,
            nullptr, [this, k] { return mirrorAxes_[k]; }, nullptr, "An axis Mirror flips along (several: one after the other).");
    }
    // Whole-mesh deformations (core/Deform.h) at amount 50, for menus, keys and search; the panel has them as Stack
    // steps with their own amount (PanelDeform.cpp).
    {
        static const char* const kIds[4] = {"deformation.polish", "deformation.relax", "deformation.smooth", "deformation.balloon"};
        static const char* const kLabels[4] = {"Polish", "Relax", "Smooth", "Balloon"};
        static const char* const kCommands[4] = {"deform_polish", "deform_relax", "deform_smooth", "deform_balloon"};
        static const char* const kHelp[4] = {
            "Polish the whole Part (amount 50): fine noise and small bumps flatten out while flat areas and sharp "
            "edges stay. For another amount, add a Polish step to Tool > Reshape > Stack. The mask holds what it "
            "covers; one undo step.",
            "Relax the whole Part: the vertices even out their spacing along the surface without changing its shape "
            "(stretched areas after big moves). For another amount, add a Relax step to the Stack. The mask holds "
            "what it covers; one undo step.",
            "Smooth the whole Part (amount 50). For another amount, or to keep the volume, add a Smooth step to the "
            "Stack. The mask holds what it covers; one undo step.",
            "Fill the whole Part out toward a rounder volume, like a balloon (amount 50). For another amount, or a "
            "negative one that lets the air out, add a Balloon step to the Stack. The mask holds what it covers; "
            "one undo step."};
        for (int k = 0; k < 4; ++k)
            add(kIds[k], kLabels[k], "Tool > Reshape", nullptr,
                [this, k] { runCommand(kCommands[k], {{"amount", 50.0}}); }, nullptr, kCommands[k], nullptr,
                [this] { return doc_.mesh.vertexCount() > 0; },
                kHelp[k]);
    }
    // Reshape > Stack (PanelDeform.cpp, docs/design/deform-panel.md).
    {
        auto sel = [this]() -> DeformStep* {
            DeformStackUi& ds = deformStack_;
            return ds.selected >= 0 && ds.selected < int(ds.steps.size()) ? &ds.steps[size_t(ds.selected)] : nullptr;
        };
        auto any = [this] { return !deformStack_.steps.empty(); };
        add("deformation.stack_apply", "Apply", "Tool > Reshape", nullptr, [this] { deformStackApply(false); }, nullptr,
            nullptr, nullptr, any,
            "Keep the stack's result on the Part (one undo step) and start a new stack. With Preview on, what you "
            "see is what stays.");
        add("deformation.stack_apply_selected", "Apply Selected", "Tool > Reshape", nullptr,
            [this] { deformStackApply(true); }, nullptr, nullptr, nullptr, [sel] { return sel() != nullptr; },
            "Keep only the selected step on the Part (one undo step) and take it off the stack; the rest stay to "
            "edit.");
        add("deformation.stack_reset", "Reset", "Tool > Reshape", nullptr, [this] { deformStackReset(); }, nullptr,
            nullptr, nullptr, any, "Take the preview back off the Part and empty the stack.");
        add("deformation.stack_repeat", "Repeat", "Tool > Reshape", nullptr,
            [this, sel] {
                if (DeformStep* st = sel()) {
                    const DeformStep copy = *st;
                    deformStack_.steps.insert(deformStack_.steps.begin() + deformStack_.selected + 1, copy);
                    deformStack_.on.insert(deformStack_.on.begin() + deformStack_.selected + 1, char(1));
                    ++deformStack_.selected;
                    deformStackRefresh();
                }
            },
            nullptr, nullptr, nullptr, [sel] { return sel() != nullptr; },
            "Add the selected step again right after it (twice the twist, a second bend...).");
        add("deformation.stack_remove", "Remove", "Tool > Reshape", nullptr,
            [this, sel] {
                if (sel()) {
                    deformStack_.steps.erase(deformStack_.steps.begin() + deformStack_.selected);
                    deformStack_.on.erase(deformStack_.on.begin() + deformStack_.selected);
                    deformStack_.selected = std::min(deformStack_.selected, int(deformStack_.steps.size()) - 1);
                    deformStackRefresh();
                }
            },
            nullptr, nullptr, nullptr, [sel] { return sel() != nullptr; }, "Take the selected step off the stack.");
        static const char* const kAxisIds[3] = {"deformation.stack_x", "deformation.stack_y", "deformation.stack_z"};
        static const char* const kAxisNames[3] = {"X", "Y", "Z"};
        for (int k = 0; k < 3; ++k)
            add(kAxisIds[k], kAxisNames[k], "Tool > Reshape", nullptr,
                [this, sel, k] {
                    DeformStep* st = sel();
                    if (!st) return;
                    const uint8_t bit = uint8_t(1 << k);
                    if ((st->options.axes & ~bit) == 0) return; // keep at least one axis
                    st->options.axes ^= bit;
                    deformStackRefresh();
                },
                nullptr, nullptr,
                [sel, k] {
                    const DeformStep* st = sel();
                    return st && (st->options.axes & (1 << k));
                },
                [sel] {
                    const DeformStep* st = sel();
                    return st && deformUsesAxes(st->op);
                },
                "An axis the selected step works along (several: one after the other, or together for Move, Scale, "
                "Round and Box).");
    }
    add("file.new_sphere", "New Sphere", "File", "Ctrl+N", [this] { confirmDiscard([this] { runCommand("new_sphere"); }); },
        ICON_LC_FILE_PLUS, "new_sphere");
    add("file.new_plane", "New Plane", "File", nullptr, [this] { confirmDiscard([this] { runCommand("new_plane"); }); }, nullptr,
        "new_plane");
    add("file.new_starter", "New from Starter Mesh...", "File", nullptr, [this] { showStarterMeshes(); }, nullptr, nullptr,
        nullptr, nullptr,
        "Start from a base mesh: human head, bust, full body, hand, foot, ear, eye, stylised and creature heads, "
        "torso, or a bevelled box, cylinder, capsule or ring (Library > Tool > Starter meshes).");
    add("file.open", "Open...", "File", "Ctrl+O", [this] { confirmDiscard([this] { openFileDialog(); }); }, ICON_LC_FOLDER_OPEN,
        nullptr, nullptr, nullptr, "Open a project (.sfproj), a model (.obj, .glb, .fbx, .stl, .ply, .usd ...) or splats.");
    add("file.save", "Save", "File", "Ctrl+S", [this] { saveProjectAs(false); }, ICON_LC_SAVE, nullptr, nullptr, nullptr,
        "Save the project (.sfproj: every Part, layer, brush and setting). Asks for a name the first time.");
    add("file.save_as", "Save As...", "File", "Ctrl+Shift+S", [this] { saveProjectAs(true); }, nullptr, nullptr, nullptr, nullptr,
        "Save the project under a new name.");
    add("file.export_glb", "Export GLB...", "File > Export", "Ctrl+E", [this] { saveFileDialog(FileAction::ExportGlb); },
        ICON_LC_PACKAGE, nullptr, nullptr, nullptr,
        "Export the model as glTF binary, with sculpt layers as blend shapes (morph targets).");
    add("file.export_stl", "Export STL...", "File > Export", nullptr, [this] {
            static const SDL_DialogFileFilter filters[] = {{"STL", "stl"}, {"All files", "*"}};
            sf::dialogs::saveFile(onBridgeFileChosen, &bridgeDialogs_[int(BridgeFile::ExportStl)], window_, filters, 2, nullptr);
        },
        nullptr, nullptr, nullptr, nullptr, "Save the visible Parts as one binary STL, for 3D printing.");
    add("file.send_unreal", "Send to Unreal", "File > Export", "Ctrl+U", [this] { sendToUnreal(); }, ICON_LC_SEND, nullptr,
        nullptr, [this] { return !pendingUnreal_.valid(); },
        "Export a game-ready FBX and import it into the running Unreal Editor (Python remote execution).");
    // Blender bridge (PanelBlender.cpp, addons/blender).
    add("file.send_blender", "Send to Blender", "File > Blender", nullptr, [this] {
            runCommand("blender_send");
            blenderLastBeatNs_ = 0; // beat now: the add-on must see this SculptForge as running when Blender sends next
        },
        ICON_LC_SEND, "blender_send", nullptr, nullptr,
        "Send the visible Parts back to Blender (the SculptForge add-on): objects that came from Blender update "
        "in place, keeping their transform, materials and modifiers; new Parts become new objects.");
    add("file.receive_blender", "Receive from Blender", "File > Blender", nullptr, [this] { receiveFromBlender(); },
        nullptr, "blender_receive", nullptr, nullptr,
        "Load what Blender last sent with the add-on's Send to SculptForge. Each object updates its Part (undoable) "
        "or becomes a new one; this happens by itself while Receive automatically is on.");
    // Maya bridge (PanelMaya.cpp, integrations/maya, #42): off until an exchange folder is chosen.
    add("file.send_maya", "Send to Maya", "File > Maya", nullptr, [this] { sendToMaya(); }, ICON_LC_SEND, "maya_send",
        nullptr, nullptr,
        "Send the visible Parts to Maya (OBJ with quads and UVs) through the exchange folder you chose; Get from "
        "SculptForge on Maya's SculptForge shelf loads them, replacing the objects they came from by name.");
    add("file.receive_maya", "Get from Maya", "File > Maya", nullptr, [this] { receiveFromMaya(); }, nullptr,
        "maya_receive", nullptr, nullptr,
        "Load what Maya last sent with the shelf's Send to SculptForge. Each object replaces the Part with its name "
        "(undoable) or becomes a new one.");
    add("file.maya_folder", "Exchange folder...", "File > Maya", nullptr, [this] { chooseMayaFolder(); },
        ICON_LC_FOLDER_OPEN, nullptr, nullptr, nullptr,
        "Choose the folder SculptForge and Maya exchange meshes through (pick the same one in Maya's shelf script). "
        "SculptForge reads and writes only there: when you send or get, and when Maya sends while Get "
        "automatically is on.");
    add("brush.paste", "Paste brush", "Brush", "Ctrl+V", [this] { pasteBrush(); }, ICON_LC_CLIPBOARD_PASTE, nullptr, nullptr,
        nullptr, "Add the brush on the clipboard: a brush code someone shared, or the text of a .sfbrush file.");
    // Per-brush settings (#78, BrushSettingsUi.cpp).
    add("brush.save_settings", "Save Brush Settings", "Brush", nullptr, [this] { saveBrushSettings(); }, ICON_LC_SAVE,
        nullptr, nullptr, nullptr,
        "Every brush keeps its own Size, Intensity, Falloff, Colour intensity, stroke and alpha while you work. "
        "This saves the current brush's, so it starts with them every time SculptForge opens.");
    add("brush.reset_settings", "Reset Brush to Default", "Brush", nullptr, [this] { resetBrushSettings(); },
        ICON_LC_ROTATE_CCW, nullptr, nullptr, nullptr,
        "Put the current brush's Size, Intensity, Falloff, Colour intensity, stroke and alpha back to the values "
        "SculptForge ships with, and forget what Save Brush Settings stored for it.");
    // Pen pressure per brush (#122, BrushSettingsUi.cpp): segments of Brush, Brush Lab and Preferences > Tablet.
    for (const bool size : {true, false})
        for (const PenPressure p : {PenPressure::Global, PenPressure::On, PenPressure::Off}) {
            const std::string id = std::string("brush.pen_") + (size ? "size_" : "strength_") + penPressureId(p);
            const std::string switchName = std::string("Pressure > ") + (size ? "Size" : "Strength");
            const std::string description =
                p == PenPressure::Global
                    ? "This brush follows " + switchName + " (Preferences > Tablet, the shelf). Grab brushes (Move, KelvinGrab, "
                      "KelvinTwist, KelvinScale, the tendrils) start with Size off instead, since a pen's press is "
                      "nearly weightless."
                    : std::string("Pen pressure ") + (p == PenPressure::On ? "always scales" : "never scales") +
                          " this brush's " + (size ? "size" : "strength") + ", whatever " + switchName +
                          " says. Kept per brush; Save Brush Settings keeps it for next time.";
            add(id.c_str(), p == PenPressure::Global ? "Use global" : p == PenPressure::On ? "On" : "Off",
                size ? "Brush > Pen pressure > Size" : "Brush > Pen pressure > Strength", nullptr,
                [this, size, p] { setPenPressure(size, p); }, nullptr, nullptr,
                [this, size, p] { return (size ? penSize_ : penStrength_) == p; }, nullptr, description.c_str());
        }
    add("brush.share_size", "Share size and intensity across brushes", "Preferences > Input", nullptr,
        [this] { setShareBrushSize(!brushSettings_.shareSizeIntensity); }, nullptr, nullptr,
        [this] { return brushSettings_.shareSizeIntensity; }, nullptr,
        "On: Size and Intensity stay as they are when you pick another brush (as before 0.10.8); Falloff, "
        "Colour intensity, the stroke and the alpha still follow each brush. Off (the default): every brush keeps its own.");
    add("brush.size_down", "Smaller brush", "Draw", "[", [this] { radiusPx_ = drawSizeStep(radiusPx_, -1, false); }, nullptr,
        nullptr, nullptr, nullptr, "Make the brush 10% smaller (hold to repeat; Shift+[ steps one pixel).");
    add("brush.size_up", "Larger brush", "Draw", "]", [this] { radiusPx_ = drawSizeStep(radiusPx_, 1, false); }, nullptr,
        nullptr, nullptr, nullptr, "Make the brush 10% larger (hold to repeat; Shift+] steps one pixel).");
    add("transform.symmetry_x", "Symmetry X", "Transform", "X", [this] { symmetry_.x = !symmetry_.x; }, ICON_LC_FLIP_HORIZONTAL_2,
        nullptr, [this] { return symmetry_.x; }, nullptr, "Mirror strokes across the X axis (left / right).");
    add("view.wireframe", "Wireframe", "Render", "Shift+F", [this] { renderSettings_.wireframe = !renderSettings_.wireframe; },
        ICON_LC_GRID_3X3, nullptr, [this] { return renderSettings_.wireframe; }, nullptr,
        "Show the mesh's polygons (quads and triangles) over the surface.");
    add("view.frame", "Frame model", "Transform", "F", [this] { frameKeyPress(); }, ICON_LC_SCAN, nullptr, nullptr,
        nullptr, "Fit the whole model in the view. Classic sculpting keys, with several Parts: F frames the active Part, "
        "F twice quickly frames the whole scene.");
    add("view.frame_scene", "Frame scene", "Transform", "", [this] { frameView(sceneBounds()); }, ICON_LC_SCAN, nullptr, nullptr,
        nullptr, "Fit every visible Part in the view (bind a key to it: F frames the active Part on the Classic sculpting keys).");
    add("color.pick", "Pick colour", "Color", "C", [this] { pickColorUnderCursor(); }, ICON_LC_PIPETTE, nullptr, nullptr, nullptr,
        "Take the paint colour from the model under the cursor.");
    for (int i = 0; i < int(BrushType::Count); ++i) {
        const auto type = BrushType(i);
        std::string id = "brush.";
        for (const char* c = brushName(type); *c; ++c)
            if (*c != ' ') id += char(std::tolower(static_cast<unsigned char>(*c)));
        const std::string key = i < 9 ? std::to_string(i + 1) : i == 9 ? std::string("0") : std::string();
        add(id.c_str(), brushLabel(type), "Brush", key.c_str(),
            [this, type] {
                brush_ = type;
                doc_.activeBrush = -1;
                paintMode_ = false;
                guidesMode_ = false; // leaves the QRemesh Guides brush
                edgesMode_ = false;  // and Edges mode
                maskBrush_.on = false;
                leaveDrawModesStatus(); // and Draw Tube / Append > Insert
            },
            ICON_LC_BRUSH, nullptr, [this, type] { return brush_ == type && doc_.activeBrush < 0 && !paintMode_ && !guidesMode_; }, nullptr,
            (std::string("Sculpt with the ") + brushLabel(type) + " brush.").c_str());
    }

    // --- Help ---
    add("help.controls", "Controls", "Help", "F1", [this] { showHelp_ = !showHelp_; }, ICON_LC_KEYBOARD, nullptr,
        [this] { return showHelp_; }, nullptr, "Every shortcut and mouse gesture, generated from the commands themselves.");
    add("help.guide", "User Guide", "Help", "Shift+F1", [this] { guide_.toggle(); }, ICON_LC_BOOK_OPEN_TEXT, nullptr,
        [this] { return guide_.isOpen(); }, nullptr,
        "The SculptForge user guide: every tool explained, with search. Hold Ctrl over a control and press F1 to open "
        "the chapter about it.");
    add("help.first_sculpt", "Your first sculpt (10 min)", "Help", nullptr, [this] { guide_.open("first-sculpt.md"); },
        ICON_LC_GRADUATION_CAP, nullptr, nullptr, nullptr,
        "A step-by-step lesson for complete beginners: a cute owl head from the default sphere, painted and "
        "rendered, in about ten minutes.");
    add("help.quick_tour", "Quick tour", "Help", nullptr, [this] { showQuickTour(0); }, ICON_LC_COMPASS, nullptr, nullptr,
        nullptr, "Ten short slides for sculptors who know another app: where remeshing, dynamic topology, retopology, "
                 "levels, masking, cut, brushes and baking are, each with a Show me button.");
    add("help.whats_new", "What's New", "Help", nullptr, [this] { showWhatsNew(); }, ICON_LC_SPARKLES, nullptr,
        nullptr, nullptr, "What changed in this version and the ones before it (opens once by itself after an update).");
    add("help.check_updates", "Check for updates", "Help", nullptr, [this] { startUpdateCheck(true); }, ICON_LC_REFRESH_CW,
        nullptr, nullptr, [this] { return !updateJob_; },
        "Ask the SculptForge website whether a newer version is out. SculptForge also checks by itself once a day "
        "(Preferences > Updates).");
    add("help.about", "About SculptForge", "Help", nullptr, [this] { showAbout_ = true; }, ICON_LC_INFO, nullptr, nullptr, nullptr,
        "Version, licence and the open-source components SculptForge is built with.");
    add("help.support", "Support SculptForge...", "Help", nullptr, [this] { openSupportPage(); }, ICON_LC_HEART, nullptr,
        nullptr, nullptr,
        "SculptForge is free and stays free. A membership adds betas and early builds, Member Requests (one request "
        "a month, built first and credited to you), a vote on what we build next and your name in the credits: opens sculptforge.app/support.html in your browser. Nothing in the app changes either way.");
    add("help.discord", "Join the Discord...", "Help", nullptr, [this] { openDiscordPage(); }, ICON_LC_MESSAGE_CIRCLE,
        nullptr, nullptr, nullptr,
        "The official SculptForge community on Discord: questions, tips and work in progress. Opens the invite in "
        "your browser. Bugs still go to Report a problem (GitHub issues).");
    add("help.welcome", "Welcome", "Help", nullptr, [this] { showWelcome_ = true; }, ICON_LC_SPARKLES, nullptr, nullptr,
        nullptr, "The welcome note shown the first time SculptForge starts.");
    add("help.website", "Website & documentation", "Help", nullptr, [] { sf::dialogs::openUrl("https://sculptforge.app/"); },
        ICON_LC_BOOK_OPEN, nullptr, nullptr, nullptr, "Open the SculptForge website and user guide in your browser.");
    add("help.report", "Report a problem", "Help", nullptr, [] { sf::dialogs::openUrl("https://github.com/islandmed/SculptForge/issues"); },
        ICON_LC_CIRCLE_ALERT, nullptr, nullptr, nullptr, "Open the issue tracker in your browser.");
    add("help.log", "Open log folder", "Help", nullptr,
        [this] {
            const std::string url = crashlog::folderUrl();
            if (url.empty()) status_ = "There is no log file (the user data folder couldn't be written).";
            else if (!sf::dialogs::openUrl(url.c_str()))
                status_ = "The log is " + crashlog::path() + " (couldn't open its folder: " + SDL_GetError() + ")";
        },
        ICON_LC_FILE_TEXT, nullptr, nullptr, nullptr,
        "Open the folder with sculptforge.log: what SculptForge did and, after a crash, what crashed. "
        "sculptforge-previous.log beside it is the last session's. Attach both to a problem report.");
    add("help.perf_trace", "Record a performance trace", "Help", nullptr, [this] { togglePerfTrace(); }, ICON_LC_ACTIVITY,
        nullptr, [] { return perftrace::on(); }, nullptr,
        "While on, sculptforge-trace.log in the log folder records what makes SculptForge slow: every frame over 33 ms "
        "with where its time went, strokes, uploads to the GPU, remeshes and other background jobs, the mesh size and "
        "levels, and the GPU and driver. Turn it on, do what is slow, turn it off and attach the file to your report. "
        "A small file on this computer only; off by default and costs nothing when off.");
    add("help.feedback", "Fixes & suggestions", "Help", nullptr,
        [] { sf::dialogs::openUrl("https://github.com/islandmed/SculptForge/issues/new/choose"); }, ICON_LC_MESSAGE_SQUARE_PLUS,
        nullptr, nullptr, nullptr,
        "Submit a request for a fix or a suggestion: opens the bug report and feature request forms in your browser.");

    // --- Command palette and console ---
    add("ui.command_palette", "Command palette", "Help", "F3 / Ctrl+Shift+P", [this] { palette_.open(); }, ICON_LC_SEARCH, nullptr,
        nullptr, nullptr,
        "Search every command, brush and setting by name and run it. Commands that need parameters open in the console.");
    add("ui.console", "Console", "Script", "`", [this] { setConsoleOpen(!layout_.consoleOpen); }, ICON_LC_SQUARE_TERMINAL, nullptr,
        [this] { return layout_.consoleOpen; }, nullptr,
        "The script console: every command you run appears as a script line, and you can type commands yourself.");
    add("script.run_file", "Run script file...", "Script", nullptr,
        [this] {
            sf::dialogs::openFile(onUiFileChosen, &g_requests[kRunScript], window_, kScriptFilters, 2, nullptr, false);
        },
        ICON_LC_FILE_CODE, nullptr, nullptr, nullptr, "Run a text file of script statements, one per line (# starts a comment).");
    add("script.copy_session", "Copy session as script", "Script", nullptr,
        [this] {
            const std::string script = console_.sessionScript();
            SDL_SetClipboardText(script.c_str());
            status_ = script.empty() ? "No commands run yet" : "Session copied as a script - paste it into a file to run it again";
        },
        ICON_LC_COPY, nullptr, nullptr, nullptr,
        "Put every command run this session on the clipboard as a script, ready to replay (a macro recorder).");
    add("script.clear_console", "Clear console", "Script", nullptr, [this] { console_.clear(); }, ICON_LC_TRASH_2, nullptr, nullptr,
        nullptr, "Empty the console log (the session script is kept).");

    // --- Config (Preferences > Saving & Layouts) ---
    // Preferences save themselves (#117); the layout is kept by Save Layout. config.store (Ctrl+Shift+I before
    // 0.11.1) stays as an id, so keymaps and macros that name it still load: it saves the preferences at once.
    add("config.save_layout", "Save Layout", "Preferences > Saving & Layouts", nullptr, [this] { saveLayout(); }, ICON_LC_SAVE, nullptr,
        nullptr, nullptr,
        "Keep the current layout (shelves, trays, open palettes) for future sessions. Preferences and shortcuts save "
        "themselves.");
    add("config.store", "Save Preferences Now", "Preferences > Saving & Layouts", nullptr, [this] { savePrefsNow(); }, ICON_LC_SAVE,
        nullptr, nullptr, nullptr,
        "Write the preferences and shortcuts to disk now. Not needed: they save themselves a moment after every change.");
    add("config.restore_standard", "Reset Layout and Shortcuts", "Preferences > Saving & Layouts", nullptr, [this] { restoreStandardUi(); },
        ICON_LC_ROTATE_CCW, nullptr, nullptr, nullptr,
        "Go back to the built-in layout, shortcuts and radial menus. The shortcuts save themselves; Save Layout keeps "
        "the layout.");
    add("config.restore_custom", "Revert to Saved Layout", "Preferences > Saving & Layouts", nullptr, [this] { restoreCustomUi(); },
        ICON_LC_ARCHIVE_RESTORE, nullptr, nullptr, [this] { return fileExists(uiConfigPath()); },
        "Go back to the layout the last Save Layout kept.");
    add("config.load", "Import Interface...", "Preferences > Saving & Layouts", "Ctrl+L", [this] { loadUiDialog(); },
        ICON_LC_FOLDER_INPUT, nullptr, nullptr, nullptr,
        "Load a layout and shortcuts file someone shared (or you saved with Export Interface).");
    add("config.save", "Export Interface...", "Preferences > Saving & Layouts", nullptr, [this] { saveUiDialog(); },
        ICON_LC_FOLDER_OUTPUT, nullptr, nullptr, nullptr,
        "Save the layout, shortcuts and interface preferences to a file you can share.");
    add("config.customize", "Customize Shelves", "Preferences > Saving & Layouts", nullptr,
        [this] {
            layout_.customize = !layout_.customize;
            status_ = layout_.customize ? "Customize: Ctrl+Alt+drag to move or remove items" : "Customize off";
        },
        ICON_LC_LAYOUT_TEMPLATE, nullptr, [this] { return layout_.customize; }, nullptr,
        "Rearrange the shelves: Ctrl+Alt+drag any button onto a shelf to add or move it, or off the shelves to remove it.");

    // The shell's actions (Shell.cpp: brushes, shelves, palettes, trays): they replace any of the above with
    // the same id and own P (Paint), the symmetry toggles and Tab.
    registerShellActions();
    registerTextureUiActions(); // Tool > Texture Image, the Vertex / Texture paint target (PanelTexture.cpp)
    registerLineGradientActions(); // Color > Line Gradient (LineGradient.cpp)
    registerStencilActions();   // Alpha > Stencil (StencilOverlay.cpp)
    registerPbrUiActions();     // material channels, solo view, map export (PanelPbr.cpp)
    registerGizmoActions();     // Q W E R: Draw, and the Transform gizmo's Move, Scale, Rotate; Shift+W All (Gizmo.cpp)
    registerBakeUiActions();    // Tool > Bake (PanelBake.cpp)
    registerUvEditorActions();  // UV Editor (PanelUvEditor.cpp)
    registerGarmentActions();   // Tool > Garments, the Pattern window (PanelGarments.cpp)
    registerRetopoUiActions(); // Geometry > QRemesh, Part > ProjectAll (PanelRetopo.cpp)
    registerEdgeUiActions();   // Geometry > Edges (PanelEdges.cpp)
    registerForgeSphereUiActions(); // Tool > Forge Spheres (PanelForge Spheres.cpp)
    registerTubeUiActions();        // Tool > Tube (PanelTube.cpp)
    registerSplatTrainUi();    // Splats > Train from capture (PanelSplatTrain.cpp)
    registerSculptableUiActions(); // Splats > Make sculptable (PanelSculptable.cpp)
    registerVdmActions();      // Brush > Vector Displacement (PanelVdm.cpp)
    registerKelvinletActions(); // Brush > Kelvinlet & Capture: Capture from view (PanelKelvinlet.cpp)
    registerFaceUiActions();   // Tool > Facial Shapes, face_take_play / face_take_stop (PanelFace.cpp)
    registerPerformanceUiActions(); // Preferences > Performance (PanelPerformance.cpp)
    registerInputActions();     // the Classic sculpting keys: U I O, Space, L, V, Ctrl+A / I / H / W..., the input style (Input.cpp)
    registerTimelineUiActions(); // Sculpt Timeline, timelapse, Help > Benchmark (PanelTimeline.cpp)
    registerPanelActions();      // panels in windows of their own, Reset panel windows (PanelWindows.cpp)

    registerBrushSets(); // brush set commands and actions; the active set's shelf back (PanelBrushSets.cpp)
    // Every command, so the command palette finds anything scriptable.
    actions_.registerCommands(
        commands_, [this](const std::string& name) { runCommand(name); },
        [this](const std::string& name) { consolePrefill(name + " "); });
    registerPackActions();   // Install Pack, New Pack, Record Macro, the editor, one action per macro (PanelPacks.cpp)
    registerRadialActions(); // last: a radial menu's items may be any of the above (RadialMenu.cpp)

    if (!uiConfigDeterministic_ && !configDir_.empty()) {
        console_.loadHistory(configDir_ + "console_history.txt");
        std::ifstream f(fsPath(configDir_ + "recent_actions.txt"));
        std::vector<std::string> recent;
        for (std::string line; std::getline(f, line);)
            if (!line.empty()) recent.push_back(line);
        actions_.setRecent(std::move(recent));
    }
    layoutBaseline_ = layoutKey();
    actionsReady_ = true;
}

void App::saveRecentActions() {
    if (uiConfigDeterministic_ || configDir_.empty()) return;
    std::ofstream f(fsPath(configDir_ + "recent_actions.txt"), std::ios::binary | std::ios::trunc);
    for (const auto& id : actions_.recent()) f << id << "\n";
}

// ---------------------------------------------------------------------------
// Store / restore
// ---------------------------------------------------------------------------


namespace {
std::string windowStatePath() {
    const std::string dir = userDataDir();
    return dir.empty() ? std::string() : dir + "window.json";
}
} // namespace

App::WindowState App::loadWindowState() {
    WindowState st;
    const std::string path = windowStatePath();
    std::ifstream f(path);
    if (path.empty() || !f) return st;
    Json j;
    std::string err;
    if (!Json::parse(std::string(std::istreambuf_iterator<char>(f), {}), j, err)) return st;
    st.stored = true;
    st.scale = std::clamp(float(j["scale"].asNumber(0.0)), 0.0f, 4.0f);
    st.w = std::clamp(int(j["w"].asNumber(st.w)), 640, 16384);
    st.h = std::clamp(int(j["h"].asNumber(st.h)), 400, 16384);
    st.maximized = j["maximized"].asBool(false);
    st.fullscreen = j["fullscreen"].asBool(false);
    if (j["x"].isNumber() && j["y"].isNumber()) {
        const int x = int(j["x"].asNumber()), y = int(j["y"].asNumber());
        // Only where a display still shows at least a corner of the title bar (a monitor may have gone).
        int count = 0;
        if (SDL_DisplayID* displays = SDL_GetDisplays(&count)) {
            for (int i = 0; i < count && !st.placed; ++i) {
                SDL_Rect r;
                if (SDL_GetDisplayUsableBounds(displays[i], &r) && x + 100 > r.x && x < r.x + r.w - 100 && y >= r.y - 10 &&
                    y < r.y + r.h - 50) {
                    st.x = x;
                    st.y = y;
                    st.placed = true;
                }
            }
            SDL_free(displays);
        }
    }
    return st;
}

void App::saveWindowState() const {
    if (!rememberWindow_ || !window_) return;
    const std::string path = windowStatePath();
    if (path.empty()) return;
    const SDL_WindowFlags flags = SDL_GetWindowFlags(window_);
    const bool maximized = (flags & SDL_WINDOW_MAXIMIZED) != 0, fullscreen = (flags & SDL_WINDOW_FULLSCREEN) != 0;
    WindowState st = loadWindowState(); // keep the normal size while maximised or full screen
    if (!maximized && !fullscreen && !(flags & SDL_WINDOW_MINIMIZED)) {
        SDL_GetWindowSize(window_, &st.w, &st.h);
        SDL_GetWindowPosition(window_, &st.x, &st.y);
        st.placed = true;
        st.scale = automaticUiScale(SDL_GetWindowDisplayScale(window_), SDL_GetWindowPixelDensity(window_));
    }
    Json j;
    j.set("w", double(st.w));
    j.set("h", double(st.h));
    if (st.scale > 0.0f) j.set("scale", double(st.scale));
    if (st.placed) {
        j.set("x", double(st.x));
        j.set("y", double(st.y));
    }
    j.set("maximized", maximized);
    j.set("fullscreen", fullscreen);
    std::string err; // (temp file, then rename: a quit that ends mid-write leaves the last one whole)
    if (!writeJsonAtomic(path, j, err)) SDL_Log("%s", err.c_str());
}

std::string App::uiConfigPath() const { return configDir_.empty() ? std::string() : configDir_ + "ui.json"; }

bool App::writeUiConfig(const std::string& path, std::string& err) {
    Json j = Json::object();
    j.set("version", 1.0);
    j.set("layout", toJson(layout_));
    j.set("shortcuts", actions_.overridesToJson());
    j.set("prefs", toJson(uiPrefs_));
    j.set("radialMenus", toJson(radialMenus_));
    return writeJsonAtomic(path, j, err);
}

// Preferences save themselves (#117): ui.json gets the shortcuts, the preferences and the radial menus a moment
// after they change, with the layout as Save Layout last kept it (storedLayout_; none yet: the standard one).
bool App::writeJsonAtomic(const std::string& path, const Json& j, std::string& err) {
    std::error_code ec;
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(fsPath(tmp), std::ios::binary | std::ios::trunc);
        f << j.dump(true) << "\n";
        if (!f) {
            err = "Could not write " + path;
            return false;
        }
    }
    std::filesystem::rename(fsPath(tmp), fsPath(path), ec);
    if (ec) {
        err = "Could not write " + path + ": " + ec.message();
        return false;
    }
    return true;
}

std::string App::prefsKey() const {
    Json j = Json::object();
    j.set("shortcuts", actions_.overridesToJson());
    j.set("prefs", toJson(uiPrefs_));
    j.set("radialMenus", toJson(radialMenus_));
    return j.dump();
}

bool App::saveUiPrefs(std::string& err) {
    Json j = Json::object();
    j.set("version", 1.0);
    if (storedLayout_.isObject()) j.set("layout", storedLayout_);
    j.set("shortcuts", actions_.overridesToJson());
    j.set("prefs", toJson(uiPrefs_));
    j.set("radialMenus", toJson(radialMenus_));
    if (!writeJsonAtomic(uiConfigPath(), j, err)) return false;
    prefsSavedKey_ = prefsKey();
    prefsSaveAt_ = 0.0;
    return true;
}

void App::autosavePrefs(bool flush) {
    // Renders and recordings use the standard layout and the user's own settings folder: they never write it. UI
    // tests do (their folder is a fresh one).
    if (uiConfigPath().empty() || !actionsReady_ || (uiConfigDeterministic_ && options_.uiTest.empty())) return;
    const double now = double(SDL_GetTicksNS()) * 1e-9;
    if (!flush && now - prefsCheckedAt_ < 0.25) return; // a few looks a second while the app draws
    prefsCheckedAt_ = now;
    std::string key = prefsKey();
    if (!prefsKeyKnown_) { // the first look: what the start read (or the defaults) is already on disk
        prefsKeyKnown_ = true;
        prefsSavedKey_ = languageUnsaved_ ? std::string() : std::move(key); // (the first start's language still has to be written)
        languageUnsaved_ = false;
        return;
    }
    if (key == prefsSavedKey_) {
        prefsSaveAt_ = 0.0;
        return;
    }
    if (prefsSaveAt_ == 0.0) { // debounced: a slider being dragged is written once it settles (or every half second)
        prefsSaveAt_ = now + 0.5;
        redrawIn(0.5); // (a frame then, even if the app is idle by then)
    }
    if (!flush && now < prefsSaveAt_) return;
    std::string err;
    if (!saveUiPrefs(err)) {
        SDL_Log("%s", err.c_str());
        prefsSavedKey_ = std::move(key); // (no retry every frame; the next change tries again)
        prefsSaveAt_ = 0.0;
    }
}

void App::restartPrefsForTest(const std::string& uiJson) {
    autosavePrefs(true);
    if (!uiJson.empty() && !uiConfigPath().empty()) {
        std::error_code ec;
        std::filesystem::copy_file(fsPath(uiJson), fsPath(uiConfigPath()), std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) status_ = "restart: " + ec.message();
    }
    UiPrefs defaults;
    defaults.mayaFolder = uiPrefs_.mayaFolder; // (maya.json, not ui.json)
    defaults.mayaAutoReceive = uiPrefs_.mayaAutoReceive;
    defaults.mayaLive = uiPrefs_.mayaLive;
    uiPrefs_ = defaults;
    actions_.clearOverrides();
    resetRadialMenus();
    const bool customize = layout_.customize;
    layout_ = standardLayout(uiPrefs_.shellLayout);
    layout_.customize = customize;
    storedLayout_ = Json();
    std::string err;
    if (fileExists(uiConfigPath()) && !readUiConfig(uiConfigPath(), err, true)) status_ = err;
    applyInputStyle();
    registerLibraryActions(); // as at a start: registered after the style is applied (#147)
    restoreBrushSet();
    layoutBaseline_ = layoutKey();
    prefsSavedKey_ = prefsKey();
    prefsSaveAt_ = 0.0;
    ImGui::ClearActiveID(); // a new start has no text field in use (the console's would take the next keys)
    ImGui::SetWindowFocus(nullptr);
}

void App::savePrefsNow() {
    std::string err;
    if (uiConfigPath().empty()) status_ = "Can't save the preferences: no settings folder";
    else if (!saveUiPrefs(err)) status_ = err;
    else status_ = "Preferences saved (they also save themselves)";
}

bool App::readUiConfig(const std::string& path, std::string& err, bool own) {
    std::ifstream f(fsPath(path), std::ios::binary);
    if (!f) {
        err = "Could not read " + path;
        return false;
    }
    Json j;
    if (!Json::parse(std::string(std::istreambuf_iterator<char>(f), {}), j, err)) {
        err = path + ": " + err;
        return false;
    }
    j = legacy::modernizeKeys(j); // keys an older version wrote
    // Our own ui.json may have no layout yet (the preferences save themselves; the layout waits for Save Layout).
    const bool hasLayout = j.isObject() && j["layout"].isObject();
    if (!j.isObject() || (!hasLayout && !(own && j["prefs"].isObject()))) {
        err = path + " is not a SculptForge UI configuration";
        return false;
    }
    // The prefs first: they name the shell layout, whose standard shelves fill in what the file didn't store.
    UiPrefs prefs = uiPrefs_;
    fromJson(j["prefs"], prefs);
    UiLayout layout = standardLayout(prefs.shellLayout);
    if (hasLayout && !fromJson(j["layout"], layout, err, prefs.shellLayout)) return false;
    if (own) storedLayout_ = hasLayout ? j["layout"] : Json();
    uiPrefs_ = std::move(prefs);
    lookDirty_ = true;
    requestUiScale(); // its UI scale
    const bool customize = layout_.customize;
    layout_ = std::move(layout);
    layout_.customize = customize;
    actions_.overridesFromJson(j["shortcuts"]);
    if (j.has("radialMenus")) applyRadialMenusJson(j["radialMenus"]); // (an older file keeps the menus there are)
    setLanguage(languageFromCode(uiPrefs_.language)); // (the requestUiScale above adds the Chinese glyphs if needed)
    applyPerformancePrefs();
    applyInputStyle();
    doc_.sculptor.dyntopo.constantDetail = uiPrefs_.dyntopoConstantDetail;
    doc_.sculptor.dyntopo.smoothRemesh = uiPrefs_.dyntopoSmoothRemesh;
    renderSettings_.flatShading = uiPrefs_.flatShading;
    renderSettings_.backfaceCulling = uiPrefs_.backfaceCulling;
    renderSettings_.floorGrid = uiPrefs_.floorGrid;
    uvPackColumns_ = uiPrefs_.uvPackColumns, uvPackRows_ = uiPrefs_.uvPackRows;
    renderSettings_.maskDarkness = uiPrefs_.maskDarkness;
    renderSettings_.gradientBackground = uiPrefs_.gradientBackground;
    renderSettings_.backgroundColor = uiPrefs_.backgroundColor;
    return true;
}

void App::saveLayout() {
    std::string err;
    if (uiConfigPath().empty()) {
        status_ = "Can't save the layout: no settings folder";
    } else if (!writeUiConfig(uiConfigPath(), err)) {
        status_ = err;
    } else {
        status_ = "Layout saved";
        noteLayoutStored();
        prefsSavedKey_ = prefsKey();
        prefsSaveAt_ = 0.0;
    }
}

void App::noteLayoutStored() {
    storedLayout_ = toJson(layout_);
    layoutBaseline_ = layoutKey();
}

void App::restoreStandardUi() {
    const bool customize = layout_.customize;
    layout_ = standardLayout(uiPrefs_.shellLayout);
    layout_.customize = customize;
    actions_.clearOverrides();
    resetRadialMenus();
    resetPanels(); // every panel back in the main window (PanelWindows.cpp)
    layoutBaseline_ = layoutKey();
    status_ = "Standard layout and shortcuts restored - Save Layout keeps the layout";
}

void App::restoreCustomUi() {
    std::string err;
    if (!fileExists(uiConfigPath())) {
        status_ = "No saved layout yet - Save Layout first";
    } else if (!readUiConfig(uiConfigPath(), err, true)) {
        status_ = err;
    } else {
        layoutBaseline_ = layoutKey();
        status_ = "Saved layout restored";
    }
}

void App::saveUiDialog() {
    sf::dialogs::saveFile(onUiFileChosen, &g_requests[kSaveUi], window_, kUiFilters, 1, nullptr);
}

void App::importBlenderKeymapDialog() {
    sf::dialogs::openFile(onUiFileChosen, &g_requests[kBlenderKeymap], window_, kBlenderKeymapFilters, 2, nullptr, false);
}

void App::importRadialMenuDialog() {
    sf::dialogs::openFile(onUiFileChosen, &g_requests[kRadialImport], window_, kRadialFilters, 1, nullptr, false);
}

void App::exportRadialMenuDialog(int menu) {
    radialEdit_.exportMenu = menu;
    sf::dialogs::saveFile(onUiFileChosen, &g_requests[kRadialExport], window_, kRadialFilters, 1, nullptr);
}

void App::chooseMayaFolder() { // the only way the Maya bridge gets a folder: the user picks it (PanelMaya.cpp)
    const std::string start = uiPrefs_.mayaFolder;
    sf::dialogs::openFolder(onUiFileChosen, &g_requests[kMayaFolder], window_, start.empty() ? nullptr : start.c_str(),
                             false);
}

void App::loadUiDialog() {
    sf::dialogs::openFile(onUiFileChosen, &g_requests[kLoadUi], window_, kUiFilters, 1, nullptr, false);
}

void App::queueUiFile(int kind, std::string path) {
    std::lock_guard<std::mutex> lock(uiFileMutex_);
    uiFiles_.emplace_back(kind, std::move(path));
}

std::string App::layoutKey() const {
    UiLayout l = layout_; // what a session changes on its own doesn't count as a layout edit
    l.consoleOpen = false;
    l.consoleHeight = 0;
    l.libraryOpen = false;
    l.libraryTab.clear();
    l.customize = false;
    return toJson(l).dump();
}

// ---------------------------------------------------------------------------
// Viewport hints (U11): every note shown over the 3D view goes through here, with its Don't show again
// ---------------------------------------------------------------------------

namespace {
struct HintInfo {
    const char* id;
    const char* name; // as Preferences > Hints lists it
};
constexpr HintInfo kHints[] = {
    {"air_strokes", "Strokes start off the model"},
    {"lod_preview", "Previewing an LOD"},
    {"face_landmarks", "Face landmark steps"},
};
} // namespace

bool App::hintShown(const char* id) const {
    return uiPrefs_.showHints && !uiPrefs_.hiddenHints.count(id);
}

void App::drawViewportHint(const char* id, const char* text, ImU32 colour, float top) {
    if (!hintShown(id)) return;
    const CanvasRect c = canvasRect();
    const float s = uiScale_;
    ImGui::SetNextWindowPos(ImVec2(c.x + c.w * 0.5f, top), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.72f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * s, 4.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, theme::rounding(4.0f * s));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
    const std::string window = std::string("##hint.") + id;
    if (ImGui::Begin(window.c_str(), nullptr, flags)) {
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colour));
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, 12.0f * s);
        const std::string button = std::string(tr("Don't show again")) + "##hint." + id;
        if (ImGui::SmallButton(button.c_str())) {
            uiPrefs_.hiddenHints.insert(id);
            status_ = "Hint hidden (Preferences > Hints shows it again)";
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}

void App::drawHintsSection() {
    ui::Checkbox("Show hints in the viewport##hints.show", &uiPrefs_.showHints);
    ui::HelpText("Show hints in the viewport", nullptr,
                 "The short notes SculptForge shows over the 3D view (a mode that is on, the next step of a tool). Off "
                 "hides them all; each also has its own Don't show again.",
                 "Preferences > Hints");
    ImGui::BeginDisabled(!uiPrefs_.showHints);
    for (const HintInfo& h : kHints) {
        bool on = !uiPrefs_.hiddenHints.count(h.id);
        const std::string label = std::string(tr(h.name)) + "##hints." + h.id;
        if (ui::Checkbox(label.c_str(), &on)) {
            if (on) uiPrefs_.hiddenHints.erase(h.id);
            else uiPrefs_.hiddenHints.insert(h.id);
        }
    }
    ImGui::EndDisabled();
    if (ImGui::Button(tr("Show all hints again##hints.reset"), ImVec2(-FLT_MIN, 0))) {
        uiPrefs_.showHints = true;
        uiPrefs_.hiddenHints.clear();
    }
}

void App::watchLayoutChanges() {
    syncShelfBrushActions();
    autosavePrefs();
    if (layoutHintShown_ || uiConfigDeterministic_ || ++layoutCheckFrame_ % 20 != 0) return;
    if (layoutKey() == layoutBaseline_) return;
    layoutHintShown_ = true; // once per session (never saves the layout by itself)
    status_ = "Layout changed - Preferences > Saving & Layouts > Save Layout keeps it";
}

// ---------------------------------------------------------------------------
// Console and command palette glue
// ---------------------------------------------------------------------------

void App::setConsoleOpen(bool open) {
    layout_.consoleOpen = open;
    if (open) console_.focusInput();
}

void App::consolePrefill(const std::string& text) {
    layout_.consoleOpen = true;
    console_.prefill(text);
}

void App::consoleEcho(const std::string& name, const Args& args, const CommandResult& r) {
    console_.echo(formatStatement(name, args), r, commandSource_);
    console_.seenStatus(status_);
}

CanvasRect App::paletteArea() const { return canvasRect(); } // top centre of the canvas

CanvasRect App::consoleRect() const {
    // The shell reserves this strip between the canvas and the bottom shelf (ShellGeometry::console);
    // with the UI hidden (demos), the bottom of the window.
    if (!hideUi_) return shellGeometry().console;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float h = std::min(std::clamp(layout_.consoleHeight, 100.0f, 400.0f) * uiScale_, vp->WorkSize.y * 0.6f);
    return {vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - h, vp->WorkSize.x, h};
}

// ---------------------------------------------------------------------------
// Shortcut assignment (Ctrl+Alt+click a button, or Preferences > Hotkeys)
// ---------------------------------------------------------------------------

void App::beginShortcutCapture(const std::string& actionId) {
    const Action* a = actions_.find(actionId);
    if (!a) return;
    capturingShortcut_ = actionId;
    conflict_ = {};
    shortcutAnchor_ = {}; // the button (or tile) clicked just now, so the Replace / Cancel popup can stay clear of it
    if (ImGui::GetCurrentContext() && ImGui::GetCurrentWindowRead()) {
        const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
        if (hi.x > lo.x && hi.y > lo.y) shortcutAnchor_ = ImVec4(lo.x, lo.y, hi.x, hi.y);
    }
    status_ = "Press a key for '" + a->label + "' (Esc cancels, Backspace clears)";
}

void App::resolveShortcutConflict(bool replace) {
    const ShortcutConflict c = conflict_;
    conflict_ = {};
    const Action* a = actions_.find(c.actionId);
    const std::string label = a ? a->label : c.actionId;
    if (!replace) {
        status_ = "Shortcut unchanged";
        return;
    }
    actions_.assignShortcut(c.actionId, c.chord);
    status_ = label + ": " + c.chord + " (was " + c.holders + ")";
}

bool App::captureShortcutKey(SDL_Keycode key, SDL_Keymod mod) {
    if (shortcutConflictOpen()) { // Enter replaces, Esc cancels; the popup's buttons do the same
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) resolveShortcutConflict(true);
        else if (key == SDLK_ESCAPE) resolveShortcutConflict(false);
        return true;
    }
    if (capturingShortcut_.empty()) return false;
    if (isModifierKey(key)) return true;
    const Action* a = actions_.find(capturingShortcut_);
    const std::string label = a ? a->label : capturingShortcut_;
    if (key == SDLK_ESCAPE) {
        status_ = "Shortcut unchanged";
    } else if (key == SDLK_BACKSPACE || key == SDLK_DELETE) {
        actions_.setShortcut(capturingShortcut_, "");
        status_ = label + ": no shortcut";
    } else {
        const Chord pressed = chordFromEvent(key, mod);
        const std::string chord = chordText(pressed);
        std::string holders;
        for (const Action* h : actions_.holdersOf(pressed, capturingShortcut_)) holders += (holders.empty() ? "" : ", ") + h->label;
        if (!holders.empty()) { // taken: ask first (Artizmo on Discord)
            conflict_ = {capturingShortcut_, chord, holders};
            capturingShortcut_.clear();
            return true;
        }
        actions_.assignShortcut(capturingShortcut_, chord);
        status_ = label + ": " + chord;
    }
    capturingShortcut_.clear();
    return true;
}

// ---------------------------------------------------------------------------
// Per-frame UI
// ---------------------------------------------------------------------------

// The key is used already: say by what, Replace or Cancel. Beside the button that was clicked (below it, or above
// when there is no room), never over it, so the key and the action stay in view. It is an ImGui popup drawn from
// where the button is (Preferences > Hotkeys, so inside the Preferences menu's popup stack): Esc, a click elsewhere
// or Cancel close this box and nothing else, and the Preferences menu stays open behind it. Anywhere else (a shelf
// tile's Ctrl+Alt+click) drawHelpConsoleUi draws it at the end of the frame.
void App::drawShortcutConflict() {
    shortcutPopupRect_ = {};
    const char* const id = "##shortcut_conflict";
    conflictDrawnFrame_ = ImGui::GetFrameCount();
    if (shortcutConflictOpen() && !conflictPopupOpened_) {
        ImGui::OpenPopup(id);
        conflictPopupOpened_ = true;
        conflictOpenedFrame_ = ImGui::GetFrameCount();
    }
    if (!conflictPopupOpened_) return;
    const float s = uiScale_;
    const ImVec2 view = ImGui::GetMainViewport()->Size;
    const ImVec4 an = shortcutAnchor_;
    const bool known = an.z > an.x;
    const float w = 340.0f * s, gap = 6.0f * s;
    ImVec2 pos(view.x * 0.5f - w * 0.5f, 120.0f * s);
    ImVec2 pivot(0.0f, 0.0f);
    if (known) {
        const ImVec2 hp = ImGui::GetWindowPos(), hz = ImGui::GetWindowSize();
        const bool hosted = hz.x > 0.0f && an.x >= hp.x && an.z <= hp.x + hz.x; // drawn from inside the window the button is in
        if (hosted && hp.x + hz.x + gap + w <= view.x) { // beside the window, level with the button: the list stays in view
            pos = ImVec2(hp.x + hz.x + gap, an.y);
        } else if (hosted && hp.x - gap - w >= 0.0f) {
            pos = ImVec2(hp.x - gap - w, an.y);
        } else {
            pos.x = std::clamp(an.x, 8.0f * s, std::max(8.0f * s, view.x - w - 8.0f * s));
            const float below = an.w + gap;
            if (below + 130.0f * s < view.y) pos.y = below;
            else pos.y = an.y - gap, pivot.y = 1.0f;
        }
    }
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always, pivot);
    ImGui::SetNextWindowSize(ImVec2(w, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f * s, 12.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f * s);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, col(IM_COL32(0x18, 0x18, 0x1a, 0xff)));
    ImGui::PushStyleColor(ImGuiCol_Border, col(IM_COL32(0xf0, 0x88, 0x2e, 0xa0)));
    const bool shown = ImGui::BeginPopup(id, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
    if (shown) {
        if (!shortcutConflictOpen()) { // answered (Enter or Esc went through captureShortcutKey): close just this box
            ImGui::CloseCurrentPopup();
        } else {
            const Action* a = actions_.find(conflict_.actionId);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w - 28.0f * s);
            ImGui::Text(tr("%s is already used by %s."), conflict_.chord.c_str(), conflict_.holders.c_str());
            ImGui::TextColored(col(ui::color::textDim), tr("Replace gives it to '%s' and takes it from there."),
                               a ? trAction(a->id, a->label) : conflict_.actionId.c_str());
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
            if (ImGui::Button((std::string(tr("Replace")) + "##shortcut_replace").c_str())) {
                resolveShortcutConflict(true);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button((std::string(tr("Cancel")) + "##shortcut_cancel").c_str())) {
                resolveShortcutConflict(false);
                ImGui::CloseCurrentPopup();
            }
            const ImVec2 p = ImGui::GetWindowPos(), z = ImGui::GetWindowSize();
            shortcutPopupRect_ = ImVec4(p.x, p.y, p.x + z.x, p.y + z.y);
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
    if (!shown) { // closed by ImGui (a click elsewhere): that is Cancel; the popup gets a frame to open first
        if (shortcutConflictOpen() && ImGui::GetFrameCount() > conflictOpenedFrame_ + 1) resolveShortcutConflict(false);
        if (!shortcutConflictOpen() && ImGui::GetFrameCount() > conflictOpenedFrame_ + 1) conflictPopupOpened_ = false;
    }
}

void App::drawHelpConsoleUi() {
    {
        std::vector<std::pair<int, std::string>> files;
        {
            std::lock_guard<std::mutex> lock(uiFileMutex_);
            files.swap(uiFiles_);
        }
        for (auto& [kind, path] : files) {
            std::string err;
            if (kind == kSaveUi) {
                if (fsPath(path).extension().empty()) path += ".json";
                status_ = writeUiConfig(path, err) ? "Interface exported to " + path : err;
            } else if (kind == kLoadUi) {
                if (readUiConfig(path, err)) {
                    status_ = "Interface loaded - Save Layout keeps its layout";
                } else {
                    status_ = err;
                }
            } else if (kind == kBlenderKeymap) {
                importBlenderKeymap(path);
            } else if (kind == kRadialImport) {
                importRadialMenu(path);
            } else if (kind == kRadialExport) {
                if (fsPath(path).extension().empty()) path += ".json";
                exportRadialMenu(radialEdit_.exportMenu, path);
            } else if (kind == kMayaFolder) {
                setMayaFolder(path);
            } else if (kind == kRunScript) {
                setConsoleOpen(true);
                console_.queueScriptFile(path);
            } else if (kind == kLibraryFolder) {
                uiPrefs_.libraryFolder = path;
                std::string libErr;
                if (ensureUserLibrary(path, libErr)) { // the Library's User folder, from now on
                    library_.user = path;
                    libraryScanner_.invalidate();
                    status_ = "Library folder: " + path;
                } else {
                    status_ = libErr;
                }
            }
        }
    }
    console_.update(*this);
    console_.watchStatus(status_);
    if (layout_.consoleOpen) console_.draw(*this, consoleRect());
    palette_.draw(*this, paletteArea());
    if (showHelp_ && !panelDetached("controls")) drawControlsWindow();
    if (!capturingShortcut_.empty()) {
        const Action* a = actions_.find(capturingShortcut_);
        const CanvasRect area = paletteArea();
        const float s = uiScale_;
        ImGui::SetNextWindowPos(ImVec2(area.x + area.w * 0.5f, area.y + 60.0f * s), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f * s, 12.0f * s));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f * s);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, col(IM_COL32(0x18, 0x18, 0x1a, 0xff)));
        ImGui::PushStyleColor(ImGuiCol_Border, col(IM_COL32(0xf0, 0x88, 0x2e, 0xa0)));
        if (ImGui::Begin("##shortcut_capture", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs)) {
            ImGui::PushFont(helpTitleFont());
            ImGui::Text(tr(ICON_LC_KEYBOARD "  Press a key for '%s'"),
                        a ? a->label.c_str() : capturingShortcut_.c_str());
            ImGui::PopFont();
            const std::string now = actions_.shortcutText(capturingShortcut_);
            ImGui::TextColored(col(ui::color::textDim), "%s", tr("Now:"));
            ImGui::SameLine();
            if (now.empty())
                ImGui::TextColored(col(ui::color::textDim), "%s", tr("none"));
            else ui::Keycaps(now);
            ImGui::TextColored(col(ui::color::textDim), "%s",
                               tr("Esc cancels, Backspace clears. A key that's taken asks first."));
        }
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
    }
    if (conflictDrawnFrame_ != ImGui::GetFrameCount()) drawShortcutConflict(); // (Hotkeys draws it where its button is)
    watchLayoutChanges();
    ui::DragScrollPanels(); // #100: pen and middle drags scroll panels (after every window)
    ui::DrawHover(*this); // the hover and keyboard-focus rings, over every window
    ui::DrawHelp(*this); // last: on top of everything
}

namespace {
char g_controlsFilter[96] = ""; // the Controls window's filter field
char g_hotkeysFilter[64] = "";  // Preferences > Hotkeys' filter field
} // namespace

void resetConfigGlobals() {
    g_controlsFilter[0] = g_hotkeysFilter[0] = 0;
    for (auto& r : g_requests) r = {};
}

void App::drawControlsWindow() {
    const float s = uiScale_;
    ImGui::SetNextWindowSize(ImVec2(560.0f * s, 640.0f * s), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    fillPanelHost(flags); // in a window of its own: fills it (PanelWindows.cpp)
    if (!ImGui::Begin(tr("Controls"), &showHelp_, flags)) {
        ImGui::End();
        return;
    }
    // The key style (Preferences > Input), and a keymap exported from Blender (#12).
    ImGui::TextDisabled("%s", tr("Style"));
    ImGui::SameLine();
    drawInputStyleRow(ImGui::GetContentRegionAvail().x * 0.6f);
    ImGui::SameLine();
    ui::ActionButton(*this, "input.import_blender", ImVec2(-FLT_MIN, 0), ui::Look::IconLabel);
    char (&filter)[96] = g_controlsFilter;
    const bool canDetach = !panelHost_; // in the main window: a button that moves it to a window of its own
    const float detachW = ImGui::GetFrameHeight() * 1.2f;
    ImGui::SetNextItemWidth(canDetach ? -(detachW + ImGui::GetStyle().ItemSpacing.x) : -FLT_MIN);
    ImGui::InputTextWithHint("##controls_filter", tr(ICON_LC_SEARCH "  Filter shortcuts and gestures"), filter, sizeof filter);
    if (canDetach) {
        ImGui::SameLine();
        if (ImGui::Button(ICON_LC_SQUARE_ARROW_OUT_UP_RIGHT "##controls_detach", ImVec2(detachW, 0))) detachPanel("controls");
        ui::HelpText("Open in its own window", nullptr,
                     "Move Controls to a window of its own, to keep on another monitor. It opens there next time too.", "Help");
        ui::DelayedTooltip("Open in its own window");
    }
    const std::string f = lower(filter);
    auto matches = [&](const std::string& a, const std::string& b, const std::string& c) {
        return f.empty() || lower(a).find(f) != std::string::npos || lower(b).find(f) != std::string::npos ||
               lower(c).find(f) != std::string::npos;
    };
    ImGui::Spacing();
    if (ImGui::BeginTabBar("##controls_tabs")) {
        if (ImGui::BeginTabItem(tr("Keyboard"))) {
            ImGui::BeginChild("##controls_keys", ImVec2(0, -ImGui::GetTextLineHeightWithSpacing() * 1.6f));
            // Grouped by palette, alphabetically (as classic lists its palettes).
            std::map<std::string, std::vector<const Action*>> groups;
            for (const auto& a : actions_.all()) {
                const std::string chord = actions_.shortcutText(a.id);
                if (chord.empty() || !matches(a.label, a.palette, chord)) continue;
                const std::string top = a.palette.substr(0, a.palette.find(" > "));
                groups[top.empty() ? "General" : top].push_back(&a);
            }
            if (groups.empty()) ImGui::TextColored(col(ui::color::textDim), tr("No shortcut matches \"%s\"."), filter);
            for (auto& [group, list] : groups) {
                std::stable_sort(list.begin(), list.end(), [](const Action* x, const Action* y) { return x->label < y->label; });
                ImGui::SeparatorText(group.c_str());
                if (!ImGui::BeginTable(("##keys_" + group).c_str(), 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX)) continue;
                ImGui::TableSetupColumn("action", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("keys", ImGuiTableColumnFlags_WidthFixed, 190.0f * s);
                for (const Action* a : list) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    const ImVec2 p = ImGui::GetCursorScreenPos();
                    const float iconW = ImGui::GetFontSize() + 8.0f * s;
                    if (a->icon) ImGui::GetWindowDrawList()->AddText(p, ui::color::textDim, a->icon);
                    ImGui::SetCursorScreenPos(ImVec2(p.x + iconW, p.y));
                    ImGui::Selectable((a->label + "##ctl." + a->id).c_str(), capturingShortcut_ == a->id,
                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick);
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) beginShortcutCapture(a->id);
                    ui::HelpAction(*this, a->id.c_str());
                    ImGui::TableNextColumn();
                    ui::Keycaps(actions_.shortcutText(a->id));
                }
                ImGui::EndTable();
                ImGui::Spacing();
            }
            ImGui::EndChild();
            ImGui::TextColored(col(ui::color::textDim), "%s",
                               tr("Double-click a row, or Ctrl+Alt+click any button, to assign a key."));
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(tr("Mouse & pen"))) {
            if (ImGui::BeginTable("##gestures", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX)) {
                ImGui::TableSetupColumn("what", ImGuiTableColumnFlags_WidthFixed, 180.0f * s);
                ImGui::TableSetupColumn("how", ImGuiTableColumnFlags_WidthStretch);
                const bool blender = blenderStyle();
                for (const auto& g : blender ? std::span<const Gesture>(kBlenderGestures) : std::span<const Gesture>(kGestures)) {
                    if (!matches(g.what, g.how, "")) continue;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(g.what);
                    ImGui::TableNextColumn();
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextColored(col(IM_COL32(0xc8, 0xc8, 0xc8, 0xff)), "%s", g.how);
                    ImGui::PopTextWrapPos();
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Preferences sections (drawn inside the shell's Preferences palette)
// ---------------------------------------------------------------------------

namespace config {

void drawConfigSection(App& app) {
    const ImVec2 full(-FLT_MIN, 0);
    bool customize = app.layout().customize;
    if (ImGui::Checkbox(tr("Customize Shelves"), &customize)) app.actions().find("config.customize")->run();
    ui::HelpAction(app, "config.customize");
    ui::ActionButton(app, "config.save_layout", full, ui::Look::IconLabel);
    ui::ActionButton(app, "config.restore_custom", full, ui::Look::IconLabel);
    ui::ActionButton(app, "config.restore_standard", full, ui::Look::IconLabel);
    ui::ActionButton(app, "panel.reset", full, ui::Look::IconLabel); // panels in windows of their own (PanelWindows.cpp)
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    ui::ActionButton(app, "config.save", ImVec2(half, 0), ui::Look::IconLabel);
    ImGui::SameLine();
    ui::ActionButton(app, "config.load", ImVec2(half, 0), ui::Look::IconLabel);
    ImGui::PushStyleColor(ImGuiCol_Text, col(ui::color::textDim));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(tr("Preferences and shortcuts save themselves. Layout changes last until you quit; Save Layout keeps them."));
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

void drawInterfaceSection(App& app) {
    UiPrefs& p = app.uiPrefs();
    app.drawLookSection();
    // Language (#44): each name in its own language, never translated, so it can be found in either.
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("Language"));
    for (int i = 0; i < kLanguageCount; ++i) {
        const Language lang = Language(i);
        ImGui::SameLine();
        // Four names don't fit a narrow tray in one row: a button that would run past the edge starts the next one.
        const float width = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x +
                            ImGui::CalcTextSize(languageName(lang)).x;
        if (ImGui::GetContentRegionAvail().x < width) ImGui::NewLine();
        const std::string label = std::string(languageName(lang)) + "##language." + languageCode(lang);
        if (ImGui::RadioButton(label.c_str(), languageFromCode(p.language) == lang)) {
            app.setInterfaceLanguage(lang);
        }
        ui::HelpText("Language", nullptr,
                     "The language of menus, palettes, shelves and messages. Brush and tool names (Quad Remesh, Forge Spheres, "
                     "Clay Block...) stay as they are.",
                     "Preferences > Interface");
    }
    // Units (#148): sizes show in this unit; OBJ and PLY files are read in the second (core/Units.h).
    auto unitRow = [&](const char* label, std::string& value, const char* id, const char* help) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr(label));
        for (const LengthUnit& u : lengthUnits()) {
            ImGui::SameLine();
            if (ImGui::RadioButton((std::string(u.id) + "##" + id + "." + u.id).c_str(), value == u.id)) value = u.id;
            ui::HelpText(label, nullptr, help, "Preferences > Interface");
        }
    };
    unitRow("Units", p.lengthUnit, "units",
            "The unit sizes show in: the brush size, the Part size in Tool and the export size in File. "
            "SculptForge works in metres; this only changes how sizes read.");
    unitRow("OBJ/PLY in", p.fileUnit, "file_units",
            "OBJ and PLY files have no units. Their numbers are read in this unit (mm for most 3D printing files, m for "
            "Blender and game tools), and exporting OBJ or PLY writes them back in the unit they were opened with, so "
            "a part keeps its size. STL is always millimetres.");
    ImGui::Checkbox(tr("Show startup splash"), &p.startupSplash);
    ui::HelpText("Show startup splash", nullptr,
                 "Show the SculptForge logo while a slow start runs. A quick start shows nothing. Applies at the next start.",
                 "Preferences > Interface");
    {
        // #173: Don't ask again in the pop-ups turns these off; this brings the asking back (or turns it off in one go).
        bool ask = p.confirmRemeshLevels || p.confirmSubdivide;
        if (ImGui::Checkbox(tr("Ask before dropping subdivision levels"), &ask)) p.confirmRemeshLevels = p.confirmSubdivide = ask;
        ui::HelpText("Ask before dropping subdivision levels", nullptr,
                     "Subdivide, Voxel and Quad Remesh, Decimate, Delete Masked or Hidden, Split, Mirror, Make Symmetric and "
                     "Merge by Distance remove the subdivision levels of a Part that has them. On: they ask first (Ctrl+Z "
                     "brings the levels back). Off: they go straight through.",
                     "Preferences > Interface");
    }
    ImGui::Checkbox(tr("Ctrl+hover help"), &p.popupInfo);
    ui::HelpText("Ctrl+hover help", nullptr,
                 "Hold Ctrl and point at any control to see what it does and its shortcut. Ctrl+Alt shows a command's "
                 "scripting parameters.",
                 "Preferences > Interface");
    ImGui::BeginDisabled(!p.popupInfo);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ui::SliderInside("##help_opacity", &p.helpOpacity, 0.3f, 1.0f, "Help box opacity %.2f");
    ui::HelpText("Help box opacity", nullptr, "How solid the Ctrl-help box is.",
                 "Preferences > Interface");
    ImGui::EndDisabled();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ui::SliderInside("##tooltip_delay", &p.tooltipDelay, 0.0f, 2.0f, "Tooltip delay %.1f s");
    ui::HelpText("Tooltip delay", nullptr,
                 "How long to point at an icon-only button (or a clipped label) before its name appears.",
                 "Preferences > Interface");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ui::SliderInside("##mask_darkness", &p.maskDarkness, 0.0f, 1.0f, "Mask darkness %.2f"))
        app.setMaskDarkness(p.maskDarkness);
    ui::HelpText("Mask darkness", nullptr,
                 "How dark masked areas show: 0 = not at all, 1 = nearly black. The default keeps the form readable "
                 "under the mask.",
                 "Preferences > Interface");
    ImGui::Checkbox(tr("Reduce motion"), &p.reduceMotion);
    ui::HelpText("Reduce motion", nullptr,
                 "Hover and focus highlights change colour only: no easing, and tiles don't lift or grow.",
                 "Preferences > Interface");
    ImGui::Checkbox(tr("One sub-palette open"), &p.oneSubpalette);
    ui::HelpText("One sub-palette open", nullptr,
                 "Opening a sub-palette closes the others in that palette. Shift+click opens more than one.",
                 "Preferences > Interface");
    ImGui::Checkbox(tr("Auto collapse left tray"), &p.autoCollapseLeft);
    ui::HelpText("Auto collapse left tray", nullptr,
                 "Opening a palette in the left tray collapses the others when they would not fit.", "Preferences > Interface");
    ImGui::Checkbox(tr("Auto collapse right tray"), &p.autoCollapseRight);
    ui::HelpText("Auto collapse right tray", nullptr,
                 "Opening a palette in the right tray collapses the others when they would not fit.", "Preferences > Interface");
    ImGui::Checkbox(tr("Palettes strictly A-Z"), &p.alphabeticalPalettes);
    ui::HelpText("Palettes strictly A-Z", nullptr,
                 "Off: File and Edit come first in the palette bar and Help last, the rest A to Z. On: every palette A to Z.",
                 "Preferences > Interface");
    ImGui::Checkbox(tr("Left-handed layout"), &app.layout().leftHanded);
    ui::HelpText("Left-handed layout", nullptr, "Mirror the shelves and trays: tools on the left, navigation on the right.",
                 "Preferences > Interface");
    {
        static const char* kTiles[] = {"Small", "Medium", "Large"};
        int& tiles = app.layout().shelfTiles;
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##shelf_tiles", (std::string(tr("Shelf tiles:")) + " " + tr(kTiles[std::clamp(tiles, 0, 2)])).c_str())) {
            for (int i = 0; i < 3; ++i)
                if (ImGui::Selectable(tr(kTiles[i]), tiles == i)) tiles = i;
            ImGui::EndCombo();
        }
        ui::HelpText("Shelf tiles", nullptr, "How big the brush tiles on the bottom shelf are. Small leaves the most room for the model.",
                     "Preferences > Interface");
    }
    // Timeline position (#63): the Sculpt Timeline strip at the top of the viewport (the default) or the bottom.
    ImGui::TextDisabled("%s", tr("Timeline position"));
    ui::Segmented(app, {"view.timeline_top", "view.timeline_bottom"});
    if (const Action* a = app.actions().find("view.timeline_compact"); a && a->checked) { // one thin row (@factoberplay)
        bool compact = a->checked();
        if (ui::Checkbox("Compact timeline##view.timeline_compact", &compact)) a->run();
        ui::HelpAction(app, "view.timeline_compact");
    }
    // Finer steps near 100 % (125 % was too big a first step) and up to 300 % for a 5K or 5K2K monitor at its native
    // resolution, no system scaling (fenix 11-08).
    static const char* kScales[] = {"Automatic", "100%", "110%", "115%", "125%", "135%", "150%",
                                    "175%",      "200%", "250%", "300%"};
    static const float kScaleValues[] = {0.0f, 1.0f, 1.1f, 1.15f, 1.25f, 1.35f, 1.5f, 1.75f, 2.0f, 2.5f, 3.0f};
    constexpr int kScaleCount = int(sizeof kScaleValues / sizeof kScaleValues[0]);
    int current = 0;
    for (int i = 0; i < kScaleCount; ++i)
        if (std::abs(kScaleValues[i] - p.uiScale) < 0.01f) current = i;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##ui_scale", (std::string(tr("UI scale:")) + " " + tr(kScales[current])).c_str())) {
        for (int i = 0; i < kScaleCount; ++i)
            if (ImGui::Selectable(tr(kScales[i]), i == current)) {
                p.uiScale = kScaleValues[i];
                app.requestUiScale(); // live, from the next frame (#87)
            }
        ImGui::EndCombo();
    }
    ui::HelpText("UI scale", nullptr,
                 "Size of text and controls, applied at once. Automatic follows the display the window is on (Windows: "
                 "Settings > Display > Scale, per monitor).",
                 "Preferences > Interface");
    static const char* kRotation[] = {"Free", "Turntable", "X axis", "Z axis"}; // UiPrefs::orbitLock
    p.orbitLock = std::clamp(p.orbitLock, 0, 3);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##rotation", (std::string(tr("Rotation:")) + " " + tr(kRotation[p.orbitLock])).c_str())) {
        for (int i = 0; i < 4; ++i)
            if (ImGui::Selectable(tr(kRotation[i]), i == p.orbitLock)) p.orbitLock = i;
        ImGui::EndCombo();
    }
    ui::HelpText("Rotation", nullptr,
                 "Free: dragging turns the model about the screen's axes and can roll it over the top; press "
                 "Shift while rotating to snap to the nearest front, side, top or bottom view. Turntable: the model stays "
                 "upright and turns about its vertical axis, as in Blender or Maya. X axis, Z axis: it turns about that "
                 "world axis only (View > Orbit lock).",
                 "Preferences > Interface");
    static const char* kNotch[] = {"Automatic", "Always reserve", "Off"};
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##camera_notch", (std::string(tr("Camera notch:")) + " " + tr(kNotch[std::clamp(p.cameraNotch, 0, 2)])).c_str())) {
        for (int i = 0; i < 3; ++i)
            if (ImGui::Selectable(tr(kNotch[i]), i == p.cameraNotch)) p.cameraNotch = i;
        ImGui::EndCombo();
    }
    ui::HelpText("Camera notch", nullptr,
                 "Keeps the interface below the camera notch of MacBooks (and other laptops) when SculptForge fills the "
                 "screen. Automatic asks the system (macOS) or recognises a notched MacBook display (Linux).",
                 "Preferences > Interface");
}

void drawHotkeysSection(App& app) {
    char (&filter)[64] = g_hotkeysFilter;
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##hotkeys_filter", tr(ICON_LC_SEARCH "  Filter"), filter, sizeof filter);
    const std::string f = lower(filter);
    ActionRegistry& reg = app.actions();
    const float s = ImGui::GetFontSize() / 15.0f;
    std::vector<const Action*> list;
    for (const auto& a : reg.all()) {
        if (!a.run || a.id.rfind("cmd.", 0) == 0) continue; // commands: through the command palette
        if (!f.empty() && lower(a.label).find(f) == std::string::npos && lower(a.palette).find(f) == std::string::npos &&
            lower(trAction(a.id, a.label)).find(f) == std::string::npos)
            continue;
        list.push_back(&a);
    }
    std::stable_sort(list.begin(), list.end(), [](const Action* x, const Action* y) {
        return x->palette != y->palette ? x->palette < y->palette : x->label < y->label;
    });
    const float listH = std::min(320.0f * s, float(list.size()) * ImGui::GetFrameHeightWithSpacing() + 8.0f * s);
    if (ImGui::BeginTable("##hotkeys", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_PadOuterX,
                          ImVec2(0, std::max(listH, 60.0f * s)))) {
        ImGui::TableSetupColumn("action", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("key", ImGuiTableColumnFlags_WidthFixed, 140.0f * s);
        for (const Action* a : list) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(trAction(a->id, a->label));
            ui::HelpAction(app, a->id.c_str());
            ImGui::TableNextColumn();
            const std::string chord = reg.shortcutText(a->id);
            const bool capturing = app.capturingShortcut() == a->id;
            const std::string text = capturing ? tr("press a key...") : chord.empty() ? "-" : chord;
            if (reg.hasOverride(a->id)) ImGui::PushStyleColor(ImGuiCol_Text, col(ui::color::accent));
            const bool modified = reg.hasOverride(a->id); // the arrow beside it puts the default back
            if (ImGui::Button((text + "##hk." + a->id).c_str(), ImVec2(modified ? -(ImGui::GetFrameHeight() + 2.0f * s) : -FLT_MIN, 0)))
                app.beginShortcutCapture(a->id);
            if (reg.hasOverride(a->id)) ImGui::PopStyleColor();
            ui::HelpText((std::string(tr("Shortcut for")) + " " + trAction(a->id, a->label)).c_str(), chord.c_str(),
                         "Click, then press the new key. Esc cancels, Backspace clears. Orange: changed from the default; "
                         "the arrow beside it puts the default back.",
                         "Preferences > Hotkeys");
            if (reg.hasOverride(a->id)) {
                ImGui::SameLine(0.0f, 2.0f * s);
                const bool resetClicked = ImGui::SmallButton(("##hkreset." + a->id).c_str());
                {   // the arrow drawn over the button, which keeps a plain id (scripts click it by "##hkreset.<action>")
                    const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
                    const ImVec2 sz = ImGui::CalcTextSize(ICON_LC_ROTATE_CCW);
                    ImGui::GetWindowDrawList()->AddText(ImVec2((lo.x + hi.x - sz.x) * 0.5f, (lo.y + hi.y - sz.y) * 0.5f),
                                                        ImGui::GetColorU32(ImGuiCol_Text), ICON_LC_ROTATE_CCW);
                }
                if (resetClicked) {
                    const std::string lost = reg.resetShortcut(a->id);
                    app.status_ = std::string(trAction(a->id, a->label)) + ": default " + reg.shortcutText(a->id) +
                                  (lost.empty() ? "" : " (was " + lost + ")");
                }
                ui::HelpText("Reset to default", nullptr, "Puts this action's default shortcut back.", "Preferences > Hotkeys");
            }
        }
        ImGui::EndTable();
    }
    if (ImGui::Button(tr(ICON_LC_ROTATE_CCW "  Default shortcuts"), ImVec2(-FLT_MIN, 0))) {
        reg.clearOverrides();
        app.status_ = "Default shortcuts restored";
    }
    ui::HelpText("Default shortcuts", nullptr, "Undo every shortcut change.", "Preferences > Hotkeys");
    app.drawShortcutConflict(); // inside the Preferences popup stack: Esc closes only the box
}

void drawLibraryBrowserSection(App& app) {
    UiPrefs& p = app.uiPrefs();
    UiLayout& l = app.layout();
    ImGui::Checkbox(tr("Show at startup"), &p.libraryAtStartup);
    ui::HelpText("Show Library at startup", nullptr, "Open Library on the Project tab when SculptForge starts.",
                 "Preferences > Library");
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("Rows"));
    for (int r = 1; r <= 4; ++r) {
        ImGui::SameLine();
        const bool on = l.libraryRows == r;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, col(ui::color::accent));
        if (ImGui::Button((std::to_string(r) + "##lbrows").c_str(), ImVec2(ImGui::GetFrameHeight() * 1.3f, 0))) l.libraryRows = r;
        if (on) ImGui::PopStyleColor();
        ui::HelpText("Library rows", nullptr, "How many rows of thumbnails the Library strip shows.", "Preferences > Library");
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("Placement"));
    ImGui::SameLine();
    if (ImGui::RadioButton(tr("Top"), !l.libraryBottom)) l.libraryBottom = false;
    ui::HelpText("Library at the top", nullptr, "Library opens along the top edge of the canvas.", "Preferences > Library");
    ImGui::SameLine();
    if (ImGui::RadioButton(tr("Bottom"), l.libraryBottom)) l.libraryBottom = true;
    ui::HelpText("Library at the bottom", nullptr, "Library opens above the bottom shelf.", "Preferences > Library");
    ImGui::TextColored(col(ui::color::textDim), "%s", tr("Library folder"));
    const std::string shown = p.libraryFolder.empty() ? std::string("Documents/SculptForge (default)") : p.libraryFolder;
    if (ImGui::Button((std::string(ICON_LC_FOLDER_OPEN "  ") + shown + "##libfolder").c_str(), ImVec2(-FLT_MIN, 0)))
        sf::dialogs::openFolder(onUiFileChosen, &g_requests[kLibraryFolder], app.window_, nullptr, false);
    ui::HelpText("Library folder", nullptr,
                 "Where your projects, tools, brushes, alphas, images, splats and Snapshots live. Library shows it as User.",
                 "Preferences > Library");
    if (!p.libraryFolder.empty() && ImGui::SmallButton(tr("Use the default folder"))) p.libraryFolder.clear();
}

} // namespace config

namespace config {

looks::Palette classicPalette() {
    return {0x121212, 0x1c1c1c, 0x232323, 0x2c2c2c, 0x393939, 0x505050, 0x8d8d8d, 0xb6b6b6, 0xd8d8d8, 0xf2f2f2, 0xf0882e,
            0xf0882e, 0xe5604a, 0x9ece6a, 0xe0af68, false};
}

void drawLookMiniature(ImDrawList* dl, ImVec2 a, ImVec2 b, const looks::Palette& pal, const char* name, ImFont* font,
                       float s, int layout) {
    auto col = [](unsigned hex) { return theme::u32(theme::rgb(hex)); };
    dl->AddRectFilled(a, b, col(pal.crust));
    const float g = 3.0f * s;
    float top = a.y + g;
    if (layout == 0) { // the palette bar: a row of short words
        for (float x = a.x + g + 2.0f * s; x < b.x - g - 14.0f * s; x += 13.0f * s)
            dl->AddRectFilled(ImVec2(x, top + 2.0f * s), ImVec2(x + 9.0f * s, top + 4.0f * s), col(pal.overlay));
        top += 7.0f * s;
    }
    // The bar (the status bar with its menus, or the top shelf), its accent marking the current tool.
    const float barH = (layout == 0 ? 14.0f : 8.0f) * s;
    dl->AddRectFilled(ImVec2(a.x + g, top), ImVec2(b.x - g, top + barH), col(pal.mantle));
    dl->AddRectFilled(ImVec2(a.x + g + 2.0f * s, top + 2.0f * s), ImVec2(a.x + g + 16.0f * s, top + 6.0f * s), col(pal.accent));
    if (layout >= 0) // the shelf's tiles: one row, or two in the Classic layout
        for (int row = 0; row < (layout == 0 ? 2 : 1); ++row)
            for (float x = a.x + g + 22.0f * s; x < b.x - g - 8.0f * s; x += 9.0f * s)
                dl->AddRectFilled(ImVec2(x, top + 2.0f * s + float(row) * 6.0f * s), ImVec2(x + 6.0f * s, top + 6.0f * s + float(row) * 6.0f * s),
                                  col(pal.surface1));
    top += barH + g;
    dl->AddRectFilled(ImVec2(a.x + g, top), ImVec2(b.x - g - 30.0f * s, b.y - g), col(pal.base));
    dl->AddRectFilled(ImVec2(b.x - g - 26.0f * s, top), ImVec2(b.x - g, b.y - g), col(pal.base));
    dl->AddRectFilled(ImVec2(b.x - g - 22.0f * s, b.y - g - 8.0f * s), ImVec2(b.x - g - 4.0f * s, b.y - g - 4.0f * s), col(pal.accent2));
    if (layout >= 0) { // a sphere in the view, so the colours read around a model
        const ImVec2 c((a.x + b.x - 30.0f * s) * 0.5f, (top + b.y) * 0.5f);
        const float r = std::min(b.x - a.x, b.y - top) * 0.2f;
        dl->AddCircleFilled(c, r, col(0x8a8a8e));
        dl->AddCircleFilled(ImVec2(c.x - r * 0.3f, c.y - r * 0.3f), r * 0.55f, col(0xb0b0b4));
    }
    if (!font) font = ImGui::GetFont();
    dl->AddText(font, font->FontSize, ImVec2(a.x + g + 6.0f * s, b.y - g - font->FontSize - 3.0f * s), col(pal.text), name);
}

} // namespace config

// Preferences > Interface > Look: four looks as swatch cards, the look's own options, and the layout.
void App::drawLookSection() {
    theme::LookSettings& l = uiPrefs_.look;
    const float s = uiScale_;
    tray::GroupLabel(*this, "Look");
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float cardW = std::floor((ImGui::GetContentRegionAvail().x - spacing) * 0.5f), cardH = 46.0f * s;
    for (int i = 0; i < int(looks::LookId::Count); ++i) {
        const looks::LookId id = looks::LookId(i);
        if (i % 2) ImGui::SameLine();
        looks::Palette pal;
        if (id == looks::LookId::ForgeTerminal) {
            const looks::SchemeInfo* sc = looks::findScheme(l.scheme);
            int n = 0;
            pal = (sc ? sc : looks::schemes(n))->palette;
        } else if (id == looks::LookId::Phosphor) {
            pal = looks::phosphorPalette(l.amber);
        } else if (id == looks::LookId::Blueprint) {
            pal = looks::blueprintPalette();
        } else {
            pal = config::classicPalette();
        }
        const std::string label = std::string(looks::lookName(id)) + "##look." + looks::lookKey(id);
        const bool clicked = ImGui::InvisibleButton(label.c_str(), ImVec2(cardW, cardH));
        const bool hovered = ImGui::IsItemHovered(), current = l.look == i;
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ui::TileLift lift = ui::BeginTileLift(a, b, 0.0f);
        config::drawLookMiniature(dl, a, b, pal, looks::lookName(id), i == int(looks::LookId::Classic) ? fontInterSmall_ : fontMonoSmall_, s);
        dl->AddRect(a, b, theme::u32(current ? theme::accent : hovered ? theme::textDim : theme::tileBorder), 0.0f, 0,
                    current ? 2.0f * s : 1.0f * s);
        ui::EndTileLift(lift, 0.0f);
        ImGui::LogText("%s", looks::lookName(id));
        ui::HelpText(looks::lookName(id), nullptr,
                     i == 0   ? "The original dark grey look with the orange accent."
                     : i == 1 ? "Keyboard-first tiles with gaps and thin borders, monospace labels, a status bar, and the colours "
                                "of a well-known terminal theme (or your desktop theme)."
                     : i == 2 ? "A phosphor screen: green or amber on black, monospace, ASCII rules; optional scanlines on the interface."
                              : "Engineering paper: deep blue, cyan and white line work, grid headers, technical numbers.",
                     "Preferences > Interface > Look");
        if (clicked && !current) {
            l.look = i;
            if (i != 0) setShellLayout(1);
            lookDirty_ = true;
        }
    }
    if (l.look == int(looks::LookId::ForgeTerminal)) {
        int n = 0;
        const looks::SchemeInfo* list = looks::schemes(n);
        const looks::SchemeInfo* cur = looks::findScheme(l.scheme);
        ImGui::BeginDisabled(l.matchOmarchy);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##look_scheme", (std::string(tr("Colours:")) + " " + (cur ? cur->name : list[0].name)).c_str())) {
            for (int i = 0; i < n; ++i) {
                const std::string item = std::string(list[i].name) + "##scheme." + list[i].key;
                if (ImGui::Selectable(item.c_str(), cur == &list[i])) {
                    l.scheme = list[i].key;
                    lookDirty_ = true;
                }
                // Its background and accent as two dots at the right.
                const ImVec2 b = ImGui::GetItemRectMax();
                const float r = 4.0f * s, cy = (ImGui::GetItemRectMin().y + b.y) * 0.5f;
                ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(b.x - 22.0f * s, cy), r, theme::u32(theme::rgb(list[i].palette.base)));
                ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(b.x - 10.0f * s, cy), r, theme::u32(theme::rgb(list[i].palette.accent)));
            }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
        ui::HelpText("Colours", nullptr, "Forge Terminal's colour scheme, from well-known open-source terminal themes.",
                     "Preferences > Interface > Look");
        // Match my desktop theme: only where there is a theme to match (an Omarchy theme folder, Linux), or to
        // switch it off again once that theme is gone.
        const bool omarchy = !looks::omarchyThemeDir().empty();
        if (omarchy || l.matchOmarchy) {
            if (ui::Checkbox("Match my desktop theme##look.omarchy", &l.matchOmarchy)) lookDirty_ = true;
            if (!ImGui::GetIO().KeyCtrl) ImGui::SetItemTooltip("%s", tr("Uses your Omarchy theme's colours when Omarchy is installed"));
            ui::HelpText("Match my desktop theme", nullptr,
                         "Uses your Omarchy theme's colours when Omarchy is installed (read from its theme folder; SculptForge "
                         "never changes it). After switching themes, toggle this to pick up the new colours.",
                         "Preferences > Interface > Look");
        }
    } else if (l.look == int(looks::LookId::Phosphor)) {
        if (ImGui::RadioButton(tr("Green##phosphor.green"), !l.amber)) {
            l.amber = false;
            lookDirty_ = true;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton(tr("Amber##phosphor.amber"), l.amber)) {
            l.amber = true;
            lookDirty_ = true;
        }
        ImGui::SameLine();
        if (ui::Checkbox("Scanlines & glow##look.effects", &l.effects)) lookDirty_ = true;
        ui::HelpText("Scanlines & glow", nullptr,
                     "Faint scanlines and a glow around the focused panel, on the interface only (never on the 3D view). Off by default.",
                     "Preferences > Interface > Look");
    }
    if (l.look != int(looks::LookId::Classic)) { // the tiled looks' panel borders (Classic has none to speak of)
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr("Outlines"));
        const char* const kOutlines[] = {"Off##look.outlines", "Subtle##look.outlines", "On##look.outlines"};
        for (int i = 0; i < 3; ++i) {
            ImGui::SameLine();
            if (ImGui::RadioButton(tr(kOutlines[i]), l.outlines == i)) {
                l.outlines = i;
                lookDirty_ = true;
            }
            ui::HelpText("Outlines", nullptr,
                         "The borders around the panels in Forge Terminal, Phosphor and Blueprint. Subtle (the default): "
                         "faint, and nothing lights up under the pointer. Off: no borders, the gaps alone separate the "
                         "panels. On: clear borders, the panel under the pointer a faint step lighter.",
                         "Preferences > Interface > Look");
        }
        ImGui::BeginDisabled(l.outlines == 0);
        float opacity = l.outlineOpacity * 100.0f;
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ui::SliderInside("##look.outline_opacity", &opacity, 0.0f, 100.0f, "Outline opacity %.0f%%")) {
            l.outlineOpacity = std::clamp(opacity / 100.0f, 0.0f, 1.0f);
            lookDirty_ = true;
        }
        ImGui::EndDisabled();
        ui::HelpText("Outline opacity", nullptr,
                     "How strong the panel borders are (0-100%), on top of Outlines: 100% draws them as Subtle or On "
                     "does, lower fades them into the panels (#98). Ctrl+click to type.",
                     "Preferences > Interface > Look");
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("Layout"));
    ImGui::SameLine();
    if (ImGui::RadioButton(tr("SculptForge##layout.sculptforge"), uiPrefs_.shellLayout == 1)) {
        setShellLayout(1);
        lookDirty_ = true;
    }
    ui::HelpText("SculptForge layout", nullptr,
                 "Menus by task (File, Edit, Sculpt, Paint, Mesh, View, Render, Help), a status bar and a one-row tool shelf. "
                 "Every palette is a section of one menu, and F3 finds them all.",
                 "Preferences > Interface > Look");
    ImGui::SameLine();
    if (ImGui::RadioButton(tr("Classic##layout.classic"), uiPrefs_.shellLayout == 0)) {
        setShellLayout(0);
        lookDirty_ = true;
    }
    ui::HelpText("Classic layout", nullptr, "The alphabetical palette bar and the two-row top shelf.",
                 "Preferences > Interface > Look");
    tray::GroupLabel(*this, "Interface");
}

} // namespace sf
