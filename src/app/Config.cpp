// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Default actions and shortcuts, Store Config / Restore / Save / Load UI,
// the Preferences > Config / Interface / Hotkeys / LightBox sections, the F1
// Controls sheet (generated from the action registry) and ZBrush-style
// shortcut assignment.
#include "app/Config.h"

#include "app/App.h"
#include "app/CrashLog.h"
#include "app/LookSchemes.h"
#include "app/Palettes.h"
#include "app/Ui.h"
#include "app/I18n.h"
#include "core/Bake.h"
#include "core/Micro.h"
#include "core/Multires.h"
#include "core/Script.h"

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
    j.set("layout", std::string(p.shellLayout == 1 ? "sculptforge" : "zbrush"));
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
    j.set("lightboxAtStartup", p.lightboxAtStartup);
    j.set("libraryFolder", p.libraryFolder);
    j.set("cameraNotch", double(p.cameraNotch));
    j.set("turntable", p.turntable);
    j.set("inputStyle", std::string(p.inputStyle == 3 ? "maya" : p.inputStyle == 2 ? "blender" : p.inputStyle == 1 ? "sculptforge" : "zbrush"));
    j.set("emulateNumpad", p.emulateNumpad);
    j.set("rightClickQuickMenu", p.rightClickQuickMenu);
    j.set("spaceCamera", p.spaceCamera);
    j.set("emulate3Button", p.emulate3Button);
    j.set("mayaZoomHorizontal", p.mayaZoomHorizontal);
    j.set("invertZoom", p.invertZoom);
    j.set("alongNormal", std::string(p.alongNormal == 1 ? "always" : p.alongNormal == 2 ? "never" : "invert_key"));
    j.set("connectedOnly", p.connectedOnly);
    j.set("viewFalloff", p.viewFalloff);
    j.set("dyntopoSmoothRemesh", double(p.dyntopoSmoothRemesh));
    j.set("airStrokes", p.airStrokes);
    j.set("cursorFollowsSurface", p.cursorFollowsSurface);
    j.set("hideRingWhileStroking", p.hideRingWhileStroking);
    j.set("timelinePosition", std::string(p.timelineTop ? "top" : "bottom"));
    j.set("showAxes", p.showAxes);
    j.set("axisGizmoSize", std::string(p.axisGizmoSize == 0 ? "small" : p.axisGizmoSize == 2 ? "large" : "medium"));
    j.set("showViewFigure", p.showViewFigure);
    j.set("alphabeticalPalettes", p.alphabeticalPalettes);
    lookToJson(p, j);
    j.set("language", p.language);
    j.set("tablet", toJson(p.tablet));
    j.set("spaceMouse", toJson(p.spaceMouse));
    j.set("antialiasing", double(p.antialiasing));
    j.set("fastNavigation", p.fastNavigation);
    j.set("multithreading", p.multithreading);
    j.set("predictiveStrokes", p.predictiveStrokes);
    j.set("draftStrokes", p.draftStrokes);
    j.set("dyntopoConstantDetail", p.dyntopoConstantDetail);
    j.set("flatShading", p.flatShading);
    j.set("viewportCursor", double(p.viewportCursor));
    j.set("backfaceCulling", p.backfaceCulling);
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
    p.lightboxAtStartup = j["lightboxAtStartup"].asBool(p.lightboxAtStartup);
    if (j["libraryFolder"].isString()) p.libraryFolder = j["libraryFolder"].asString();
    p.cameraNotch = std::clamp(int(j["cameraNotch"].asNumber(p.cameraNotch)), 0, 2);
    p.turntable = j["turntable"].asBool(p.turntable);
    if (j["inputStyle"].isString()) {
        p.inputStyleSaved = true;
        const std::string style = j["inputStyle"].asString();
        p.inputStyle = style == "maya" ? 3 : style == "blender" ? 2 : style == "sculptforge" ? 1 : 0;
    }
    p.emulateNumpad = j["emulateNumpad"].asBool(p.emulateNumpad);
    p.rightClickQuickMenu = j["rightClickQuickMenu"].asBool(p.rightClickQuickMenu);
    p.spaceCamera = j["spaceCamera"].asBool(p.spaceCamera);
    p.emulate3Button = j["emulate3Button"].asBool(p.emulate3Button);
    p.mayaZoomHorizontal = j["mayaZoomHorizontal"].asBool(p.mayaZoomHorizontal);
    p.invertZoom = j["invertZoom"].asBool(p.invertZoom);
    if (j["alongNormal"].isString()) {
        const std::string a = j["alongNormal"].asString();
        p.alongNormal = a == "always" ? 1 : a == "never" ? 2 : 0;
    }
    p.connectedOnly = j["connectedOnly"].asBool(p.connectedOnly);
    p.viewFalloff = j["viewFalloff"].asBool(p.viewFalloff);
    p.dyntopoSmoothRemesh = std::clamp(float(j["dyntopoSmoothRemesh"].asNumber(p.dyntopoSmoothRemesh)), 0.0f, 1.0f);
    p.airStrokes = j["airStrokes"].asBool(p.airStrokes);
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
    // A ui.json from before 0.10.7 names neither: it was stored with the Classic look and the ZBrush-style layout,
    // and keeps them (the new-look card offers the new default once, PanelSupport.cpp).
    p.lookSaved = lookFromJson(j, p);
    if (!p.lookSaved) {
        p.look.look = int(looks::LookId::Classic);
        p.shellLayout = 0;
    }
    if (j["language"].isString()) p.language = languageCode(languageFromCode(j["language"].asString()));
    fromJson(j["tablet"], p.tablet);
    fromJson(j["spaceMouse"], p.spaceMouse);
    const int aa = int(j["antialiasing"].asNumber(p.antialiasing));
    p.antialiasing = aa >= 8 ? 8 : aa >= 4 ? 4 : aa >= 2 ? 2 : 1;
    p.fastNavigation = j["fastNavigation"].asBool(p.fastNavigation);
    p.multithreading = j["multithreading"].asBool(p.multithreading);
    p.predictiveStrokes = j["predictiveStrokes"].asBool(p.predictiveStrokes);
    p.draftStrokes = j["draftStrokes"].asBool(p.draftStrokes);
    p.dyntopoConstantDetail = j["dyntopoConstantDetail"].asBool(p.dyntopoConstantDetail);
    p.flatShading = j["flatShading"].asBool(p.flatShading);
    p.viewportCursor = std::clamp(int(j["viewportCursor"].asNumber(p.viewportCursor)), 0, 1);
    p.backfaceCulling = j["backfaceCulling"].asBool(p.backfaceCulling);
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
    {"Mask a lasso or rectangle", "Ctrl + drag with Mask Lasso / Mask Rectangle (Tool > Masking; Ctrl+Alt unmasks)"},
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
    {"Hide face group", "H over it (Shift+H: show only it, again: everything; Alt+H shows all)"},
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
    if (char* pref = SDL_GetPrefPath("SculptForge", "SculptForge")) {
        configDir_ = pref;
        SDL_free(pref);
    }
    layout_ = standardLayout(uiPrefs_.shellLayout);
    if (deterministic) return; // tests, renders and recordings always use the standard layout
    std::string err;
    if (fileExists(uiConfigPath()) && !readUiConfig(uiConfigPath(), err)) SDL_Log("%s", err.c_str());
}

