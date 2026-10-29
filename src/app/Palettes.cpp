// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The palettes of the palette bar (docs/ui-design.md §3.2), listed
// alphabetically like ZBrush's. Each body is one function, drawn the same way
// in a drop-down and in a tray. ZBrush's Tool palette holds the SubTool,
// Layers, Geometry, Remesh, Masking, Polygroups, Visibility, Export and
// Display sub-palettes.
#include "app/App.h"

#include "app/Config.h"
#include "app/Demo.h"
#include "app/Palettes.h"
#include "app/Shell.h"
#include "app/Theme.h"
#include "app/Ui.h"
#include "core/Matcap.h"

#include <IconsLucide.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <string>

namespace sf {

const PaletteDef* palettes(int& count) {
    // Sorted by name; a new palette lands in alphabetical order by being added in its place here.
    static const PaletteDef kPalettes[] = {
        {"alpha", "Alpha", ICON_LC_CIRCLE_DASHED, nullptr, &App::drawPalette_Alpha},
        {"brush", "Brush", ICON_LC_BRUSH, nullptr, &App::drawPalette_Brush},
        {"color", "Color", ICON_LC_PALETTE, "Polypaint & Materials", &App::drawPalette_Color},
        {"draw", "Draw", ICON_LC_PENCIL, nullptr, &App::drawPalette_Draw},
        {"edit", "Edit", ICON_LC_UNDO_2, nullptr, &App::drawPalette_Edit},
        {"file", "File", ICON_LC_FILE, nullptr, &App::drawPalette_File},
        {"generate", "Generate", ICON_LC_WAND_SPARKLES, nullptr, &App::drawPalette_Generate},
        {"help", "Help", ICON_LC_CIRCLE_HELP, nullptr, &App::drawPalette_Help},
        {"material", "Material", ICON_LC_SUN, "Polypaint & Materials", &App::drawPalette_Material},
        {"preferences", "Preferences", ICON_LC_SETTINGS, nullptr, &App::drawPalette_Preferences},
        {"render", "Render", ICON_LC_EYE, nullptr, &App::drawPalette_Render},
        {"script", "Script", ICON_LC_SQUARE_TERMINAL, nullptr, &App::drawPalette_Script},
        {"splats", "Splats", ICON_LC_SPARKLE, "Gaussian Splats", &App::drawPalette_Splats},
        {"stroke", "Stroke", ICON_LC_SPLINE, nullptr, &App::drawPalette_Stroke},
        {"tool", "Tool", ICON_LC_BOXES, nullptr, &App::drawPalette_Tool},
        {"transform", "Transform", ICON_LC_MOVE_3D, nullptr, &App::drawPalette_Transform},
    };
    count = int(std::size(kPalettes));
    return kPalettes;
}

const PaletteDef* findPalette(std::string_view id) {
    int count = 0;
    const PaletteDef* list = palettes(count);
    for (int i = 0; i < count; ++i)
        if (id == list[i].id) return &list[i];
    return nullptr;
}

namespace {

// The hairline under each sub-palette header: opaque, a clear step above panelBg so the closed list reads as rows.
// Should become a theme token (Theme.h).
constexpr ImU32 kSectionRule = IM_COL32(54, 54, 60, 255);
constexpr float kSectionHeaderH = 22.0f; // sub-palette header height, unscaled
constexpr float kSectionInset = 8.0f;    // header titles and section content start this far in, unscaled
// Check marks in the palettes: the text colour, so a column of options doesn't read as a row of accents.
constexpr ImVec4 kCheckMark = theme::rgb(0xd8d8d8);

// A row of equal-width action buttons filling the line.
void actionRow(App& app, std::initializer_list<const char*> ids) {
    std::vector<const char*> present;
    for (const char* id : ids)
        if (app.actions().find(id)) present.push_back(id);
    if (present.empty()) return;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float w = (ImGui::GetContentRegionAvail().x - spacing * float(present.size() - 1)) / float(present.size());
    for (size_t i = 0; i < present.size(); ++i) {
        if (i) ImGui::SameLine();
        const Action* a = app.actions().find(present[i]);
        ui::ActionButton(app, present[i], ImVec2(w, 0), a && a->icon ? ui::Look::IconLabel : ui::Look::Label);
    }
}

void segmented(App& app, std::initializer_list<const char*> ids, float width = -1.0f) {
    ui::Segmented(app, ids, width);
}

// A menu-style row for an action: label, shortcut on the right, check mark for toggles.
void menuAction(App& app, const char* id) {
    const Action* a = app.actions().find(id);
    if (!a) return;
    const std::string shortcut = app.actions().shortcutText(id);
    const bool enabled = !a->enabled || a->enabled();
    const bool checked = a->checked && a->checked();
    const std::string label = a->label + "##" + a->id; // no icons: they'd misalign with the plain project items
    if (ImGui::MenuItem(label.c_str(), shortcut.empty() ? nullptr : shortcut.c_str(), checked, enabled) && a->run) {
        app.actions().touch(a->id);
        a->run();
    }
    ui::HelpAction(app, id);
}

// Helper text under a palette's controls: small and dim, so it doesn't compete with them.
void hint(const App& app, const char* text) {
    if (app.smallFont()) ImGui::PushFont(app.smallFont());
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
    if (app.smallFont()) ImGui::PopFont();
}

} // namespace

namespace tray {

Section::Section() { ImGui::Indent(kSectionInset * ImGui::GetFontSize() / 15.0f); }

Section::~Section() {
    const float s = ImGui::GetFontSize() / 15.0f;
    ImGui::Unindent(kSectionInset * s);
    ImGui::Dummy(ImVec2(0, 4.0f * s));
}

Style::Style() { ImGui::PushStyleColor(ImGuiCol_CheckMark, kCheckMark); }

Style::~Style() { ImGui::PopStyleColor(); }

void GroupLabel(const App& app, const char* text) {
    const float s = ImGui::GetFontSize() / 15.0f;
    ImGui::Dummy(ImVec2(0, 2.0f * s));
    if (app.smallFont()) ImGui::PushFont(app.smallFont());
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::PushStyleColor(ImGuiCol_Separator, ImGui::ColorConvertU32ToFloat4(kSectionRule));
    ImGui::PushStyleVar(ImGuiStyleVar_SeparatorTextPadding, ImVec2(0.0f, 2.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_SeparatorTextAlign, ImVec2(0.0f, 0.5f));
    ImGui::SeparatorText(text);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    if (app.smallFont()) ImGui::PopFont();
}

bool SliderInt(const char* id, int* v, int lo, int hi, const char* format, ImGuiSliderFlags flags) {
    // The same widget as the float sliders: the value as a float shown with no decimals, rounded back.
    std::string fmt = format ? format : "%d";
    if (const size_t at = fmt.find("%d"); at != std::string::npos) fmt.replace(at, 2, "%.0f");
    float f = float(*v);
    if (!ui::SliderInside(id, &f, float(lo), float(hi), fmt.c_str(), flags)) return false;
    const int next = std::clamp(int(std::lround(f)), lo, hi);
    if (next == *v) return false;
    *v = next;
    return true;
}

} // namespace tray

bool App::subpalette(const char* id, const char* title) {
    auto& open = layout_.openSubpalettes;
    bool isOpen = std::find(open.begin(), open.end(), id) != open.end();
    const float s = uiScale_;
    const float h = kSectionHeaderH * s;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    const bool clicked = ImGui::InvisibleButton((std::string(title) + "##" + id).c_str(), ImVec2(w, h));
    const bool hovered = ImGui::IsItemHovered();
    // A quiet section header in the palette header's grammar: title at the left, chevron at the right. No fill until
    // hovered or open, a hairline under it. The title lines up with the section's content (tray::Section). The accent
    // is kept for the current tool (docs/ui-design.md §10), so an open section shows by its fill and text only.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (isOpen || hovered)
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), theme::u32(hovered ? theme::headerHover : theme::header, isOpen ? 0.9f : 0.6f),
                          2.0f * s);
    dl->AddLine(ImVec2(p.x, p.y + h - 0.5f * s), ImVec2(p.x + w, p.y + h - 0.5f * s), kSectionRule, 1.0f * s);
    ImFont* font = ImGui::GetFont();
    const float chevronSize = ImGui::GetFontSize() * 0.8f;
    const char* chevron = isOpen ? ICON_LC_CHEVRON_DOWN : ICON_LC_CHEVRON_RIGHT;
    const ImVec2 cs = font->CalcTextSizeA(chevronSize, FLT_MAX, 0.0f, chevron);
    dl->AddText(font, chevronSize, ImVec2(std::floor(p.x + w - h * 0.5f - cs.x * 0.5f), std::floor(p.y + (h - cs.y) * 0.5f)),
                theme::u32(isOpen || hovered ? theme::text : theme::textDim, isOpen ? 0.9f : 1.0f), chevron);
    const float ty = std::floor(p.y + (h - ImGui::GetFontSize()) * 0.5f);
    dl->AddText(ImVec2(p.x + kSectionInset * s, ty), theme::u32(theme::text, isOpen || hovered ? 1.0f : 0.78f), title);
    ImGui::LogText("%s", title);
    ui::HelpText(title, nullptr,
                 "Click to open or close. Opening one closes the others (Shift+click keeps them open; Preferences > "
                 "Interface).",
                 "Tool");
    if (clicked) {
        if (isOpen) {
            open.erase(std::remove(open.begin(), open.end(), id), open.end());
        } else {
            if (uiPrefs_.oneSubpalette && !(SDL_GetModState() & SDL_KMOD_SHIFT)) {
                // Close the siblings: sub-palettes that share this one's palette prefix ("tool.").
                const std::string prefix = std::string(id).substr(0, std::string(id).find('.') + 1);
                open.erase(std::remove_if(open.begin(), open.end(), [&](const std::string& o) { return o.rfind(prefix, 0) == 0; }),
                           open.end());
            }
            open.push_back(id);
        }
        isOpen = !isOpen;
    }
    if (isOpen) ImGui::Dummy(ImVec2(0, 2.0f * s));
    return isOpen;
}

