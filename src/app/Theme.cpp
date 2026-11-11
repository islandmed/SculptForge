// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Theme.h"

#include "app/LookSchemes.h"
#include "app/Ui.h"

#include <algorithm>
#include <vector>

namespace sf::theme {

void applyStyle() {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0.0f; // shells; floating windows keep square corners too, 
    s.ChildRounding = 0.0f;
    s.PopupRounding = 6.0f;
    s.FrameRounding = 3.0f;
    s.GrabRounding = 2.0f;
    s.TabRounding = 3.0f;
    s.ScrollbarRounding = 3.0f;
    s.WindowBorderSize = 1.0f;
    s.PopupBorderSize = 1.0f;
    s.ChildBorderSize = 1.0f;
    s.FrameBorderSize = 0.0f;
    s.WindowPadding = ImVec2(8, 8);
    s.FramePadding = ImVec2(6, 3);
    s.ItemSpacing = ImVec2(4, 4);
    s.ItemInnerSpacing = ImVec2(4, 4);
    s.IndentSpacing = 14.0f;
    s.ScrollbarSize = 10.0f;
    s.GrabMinSize = 6.0f;
    s.SeparatorTextBorderSize = 1.0f;
    s.SeparatorTextPadding = ImVec2(10, 2);
    s.WindowTitleAlign = ImVec2(0.0f, 0.5f);

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = text;
    c[ImGuiCol_TextDisabled] = textDim;
    // Opaque: text behind a window, menu or tooltip showed through at 200% (overlays on the 3D view set their own alpha).
    c[ImGuiCol_WindowBg] = ImVec4(panelBg.x, panelBg.y, panelBg.z, 1.0f);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = ImVec4(panelBg.x, panelBg.y, panelBg.z, 1.0f);
    c[ImGuiCol_Border] = rgb(0x3c3c3c, 0.9f);
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = frame;
    c[ImGuiCol_FrameBgHovered] = frameHover;
    c[ImGuiCol_FrameBgActive] = rgb(0x404040);
    c[ImGuiCol_TitleBg] = header;
    c[ImGuiCol_TitleBgActive] = rgb(0x353535);
    c[ImGuiCol_TitleBgCollapsed] = header;
    c[ImGuiCol_MenuBarBg] = shellBg;
    c[ImGuiCol_ScrollbarBg] = rgb(0x1b1b1b, 0.6f);
    c[ImGuiCol_ScrollbarGrab] = rgb(0x4a4a4a);
    c[ImGuiCol_ScrollbarGrabHovered] = rgb(0x5a5a5a);
    c[ImGuiCol_ScrollbarGrabActive] = accent;
    c[ImGuiCol_CheckMark] = checkMark; // neutral: the accent marks the current mode, not every option that is on
    c[ImGuiCol_SliderGrab] = sliderBar;
    c[ImGuiCol_SliderGrabActive] = accent; // the accent only while dragging
    c[ImGuiCol_Button] = button;
    c[ImGuiCol_ButtonHovered] = buttonHover;
    c[ImGuiCol_ButtonActive] = buttonPressed;
    c[ImGuiCol_Header] = selection; // selected rows: a lighter neutral row (the accent is for the current tool)
    c[ImGuiCol_HeaderHovered] = headerHover;
    c[ImGuiCol_HeaderActive] = selectionActive;
    c[ImGuiCol_Separator] = rgb(0x3c3c3c);
    c[ImGuiCol_SeparatorHovered] = accentHover;
    c[ImGuiCol_SeparatorActive] = accent;
    c[ImGuiCol_ResizeGrip] = rgb(0x4a4a4a, 0.5f);
    c[ImGuiCol_ResizeGripHovered] = accentHover;
    c[ImGuiCol_ResizeGripActive] = accent;
    c[ImGuiCol_Tab] = header;
    c[ImGuiCol_TabHovered] = headerHover;
    c[ImGuiCol_TabSelected] = rgb(0x474747);
    c[ImGuiCol_TabSelectedOverline] = accent;
    c[ImGuiCol_TabDimmed] = header;
    c[ImGuiCol_TabDimmedSelected] = rgb(0x3c3c3c);
    c[ImGuiCol_PlotHistogram] = accent;
    c[ImGuiCol_PlotHistogramHovered] = accentHover;
    c[ImGuiCol_TableHeaderBg] = header;
    c[ImGuiCol_TableBorderStrong] = rgb(0x3c3c3c);
    c[ImGuiCol_TableBorderLight] = rgb(0x333333);
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = rgb(0xffffff, 0.025f);
    c[ImGuiCol_TextSelectedBg] = rgb(0xf0882e, 0.30f);
    c[ImGuiCol_DragDropTarget] = accentHover;
    c[ImGuiCol_NavCursor] = ImVec4(0, 0, 0, 0); // the keyboard focus gets the hover ring instead (ui::DrawHover)
    c[ImGuiCol_ModalWindowDimBg] = rgb(0x000000, 0.45f);
}

namespace {

ImVec4* const kTokens[] = {&shellBg, &panelBg, &header, &headerHover, &frame, &frameHover, &button, &buttonHover,
                           &buttonPressed, &accent, &accentHover, &toggleOff, &toggleOffHover, &toggleOn, &toggleOnHover,
                           &trough, &selection, &selectionActive, &sliderFill, &sliderBar, &checkMark, &checkBorder,
                           &sectionRule, &text, &textBright, &textOff, &textDim, &textOnAccent, &textHover, &border, &danger,
                           &canvasBorder, &tile, &gapBg, &tileBorder, &focusBorder};

// The Classic look's tokens, as Theme.h initialises them (taken before any look changes them).
const std::vector<ImVec4>& classicTokens() {
    static const std::vector<ImVec4> saved = [] {
        std::vector<ImVec4> v;
        for (ImVec4* t : kTokens) v.push_back(*t);
        return v;
    }();
    return saved;
}

ImVec4 mixc(const ImVec4& a, const ImVec4& b, float t, float alpha = 1.0f) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, alpha);
}