void App::addHelpFonts(float scale) {
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;
    cfg.OversampleH = 2;
    fontHelpTitle_ = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(assets::fontSemiBold),
                                                    int(assets::fontSemiBold_size), 15.0f * scale, &cfg);
    static const ImWchar kIconRanges[] = {ICON_MIN_LC, ICON_MAX_LC, 0};
    ImFontConfig icons;
    icons.MergeMode = true;
    icons.PixelSnapH = true;
    icons.FontDataOwnedByAtlas = false;
    icons.GlyphMinAdvanceX = 15.0f * scale;
    icons.GlyphOffset = ImVec2(0, 2.0f * scale);
    io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(assets::fontLucide), int(assets::fontLucide_size), 15.0f * scale,
                                   &icons, kIconRanges);
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
    add("geometry.subdivide", "Subdivide", "Tool > Geometry", nullptr, [this] { runCommand("subdivide"); }, nullptr, "subdivide");
    // Subdivision levels (ZBrush Tool > Geometry; core/Multires.h).
    add("geometry.divide", "Divide", "Tool > Geometry", "Ctrl+D", [this] { runCommand("subdiv_divide", {{"smooth", smt_}}); },
        ICON_LC_GRID_2X2, "subdiv_divide", nullptr, nullptr,
        "Add a subdivision level: four times the polygons, and the lower levels are kept, so you can step down to "
        "shape big forms and back up to the detail. Quads stay quads (Catmull-Clark). Smt off divides without smoothing.");
    add("geometry.smt", "Smt", "Tool > Geometry", nullptr, [this] { smt_ = !smt_; }, nullptr, nullptr,
        [this] { return smt_; }, nullptr,
        "Smooth the next Divide (on by default). Off: the new level keeps the hard edges and flat faces of the one below.");
    add("geometry.lower_res", "Lower Res", "Tool > Geometry", "Shift+D", [this] { runCommand("subdiv_lower"); },
        ICON_LC_CHEVRON_DOWN, "subdiv_lower", nullptr, [this] { return subdivCurrentLevel(doc_.mesh) > 0; },
        "Step one subdivision level down (fewer polygons, for big forms). The detail of the higher levels is kept.");
    add("geometry.higher_res", "Higher Res", "Tool > Geometry", "D", [this] { runCommand("subdiv_higher"); },
        ICON_LC_CHEVRON_UP, "subdiv_higher", nullptr,
        [this] { return subdivCurrentLevel(doc_.mesh) + 1 < subdivLevelCount(doc_.mesh); },
        "Step one subdivision level up (more polygons, for detail). Edits made below carry the detail along.");
    add("geometry.del_lower", "Del Lower", "Tool > Geometry", nullptr, [this] { runCommand("subdiv_delete_lower"); }, nullptr,
        "subdiv_delete_lower", nullptr, [this] { return subdivCurrentLevel(doc_.mesh) > 0; });
    add("geometry.del_higher", "Del Higher", "Tool > Geometry", nullptr, [this] { runCommand("subdiv_delete_higher"); }, nullptr,
        "subdiv_delete_higher", nullptr,
        [this] { return subdivCurrentLevel(doc_.mesh) + 1 < subdivLevelCount(doc_.mesh); });
    add("geometry.reconstruct", "Reconstruct Subdiv", "Tool > Geometry", nullptr, [this] { runCommand("subdiv_reconstruct"); },
        nullptr, "subdiv_reconstruct", nullptr, [this] { return subdivCurrentLevel(doc_.mesh) == 0; });
    // Micro-displacement levels (Tool > Geometry > Micro; core/Micro.h).
    add("geometry.micro_divide", "Divide to Micro", "Tool > Geometry", nullptr,
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
    add("subtool.duplicate", "Duplicate Part", "Tool > Parts", "Ctrl+Shift+D", [this] { runCommand("subtool_duplicate"); },
        ICON_LC_COPY, "subtool_duplicate", nullptr, nullptr,
        "Copy the active Part, with its layers and attributes, and make the copy active (also in the Parts list's "
        "right-click menu).");
    // Tool > SubTool > Extract, Tool > Geometry > Modify Topology, Tool > Deformation (PanelExtract.cpp).
    add("subtool.extract", "Extract", "Tool > Parts", nullptr, [this] { runExtract(); }, nullptr, "extract", nullptr,
        [this] { return doc_.mesh.hasMask(); },
        "Make a shell from the masked part of the Part (a helmet, a strap, clothing), shown as a preview below it. "
        "Change Thick, E Smt or Double and it updates; Accept keeps it as a new Part.");
    add("subtool.extract_accept", "Accept", "Tool > Parts", nullptr, [this] { runCommand("extract_accept"); }, nullptr,
        "extract_accept", nullptr, [this] { return doc_.extractPreviewCurrent(); },
        "Keep the Extract preview as a new Part below this one. Ctrl+Z takes it away again.");
    add("subtool.extract_double", "Double", "Tool > Parts", nullptr,
        [this] {
            extractDouble_ = !extractDouble_;
            if (doc_.extractPreviewCurrent()) runExtract();
        },
        nullptr, nullptr, [this] { return extractDouble_; }, nullptr,
        "Grow the extracted shell both inward and outward from the surface, instead of outward only.");
    add("subtool.extract_tcorne", "TCorne", "Tool > Parts", nullptr,
        [this] {
            extractTCorne_ = !extractTCorne_;
            if (doc_.extractPreviewCurrent()) runExtract();
        },
        nullptr, nullptr, [this] { return extractTCorne_; }, nullptr,
        "Triangle corners: where the extracted edge turns sharply, the rim is made of triangles instead of quads.");
    add("subtool.extract_tborder", "TBorder", "Tool > Parts", nullptr,
        [this] {
            extractTBorder_ = !extractTBorder_;
            if (doc_.extractPreviewCurrent()) runExtract();
        },
        nullptr, nullptr, [this] { return extractTBorder_; }, nullptr,
        "Triangle border: the whole rim of the extracted shell is made of triangles instead of quads.");
    add("geometry.mirror_weld", "Mirror And Weld", "Tool > Geometry", nullptr,
        [this] { runCommand("mirror_and_weld", {{"axis", axesText(mirrorWeldAxes_)}}); }, nullptr, "mirror_and_weld",
        nullptr, [this] { return !axesText(mirrorWeldAxes_).empty() && doc_.mesh.triangleCount() > 0; },
        "Mirror the + side of the Part onto the - side across the chosen axis and weld the seam into one "
        "watertight mesh. Face Groups and vertex paint come along. To keep the other side, Deformation > Mirror first.");
    add("deformation.mirror", "Mirror", "Tool > Deformation", nullptr,
        [this] { runCommand("mirror", {{"axis", axesText(mirrorAxes_)}}); }, nullptr, "mirror", nullptr,
        [this] { return !axesText(mirrorAxes_).empty() && doc_.mesh.vertexCount() > 0; },
        "Flip the Part across the chosen axis, through the origin (the plane symmetry and Mirror And Weld use).");
    for (int k = 0; k < 3; ++k) {
        static const char* const kWeldIds[3] = {"geometry.mirror_weld_x", "geometry.mirror_weld_y", "geometry.mirror_weld_z"};
        static const char* const kMirrorIds[3] = {"deformation.mirror_x", "deformation.mirror_y", "deformation.mirror_z"};
        static const char* const kNames[3] = {"X", "Y", "Z"};
        add(kWeldIds[k], kNames[k], "Tool > Geometry", nullptr, [this, k] { mirrorWeldAxes_[k] = !mirrorWeldAxes_[k]; },
            nullptr, nullptr, [this, k] { return mirrorWeldAxes_[k]; }, nullptr,
            "An axis Mirror And Weld mirrors across (several: one after the other).");
        add(kMirrorIds[k], kNames[k], "Tool > Deformation", nullptr, [this, k] { mirrorAxes_[k] = !mirrorAxes_[k]; }, nullptr,
            nullptr, [this, k] { return mirrorAxes_[k]; }, nullptr, "An axis Mirror flips along (several: one after the other).");
    }
    // Whole-mesh deformations (core/Deform.h, PanelExtract.cpp), each by its amount slider.
    {
        static const char* const kIds[4] = {"deformation.polish", "deformation.relax", "deformation.smooth", "deformation.balloon"};
        static const char* const kLabels[4] = {"Polish", "Relax", "Smooth", "Balloon"};
        static const char* const kCommands[4] = {"deform_polish", "deform_relax", "deform_smooth", "deform_balloon"};
        static const char* const kHelp[4] = {
            "Polish the whole Part by the amount beside it: fine noise and small bumps flatten out while flat areas and "
            "sharp edges stay. The mask holds what it covers; one undo step.",
            "Relax the whole Part: the vertices even out their spacing along the surface without changing its shape "
            "(stretched areas after big moves). The mask holds what it covers; one undo step.",
            "Smooth the whole Part by the amount beside it (Smooth keeps volume: without shrinking it). The mask "
            "holds what it covers; one undo step.",
            "Fill the whole Part out toward a rounder volume, like a balloon (ZBrush's Inflate Balloon); a negative "
            "amount lets the air out. The mask holds what it covers; one undo step."};
        for (int k = 0; k < 4; ++k)
            add(kIds[k], kLabels[k], "Tool > Deformation", nullptr,
                [this, k] {
                    Args a{{"amount", double(deformAmount_[k])}};
                    if (k == 2) a["keep_volume"] = deformKeepVolume_;
                    runCommand(kCommands[k], a);
                },
                nullptr, kCommands[k], nullptr, [this, k] { return doc_.mesh.vertexCount() > 0 && deformAmount_[k] != 0.0f; },
                kHelp[k]);
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
            SDL_ShowSaveFileDialog(onGozFileChosen, &gozDialogs_[int(GozFile::ExportStl)], window_, filters, 2, nullptr);
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
        "SculptForge reads and writes only there, and only when you send or get.");
    add("brush.paste", "Paste brush", "Brush", "Ctrl+V", [this] { pasteBrush(); }, ICON_LC_CLIPBOARD_PASTE, nullptr, nullptr,
        nullptr, "Add the brush on the clipboard: a brush code someone shared, or the text of a .sfbrush file.");
    // Per-brush settings (#78, BrushSettingsUi.cpp).
    add("brush.save_settings", "Save Brush Settings", "Brush", nullptr, [this] { saveBrushSettings(); }, ICON_LC_SAVE,
        nullptr, nullptr, nullptr,
        "Every brush keeps its own Draw Size, Z Intensity, Falloff, RGB Intensity, stroke and alpha while you work. "
        "This saves the current brush's, so it starts with them every time SculptForge opens.");
    add("brush.reset_settings", "Reset Brush to Default", "Brush", nullptr, [this] { resetBrushSettings(); },
        ICON_LC_ROTATE_CCW, nullptr, nullptr, nullptr,
        "Put the current brush's Draw Size, Z Intensity, Falloff, RGB Intensity, stroke and alpha back to the values "
        "SculptForge ships with, and forget what Save Brush Settings stored for it.");
    add("brush.share_size", "Share size and intensity across brushes", "Preferences > Input", nullptr,
        [this] { setShareBrushSize(!brushSettings_.shareSizeIntensity); }, nullptr, nullptr,
        [this] { return brushSettings_.shareSizeIntensity; }, nullptr,
        "On: Draw Size and Z Intensity stay as they are when you pick another brush (as before 0.10.8); Falloff, "
        "RGB Intensity, the stroke and the alpha still follow each brush. Off (the default): every brush keeps its own.");
    add("brush.size_down", "Smaller brush", "Draw", "[", [this] { radiusPx_ = drawSizeStep(radiusPx_, -1, false); }, nullptr,
        nullptr, nullptr, nullptr, "Make the brush 10% smaller (hold to repeat; Shift+[ steps one pixel).");
    add("brush.size_up", "Larger brush", "Draw", "]", [this] { radiusPx_ = drawSizeStep(radiusPx_, 1, false); }, nullptr,
        nullptr, nullptr, nullptr, "Make the brush 10% larger (hold to repeat; Shift+] steps one pixel).");
    add("transform.symmetry_x", "Symmetry X", "Transform", "X", [this] { symmetry_.x = !symmetry_.x; }, ICON_LC_FLIP_HORIZONTAL_2,
        nullptr, [this] { return symmetry_.x; }, nullptr, "Mirror strokes across the X axis (left / right).");
    add("view.wireframe", "Wireframe", "Render", "Shift+F", [this] { renderSettings_.wireframe = !renderSettings_.wireframe; },
        ICON_LC_GRID_3X3, nullptr, [this] { return renderSettings_.wireframe; }, nullptr,
        "Show the mesh's polygons (quads and triangles) over the surface (ZBrush PolyFrame).");
    add("view.frame", "Frame model", "Transform", "F", [this] { camera_.frame(sceneBounds()); }, ICON_LC_SCAN, nullptr, nullptr,
        nullptr, "Fit the whole model in the view.");
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
                maskBrush_.on = false;
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
        "A step-by-step lesson for complete beginners: a little mushroom house from the default sphere, painted and "
        "rendered, in about ten minutes.");
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
        "SculptForge is free and stays free. Memberships add your name to the credits, a monthly brush and "
        "alpha pack and a roadmap vote: opens sculptforge.app/support.html in your browser. Nothing in the app changes either way.");
    add("help.discord", "Join the Discord...", "Help", nullptr, [this] { openDiscordPage(); }, ICON_LC_MESSAGE_CIRCLE,
        nullptr, nullptr, nullptr,
        "The official SculptForge community on Discord: questions, tips and work in progress. Opens the invite in "
        "your browser. Bugs still go to Report a problem (GitHub issues).");
    add("help.welcome", "Welcome", "Help", nullptr, [this] { showWelcome_ = true; }, ICON_LC_SPARKLES, nullptr, nullptr,
        nullptr, "The welcome note shown the first time SculptForge starts.");
    add("help.website", "Website & documentation", "Help", nullptr, [] { SDL_OpenURL("https://sculptforge.app/"); },
        ICON_LC_BOOK_OPEN, nullptr, nullptr, nullptr, "Open the SculptForge website and user guide in your browser.");
    add("help.report", "Report a problem", "Help", nullptr, [] { SDL_OpenURL("https://github.com/islandmed/SculptForge/issues"); },
        ICON_LC_CIRCLE_ALERT, nullptr, nullptr, nullptr, "Open the issue tracker in your browser.");
    add("help.log", "Open log folder", "Help", nullptr,
        [this] {
            const std::string url = crashlog::folderUrl();
            if (url.empty()) status_ = "There is no log file (the user data folder couldn't be written).";
            else if (!SDL_OpenURL(url.c_str()))
                status_ = "The log is " + crashlog::path() + " (couldn't open its folder: " + SDL_GetError() + ")";
        },
        ICON_LC_FILE_TEXT, nullptr, nullptr, nullptr,
        "Open the folder with sculptforge.log: what SculptForge did and, after a crash, what crashed. "
        "sculptforge-previous.log beside it is the last session's. Attach both to a problem report.");
    add("help.feedback", "Fixes & suggestions", "Help", nullptr,
        [] { SDL_OpenURL("https://github.com/islandmed/SculptForge/issues/new/choose"); }, ICON_LC_MESSAGE_SQUARE_PLUS,
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
            SDL_ShowOpenFileDialog(onUiFileChosen, &g_requests[kRunScript], window_, kScriptFilters, 2, nullptr, false);
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

    // --- Config (ZBrush: Preferences > Config) ---
    add("config.store", "Store Config", "Preferences > Config", "Ctrl+Shift+I", [this] { storeConfig(); }, ICON_LC_SAVE, nullptr,
        nullptr, nullptr,
        "Keep the current layout, shortcuts and interface preferences for future sessions. Like ZBrush, SculptForge never "
        "saves them on its own.");
    add("config.restore_standard", "Restore Standard UI", "Preferences > Config", nullptr, [this] { restoreStandardUi(); },
        ICON_LC_ROTATE_CCW, nullptr, nullptr, nullptr,
        "Go back to the built-in layout and default shortcuts. Store Config afterwards to make it stick.");
    add("config.restore_custom", "Restore Custom UI", "Preferences > Config", nullptr, [this] { restoreCustomUi(); },
        ICON_LC_ARCHIVE_RESTORE, nullptr, nullptr, [this] { return fileExists(uiConfigPath()); },
        "Go back to the layout and shortcuts saved by the last Store Config.");
    add("config.load", "Load UI...", "Preferences > Config", "Ctrl+L", [this] { loadUiDialog(); }, ICON_LC_FOLDER_INPUT, nullptr,
        nullptr, nullptr, "Load a layout file someone shared (or you saved with Save UI).");
    add("config.save", "Save UI...", "Preferences > Config", nullptr, [this] { saveUiDialog(); }, ICON_LC_FOLDER_OUTPUT, nullptr,
        nullptr, nullptr, "Save the layout, shortcuts and interface preferences to a file you can share.");
    add("config.customize", "Enable Customize", "Preferences > Config", nullptr,
        [this] {
            layout_.customize = !layout_.customize;
            status_ = layout_.customize ? "Customize: Ctrl+Alt+drag to move or remove items" : "Customize off";
        },
        ICON_LC_LAYOUT_TEMPLATE, nullptr, [this] { return layout_.customize; }, nullptr,
        "Rearrange the shelves: Ctrl+Alt+drag any button onto a shelf to add or move it, or off the shelves to remove it.");

    // The shell's actions (Shell.cpp: brushes, shelves, palettes, trays): they replace any of the above with
    // the same id and own P (Paint), the symmetry toggles and Tab.
    registerShellActions();
    registerTextureUiActions(); // Tool > Texture Map, the Vertex / Texture paint target (PanelTexture.cpp)
    registerPbrUiActions();     // material channels, solo view, map export (PanelPbr.cpp)
    registerGizmoActions();     // Q W E R: Draw, and the Gizmo 3D's Move, Scale, Rotate (Gizmo.cpp)
    registerBakeUiActions();    // Tool > Bake (PanelBake.cpp)
    registerUvEditorActions();  // UV Editor (PanelUvEditor.cpp)
    registerRetopoUiActions(); // Geometry > QRemesh, SubTool > ProjectAll (PanelRetopo.cpp)
    registerZSphereUiActions(); // Tool > ZSpheres (PanelZSpheres.cpp)
    registerSplatTrainUi();    // Splats > Train from capture (PanelSplatTrain.cpp)
    registerSculptableUiActions(); // Splats > Make sculptable (PanelSculptable.cpp)
    registerVdmActions();      // Brush > Vector Displacement (PanelVdm.cpp)
    registerKelvinletActions(); // Brush > Kelvinlet & Capture: Capture from view (PanelKelvinlet.cpp)
    registerFaceUiActions();   // Tool > Facial Shapes, face_take_play / face_take_stop (PanelFace.cpp)
    registerPerformanceUiActions(); // Preferences > Performance (PanelPerformance.cpp)
    registerInputActions();     // ZBrush keys: U I O, Space, L, V, Ctrl+A / I / H / W..., the input style (Input.cpp)
    registerTimelineUiActions(); // Sculpt Timeline, timelapse, Help > Benchmark (PanelTimeline.cpp)

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
    std::string dir;
    if (char* pref = SDL_GetPrefPath("SculptForge", "SculptForge")) {
        dir = pref;
        SDL_free(pref);
    }
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
    }
    Json j;
    j.set("w", double(st.w));
    j.set("h", double(st.h));
    if (st.placed) {
        j.set("x", double(st.x));
        j.set("y", double(st.y));
    }
    j.set("maximized", maximized);
    j.set("fullscreen", fullscreen);
    std::ofstream out(path);
    out << j.dump(true);
}