// ---------------------------------------------------------------------------

void App::drawPalette_Alpha() { drawAlphaSection(); }

void App::drawPalette_Brush() {
    const tray::Style style; // quiet check marks
    // Your brushes first (PanelBrushLibrary.cpp: tags, and the right-click menu to share, rename or delete),
    // then every brush as thumbnails (Pickers.cpp, the same grid as the B picker).
    actionRow(*this, {"ui.brush_lab", "ui.brush_gallery"});
    actionRow(*this, {"brush.paste"});
    drawCustomBrushes();
    ImGui::Checkbox("Front faces only", &frontFacesOnly_);
    ui::HelpAction(*this, "draw.front_faces");
    ImGui::SameLine();
    ImGui::Checkbox("Invert (Alt)", &invertToggle_);
    ui::HelpText("Invert", "Alt", "Brushes subtract instead of add (ZBrush Zsub). Holding Alt flips it for one stroke.", "Brush");
    if (BrushRecipe* recipe = doc_.activeBrush >= 0 && doc_.activeBrush < int(doc_.brushes.size())
                                  ? &doc_.brushes[size_t(doc_.activeBrush)]
                                  : nullptr) {
        if (subpalette("brush.recipe", ("Edit recipe: " + recipe->name).c_str())) {
            tray::Section section;
            drawRecipeEditor(*recipe);
            if (!pendingRename_.empty()) {
                runCommand("brush_rename", {{"to", pendingRename_}});
                pendingRename_.clear();
            }
        }
    }
    if (subpalette("brush.vdm", "Vector Displacement")) {
        tray::Section section;
        drawVdmSection(); // PanelVdm.cpp
    }
    drawBrushGrid(*this, GridMode::Palette);
}

