// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Default actions and shortcuts, Store Config / Restore / Save / Load UI,
// the Preferences > Config / Interface / Hotkeys / LightBox sections, the F1
// Controls sheet (generated from the action registry) and ZBrush-style
// shortcut assignment.
#include "app/Config.h"

#include "app/App.h"
#include "app/Ui.h"
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

namespace sf {

namespace assets {
extern const unsigned char fontSemiBold[];
extern const unsigned long long fontSemiBold_size;
extern const unsigned char fontLucide[];
extern const unsigned long long fontLucide_size;
} // namespace assets

Json toJson(const UiPrefs& p) {
    Json j = Json::object();
    j.set("popupInfo", p.popupInfo);
    j.set("helpOpacity", double(p.helpOpacity));
    j.set("tooltipDelay", double(p.tooltipDelay));
    j.set("oneSubpalette", p.oneSubpalette);
    j.set("autoCollapseLeft", p.autoCollapseLeft);
    j.set("autoCollapseRight", p.autoCollapseRight);
    j.set("uiScale", double(p.uiScale));
    j.set("lightboxAtStartup", p.lightboxAtStartup);
    j.set("libraryFolder", p.libraryFolder);
    j.set("cameraNotch", double(p.cameraNotch));
    j.set("turntable", p.turntable);
    j.set("inputStyle", std::string(p.inputStyle == 1 ? "sculptforge" : "zbrush"));
    j.set("alphabeticalPalettes", p.alphabeticalPalettes);
    j.set("tablet", toJson(p.tablet));
    j.set("antialiasing", double(p.antialiasing));
    j.set("fastNavigation", p.fastNavigation);
    j.set("multithreading", p.multithreading);
    j.set("dyntopoConstantDetail", p.dyntopoConstantDetail);
    return j;
}

void fromJson(const Json& j, UiPrefs& p) {
    if (!j.isObject()) return;
    p.popupInfo = j["popupInfo"].asBool(p.popupInfo);
    p.helpOpacity = std::clamp(float(j["helpOpacity"].asNumber(p.helpOpacity)), 0.3f, 1.0f);
    p.tooltipDelay = std::clamp(float(j["tooltipDelay"].asNumber(p.tooltipDelay)), 0.0f, 3.0f);
    p.oneSubpalette = j["oneSubpalette"].asBool(p.oneSubpalette);
    p.autoCollapseLeft = j["autoCollapseLeft"].asBool(p.autoCollapseLeft);
    p.autoCollapseRight = j["autoCollapseRight"].asBool(p.autoCollapseRight);
    p.uiScale = std::clamp(float(j["uiScale"].asNumber(p.uiScale)), 0.0f, 3.0f);
    p.lightboxAtStartup = j["lightboxAtStartup"].asBool(p.lightboxAtStartup);
    if (j["libraryFolder"].isString()) p.libraryFolder = j["libraryFolder"].asString();
    p.cameraNotch = std::clamp(int(j["cameraNotch"].asNumber(p.cameraNotch)), 0, 2);
    p.turntable = j["turntable"].asBool(p.turntable);
    if (j["inputStyle"].isString()) p.inputStyle = j["inputStyle"].asString() == "sculptforge" ? 1 : 0;
    p.alphabeticalPalettes = j["alphabeticalPalettes"].asBool(p.alphabeticalPalettes);
    fromJson(j["tablet"], p.tablet);
    const int aa = int(j["antialiasing"].asNumber(p.antialiasing));
    p.antialiasing = aa >= 8 ? 8 : aa >= 4 ? 4 : aa >= 2 ? 2 : 1;
    p.fastNavigation = j["fastNavigation"].asBool(p.fastNavigation);
    p.multithreading = j["multithreading"].asBool(p.multithreading);
    p.dyntopoConstantDetail = j["dyntopoConstantDetail"].asBool(p.dyntopoConstantDetail);
}

namespace {

enum UiFileKind { kSaveUi, kLoadUi, kRunScript, kLibraryFolder, kUiFileKinds };

struct UiFileRequest {
    App* app = nullptr;
    int kind = 0;
};
UiFileRequest g_requests[kUiFileKinds];

void SDLCALL onUiFileChosen(void* userdata, const char* const* files, int) {
    if (!files || !files[0]) return;
    auto* req = static_cast<UiFileRequest*>(userdata);
    req->app->queueUiFile(req->kind, files[0]);
}

const SDL_DialogFileFilter kUiFilters[] = {{"SculptForge UI layout", "json"}};
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
    {"Clear mask", "Ctrl + drag on the background (MaskPen)"},
    {"Mask a lasso or rectangle", "Ctrl + drag with MaskLasso / MaskRect (Tool > Masking; Ctrl+Alt unmasks)"},
    {"Isolate polygroup", "Ctrl+Shift + click it (again: invert)"},
    {"Hide polygroup", "Ctrl+Shift+Alt + click it"},
    {"Show all", "Ctrl+Shift + click the background"},
    {"Hide outside a rectangle or lasso", "Ctrl+Shift + drag (add Alt: inside; shape in Tool > Visibility)"},
    {"Select a SubTool", "Alt + click it"},
    {"Help on any control", "Hold Ctrl and point at it (Ctrl+Alt: scripting parameters)"},
    {"Assign a shortcut", "Ctrl+Alt + click any button, then press the key"},
    {"Tablet pressure", "Used automatically when a pen is detected (Preferences > Tablet: curve, test area)"},
    {"Pen eraser end", "Inverts the brush (Ctrl + eraser erases mask); Preferences > Tablet"},
    {"Pen side buttons", "Lower: right-click (drag to rotate), upper: Alt (invert); Preferences > Tablet"},
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
    layout_ = standardLayout();
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
    add("file.new_sphere", "New Sphere", "File", "Ctrl+N", [this] { confirmDiscard([this] { runCommand("new_sphere"); }); },
        ICON_LC_FILE_PLUS, "new_sphere");
    add("file.new_plane", "New Plane", "File", nullptr, [this] { confirmDiscard([this] { runCommand("new_plane"); }); }, nullptr,
        "new_plane");
    add("file.open", "Open...", "File", "Ctrl+O", [this] { confirmDiscard([this] { openFileDialog(); }); }, ICON_LC_FOLDER_OPEN,
        nullptr, nullptr, nullptr, "Open a project (.sfproj), a model (.obj, .glb, .fbx, .stl, .ply, .usd ...) or splats.");
    add("file.save", "Save", "File", "Ctrl+S", [this] { saveProjectAs(false); }, ICON_LC_SAVE, nullptr, nullptr, nullptr,
        "Save the project (.sfproj: every SubTool, layer, brush and setting). Asks for a name the first time.");
    add("file.save_as", "Save As...", "File", "Ctrl+Shift+S", [this] { saveProjectAs(true); }, nullptr, nullptr, nullptr, nullptr,
        "Save the project under a new name.");
    add("file.export_glb", "Export GLB...", "File > Export", "Ctrl+E", [this] { saveFileDialog(FileAction::ExportGlb); },
        ICON_LC_PACKAGE, nullptr, nullptr, nullptr,
        "Export the model as glTF binary, with sculpt layers as blend shapes (morph targets).");
    add("file.export_stl", "Export STL...", "File > Export", nullptr, [this] {
            static const SDL_DialogFileFilter filters[] = {{"STL", "stl"}, {"All files", "*"}};
            SDL_ShowSaveFileDialog(onGozFileChosen, &gozDialogs_[int(GozFile::ExportStl)], window_, filters, 2, nullptr);
        },
        nullptr, nullptr, nullptr, nullptr, "Save the visible SubTools as one binary STL, for 3D printing.");
    add("file.send_unreal", "Send to Unreal", "File > Export", "Ctrl+U", [this] { sendToUnreal(); }, ICON_LC_SEND, nullptr,
        nullptr, [this] { return !pendingUnreal_.valid(); },
        "Export a game-ready FBX and import it into the running Unreal Editor (Python remote execution).");
    // Blender bridge (PanelBlender.cpp, addons/blender).
    add("file.send_blender", "Send to Blender", "File > Blender", nullptr, [this] {
            runCommand("blender_send");
            blenderLastBeatNs_ = 0; // beat now: the add-on must see this SculptForge as running when Blender sends next
        },
        ICON_LC_SEND, "blender_send", nullptr, nullptr,
        "Send the visible SubTools back to Blender (the SculptForge add-on): objects that came from Blender update "
        "in place, keeping their transform, materials and modifiers; new SubTools become new objects.");
    add("file.receive_blender", "Receive from Blender", "File > Blender", nullptr, [this] { receiveFromBlender(); },
        nullptr, "blender_receive", nullptr, nullptr,
        "Load what Blender last sent with the add-on's Send to SculptForge. Each object updates its SubTool (undoable) "
        "or becomes a new one; this happens by itself while Receive automatically is on.");
    add("brush.paste", "Paste brush", "Brush", "Ctrl+V", [this] { pasteBrush(); }, ICON_LC_CLIPBOARD_PASTE, nullptr, nullptr,
        nullptr, "Add the brush on the clipboard: a brush code someone shared, or the text of a .sfbrush file.");
    add("brush.size_down", "Smaller brush", "Draw", "[", [this] { radiusPx_ = std::max(2.0f, radiusPx_ / 1.1f); }, nullptr,
        nullptr, nullptr, nullptr, "Make the brush 10% smaller (hold to repeat).");
    add("brush.size_up", "Larger brush", "Draw", "]", [this] { radiusPx_ = std::min(800.0f, radiusPx_ * 1.1f); }, nullptr,
        nullptr, nullptr, nullptr, "Make the brush 10% larger (hold to repeat).");
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
        add(id.c_str(), brushName(type), "Brush", key.c_str(),
            [this, type] {
                brush_ = type;
                doc_.activeBrush = -1;
                paintMode_ = false;
                guidesMode_ = false; // leaves the QRemesh Guides brush
            },
            ICON_LC_BRUSH, nullptr, [this, type] { return brush_ == type && doc_.activeBrush < 0 && !paintMode_ && !guidesMode_; }, nullptr,
            (std::string("Sculpt with the ") + brushName(type) + " brush.").c_str());
    }

