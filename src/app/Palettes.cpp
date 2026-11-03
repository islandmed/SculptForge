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
#include "app/I18n.h"
#include "core/Matcap.h"
#include "core/StarterMeshes.h"

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
        {"color", "Color", ICON_LC_PALETTE, "Vertex Paint & Materials", &App::drawPalette_Color},
        {"draw", "Draw", ICON_LC_PENCIL, nullptr, &App::drawPalette_Draw},
        {"edit", "Edit", ICON_LC_UNDO_2, nullptr, &App::drawPalette_Edit},
        {"file", "File", ICON_LC_FILE, nullptr, &App::drawPalette_File},
        {"generate", "Generate", ICON_LC_WAND_SPARKLES, nullptr, &App::drawPalette_Generate},
        {"help", "Help", ICON_LC_CIRCLE_HELP, nullptr, &App::drawPalette_Help},
        {"material", "Material", ICON_LC_SUN, "Vertex Paint & Materials", &App::drawPalette_Material},
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

const TaskMenuDef* taskMenus(int& count) {
    // Every palette in exactly one menu (tests/ui/looks.sfui opens every section). View is how the model shows, Render how it
    // is shaded (Lit Sphere materials, PBR) and the Gaussian splats.
    static const TaskMenuDef kMenus[] = {
        {"file", "File", {"file", "script", nullptr}, {nullptr, nullptr}},
        {"edit", "Edit", {"edit", "preferences", nullptr}, {nullptr, nullptr}},
        {"sculpt", "Sculpt", {"brush", "draw", "stroke", "alpha", "transform", nullptr}, {nullptr, "Draw & Tablet", nullptr, nullptr, "Symmetry & Gizmo"}},
        {"paint", "Paint", {"color", nullptr}, {"Vertex Paint & Color"}},
        {"mesh", "Mesh", {"tool", "generate", nullptr}, {"Parts & Geometry", "Generate 3D"}},
        {"view", "View", {"render", nullptr}, {"Display"}},
        {"render", "Render", {"material", "splats", nullptr}, {"Materials", "Gaussian Splats"}},
        {"help", "Help", {"help", nullptr}, {nullptr}},
    };
    count = int(std::size(kMenus));
    return kMenus;
}

int taskMenuOf(std::string_view paletteId) {
    int count = 0;
    const TaskMenuDef* menus = taskMenus(count);
    for (int i = 0; i < count; ++i)
        for (int k = 0; k < 6 && menus[i].sections[k]; ++k)
            if (paletteId == menus[i].sections[k]) return i;
    return -1;
}

namespace {

// The hairline under each sub-palette header: opaque, a clear step above panelBg so the closed list reads as rows.
// Should become a theme token (Theme.h).
ImU32 sectionRule() { return theme::look.id == 0 ? IM_COL32(54, 54, 60, 255) : theme::u32(theme::sectionRule); }
constexpr float kSectionHeaderH = 22.0f; // sub-palette header height, unscaled
constexpr float kSectionInset = 8.0f;    // header titles and section content start this far in, unscaled
// Check marks in the palettes: the text colour, so a column of options doesn't read as a row of accents.
ImVec4 checkMark() { return theme::look.id == 0 ? theme::rgb(0xd8d8d8) : theme::text; }

// Phosphor's rules: a run of box-drawing lines from x0 to x1 on the text's centre line, in the current font.
void asciiRule(ImDrawList* dl, float x0, float x1, float y, ImU32 col) {
    ImFont* f = ImGui::GetFont();
    const float cw = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0.0f, "\xe2\x94\x80").x;
    if (cw <= 0.0f || x1 - x0 < cw) return;
    std::string run;
    for (float x = x0; x + cw <= x1; x += cw) run += "\xe2\x94\x80";
    dl->AddText(ImVec2(std::floor(x0), std::floor(y)), col, run.c_str());
}

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
    const std::string label = std::string(trAction(a->id, a->label)) + "##" + a->id; // no icons: they'd misalign with the plain project items
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
    ImGui::TextWrapped("%s", tr(text));
    ImGui::PopStyleColor();
    if (app.smallFont()) ImGui::PopFont();
}