void App::drawPalette_Draw() {
    const tray::Style style; // quiet check marks
    ImGui::SetNextItemWidth(-1);
    ui::SliderInside("##drawsize", &radiusPx_, 2.0f, 800.0f, "Draw Size %.0f", ImGuiSliderFlags_Logarithmic);
    ui::HelpText("Draw Size", "S", "Brush radius in screen pixels. [ and ] change it by 10%.", "Draw");
    ImGui::SetNextItemWidth(-1);
    drawZIntensitySlider("##strength");
    ImGui::SetNextItemWidth(-1);
    float rgb = rgbIntensity_ * 100.0f;
    if (ui::SliderInside("##rgbintensity", &rgb, 1.0f, 100.0f, "RGB Intensity %.0f")) rgbIntensity_ = rgb / 100.0f;
    ui::HelpText("RGB Intensity", "I", "How strongly painting colours the surface (1-100); Ctrl+click to type.", "Draw");
    ImGui::SetNextItemWidth(-1);
    float focal = focalShift_ * 100.0f;
    if (ui::SliderInside("##focalshift", &focal, -100.0f, 100.0f, "Focal Shift %.0f")) focalShift_ = focal / 100.0f;
    ui::HelpText("Focal Shift", "O",
                 "The brush falloff: below 0 a broad, hard tip, above 0 a soft point. The brush cursor's inner ring shows "
                 "where the brush works at half strength.",
                 "Draw");
    segmented(*this, {"draw.sculpt", "draw.paint"});
    segmented(*this, {"draw.zadd", "draw.zsub"});
    actionRow(*this, {"draw.rgb", "draw.front_faces"});
    tray::GroupLabel(*this, "Dynamic topology");
    actionRow(*this, {"draw.dynamic"});
    ImGui::SetNextItemWidth(-1);
    drawDetailSlider("##dyntopo_detail_palette");
    if (ImGui::Checkbox("Constant detail", &uiPrefs_.dyntopoConstantDetail))
        doc_.sculptor.dyntopo.constantDetail = uiPrefs_.dyntopoConstantDetail;
    ui::HelpText("Constant detail", nullptr,
                 "Off (the default): the triangle size follows the brush size on screen, as in Sculptris Pro. On: one "
                 "size for the whole mesh, set by Detail. A preference (Store Config keeps it).",
                 "Draw > Dynamic topology");
    if (doc_.sculptor.dyntopo.enabled && !dyntopoBlocker(doc_.mesh).empty())
        hint(*this, "This SubTool has subdivision levels, so strokes don't remesh it: delete the lower (or higher) levels "
             "first.");
    tray::GroupLabel(*this, "Tablet");
    ImGui::Checkbox("Pressure > strength", &pressureStrength_);
    ui::HelpAction(*this, "draw.pressure_strength");
    ImGui::Checkbox("Pressure > size", &pressureSize_);
    ui::HelpAction(*this, "draw.pressure_size");
    if (penSeen_) ImGui::Text("Pen pressure: %.2f", uiPrefs_.tablet.curve.apply(penTracker_.raw()));
    else ImGui::TextDisabled("No pen detected");
}