std::string App::uiConfigPath() const { return configDir_.empty() ? std::string() : configDir_ + "ui.json"; }

bool App::writeUiConfig(const std::string& path, std::string& err) {
    Json j = Json::object();
    j.set("version", 1.0);
    j.set("layout", toJson(layout_));
    j.set("shortcuts", actions_.overridesToJson());
    j.set("prefs", toJson(uiPrefs_));
    j.set("radialMenus", toJson(radialMenus_));
    std::ofstream f(fsPath(path), std::ios::binary | std::ios::trunc);
    f << j.dump(true) << "\n";
    if (!f) {
        err = "Could not write " + path;
        return false;
    }
    return true;
}

bool App::readUiConfig(const std::string& path, std::string& err) {
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
    if (!j.isObject() || !j["layout"].isObject()) {
        err = path + " is not a SculptForge UI configuration";
        return false;
    }
    // The prefs first: they name the shell layout, whose standard shelves fill in what the file didn't store.
    UiPrefs prefs = uiPrefs_;
    fromJson(j["prefs"], prefs);
    UiLayout layout;
    if (!fromJson(j["layout"], layout, err, prefs.shellLayout)) return false;
    uiPrefs_ = std::move(prefs);
    lookDirty_ = true;
    requestUiScale(); // its UI scale
    const bool customize = layout_.customize;
    layout_ = std::move(layout);
    layout_.customize = customize;
    actions_.overridesFromJson(j["shortcuts"]);
    if (j.has("radialMenus")) applyRadialMenusJson(j["radialMenus"]); // (an older file keeps the menus there are)
    setLanguage(languageFromCode(uiPrefs_.language));
    applyPerformancePrefs();
    applyInputStyle();
    doc_.sculptor.dyntopo.constantDetail = uiPrefs_.dyntopoConstantDetail;
    doc_.sculptor.dyntopo.smoothRemesh = uiPrefs_.dyntopoSmoothRemesh;
    renderSettings_.flatShading = uiPrefs_.flatShading;
    renderSettings_.backfaceCulling = uiPrefs_.backfaceCulling;
    renderSettings_.gradientBackground = uiPrefs_.gradientBackground;
    renderSettings_.backgroundColor = uiPrefs_.backgroundColor;
    return true;
}