    // --- Help ---
    add("help.controls", "Controls", "Help", "F1", [this] { showHelp_ = !showHelp_; }, ICON_LC_KEYBOARD, nullptr,
        [this] { return showHelp_; }, nullptr, "Every shortcut and mouse gesture, generated from the commands themselves.");
    add("help.guide", "User Guide", "Help", "Shift+F1", [this] { guide_.toggle(); }, ICON_LC_BOOK_OPEN_TEXT, nullptr,
        [this] { return guide_.isOpen(); }, nullptr,
        "The SculptForge user guide: every tool explained, with search. Hold Ctrl over a control and press F1 to open "
        "the chapter about it.");
    add("help.whats_new", "What's New", "Help", nullptr, [this] { guide_.open("whats-new.md"); }, ICON_LC_SPARKLES, nullptr,
        nullptr, nullptr, "What changed in this version and the ones before it (opens once by itself after an update).");
    add("help.check_updates", "Check for updates", "Help", nullptr, [this] { startUpdateCheck(true); }, ICON_LC_REFRESH_CW,
        nullptr, nullptr, [this] { return !updateJob_; },
        "Ask the SculptForge website whether a newer version is out. SculptForge also checks by itself once a day "
        "(Preferences > Updates).");
    add("help.about", "About SculptForge", "Help", nullptr, [this] { showAbout_ = true; }, ICON_LC_INFO, nullptr, nullptr, nullptr,
        "Version, licence and the open-source components SculptForge is built with.");
    add("help.website", "Website & documentation", "Help", nullptr, [] { SDL_OpenURL("https://sculptforge.app/"); },
        ICON_LC_BOOK_OPEN, nullptr, nullptr, nullptr, "Open the SculptForge website and user guide in your browser.");
    add("help.report", "Report a problem", "Help", nullptr, [] { SDL_OpenURL("https://github.com/islandmed/SculptForge/issues"); },
        ICON_LC_CIRCLE_ALERT, nullptr, nullptr, nullptr, "Open the issue tracker in your browser.");
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
    registerGizmoActions();     // Q W E R: Draw, and the Gizmo 3D's Move, Scale, Rotate (Gizmo.cpp)
    registerBakeUiActions();    // Tool > Bake (PanelBake.cpp)
    registerRetopoUiActions(); // Geometry > QRemesh, SubTool > ProjectAll (PanelRetopo.cpp)
    registerSplatTrainUi();    // Splats > Train from capture (PanelSplatTrain.cpp)
    registerSculptableUiActions(); // Splats > Make sculptable (PanelSculptable.cpp)
    registerVdmActions();      // Brush > Vector Displacement (PanelVdm.cpp)
    registerFaceUiActions();   // Tool > Facial Shapes, face_take_play / face_take_stop (PanelFace.cpp)
    registerPerformanceUiActions(); // Preferences > Performance (PanelPerformance.cpp)
    registerInputActions();     // ZBrush keys: U I O, Space, L, V, Ctrl+A / I / H / W..., the input style (Input.cpp)