void App::drawPalette_Edit() {
    const std::string undoText = doc_.undo.canUndo() ? "Undo " + doc_.undo.undoLabel() : std::string("Undo");
    const std::string redoText = doc_.undo.canRedo() ? "Redo " + doc_.undo.redoLabel() : std::string("Redo");
    ImGui::BeginDisabled(!doc_.undo.canUndo());
    if (ImGui::MenuItem((undoText + "##edit.undo").c_str(), "Ctrl+Z")) runCommand("undo");
    ImGui::EndDisabled();
    ui::HelpAction(*this, "edit.undo");
    ImGui::BeginDisabled(!doc_.undo.canRedo());
    if (ImGui::MenuItem((redoText + "##edit.redo").c_str(), "Ctrl+Shift+Z")) runCommand("redo");
    ImGui::EndDisabled();
    ui::HelpAction(*this, "edit.redo");
    menuAction(*this, "brush.paste");
}

void App::drawPalette_File() {
    menuAction(*this, "file.new_sphere");
    menuAction(*this, "file.new_plane");
    ImGui::Separator();
    menuAction(*this, "file.open");
    drawFileMenuProjectItems(); // Open Recent, Save, Save As, autosave
    tray::GroupLabel(*this, "Import / Export");
    menuAction(*this, "file.import");
    menuAction(*this, "file.export_obj");
    menuAction(*this, "file.export_stl");
    menuAction(*this, "file.export_glb");
    menuAction(*this, "file.export_lods");
    menuAction(*this, "file.export_usd");
    menuAction(*this, "file.export_fbx");
    menuAction(*this, "file.send_unreal");
    menuAction(*this, "file.save_splats");
    ImGui::Separator();
    drawGozMenu();
    drawBlenderMenu(); // the Blender bridge (PanelBlender.cpp)
    menuAction(*this, "file.capture_phone"); // the phone link (PanelPhone.cpp)
    ImGui::Separator();
    menuAction(*this, "file.quit");
}