ImVec4 g_gradCentre(0.217f, 0.217f, 0.217f, 1.0f), g_gradEdge(0.109f, 0.109f, 0.109f, 1.0f);

void syncUiColors() {
    ui::color::accent = u32(accent);
    ui::color::accentHover = u32(accentHover);
    ui::color::text = u32(text);
    ui::color::textDim = u32(textDim);
    ui::color::danger = u32(danger);
    ui::color::panelBg = u32(panelBg);
    ui::color::shellBg = u32(shellBg);
    ui::color::header = u32(header);
    ui::color::border = u32(border);
}

// The tokens of a scheme. Every look reads its colours through these, so a new scheme needs only its Palette.
void setTokens(const looks::Palette& p, looks::LookId id) {
    const ImVec4 crust = rgb(p.crust), mantle = rgb(p.mantle), base = rgb(p.base), s0 = rgb(p.surface0),
                 s1 = rgb(p.surface1), s2 = rgb(p.surface2), ov = rgb(p.overlay), sub = rgb(p.subtext), tx = rgb(p.text),
                 br = rgb(p.bright), ac = rgb(p.accent);
    const bool phosphor = id == looks::LookId::Phosphor, blueprint = id == looks::LookId::Blueprint;
    gapBg = blueprint ? crust : mixc(crust, rgb(0x000000), p.light ? 0.0f : 0.35f); // the wallpaper: a clear step below the tiles
    shellBg = mantle;
    panelBg = base;
    header = mixc(base, s0, 0.75f);
    headerHover = s1;
    frame = mixc(s0, s1, 0.15f); // slider and check box frames: a clear step above the panels
    frameHover = mixc(s0, s1, 0.6f);
    button = s0;
    buttonHover = s1;
    buttonPressed = s2;
    accent = ac;
    accentHover = mixc(ac, p.light ? rgb(0x000000) : rgb(0xffffff), 0.18f);
    toggleOff = mixc(mantle, s0, 0.6f);
    toggleOffHover = s0;
    // On: a fill tinted with the accent, not just one surface step up (a review couldn't tell on from off).
    toggleOn = mixc(s1, ac, p.light ? 0.28f : 0.32f);
    toggleOnHover = mixc(s2, ac, p.light ? 0.34f : 0.38f);
    trough = mixc(mantle, s0, 0.7f);
    selection = mixc(s0, s1, 0.7f);
    selectionActive = s2;
    sliderFill = ImVec4(ac.x, ac.y, ac.z, phosphor ? 0.16f : 0.13f);
    sliderBar = mixc(ov, ac, 0.35f);
    checkMark = tx;
    checkBorder = s2;
    sectionRule = ImVec4(s1.x, s1.y, s1.z, 0.9f);
    text = tx;
    textBright = br;
    textOff = sub;
    textDim = mixc(ov, sub, 0.25f);
    textOnAccent = p.light ? rgb(0xffffff) : crust;
    textHover = br;
    border = crust;
    danger = rgb(p.red);
    canvasBorder = crust;
    tile = s0;
    tileBorder = blueprint ? mixc(base, ac, 0.38f) : phosphor ? mixc(base, ac, 0.3f) : mixc(s1, s2, 0.55f);
    focusBorder = ac;
    // The 3D view: lightest behind the model, the tiles' shade at the edges (the gaps frame it).
    g_gradCentre = blueprint ? mixc(base, s2, 0.55f) : phosphor ? mixc(base, s1, 0.55f) : mixc(base, s1, 0.85f);
    g_gradEdge = blueprint ? mixc(crust, base, 0.5f) : phosphor ? crust : mixc(mantle, crust, 0.3f);
}

} // namespace