void App::storeConfig() {
    std::string err;
    if (uiConfigPath().empty()) {
        status_ = "Can't store the UI configuration: no settings folder";
    } else if (!writeUiConfig(uiConfigPath(), err)) {
        status_ = err;
    } else {
        status_ = "UI configuration stored";
        layoutBaseline_ = layoutKey();
    }
}

void App::restoreStandardUi() {
    const bool customize = layout_.customize;
    layout_ = standardLayout(uiPrefs_.shellLayout);
    layout_.customize = customize;
    actions_.clearOverrides();
    resetRadialMenus();
    layoutBaseline_ = layoutKey();
    status_ = "Standard UI restored - Store Config (Ctrl+Shift+I) to keep it";
}

void App::restoreCustomUi() {
    std::string err;
    if (!fileExists(uiConfigPath())) {
        status_ = "No stored UI configuration yet - Store Config (Ctrl+Shift+I) first";
    } else if (!readUiConfig(uiConfigPath(), err)) {
        status_ = err;
    } else {
        layoutBaseline_ = layoutKey();
        status_ = "Custom UI restored";
    }
}

void App::saveUiDialog() {
    SDL_ShowSaveFileDialog(onUiFileChosen, &g_requests[kSaveUi], window_, kUiFilters, 1, nullptr);
}