void App::drawPalette_Generate() {
    const tray::Style style; // quiet check marks
    actionRow(*this, {"ui.generate3d"});
    hint(*this, "Image or text to a sculptable 3D model: Hugging Face (free), fal.ai, Meshy, your ComfyUI server, your own "
         "command, or an offline blockout.");
    tray::GroupLabel(*this, "From an image");
    if (ImGui::Button(refImage_.empty() ? "Choose image...##generate" : "Change image...##generate", ImVec2(-1, 0))) {
        static const SDL_DialogFileFilter filters[] = {{"Images", "png;jpg;jpeg;bmp;tga;psd;gif"}, {"All files", "*"}};
        SDL_ShowOpenFileDialog(
            [](void* user, const char* const* files, int) {
                if (files && files[0]) static_cast<App*>(user)->queueFile(FileAction::OpenImage, files[0]);
            },
            this, window_, filters, 2, nullptr, false);
    }
    ui::HelpText("Choose image", nullptr, "A sketch, concept, logo, photo or height map (or drop one on the window).", "Generate");
    ImGui::BeginDisabled(refImage_.empty());
    const float third = (ImGui::GetContentRegionAvail().x - 2.0f * ImGui::GetStyle().ItemSpacing.x) / 3.0f;
    if (ImGui::Button("Relief", ImVec2(third, 0)))
        runCommand("image_relief", {{"path", refImagePath_},
                                    {"depth", double(reliefDepth_)},
                                    {"resolution", double(reliefDetail_)},
                                    {"invert", reliefInvert_}});
    ui::HelpText("Relief", nullptr, "Light areas rise: logos, coins, emblems, height maps.", "Generate");
    ImGui::SameLine();
    if (ImGui::Button("Inflate", ImVec2(third, 0)))
        runCommand("image_inflate", {{"path", refImagePath_},
                                     {"thickness", double(inflateThickness_)},
                                     {"resolution", double(inflateDetail_)},
                                     {"subdivisions", 2.0}});
    ui::HelpText("Inflate", nullptr, "Inflates the outline into a rounded blockout.", "Generate");
    ImGui::SameLine();
    if (ImGui::Button("Stamp", ImVec2(third, 0))) projectImageFromView();
    ui::HelpText("Stamp", nullptr, "Projects the image onto the model from this view as surface detail.", "Generate");
    ImGui::EndDisabled();
    if (subpalette("generate.ai", "AI brush provider")) {
        tray::Section section;
        drawAiSettings();
    }
}

void App::drawPalette_Help() {
    menuAction(*this, "help.controls");
    menuAction(*this, "help.guide");
    menuAction(*this, "help.whats_new");
    menuAction(*this, "help.check_updates");
    menuAction(*this, "ui.command_palette");
    menuAction(*this, "help.website");
    menuAction(*this, "help.report");
    menuAction(*this, "help.feedback");
    ImGui::Separator();
    menuAction(*this, "help.about");
}

void App::drawPalette_Preferences() {
    const tray::Style style; // quiet check marks
    // The sub-palettes come from Config.cpp (Store / Restore UI, interface preferences, hotkeys, LightBox).
    if (subpalette("preferences.config", "Config")) {
        tray::Section section;
        config::drawConfigSection(*this);
        ImGui::TextDisabled("Layout");
        for (int i = 1; i <= 3; ++i) {
            ImGui::SameLine();
            ui::ActionButton(*this, ("ui.preset." + std::to_string(i)).c_str(), ImVec2(ImGui::GetFrameHeight() * 1.4f, 0));
        }
    }
    if (subpalette("preferences.interface", "Interface")) {
        tray::Section section;
        config::drawInterfaceSection(*this);
    }
    if (subpalette("preferences.input", "Input")) {
        tray::Section section;
        drawInputSection();
    }
    if (subpalette("preferences.hotkeys", "Hotkeys")) {
        tray::Section section;
        config::drawHotkeysSection(*this);
    }
    if (subpalette("preferences.lightbox", "LightBox")) {
        tray::Section section;
        config::drawLightBoxSection(*this);
    }
    if (subpalette("preferences.tablet", "Tablet")) {
        tray::Section section;
        drawTabletSection();
    }
    if (subpalette("preferences.performance", "Performance")) {
        tray::Section section;
        drawPerformanceSection();
    }
    if (subpalette("preferences.updates", "Updates")) {
        tray::Section section;
        drawUpdatesSection();
    }
    if (subpalette("preferences.autosave", "Autosave")) {
        tray::Section section;
        ImGui::SetNextItemWidth(-1);
        tray::SliderInt("##autosave", &autosaveMinutes_, 0, 60, autosaveMinutes_ ? "Every %d min" : "Autosave off");
        if (ImGui::IsItemDeactivatedAfterEdit()) saveProjectPrefs();
        ui::HelpText("Autosave", nullptr, "Save a recovery copy this often (0 = off). Restored after a crash.",
                     "Preferences > Autosave");
    }
}