// A row of option toggles drawn like the top shelf's (Rgb, Front, Dynamic, Pressure > ...): dark chips with a readable
// label, raised with an accent underline when on (ui::ActionButton).
void drawOptionRow(App& app, std::initializer_list<const char*> ids) {
    ImGui::PushStyleColor(ImGuiCol_Button, theme::toggleOff);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::toggleOffHover);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::textOff);
    actionRow(app, ids);
    ImGui::PopStyleColor(3);
}

} // namespace

namespace tray {

Section::Section() { ImGui::Indent(kSectionInset * ImGui::GetFontSize() / 15.0f); }

Section::~Section() {
    const float s = ImGui::GetFontSize() / 15.0f;
    ImGui::Unindent(kSectionInset * s);
    ImGui::Dummy(ImVec2(0, 4.0f * s));
}

Style::Style() { ImGui::PushStyleColor(ImGuiCol_CheckMark, checkMark()); }

Style::~Style() { ImGui::PopStyleColor(); }

void GroupLabel(const App& app, const char* text) {
    const float s = ImGui::GetFontSize() / 15.0f;
    ImGui::Dummy(ImVec2(0, 2.0f * s));
    if (theme::look.headers == theme::LookStyle::Headers::Ascii) { // "-- Extract -----------" in box-drawing lines
        if (app.smallFont()) ImGui::PushFont(app.smallFont());
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x, h = ImGui::GetTextLineHeight();
        ImGui::Dummy(ImVec2(w, h + 2.0f * s));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImU32 rule = theme::u32(theme::textDim, 0.6f);
        const std::string label = std::string(" ") + tr(text) + " ";
        asciiRule(dl, p.x, p.x + 2.0f * ImGui::CalcTextSize("-").x, p.y, rule);
        const float lx = p.x + 2.0f * ImGui::CalcTextSize("-").x;
        dl->AddText(ImVec2(lx, p.y), theme::u32(theme::textDim), label.c_str());
        asciiRule(dl, lx + ImGui::CalcTextSize(label.c_str()).x, p.x + w, p.y, rule);
        ImGui::LogText("%s", tr(text));
        if (app.smallFont()) ImGui::PopFont();
        return;
    }
    if (app.smallFont()) ImGui::PushFont(app.smallFont());
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::PushStyleColor(ImGuiCol_Separator, ImGui::ColorConvertU32ToFloat4(sectionRule()));
    ImGui::PushStyleVar(ImGuiStyleVar_SeparatorTextPadding, ImVec2(0.0f, 2.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_SeparatorTextAlign, ImVec2(0.0f, 0.5f));
    ImGui::SeparatorText(tr(text));
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

bool App::subpalette(const char* id, const char* englishTitle) {
    const char* title = tr(englishTitle); // the interface language (I18n.h); the id keeps the item's identity
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
    using Headers = theme::LookStyle::Headers;
    const Headers headers = theme::look.headers;
    if (headers == Headers::Ascii) { // "[+] Geometry" / "[-] Geometry", no fill but the hover
        if (hovered) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), theme::u32(theme::headerHover, 0.6f));
        const float ty = std::floor(p.y + (h - ImGui::GetFontSize()) * 0.5f);
        dl->AddText(ImVec2(p.x + 2.0f * s, ty), theme::u32(isOpen ? theme::accent : theme::textDim), isOpen ? "[-]" : "[+]");
        dl->AddText(ImVec2(p.x + 2.0f * s + ImGui::CalcTextSize("[-] ").x, ty), theme::u32(isOpen || hovered ? theme::textBright : theme::text),
                    title);
        ImGui::LogText("%s", title);
        ui::HelpText(title, nullptr,
                     "Click to open or close. Opening one closes the others (Shift+click keeps them open; Preferences > "
                     "Interface).",
                     "Tool");
    } else {
    if (isOpen || hovered)
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), theme::u32(hovered ? theme::headerHover : theme::header, isOpen ? 0.9f : 0.6f),
                          theme::rounding(2.0f * s));
    if (headers == Headers::Grid) { // a dimension line: the hairline in the accent with ticks at both ends
        const float y = std::floor(p.y + h - 1.0f) + 0.5f;
        const ImU32 c = theme::u32(theme::accent, isOpen ? 0.55f : 0.3f);
        dl->AddLine(ImVec2(p.x, y), ImVec2(p.x + w, y), c, 1.0f);
        dl->AddLine(ImVec2(p.x + 0.5f, y - 4.0f * s), ImVec2(p.x + 0.5f, y + 0.5f), c, 1.0f);
        dl->AddLine(ImVec2(p.x + w - 0.5f, y - 4.0f * s), ImVec2(p.x + w - 0.5f, y + 0.5f), c, 1.0f);
    } else {
        dl->AddLine(ImVec2(p.x, p.y + h - 0.5f * s), ImVec2(p.x + w, p.y + h - 0.5f * s), sectionRule(), 1.0f * s);
    }
    if (isOpen && headers == Headers::Bar) dl->AddRectFilled(p, ImVec2(p.x + 2.0f * s, p.y + h), theme::u32(theme::accent));
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
    }
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
    actionRow(*this, {"brush.save_settings"}); // this brush's own settings (#78); one per row so the labels fit
    actionRow(*this, {"brush.reset_settings"});
    drawCustomBrushes();
    ImGui::Checkbox(tr("Front faces only"), &frontFacesOnly_);
    ui::HelpAction(*this, "draw.front_faces");
    ImGui::SameLine();
    ImGui::Checkbox(tr("Invert (Alt)"), &invertToggle_);
    ui::HelpText("Invert", "Alt", "Brushes subtract instead of add (ZBrush Zsub). Holding Alt flips it for one stroke.", "Brush");
    ui::Checkbox("Connected only##draw.connected_only", &uiPrefs_.connectedOnly);
    ui::HelpAction(*this, "draw.connected_only");
    ImGui::SameLine();
    ui::Checkbox("View falloff##draw.view_falloff", &uiPrefs_.viewFalloff);
    ui::HelpAction(*this, "draw.view_falloff");
    ImGui::Checkbox(tr("Smooth keeps volume##smooth_keep_volume"), &smoothKeepVolume_);
    ui::HelpText("Smooth keeps volume", nullptr,
                 "The Smooth brush (and Shift) removes bumps without shrinking the form, instead of the plain smooth "
                 "that also flattens it. Either way, let go of Shift while still pressing a Shift-smooth stroke to "
                 "switch to the other smooth for the rest of the stroke (ZBrush's alternate smooth).",
                 "Brush");
    ImGui::Checkbox(tr("Smooth face group borders only##smooth_group_borders"), &smoothGroupBorders_);
    ui::HelpText("Smooth face group borders only", nullptr,
                 "The Smooth brush (and Shift) straightens the borders between face groups and leaves everything "
                 "else alone: a jagged clothing edge, a cuff or a panel line becomes a clean line along the surface. "
                 "Corners where three groups meet stay where they are.",
                 "Brush");
    if (BrushRecipe* recipe = doc_.activeBrush >= 0 && doc_.activeBrush < int(doc_.brushes.size())
                                  ? &doc_.brushes[size_t(doc_.activeBrush)]
                                  : nullptr) {
        if (subpalette("brush.recipe", (std::string(tr("Edit recipe:")) + " " + recipe->name).c_str())) {
            tray::Section section;
            drawRecipeEditor(*recipe);
            if (!pendingRename_.empty()) {
                runCommand("brush_rename", {{"to", pendingRename_}});
                pendingRename_.clear();
            }
        }
    }
    if (subpalette("brush.kelvinlet", "Kelvinlet & Capture")) {
        tray::Section section;
        drawKelvinletSection(); // PanelKelvinlet.cpp
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
    drawSizeSlider("##drawsize", "Draw",
                   "Brush radius in screen pixels (0.5 to 800), and in mm on the surface under the cursor. [ and ] change "
                   "it by 10%, Shift+[ and Shift+] by 1 px; Ctrl+Shift+right-drag scrubs it finely.");
    ui::ActionButton(*this, "brush.world_size", ImVec2(-1, 0));
    ImGui::SetNextItemWidth(-1);
    drawZIntensitySlider("##strength");
    ImGui::SetNextItemWidth(-1);
    float rgb = rgbIntensity_ * 100.0f;
    if (ui::SliderInside("##rgbintensity", &rgb, 1.0f, 100.0f, "RGB Intensity %.0f")) rgbIntensity_ = rgb / 100.0f;
    ui::HelpText("RGB Intensity", "I", "How strongly painting colours the surface (1-100); Ctrl+click to type.", "Draw");
    ImGui::SetNextItemWidth(-1);
    float focal = focalShift_ * 100.0f;
    if (ui::SliderInside("##focalshift", &focal, -100.0f, 100.0f, "Falloff %.0f")) focalShift_ = focal / 100.0f;
    ui::HelpText("Falloff", "O",
                 "The brush falloff: below 0 a broad, hard tip, above 0 a soft point. The brush cursor's inner ring shows "
                 "where the brush works at half strength.",
                 "Draw");
    // The same looks as the top shelf: Sculpt | Paint is a mode (the accent), Zadd | Zsub a choice (raised grey),
    // the rest options (dark chips, an underline when on). Sliders stay neutral, so the accent marks one thing.
    ImGui::Dummy(ImVec2(0, 2.0f * uiScale_));
    segmented(*this, {"draw.sculpt", "draw.paint"});
    segmented(*this, {"draw.zadd", "draw.zsub"});
    drawOptionRow(*this, {"draw.rgb", "draw.front_faces"});
    tray::GroupLabel(*this, "Dynamic topology");
    drawOptionRow(*this, {"draw.dynamic"});
    ImGui::SetNextItemWidth(-1);
    drawDetailSlider("##dyntopo_detail_palette");
    if (ui::Checkbox("Constant detail", &uiPrefs_.dyntopoConstantDetail))
        doc_.sculptor.dyntopo.constantDetail = uiPrefs_.dyntopoConstantDetail;
    ui::HelpText("Constant detail", nullptr,
                 "Off (the default): the triangle size follows the brush size on screen, as in Dynamic Topology. On: one "
                 "size for the whole mesh, set by Detail. A preference (Store Config keeps it).",
                 "Draw > Dynamic topology");
    {
        float remesh = doc_.sculptor.dyntopo.smoothRemesh * 100.0f;
        ImGui::SetNextItemWidth(-1);
        const char* shown = remesh <= 0.5f ? "Smooth remesh: none (smooth only)" : "Smooth remesh %.0f";
        if (ui::SliderInside("##dyntopo_smooth_remesh", &remesh, 0.0f, 100.0f, shown)) {
            doc_.sculptor.dyntopo.smoothRemesh = std::clamp(remesh / 100.0f, 0.0f, 1.0f);
            uiPrefs_.dyntopoSmoothRemesh = doc_.sculptor.dyntopo.smoothRemesh;
        }
        ui::HelpText("Smooth remesh", nullptr,
                     "How much Smooth (Shift) changes the topology while Dynamic is on. 100 (the default): it also "
                     "simplifies, merging triangles toward twice the size. Lower: it remeshes less. 0: Smooth only "
                     "smooths and the triangles stay as they are. A preference (Store Config keeps it).",
                     "Draw > Dynamic topology");
        const float third = (ImGui::GetContentRegionAvail().x - 2.0f * ImGui::GetStyle().ItemSpacing.x) / 3.0f;
        const struct {
            const char* label;
            float value;
        } presets[] = {{"Smooth only##sr0", 0.0f}, {"Light##sr50", 0.5f}, {"Simplify##sr100", 1.0f}};
        for (int k = 0; k < 3; ++k) {
            if (k) ImGui::SameLine();
            if (ImGui::Button(presets[k].label, ImVec2(k == 2 ? -1.0f : third, 0))) {
                doc_.sculptor.dyntopo.smoothRemesh = presets[k].value;
                uiPrefs_.dyntopoSmoothRemesh = presets[k].value;
            }
        }
    }
    if (doc_.sculptor.dyntopo.enabled && !dyntopoBlocker(doc_.mesh).empty())
        hint(*this, "This Part has subdivision levels, so strokes don't remesh it: delete the lower (or higher) levels "
             "first.");
    // Focal length (#26): the perspective view's lens in mm on a 36 x 24 mm frame (vertical field of view);
    // saved with the project as the camera's fov_y.
    tray::GroupLabel(*this, "Camera");
    {
        auto toMm = [](float fovY) { return 12.0f / std::tan(fovY * 0.5f); };
        auto toFov = [](float mm) { return 2.0f * std::atan(12.0f / mm); };
        float mm = toMm(camera_.fovY);
        ImGui::SetNextItemWidth(-1);
        if (ui::SliderInside("##focallength", &mm, 15.0f, 300.0f, "Focal Length %.0f mm", ImGuiSliderFlags_Logarithmic))
            camera_.fovY = toFov(std::clamp(mm, 15.0f, 300.0f));
        ui::HelpText("Focal Length", nullptr,
                     "The viewport's lens, as on a full-frame camera: short (15-35 mm) exaggerates perspective, long "
                     "(85-300 mm) flattens it, which helps judge proportions. Saved with the project; Ctrl+click to type.",
                     "Draw > Camera");
        const float presets[] = {35.0f, 50.0f, 85.0f, 135.0f};
        const float w = (ImGui::GetContentRegionAvail().x - 3.0f * ImGui::GetStyle().ItemSpacing.x) / 4.0f;
        for (int i = 0; i < 4; ++i) {
            if (i) ImGui::SameLine();
            char label[24];
            std::snprintf(label, sizeof label, "%.0f##focal%d", presets[i], i);
            if (ImGui::Button(label, ImVec2(w, 0))) camera_.fovY = toFov(presets[i]);
        }
        ui::Checkbox("Axes##draw.axes", &uiPrefs_.showAxes);
        ui::HelpText("Axes", nullptr, "Show the X / Y / Z directions in the corner of the canvas.", "Draw > Camera");
        ImGui::SameLine();
        ui::Checkbox("View Figure##draw.view_figure", &uiPrefs_.showViewFigure);
        ui::HelpText("View Figure", nullptr,
                     "Show a small shaded head in the corner of the canvas that turns with the view. Click it to look "
                     "from the nearest side (front, back, left, right, top or bottom).",
                     "Draw > Camera");
        hint(*this, "Gizmo size");
        segmented(*this, {"view.gizmo_small", "view.gizmo_medium", "view.gizmo_large"});
    }
    // Tablet: the same Pressure > Strength / Size toggles as the top shelf (not checkboxes), and the pen state as
    // a hint line.
    tray::GroupLabel(*this, "Tablet");
    drawOptionRow(*this, {"draw.pressure_strength", "draw.pressure_size"});
    if (penSeen_) {
        char pen[48];
        std::snprintf(pen, sizeof pen, "Pen pressure: %.2f", uiPrefs_.tablet.curve.apply(penTracker_.raw()));
        hint(*this, pen);
    } else {
        hint(*this, "No pen detected");
    }
}