void App::importBlenderKeymapDialog() {
    SDL_ShowOpenFileDialog(onUiFileChosen, &g_requests[kBlenderKeymap], window_, kBlenderKeymapFilters, 2, nullptr, false);
}

void App::importRadialMenuDialog() {
    SDL_ShowOpenFileDialog(onUiFileChosen, &g_requests[kRadialImport], window_, kRadialFilters, 1, nullptr, false);
}

void App::exportRadialMenuDialog(int menu) {
    radialEdit_.exportMenu = menu;
    SDL_ShowSaveFileDialog(onUiFileChosen, &g_requests[kRadialExport], window_, kRadialFilters, 1, nullptr);
}

void App::chooseMayaFolder() { // the only way the Maya bridge gets a folder: the user picks it (PanelMaya.cpp)
    const std::string start = uiPrefs_.mayaFolder;
    SDL_ShowOpenFolderDialog(onUiFileChosen, &g_requests[kMayaFolder], window_, start.empty() ? nullptr : start.c_str(),
                             false);
}

void App::loadUiDialog() {
    SDL_ShowOpenFileDialog(onUiFileChosen, &g_requests[kLoadUi], window_, kUiFilters, 1, nullptr, false);
}

void App::queueUiFile(int kind, std::string path) {
    std::lock_guard<std::mutex> lock(uiFileMutex_);
    uiFiles_.emplace_back(kind, std::move(path));
}