void App::drawPalette_Render() {
    const tray::Style style; // quiet check marks
    ImGui::Checkbox("Wireframe", &renderSettings_.wireframe);
    ui::HelpAction(*this, "view.wireframe");
    ImGui::Checkbox("Show Mesh", &renderSettings_.showMesh);
    ui::HelpAction(*this, "view.mesh");
    ImGui::Checkbox("Show Polypaint", &renderSettings_.showPolypaint);
    ui::HelpAction(*this, "view.polypaint");
    ImGui::Checkbox("Show Mask", &renderSettings_.showMask);
    ui::HelpAction(*this, "view.mask");
    ImGui::Checkbox("Show Polygroups##render", &renderSettings_.showPolygroups);
    ui::HelpAction(*this, "view.polygroups");
    ImGui::BeginDisabled(doc_.splats.empty());
    ImGui::Checkbox("Show Splats", &renderSettings_.showSplats);
    ImGui::EndDisabled();
    ui::HelpAction(*this, "view.splats");
    actionRow(*this, {"view.frame", "view.fullscreen"});
}

void App::drawPalette_Script() {
    const bool any = actions_.find("ui.console") || actions_.find("script.run_file");
    actionRow(*this, {"ui.console"});
    actionRow(*this, {"script.run_file"});
    actionRow(*this, {"script.copy_session", "script.clear_console"});
    if (!any)
        hint(*this, "Every action is a script command: run them with --script \"subdivide; remesh resolution=200\" or the "
             "sculptforge-cli tool.");
}

void App::drawPalette_Stroke() { drawStrokeGrid(*this, GridMode::Palette); } // Pickers.cpp, as in the stroke picker