    // Every command, so the command palette finds anything scriptable.
    actions_.registerCommands(
        commands_, [this](const std::string& name) { runCommand(name); },
        [this](const std::string& name) { consolePrefill(name + " "); });

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

std::string App::uiConfigPath() const { return configDir_.empty() ? std::string() : configDir_ + "ui.json"; }

bool App::writeUiConfig(const std::string& path, std::string& err) {
    Json j = Json::object();
    j.set("version", 1.0);
    j.set("layout", toJson(layout_));
    j.set("shortcuts", actions_.overridesToJson());
    j.set("prefs", toJson(uiPrefs_));
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
    UiLayout layout;
    if (!fromJson(j["layout"], layout, err)) return false;
    const bool customize = layout_.customize;
    layout_ = std::move(layout);
    layout_.customize = customize;
    actions_.overridesFromJson(j["shortcuts"]);
    fromJson(j["prefs"], uiPrefs_);
    applyPerformancePrefs();
    applyInputStyle();
    doc_.sculptor.dyntopo.constantDetail = uiPrefs_.dyntopoConstantDetail;
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
    layout_ = standardLayout();
    layout_.customize = customize;
    actions_.clearOverrides();
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
        if (ImGui::BeginTabItem("Keyboard")) {
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
        if (ImGui::BeginTabItem("Mouse & pen")) {
            if (ImGui::BeginTable("##gestures", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX)) {
                ImGui::TableSetupColumn("what", ImGuiTableColumnFlags_WidthFixed, 180.0f * s);
                ImGui::TableSetupColumn("how", ImGuiTableColumnFlags_WidthStretch);
                for (const auto& g : kGestures) {
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
    if (ImGui::Checkbox("Enable Customize", &customize)) app.actions().find("config.customize")->run();
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
    ImGui::TextUnformatted("Changes to the layout and shortcuts last until you quit. Store Config keeps them.");
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

void drawInterfaceSection(App& app) {
    UiPrefs& p = app.uiPrefs();
    ImGui::Checkbox("Popup Info (Ctrl+hover help)", &p.popupInfo);
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
    ImGui::Checkbox("One sub-palette open", &p.oneSubpalette);
    ui::HelpText("One sub-palette open", nullptr,
                 "Opening a sub-palette closes the others in that palette. Shift+click opens more than one.",
                 "Preferences > Interface");
    ImGui::Checkbox("Auto collapse left tray", &p.autoCollapseLeft);
    ui::HelpText("Auto collapse left tray", nullptr,
                 "Opening a palette in the left tray collapses the others when they would not fit.", "Preferences > Interface");
    ImGui::Checkbox("Auto collapse right tray", &p.autoCollapseRight);
    ui::HelpText("Auto collapse right tray", nullptr,
                 "Opening a palette in the right tray collapses the others when they would not fit.", "Preferences > Interface");
    ImGui::Checkbox("Palettes strictly A-Z (ZBrush order)", &p.alphabeticalPalettes);
    ui::HelpText("Palettes strictly A-Z", nullptr,
                 "Off: File and Edit come first in the palette bar and Help last, the rest A to Z. On: every palette A to Z, as in "
                 "ZBrush.",
                 "Preferences > Interface");
    ImGui::Checkbox("Left-handed layout", &app.layout().leftHanded);
    ui::HelpText("Left-handed layout", nullptr, "Mirror the shelves and trays: tools on the left, navigation on the right.",
                 "Preferences > Interface");
    {
        static const char* kTiles[] = {"Small", "Medium", "Large"};
        int& tiles = app.layout().shelfTiles;
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##shelf_tiles", (std::string("Shelf tiles: ") + kTiles[std::clamp(tiles, 0, 2)]).c_str())) {
            for (int i = 0; i < 3; ++i)
                if (ImGui::Selectable(kTiles[i], tiles == i)) tiles = i;
            ImGui::EndCombo();
        }
        ui::HelpText("Shelf tiles", nullptr, "How big the brush tiles on the bottom shelf are. Small leaves the most room for the model.",
                     "Preferences > Interface");
    }
    static const char* kScales[] = {"Automatic", "100%", "125%", "150%", "175%", "200%"};
    static const float kScaleValues[] = {0.0f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f};
    int current = 0;
    for (int i = 0; i < 6; ++i)
        if (std::abs(kScaleValues[i] - p.uiScale) < 0.01f) current = i;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##ui_scale", (std::string("UI scale: ") + kScales[current]).c_str())) {
        for (int i = 0; i < 6; ++i)
            if (ImGui::Selectable(kScales[i], i == current)) p.uiScale = kScaleValues[i];
        ImGui::EndCombo();
    }
    ui::HelpText("UI scale", nullptr,
                 "Size of text and controls. Automatic follows the display. Takes effect the next time SculptForge starts "
                 "(Store Config to keep it).",
                 "Preferences > Interface");
    static const char* kRotation[] = {"Free (ZBrush)", "Turntable"};
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##rotation", (std::string("Rotation: ") + kRotation[p.turntable ? 1 : 0]).c_str())) {
        for (int i = 0; i < 2; ++i)
            if (ImGui::Selectable(kRotation[i], (i == 1) == p.turntable)) p.turntable = i == 1;
        ImGui::EndCombo();
    }
    ui::HelpText("Rotation", nullptr,
                 "Free: dragging turns the model about the screen's axes and can roll it over the top, as in ZBrush; press "
                 "Shift while rotating to snap to the nearest front, side, top or bottom view. Turntable: the model stays "
                 "upright and turns about its vertical axis, as in Blender or Maya.",
                 "Preferences > Interface");
    static const char* kNotch[] = {"Automatic", "Always reserve", "Off"};
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##camera_notch", (std::string("Camera notch: ") + kNotch[std::clamp(p.cameraNotch, 0, 2)]).c_str())) {
        for (int i = 0; i < 3; ++i)
            if (ImGui::Selectable(kNotch[i], i == p.cameraNotch)) p.cameraNotch = i;
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
    ImGui::InputTextWithHint("##hotkeys_filter", ICON_LC_SEARCH "  Filter", filter, sizeof filter);
    const std::string f = lower(filter);
    ActionRegistry& reg = app.actions();
    const float s = ImGui::GetFontSize() / 15.0f;
    std::vector<const Action*> list;
    for (const auto& a : reg.all()) {
        if (!a.run || a.id.rfind("cmd.", 0) == 0) continue; // commands: through the command palette
        if (!f.empty() && lower(a.label).find(f) == std::string::npos && lower(a.palette).find(f) == std::string::npos) continue;
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
            ImGui::TextUnformatted(a->label.c_str());
            ui::HelpAction(app, a->id.c_str());
            ImGui::TableNextColumn();
            const std::string chord = reg.shortcutText(a->id);
            const bool capturing = app.capturingShortcut() == a->id;
            const std::string text = capturing ? "press a key..." : chord.empty() ? "-" : chord;
            if (reg.hasOverride(a->id)) ImGui::PushStyleColor(ImGuiCol_Text, col(ui::color::accent));
            if (ImGui::Button((text + "##hk." + a->id).c_str(), ImVec2(-FLT_MIN, 0))) app.beginShortcutCapture(a->id);
            if (reg.hasOverride(a->id)) ImGui::PopStyleColor();
            ui::HelpText(("Shortcut for " + a->label).c_str(), chord.c_str(),
                         "Click, then press the new key. Esc cancels, Backspace clears. Orange: changed from the default.",
                         "Preferences > Hotkeys");
        }
        ImGui::EndTable();
    }
    if (ImGui::Button(ICON_LC_ROTATE_CCW "  Default shortcuts", ImVec2(-FLT_MIN, 0))) {
        reg.clearOverrides();
        app.status_ = "Default shortcuts restored - Store Config (Ctrl+Shift+I) to keep them";
    }
    ui::HelpText("Default shortcuts", nullptr, "Undo every shortcut change.", "Preferences > Hotkeys");
}

void drawLightBoxSection(App& app) {
    UiPrefs& p = app.uiPrefs();
    UiLayout& l = app.layout();
    ImGui::Checkbox("Show at startup", &p.lightboxAtStartup);
    ui::HelpText("Show LightBox at startup", nullptr, "Open LightBox on the Project tab when SculptForge starts (as ZBrush does).",
                 "Preferences > LightBox");
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Rows");
    for (int r = 1; r <= 4; ++r) {
        ImGui::SameLine();
        const bool on = l.lightboxRows == r;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, col(ui::color::accent));
        if (ImGui::Button((std::to_string(r) + "##lbrows").c_str(), ImVec2(ImGui::GetFrameHeight() * 1.3f, 0))) l.lightboxRows = r;
        if (on) ImGui::PopStyleColor();
        ui::HelpText("LightBox rows", nullptr, "How many rows of thumbnails the LightBox strip shows.", "Preferences > LightBox");
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Placement");
    ImGui::SameLine();
    if (ImGui::RadioButton("Top", !l.lightboxBottom)) l.lightboxBottom = false;
    ui::HelpText("LightBox at the top", nullptr, "LightBox opens along the top edge of the canvas.", "Preferences > LightBox");
    ImGui::SameLine();
    if (ImGui::RadioButton("Bottom", l.lightboxBottom)) l.lightboxBottom = true;
    ui::HelpText("LightBox at the bottom", nullptr, "LightBox opens above the bottom shelf.", "Preferences > LightBox");
    ImGui::TextColored(col(ui::color::textDim), "Library folder");
    const std::string shown = p.libraryFolder.empty() ? std::string("Documents/SculptForge (default)") : p.libraryFolder;
    if (ImGui::Button((std::string(ICON_LC_FOLDER_OPEN "  ") + shown + "##libfolder").c_str(), ImVec2(-FLT_MIN, 0)))
        SDL_ShowOpenFolderDialog(onUiFileChosen, &g_requests[kLibraryFolder], app.window_, nullptr, false);
    ui::HelpText("Library folder", nullptr,
                 "Where your projects, tools, brushes, alphas, images, splats and QuickSaves live. LightBox shows it as User.",
                 "Preferences > LightBox");
    if (!p.libraryFolder.empty() && ImGui::SmallButton("Use the default folder")) p.libraryFolder.clear();
}

} // namespace config

} // namespace sf