void App::drawPalette_Edit() {
    const std::string undoText = doc_.undo.canUndo() ? std::string(tr("Undo")) + " " + doc_.undo.undoLabel() : std::string(tr("Undo"));
    const std::string redoText = doc_.undo.canRedo() ? std::string(tr("Redo")) + " " + doc_.undo.redoLabel() : std::string(tr("Redo"));
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
    if (ImGui::BeginMenu("New from Starter Mesh")) {
        for (const StarterMesh& m : starterMeshes()) {
            if (ImGui::MenuItem((std::string(m.name) + "##starter." + m.id).c_str())) {
                const std::string id = m.id;
                confirmDiscard([this, id] { runCommand("new_starter_mesh", {{"name", id}}); });
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", m.description);
        }
        ImGui::Separator();
        menuAction(*this, "file.new_starter");
        ImGui::EndMenu();
    }
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
    menuAction(*this, "pack.install"); // a review screen first (PanelPacks.cpp)
    ImGui::Separator();
    drawGozMenu();
    drawBlenderMenu(); // the Blender bridge (PanelBlender.cpp)
    drawMayaMenu(); // the Maya bridge (PanelMaya.cpp)
    menuAction(*this, "file.capture_phone"); // the phone link (PanelPhone.cpp)
    ImGui::Separator();
    menuAction(*this, "file.quit");
}

void App::drawPalette_Generate() {
    const tray::Style style; // quiet check marks
    if (ai::aiEnabled()) {
        actionRow(*this, {"ui.generate3d"});
        hint(*this, "Image or text to a sculptable 3D model: Hugging Face (free), fal.ai, Meshy, your ComfyUI server, your own "
             "command, or an offline blockout.");
    } else {
        hint(*this, "AI features are off (Preferences > AI): Relief, Inflate and Stamp work offline.");
    }
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
    if (ImGui::Button(tr("Relief"), ImVec2(third, 0)))
        runCommand("image_relief", {{"path", refImagePath_},
                                    {"depth", double(reliefDepth_)},
                                    {"resolution", double(reliefDetail_)},
                                    {"invert", reliefInvert_}});
    ui::HelpText("Relief", nullptr, "Light areas rise: logos, coins, emblems, height maps.", "Generate");
    ImGui::SameLine();
    if (ImGui::Button(tr("Inflate"), ImVec2(third, 0)))
        runCommand("image_inflate", {{"path", refImagePath_},
                                     {"thickness", double(inflateThickness_)},
                                     {"resolution", double(inflateDetail_)},
                                     {"subdivisions", 2.0}});
    ui::HelpText("Inflate", nullptr, "Inflates the outline into a rounded blockout.", "Generate");
    ImGui::SameLine();
    if (ImGui::Button(tr("Stamp"), ImVec2(third, 0))) projectImageFromView();
    ui::HelpText("Stamp", nullptr, "Projects the image onto the model from this view as surface detail.", "Generate");
    ImGui::EndDisabled();
    if (ai::aiEnabled() && subpalette("generate.ai", "AI brush provider")) {
        tray::Section section;
        drawAiSettings();
    }
}

void App::drawPalette_Help() {
    menuAction(*this, "help.controls");
    menuAction(*this, "help.guide");
    menuAction(*this, "help.first_sculpt");
    menuAction(*this, "help.whats_new");
    menuAction(*this, "help.check_updates");
    menuAction(*this, "ui.command_palette");
    menuAction(*this, "help.website");
    menuAction(*this, "help.discord");
    menuAction(*this, "help.report");
    menuAction(*this, "help.log");
    menuAction(*this, "help.feedback");
    menuAction(*this, "help.benchmark");
    ImGui::Separator();
    menuAction(*this, "help.support");
    menuAction(*this, "help.about");
}

void App::drawPalette_Preferences() {
    const tray::Style style; // quiet check marks
    // The sub-palettes come from Config.cpp (Store / Restore UI, interface preferences, hotkeys, LightBox).
    if (subpalette("preferences.config", "Config")) {
        tray::Section section;
        config::drawConfigSection(*this);
        ImGui::TextDisabled("%s", tr("Layout"));
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
    if (subpalette("preferences.cursor", "Cursor")) {
        tray::Section section;
        drawCursorSection();
    }
    if (subpalette("preferences.hints", "Hints")) {
        tray::Section section;
        drawHintsSection();
    }
    if (subpalette("preferences.hotkeys", "Hotkeys")) {
        tray::Section section;
        config::drawHotkeysSection(*this);
    }
    if (subpalette("preferences.display", "Display")) {
        tray::Section section;
        drawBackgroundControls("prefs");
    }
    if (subpalette("preferences.lightbox", "Library")) {
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
    if (subpalette("preferences.ai", "AI")) {
        tray::Section section;
        drawAiPrefs(); // the switch (PanelAi.cpp)
        if (ai::aiEnabled()) {
            ImGui::Spacing();
            drawAiPrefsSection(); // the Hugging Face token
        }
    }
    if (subpalette("preferences.updates", "Updates")) {
        tray::Section section;
        drawUpdatesSection();
    }
    if (subpalette("preferences.support", "Support")) {
        tray::Section section;
        drawSupportPrefs(); // the support card at launch (PanelSupportCard.cpp)
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
    ImGui::Checkbox(tr("Wireframe"), &renderSettings_.wireframe);
    ui::HelpAction(*this, "view.wireframe");
    ImGui::Checkbox(tr("Show Mesh"), &renderSettings_.showMesh);
    ui::HelpAction(*this, "view.mesh");
    ImGui::Checkbox(tr("Show Vertex Paint"), &renderSettings_.showPolypaint);
    ui::HelpAction(*this, "view.polypaint");
    ImGui::Checkbox(tr("Show Mask"), &renderSettings_.showMask);
    ui::HelpAction(*this, "view.mask");
    ImGui::Checkbox(tr("Show Face Groups##render"), &renderSettings_.showPolygroups);
    ui::HelpAction(*this, "view.polygroups");
    ImGui::BeginDisabled(doc_.splats.empty());
    ImGui::Checkbox(tr("Show Splats"), &renderSettings_.showSplats);
    ImGui::EndDisabled();
    ui::HelpAction(*this, "view.splats");
    bool lockCamera = uiPrefs_.airStrokes; // #62: drags on the background start strokes (Shift+L)
    if (ui::Checkbox("Lock camera##view.lock_camera", &lockCamera)) uiPrefs_.airStrokes = lockCamera;
    ui::HelpAction(*this, "input.toggle_air_strokes");
    drawBackgroundControls("render");
    actionRow(*this, {"view.frame", "view.fullscreen"});
    actionRow(*this, {"ui.lightbox", "ui.console"}); // (the SculptForge layout's View menu: they left the shelf)
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

void App::drawPalette_Stroke() {
    drawStrokeGrid(*this, GridMode::Palette); // Pickers.cpp, as in the stroke picker
    drawOptionRow(*this, {"stroke.continuous"});
}

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
    if (subpalette("tool.subtool", "Parts")) {
        tray::Section section;
        Demo::pushScope("SubTools");
        drawSubToolsContents();
        Demo::popScope();
        drawExtractContents(); // PanelExtract.cpp
        Demo::pushScope("Project");
        drawProjectContents(); // ProjectAll (PanelRetopo.cpp)
        Demo::popScope();
    }
    if (subpalette("tool.zspheres", "Forge Spheres")) {
        tray::Section section;
        Demo::pushScope("ZSpheres");
        drawZSpheresContents(); // PanelZSpheres.cpp
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
        if (ImGui::Button(tr("Decimate"), ImVec2(half, 0))) {
            Args args{{"ratio", double(decimateRatio_)}};
            if (decimateKeepMasked_) args["keep_masked"] = true;
            runCommand("decimate", args);
        }
        ui::HelpText("Decimate", nullptr, "Reduce the triangle count while keeping the shape. Sculpt layers are kept.",
                     "Tool > Geometry");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        ui::SliderInside("##decimate", &decimateRatio_, 0.05f, 0.95f, "Keep %.2f");
        ui::HelpText("Keep", nullptr, "Fraction of the triangles Decimate keeps.", "Tool > Geometry");
        ImGui::Checkbox(tr("Keep Masked##decimate"), &decimateKeepMasked_);
        ui::HelpText("Keep Masked", nullptr,
                     "Decimate leaves masked vertices where they are, so masked areas keep their triangles exactly; "
                     "the rest is decimated around them.",
                     "Tool > Geometry");
        Demo::pushScope("QRemesh");
        drawQRemeshContents(); // quad retopology (PanelRetopo.cpp)
        Demo::popScope();
        drawModifyTopologyContents(); // Mirror And Weld (PanelExtract.cpp)
    }
    if (subpalette("tool.remesh", "Remesh")) {
        tray::Section section;
        // QRemesh (quad retopology) first: the same controls and settings as in Tool > Geometry.
        ImGui::PushID("remesh_qremesh");
        Demo::pushScope("Remesh QRemesh");
        drawQRemeshContents();
        Demo::popScope();
        ImGui::PopID();
        tray::GroupLabel(*this, "Voxel Remesh");
        const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (voxelRemesh_) { // remeshing a painted SubTool in the background (PanelRetopo.cpp)
            ImGui::ProgressBar(voxelRemesh_->progress->load(), ImVec2(half, 0), "Remeshing...");
            ui::HelpText("Voxel Remesh", nullptr,
                         "Remeshing and keeping the texture in the background (Auto UV of the new surface, then the "
                         "texture projected onto it). The result replaces the Part when it's done.",
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
            if (ImGui::Button(tr("Cancel##voxelremesh"), ImVec2(half, 0))) voxelRemesh_->cancel->store(true);
            ui::HelpText("Cancel", nullptr, "Stop the remesh; the Part stays as it is.", "Tool > Remesh");
        } else {
            ImGui::Checkbox(tr("Keep Masked##remesh"), &voxelKeepMasked_);
            ui::HelpText("Keep Masked", nullptr, kKeepMaskedHelp, "Tool > Remesh");
            if (hasTexture(doc_.mesh)) {
                ImGui::SameLine();
                drawKeepTextureCheckbox("Keep Texture##remesh", "Tool > Remesh"); // only for a painted SubTool
            }
        }
    }
    if (subpalette("tool.deformation", "Deformation")) {
        tray::Section section;
        Demo::pushScope("Deformation");
        drawDeformationContents(); // Mirror (PanelExtract.cpp)
        Demo::popScope();
    }
    if (subpalette("tool.cut", "Cut")) {
        tray::Section section;
        drawCutSection(); // PanelCut.cpp
    }
    if (subpalette("tool.masking", "Masking")) {
        tray::Section section;
        drawMaskingSection();
    }
    if (subpalette("tool.polygroups", "Face Groups")) {
        tray::Section section;
        drawPolygroupsSection();
    }
    if (subpalette("tool.visibility", "Visibility")) {
        tray::Section section;
        drawVisibilitySection();
    }
    if (subpalette("tool.selection_sets", "Selection Sets")) {
        tray::Section section;
        drawSelectionSetsSection();
    }
    if (subpalette("tool.export", "Export")) {
        tray::Section section;
        Demo::pushScope("Game Ready");
        drawGameExportContents();
        Demo::popScope();
    }
    if (subpalette("tool.display", "Display")) {
        tray::Section section;
        ImGui::Checkbox(tr("Wireframe##display"), &renderSettings_.wireframe);
        ui::HelpAction(*this, "view.wireframe");
        ImGui::SameLine();
        ImGui::Checkbox(tr("Mask##display"), &renderSettings_.showMask);
        ui::HelpAction(*this, "view.mask");
        ImGui::SameLine();
        ImGui::Checkbox(tr("Groups##display"), &renderSettings_.showPolygroups);
        ui::HelpAction(*this, "view.polygroups");
        if (ImGui::Checkbox(tr("Backface culling##display"), &renderSettings_.backfaceCulling))
            uiPrefs_.backfaceCulling = renderSettings_.backfaceCulling; // the default for the next session
        ui::HelpText("Backface culling", nullptr,
                     "Don't draw the back faces of the model: faster on dense meshes, and you see through the openings "
                     "of open meshes (a scan, a garment) instead of their inside. Brushes then work only on faces you "
                     "can see. Off by default; Store Config keeps it, projects keep their own.",
                     "Tool > Display");
        ImGui::ColorEdit3(tr("Material colour"), &renderSettings_.baseColor.x, ImGuiColorEditFlags_NoInputs);
        ui::HelpText("Material colour", nullptr, "Colour of the Studio material (the clay colour).", "Tool > Display");
        drawShadingControls();
    }
}

// Radial symmetry (#63): the toggle with Local, then the count and the axis.
void App::drawRadialSymmetryControls() {
    segmented(*this, {"transform.symmetry_radial", "transform.symmetry_local"});
    ImGui::BeginDisabled(!symmetry_.radialEnabled);
    ImGui::SetNextItemWidth(-FLT_MIN);
    tray::SliderInt("##radialcount", &symmetry_.radial, 2, Symmetry::kMaxRadial, tr("Radial Count %d"));
    ui::HelpText("Radial Count", nullptr,
                 "How many copies radial symmetry makes of every stroke, turned evenly around the axis (2 to 64). With "
                 "X, Y or Z on too, each mirror image gets its copies.",
                 "Transform > Symmetry");
    segmented(*this, {"transform.radial_axis_x", "transform.radial_axis_y", "transform.radial_axis_z"});
    ImGui::EndDisabled();
}

void App::drawPalette_Transform() {
    const tray::Style style; // quiet check marks
    ImGui::TextDisabled("%s", tr("Gizmo"));
    drawGizmoContents(false);
    ImGui::TextDisabled("%s", tr("Symmetry"));
    segmented(*this, {"transform.symmetry_x", "transform.symmetry_y", "transform.symmetry_z"});
    drawRadialSymmetryControls();
    actionRow(*this, {"view.frame", "subtool.solo"});
    actionRow(*this, {"view.polyf"});
}

} // namespace sf