std::string App::layoutKey() const {
    UiLayout l = layout_; // what a session changes on its own doesn't count as a layout edit
    l.consoleOpen = false;
    l.consoleHeight = 0;
    l.lightboxOpen = false;
    l.lightboxTab.clear();
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
            status_ = "Hint hidden (Preferences > Hints shows it again; Store Config keeps it)";
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}

void App::drawHintsSection() {
    ui::Checkbox("Show hints in the viewport##hints.show", &uiPrefs_.showHints);
    ui::HelpText("Show hints in the viewport", nullptr,
                 "The short notes SculptForge shows over the 3D view (a mode that is on, the next step of a tool). Off "
                 "hides them all; each also has its own Don't show again. Store Config keeps it.",
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
    if (layoutHintShown_ || uiConfigDeterministic_ || ++layoutCheckFrame_ % 20 != 0) return;
    if (layoutKey() == layoutBaseline_) return;
    layoutHintShown_ = true; // once per session (ZBrush never saves the layout by itself)
    status_ = "Layout changed - Ctrl+Shift+I to keep it";
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
    status_ = "Press a key for '" + a->label + "' (Esc cancels, Backspace clears)";
}

bool App::captureShortcutKey(SDL_Keycode key, SDL_Keymod mod) {
    if (capturingShortcut_.empty()) return false;
    if (isModifierKey(key)) return true;
    const Action* a = actions_.find(capturingShortcut_);
    const std::string label = a ? a->label : capturingShortcut_;
    if (key == SDLK_ESCAPE) {
        status_ = "Shortcut unchanged";
    } else if (key == SDLK_BACKSPACE || key == SDLK_DELETE) {
        actions_.setShortcut(capturingShortcut_, "");
        status_ = label + ": no shortcut - Store Config (Ctrl+Shift+I) to keep it";
    } else {
        const std::string chord = chordText(chordFromEvent(key, mod));
        const std::string lost = actions_.assignShortcut(capturingShortcut_, chord);
        status_ = label + ": " + chord + (lost.empty() ? "" : " (was " + lost + ")") + " - Store Config (Ctrl+Shift+I) to keep it";
    }
    capturingShortcut_.clear();
    return true;
}

// ---------------------------------------------------------------------------
// Per-frame UI
// ---------------------------------------------------------------------------

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
                status_ = writeUiConfig(path, err) ? "UI layout saved to " + path : err;
            } else if (kind == kLoadUi) {
                if (readUiConfig(path, err)) {
                    status_ = "UI layout loaded - Store Config (Ctrl+Shift+I) to keep it";
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
                if (ensureUserLibrary(path, libErr)) { // the LightBox's User folder, from now on
                    library_.user = path;
                    libraryScanner_.invalidate();
                    status_ = "Library folder: " + path + " (Store Config keeps it)";
                } else {
                    status_ = libErr;
                }
            }
        }
    }
    console_.watchStatus(status_);
    if (layout_.consoleOpen) console_.draw(*this, consoleRect());
    palette_.draw(*this, paletteArea());
    if (showHelp_) drawControlsWindow();
    if (!capturingShortcut_.empty()) {
        const Action* a = actions_.find(capturingShortcut_);
        const CanvasRect area = paletteArea();
        const float s = uiScale_;
        ImGui::SetNextWindowPos(ImVec2(area.x + area.w * 0.5f, area.y + 60.0f * s), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f * s, 12.0f * s));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f * s);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, col(IM_COL32(0x18, 0x18, 0x1a, 0xf8)));
        ImGui::PushStyleColor(ImGuiCol_Border, col(IM_COL32(0xf0, 0x88, 0x2e, 0xa0)));
        if (ImGui::Begin("##shortcut_capture", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs)) {
            ImGui::PushFont(helpTitleFont());
            ImGui::Text(ICON_LC_KEYBOARD "  Press a key for '%s'", a ? a->label.c_str() : capturingShortcut_.c_str());
            ImGui::PopFont();
            const std::string now = actions_.shortcutText(capturingShortcut_);
            ImGui::TextColored(col(ui::color::textDim), "Now:");
            ImGui::SameLine();
            if (now.empty()) ImGui::TextColored(col(ui::color::textDim), "none");
            else ui::Keycaps(now);
            ImGui::TextColored(col(ui::color::textDim), "Esc cancels, Backspace clears. A key that's taken moves here.");
        }
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
    }
    watchLayoutChanges();
    ui::DragScrollPanels(); // #100: pen and middle drags scroll panels (after every window)
    ui::DrawHover(*this); // the hover and keyboard-focus rings, over every window
    ui::DrawHelp(*this); // last: on top of everything
}