void applyLook(const LookSettings& settings, std::string* status) {
    const std::vector<ImVec4>& classic = classicTokens();
    const looks::LookId id = settings.look > 0 && settings.look < int(looks::LookId::Count) ? looks::LookId(settings.look)
                                                                                         : looks::LookId::Classic;
    look = LookStyle{};
    look.id = int(id);
    // (The hover rings follow Outlines in every look: Classic has no panel borders, but a calm hover all the same.)
    look.outlines = std::clamp(settings.outlines, 0, 2);
    look.outlineOpacity = std::clamp(settings.outlineOpacity, 0.0f, 1.0f);
    if (id == looks::LookId::Classic) {
        for (size_t i = 0; i < std::size(kTokens); ++i) *kTokens[i] = classic[i];
        g_gradCentre = ImVec4(0.217f, 0.217f, 0.217f, 1.0f);
        g_gradEdge = ImVec4(0.109f, 0.109f, 0.109f, 1.0f);
        applyStyle();
        syncUiColors();
        return;
    }
    looks::Palette palette;
    if (id == looks::LookId::ForgeTerminal) {
        const looks::SchemeInfo* scheme = looks::findScheme(settings.scheme);
        int count = 0;
        palette = (scheme ? scheme : looks::schemes(count))->palette;
        if (settings.matchOmarchy) {
            std::string name, error;
            looks::Palette omarchy;
            if (looks::readOmarchyTheme(omarchy, name, error)) palette = omarchy;
            else if (status) *status = error;
        }
        look.tiled = true;
        look.gap = 6.0f;
        look.roundingScale = 0.0f;
        look.monoLabels = true;
        look.status = LookStyle::Status::Modules;
        look.headers = LookStyle::Headers::Bar;
    } else if (id == looks::LookId::Phosphor) {
        palette = looks::phosphorPalette(settings.amber);
        look.tiled = true;
        look.gap = 5.0f;
        look.roundingScale = 0.0f;
        look.monoLabels = true;
        look.monoAll = true;
        look.status = LookStyle::Status::Terminal;
        look.headers = LookStyle::Headers::Ascii;
        look.upperHeaders = true;
        look.scanlines = look.glow = settings.effects;
    } else {
        palette = looks::blueprintPalette();
        look.tiled = true;
        look.gap = 6.0f;
        look.roundingScale = 0.0f;
        look.monoLabels = true;
        look.status = LookStyle::Status::TitleBlock;
        look.headers = LookStyle::Headers::Grid;
        look.upperHeaders = true;
    }
    for (size_t i = 0; i < std::size(kTokens); ++i) *kTokens[i] = classic[i];
    setTokens(palette, id);
    // Outlines: on keeps the scheme's borders, subtle fades them most of the way into the panels, off hides them.
    // The panel under the pointer never gets the accent: on lifts its border a faint step, the others not at all.
    look.outlines = std::clamp(settings.outlines, 0, 2);
    const ImVec4 strong = tileBorder;
    // Outline opacity (#98) scales the border's step from the panel colour: 0 hides it, 1 is the setting's own.
    const float opacity = std::clamp(settings.outlineOpacity, 0.0f, 1.0f);
    tileBorder = look.outlines == 0 ? panelBg : mixc(panelBg, strong, (look.outlines == 1 ? 0.4f : 1.0f) * opacity);
    focusBorder = look.outlines == 2 ? mixc(tileBorder, textDim, 0.35f * opacity) : tileBorder;
    applyStyle();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = s.ChildRounding = s.PopupRounding = s.FrameRounding = s.GrabRounding = s.TabRounding = 0.0f;
    s.ScrollbarRounding = 0.0f;
    s.ScrollbarSize = 8.0f;
    s.WindowBorderSize = s.PopupBorderSize = 1.0f;
    ImVec4* c = s.Colors;
    const ImVec4 s1 = rgb(palette.surface1), s2 = rgb(palette.surface2);
    c[ImGuiCol_WindowBg] = ImVec4(panelBg.x, panelBg.y, panelBg.z, 1.0f); // opaque, as in Classic
    c[ImGuiCol_PopupBg] = ImVec4(panelBg.x, panelBg.y, panelBg.z, 1.0f);
    c[ImGuiCol_Border] = look.outlines == 0 ? mixc(panelBg, strong, 0.3f) : tileBorder; // (popups keep an edge)
    c[ImGuiCol_FrameBgActive] = s1;
    c[ImGuiCol_TitleBgActive] = s1;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = s1;
    c[ImGuiCol_ScrollbarGrabHovered] = s2;
    c[ImGuiCol_Separator] = s1;
    c[ImGuiCol_ResizeGrip] = ImVec4(s2.x, s2.y, s2.z, 0.5f);
    c[ImGuiCol_TabSelected] = s1;
    c[ImGuiCol_TabDimmedSelected] = s1;
    c[ImGuiCol_TableBorderStrong] = s1;
    c[ImGuiCol_TableBorderLight] = mixc(rgb(palette.surface0), s1, 0.5f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(text.x, text.y, text.z, 0.03f);
    c[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.30f);
    syncUiColors();
}

void applyOneSurface(const LookSettings& settings) {
    // The palette: Forge Night, unless the user chose colours of their own (a Forge Terminal scheme other than the
    // default, the desktop theme, Phosphor or Blueprint): theirs, laid flat the same way.
    looks::Palette palette = looks::forgeNightPalette();
    looks::LookId id = looks::LookId::ForgeTerminal;
    if (settings.look == int(looks::LookId::Phosphor)) {
        palette = looks::phosphorPalette(settings.amber);
        id = looks::LookId::Phosphor;
    } else if (settings.look == int(looks::LookId::Blueprint)) {
        palette = looks::blueprintPalette();
        id = looks::LookId::Blueprint;
    } else if (settings.look == int(looks::LookId::ForgeTerminal)) {
        looks::Palette omarchy;
        std::string name, error;
        if (settings.matchOmarchy && looks::readOmarchyTheme(omarchy, name, error)) palette = omarchy;
        else if (const looks::SchemeInfo* scheme = looks::findScheme(settings.scheme); scheme && settings.scheme != "neutral")
            palette = scheme->palette;
    }
    const std::vector<ImVec4>& classic = classicTokens();
    for (size_t i = 0; i < std::size(kTokens); ++i) *kTokens[i] = classic[i];
    setTokens(palette, id);
    look = LookStyle{};
    look.id = int(id);
    look.outlines = 0;
    look.roundingScale = 1.0f;
    // One surface: the bars, shelves, trays and menus are one colour with no borders between them; only the
    // controls inside (frames, buttons) step up from it.
    const ImVec4 surface = rgb(palette.mantle);
    shellBg = panelBg = header = surface;
    border = canvasBorder = tileBorder = focusBorder = surface;
    gapBg = surface;
    sectionRule = ImVec4(rgb(palette.surface1).x, rgb(palette.surface1).y, rgb(palette.surface1).z, 0.6f);
    applyStyle();
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = st.PopupRounding = 10.0f;
    st.ChildRounding = 8.0f;
    st.FrameRounding = st.GrabRounding = st.TabRounding = 6.0f;
    st.ScrollbarRounding = 4.0f;
    st.ScrollbarSize = 8.0f;
    st.WindowBorderSize = 0.0f;
    st.PopupBorderSize = 1.0f; // (a hairline keeps a menu apart from the model behind it)
    ImVec4* c = st.Colors;
    const ImVec4 s1 = rgb(palette.surface1), s2 = rgb(palette.surface2);
    c[ImGuiCol_WindowBg] = c[ImGuiCol_PopupBg] = c[ImGuiCol_ChildBg] = ImVec4(surface.x, surface.y, surface.z, 1.0f);
    c[ImGuiCol_Border] = ImVec4(1.0f, 1.0f, 1.0f, 0.08f); // ForgeTokens kLine
    c[ImGuiCol_FrameBgActive] = s1;
    c[ImGuiCol_TitleBg] = c[ImGuiCol_TitleBgActive] = c[ImGuiCol_TitleBgCollapsed] = surface;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = s1;
    c[ImGuiCol_ScrollbarGrabHovered] = s2;
    c[ImGuiCol_Separator] = s1;
    c[ImGuiCol_TabSelected] = c[ImGuiCol_TabDimmedSelected] = s1;
    c[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.30f);
    syncUiColors();
}

void lookViewportGradient(ImVec4& centre, ImVec4& edge) {
    centre = g_gradCentre;
    edge = g_gradEdge;
}

} // namespace sf::theme