void App::drawPalette_Tool() {
    const tray::Style style; // quiet check marks
    const float s = uiScale_;
    // Header: the active SubTool and its size, then Open / Save / Import.
    {
        const SubTool& st = doc_.activeSubtoolInfo();
        if (fontBold_) ImGui::PushFont(fontBold_);
        ImGui::TextUnformatted(st.name.c_str());
        if (fontBold_) ImGui::PopFont();
        char tris[64];
        std::snprintf(tris, sizeof tris, "%zu tris", doc_.mesh.triangleCount());
        ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(tris).x);
        ImGui::TextDisabled("%s", tris);
        actionRow(*this, {"file.open", "file.save", "file.import"});
        ImGui::Dummy(ImVec2(0, 2.0f * s));
    }
    if (subpalette("tool.subtool", "SubTool")) {
        tray::Section section;
        Demo::pushScope("SubTools");
        drawSubToolsContents();
        Demo::popScope();
        Demo::pushScope("Project");
        drawProjectContents(); // ProjectAll (PanelRetopo.cpp)
        Demo::popScope();
    }
    if (subpalette("tool.layers", "Layers")) {
        tray::Section section;
        Demo::pushScope("Layers");
        drawLayersContents();
        Demo::popScope();
    }
    if (subpalette("tool.face", "Facial Shapes")) {
        tray::Section section;
        Demo::pushScope("Facial Shapes");
        drawFaceContents(); // PanelFace.cpp
        Demo::popScope();
    }
    if (subpalette("tool.paint_layers", "Paint Layers")) {
        tray::Section section;
        Demo::pushScope("Paint Layers");
        drawPaintLayersContents();
        Demo::popScope();
    }
    if (subpalette("tool.texture", "Texture Map")) {
        tray::Section section;
        Demo::pushScope("Texture Map");
        drawTextureMapContents();
        Demo::popScope();
    }
    if (subpalette("tool.bake", "Bake")) {
        tray::Section section;
        Demo::pushScope("Bake");
        drawBakeContents();
        Demo::popScope();
    }
    if (subpalette("tool.geometry", "Geometry")) {
        tray::Section section;
        Demo::pushScope("Geometry");
        drawGeometrySection();
        actionRow(*this, {"geometry.subdivide"});
        const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (ImGui::Button("Decimate", ImVec2(half, 0))) runCommand("decimate", {{"ratio", double(decimateRatio_)}});
        ui::HelpText("Decimate", nullptr, "Reduce the triangle count while keeping the shape. Sculpt layers are kept.",
                     "Tool > Geometry");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        ui::SliderInside("##decimate", &decimateRatio_, 0.05f, 0.95f, "Keep %.2f");
        ui::HelpText("Keep", nullptr, "Fraction of the triangles Decimate keeps.", "Tool > Geometry");
        Demo::pushScope("QRemesh");
        drawQRemeshContents(); // quad retopology (PanelRetopo.cpp)
        Demo::popScope();
    }
    if (subpalette("tool.remesh", "Remesh")) {
        tray::Section section;
        const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (voxelRemesh_) { // remeshing a painted SubTool in the background (PanelRetopo.cpp)
            ImGui::ProgressBar(voxelRemesh_->progress->load(), ImVec2(half, 0), "Remeshing...");
            ui::HelpText("Voxel Remesh", nullptr,
                         "Remeshing and keeping the texture in the background (Auto UV of the new surface, then the "
                         "texture projected onto it). The result replaces the SubTool when it's done.",
                         "Tool > Remesh");
        } else {
            ui::ActionButton(*this, "geometry.remesh", ImVec2(half, 0));
            ui::HelpAction(*this, "geometry.remesh");
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        tray::SliderInt("##voxres", &voxelResolution_, 64, 400, "Resolution %d");
        ui::HelpText("Resolution", nullptr, "Voxels across the model: higher keeps more detail and makes more triangles.",
                     "Tool > Remesh");
        if (voxelRemesh_) {
            if (ImGui::Button("Cancel##voxelremesh", ImVec2(half, 0))) voxelRemesh_->cancel->store(true);
            ui::HelpText("Cancel", nullptr, "Stop the remesh; the SubTool stays as it is.", "Tool > Remesh");
        } else if (hasTexture(doc_.mesh)) {
            drawKeepTextureCheckbox("Keep Texture##remesh", "Tool > Remesh"); // only for a painted SubTool
        }
    }
    if (subpalette("tool.masking", "Masking")) {
        tray::Section section;
        drawMaskingSection();
    }
    if (subpalette("tool.polygroups", "Polygroups")) {
        tray::Section section;
        drawPolygroupsSection();
    }
    if (subpalette("tool.visibility", "Visibility")) {
        tray::Section section;
        drawVisibilitySection();
    }
    if (subpalette("tool.export", "Export")) {
        tray::Section section;
        Demo::pushScope("Game Ready");
        drawGameExportContents();
        Demo::popScope();
    }
    if (subpalette("tool.display", "Display")) {
        tray::Section section;
        ImGui::Checkbox("Wireframe##display", &renderSettings_.wireframe);
        ui::HelpAction(*this, "view.wireframe");
        ImGui::SameLine();
        ImGui::Checkbox("Mask##display", &renderSettings_.showMask);
        ui::HelpAction(*this, "view.mask");
        ImGui::SameLine();
        ImGui::Checkbox("Groups##display", &renderSettings_.showPolygroups);
        ui::HelpAction(*this, "view.polygroups");
        ImGui::ColorEdit3("Material colour", &renderSettings_.baseColor.x, ImGuiColorEditFlags_NoInputs);
        ui::HelpText("Material colour", nullptr, "Colour of the Studio material (the clay colour).", "Tool > Display");
    }
}

void App::drawPalette_Transform() {
    const tray::Style style; // quiet check marks
    ImGui::TextDisabled("Gizmo 3D");
    drawGizmoContents(false);
    ImGui::TextDisabled("Symmetry");
    segmented(*this, {"transform.symmetry_x", "transform.symmetry_y", "transform.symmetry_z"});
    actionRow(*this, {"view.frame", "subtool.solo"});
    actionRow(*this, {"view.polyf"});
}

} // namespace sf