void App::drawControlsWindow() {
    const float s = uiScale_;
    ImGui::SetNextWindowSize(ImVec2(560.0f * s, 640.0f * s), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Controls", &showHelp_, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    // The key style (Preferences > Input), and a keymap exported from Blender (#12).
    ImGui::TextDisabled("%s", tr("Style"));
    ImGui::SameLine();
    drawInputStyleRow(ImGui::GetContentRegionAvail().x * 0.6f);
    ImGui::SameLine();
    ui::ActionButton(*this, "input.import_blender", ImVec2(-FLT_MIN, 0), ui::Look::IconLabel);
    static char filter[96] = "";
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##controls_filter", ICON_LC_SEARCH "  Filter shortcuts and gestures", filter, sizeof filter);
    const std::string f = lower(filter);
    auto matches = [&](const std::string& a, const std::string& b, const std::string& c) {
        return f.empty() || lower(a).find(f) != std::string::npos || lower(b).find(f) != std::string::npos ||
               lower(c).find(f) != std::string::npos;
    };
    ImGui::Spacing();
    if (ImGui::BeginTabBar("##controls_tabs")) {
        if (ImGui::BeginTabItem(tr("Keyboard"))) {
            ImGui::BeginChild("##controls_keys", ImVec2(0, -ImGui::GetTextLineHeightWithSpacing() * 1.6f));
            // Grouped by palette, alphabetically (as ZBrush lists its palettes).
            std::map<std::string, std::vector<const Action*>> groups;
            for (const auto& a : actions_.all()) {
                const std::string chord = actions_.shortcutText(a.id);
                if (chord.empty() || !matches(a.label, a.palette, chord)) continue;
                const std::string top = a.palette.substr(0, a.palette.find(" > "));
                groups[top.empty() ? "General" : top].push_back(&a);
            }
            if (groups.empty()) ImGui::TextColored(col(ui::color::textDim), "No shortcut matches \"%s\".", filter);
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
            ImGui::TextColored(col(ui::color::textDim), "Double-click a row, or Ctrl+Alt+click any button, to assign a key.");
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
    if (ImGui::Checkbox(tr("Enable Customize"), &customize)) app.actions().find("config.customize")->run();
    ui::HelpAction(app, "config.customize");
    ui::ActionButton(app, "config.store", full, ui::Look::IconLabel);
    ui::ActionButton(app, "config.restore_custom", full, ui::Look::IconLabel);
    ui::ActionButton(app, "config.restore_standard", full, ui::Look::IconLabel);
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    ui::ActionButton(app, "config.save", ImVec2(half, 0), ui::Look::IconLabel);
    ImGui::SameLine();
    ui::ActionButton(app, "config.load", ImVec2(half, 0), ui::Look::IconLabel);
    ImGui::PushStyleColor(ImGuiCol_Text, col(ui::color::textDim));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(tr("Changes to the layout and shortcuts last until you quit. Store Config keeps them."));
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
        const std::string label = std::string(languageName(lang)) + "##language." + languageCode(lang);
        if (ImGui::RadioButton(label.c_str(), languageFromCode(p.language) == lang)) {
            p.language = languageCode(lang);
            setLanguage(lang);
        }
        ui::HelpText("Language", nullptr,
                     "The language of menus, palettes, shelves and messages. Brush and tool names (Quad Remesh, Forge Spheres, "
                     "Clay Block...) stay as they are. Store Config keeps it for the next start.",
                     "Preferences > Interface");
    }
    ImGui::Checkbox(tr("Popup Info (Ctrl+hover help)"), &p.popupInfo);
    ui::HelpText("Popup Info", nullptr,
                 "Hold Ctrl and point at any control to see what it does and its shortcut. Ctrl+Alt shows a command's "
                 "scripting parameters.",
                 "Preferences > Interface");
    ImGui::BeginDisabled(!p.popupInfo);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ui::SliderInside("##help_opacity", &p.helpOpacity, 0.3f, 1.0f, "Popup Info opacity %.2f");
    ui::HelpText("Popup Info opacity", nullptr, "How solid the Ctrl-help box is (ZBrush: Quick Info Opacity).",
                 "Preferences > Interface");
    ImGui::EndDisabled();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ui::SliderInside("##tooltip_delay", &p.tooltipDelay, 0.0f, 2.0f, "Tooltip delay %.1f s");
    ui::HelpText("Tooltip delay", nullptr,
                 "How long to point at an icon-only button (or a clipped label) before its name appears.",
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
    ImGui::Checkbox(tr("Palettes strictly A-Z (ZBrush order)"), &p.alphabeticalPalettes);
    ui::HelpText("Palettes strictly A-Z", nullptr,
                 "Off: File and Edit come first in the palette bar and Help last, the rest A to Z. On: every palette A to Z, as in "
                 "ZBrush.",
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
    static const char* kScales[] = {"Automatic", "100%", "125%", "150%", "175%", "200%"};
    static const float kScaleValues[] = {0.0f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f};
    int current = 0;
    for (int i = 0; i < 6; ++i)
        if (std::abs(kScaleValues[i] - p.uiScale) < 0.01f) current = i;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##ui_scale", (std::string(tr("UI scale:")) + " " + tr(kScales[current])).c_str())) {
        for (int i = 0; i < 6; ++i)
            if (ImGui::Selectable(tr(kScales[i]), i == current)) {
                p.uiScale = kScaleValues[i];
                app.requestUiScale(); // live, from the next frame (#87)
            }
        ImGui::EndCombo();
    }
    ui::HelpText("UI scale", nullptr,
                 "Size of text and controls, applied at once. Automatic follows the display the window is on (Windows: "
                 "Settings > Display > Scale, per monitor). Store Config keeps it for the next start.",
                 "Preferences > Interface");
    static const char* kRotation[] = {"Free (ZBrush)", "Turntable"};
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##rotation", (std::string(tr("Rotation:")) + " " + tr(kRotation[p.turntable ? 1 : 0])).c_str())) {
        for (int i = 0; i < 2; ++i)
            if (ImGui::Selectable(tr(kRotation[i]), (i == 1) == p.turntable)) p.turntable = i == 1;
        ImGui::EndCombo();
    }
    ui::HelpText("Rotation", nullptr,
                 "Free: dragging turns the model about the screen's axes and can roll it over the top, as in ZBrush; press "
                 "Shift while rotating to snap to the nearest front, side, top or bottom view. Turntable: the model stays "
                 "upright and turns about its vertical axis, as in Blender or Maya.",
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
    static char filter[64] = "";
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
        ImGui::TableSetupColumn("key", ImGuiTableColumnFlags_WidthFixed, 110.0f * s);
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
            if (ImGui::Button((text + "##hk." + a->id).c_str(), ImVec2(-FLT_MIN, 0))) app.beginShortcutCapture(a->id);
            if (reg.hasOverride(a->id)) ImGui::PopStyleColor();
            ui::HelpText((std::string(tr("Shortcut for")) + " " + trAction(a->id, a->label)).c_str(), chord.c_str(),
                         "Click, then press the new key. Esc cancels, Backspace clears. Orange: changed from the default.",
                         "Preferences > Hotkeys");
        }
        ImGui::EndTable();
    }
    if (ImGui::Button(tr(ICON_LC_ROTATE_CCW "  Default shortcuts"), ImVec2(-FLT_MIN, 0))) {
        reg.clearOverrides();
        app.status_ = "Default shortcuts restored - Store Config (Ctrl+Shift+I) to keep them";
    }
    ui::HelpText("Default shortcuts", nullptr, "Undo every shortcut change.", "Preferences > Hotkeys");
}

void drawLightBoxSection(App& app) {
    UiPrefs& p = app.uiPrefs();
    UiLayout& l = app.layout();
    ImGui::Checkbox(tr("Show at startup"), &p.lightboxAtStartup);
    ui::HelpText("Show Library at startup", nullptr, "Open Library on the Project tab when SculptForge starts (as ZBrush does).",
                 "Preferences > Library");
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("Rows"));
    for (int r = 1; r <= 4; ++r) {
        ImGui::SameLine();
        const bool on = l.lightboxRows == r;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, col(ui::color::accent));
        if (ImGui::Button((std::to_string(r) + "##lbrows").c_str(), ImVec2(ImGui::GetFrameHeight() * 1.3f, 0))) l.lightboxRows = r;
        if (on) ImGui::PopStyleColor();
        ui::HelpText("Library rows", nullptr, "How many rows of thumbnails the Library strip shows.", "Preferences > Library");
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("Placement"));
    ImGui::SameLine();
    if (ImGui::RadioButton(tr("Top"), !l.lightboxBottom)) l.lightboxBottom = false;
    ui::HelpText("Library at the top", nullptr, "Library opens along the top edge of the canvas.", "Preferences > Library");
    ImGui::SameLine();
    if (ImGui::RadioButton(tr("Bottom"), l.lightboxBottom)) l.lightboxBottom = true;
    ui::HelpText("Library at the bottom", nullptr, "Library opens above the bottom shelf.", "Preferences > Library");
    ImGui::TextColored(col(ui::color::textDim), "Library folder");
    const std::string shown = p.libraryFolder.empty() ? std::string("Documents/SculptForge (default)") : p.libraryFolder;
    if (ImGui::Button((std::string(ICON_LC_FOLDER_OPEN "  ") + shown + "##libfolder").c_str(), ImVec2(-FLT_MIN, 0)))
        SDL_ShowOpenFolderDialog(onUiFileChosen, &g_requests[kLibraryFolder], app.window_, nullptr, false);
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
    if (layout >= 0) // the shelf's tiles: one row, or two in the ZBrush-style layout
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
    if (ImGui::RadioButton(tr("ZBrush-style##layout.zbrush"), uiPrefs_.shellLayout == 0)) {
        setShellLayout(0);
        lookDirty_ = true;
    }
    ui::HelpText("ZBrush-style layout", nullptr, "The alphabetical palette bar and the two-row top shelf.",
                 "Preferences > Interface > Look");
    tray::GroupLabel(*this, "Interface");
}

} // namespace sf
