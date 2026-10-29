// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Theme.h"

namespace sf::theme {

void applyStyle() {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0.0f; // shells; floating windows keep square corners too, like ZBrush
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
    c[ImGuiCol_WindowBg] = ImVec4(panelBg.x, panelBg.y, panelBg.z, 0.98f);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = ImVec4(panelBg.x, panelBg.y, panelBg.z, 0.98f);
    c[ImGuiCol_Border] = rgb(0x3a3a3f, 0.9f);
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = frame;
    c[ImGuiCol_FrameBgHovered] = frameHover;
    c[ImGuiCol_FrameBgActive] = rgb(0x3e3e44);
    c[ImGuiCol_TitleBg] = header;
    c[ImGuiCol_TitleBgActive] = rgb(0x333338);
    c[ImGuiCol_TitleBgCollapsed] = header;
    c[ImGuiCol_MenuBarBg] = shellBg;
    c[ImGuiCol_ScrollbarBg] = rgb(0x1a1a1c, 0.6f);
    c[ImGuiCol_ScrollbarGrab] = rgb(0x48484e);
    c[ImGuiCol_ScrollbarGrabHovered] = rgb(0x58585f);
    c[ImGuiCol_ScrollbarGrabActive] = accent;
    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = accentHover;
    c[ImGuiCol_Button] = button;
    c[ImGuiCol_ButtonHovered] = buttonHover;
    c[ImGuiCol_ButtonActive] = buttonPressed;
    c[ImGuiCol_Header] = selection; // selected rows: a tint, not a solid bar (the accent is for the current tool)
    c[ImGuiCol_HeaderHovered] = headerHover;
    c[ImGuiCol_HeaderActive] = rgb(0xf0882e, 0.28f);
    c[ImGuiCol_Separator] = rgb(0x3a3a3f);
    c[ImGuiCol_SeparatorHovered] = accentHover;
    c[ImGuiCol_SeparatorActive] = accent;
    c[ImGuiCol_ResizeGrip] = rgb(0x48484e, 0.5f);
    c[ImGuiCol_ResizeGripHovered] = accentHover;
    c[ImGuiCol_ResizeGripActive] = accent;
    c[ImGuiCol_Tab] = header;
    c[ImGuiCol_TabHovered] = headerHover;
    c[ImGuiCol_TabSelected] = rgb(0x45454b);
    c[ImGuiCol_TabSelectedOverline] = accent;
    c[ImGuiCol_TabDimmed] = header;
    c[ImGuiCol_TabDimmedSelected] = rgb(0x3a3a3f);
    c[ImGuiCol_PlotHistogram] = accent;
    c[ImGuiCol_PlotHistogramHovered] = accentHover;
    c[ImGuiCol_TableHeaderBg] = header;
    c[ImGuiCol_TableBorderStrong] = rgb(0x3a3a3f);
    c[ImGuiCol_TableBorderLight] = rgb(0x323236);
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = rgb(0xffffff, 0.025f);
    c[ImGuiCol_TextSelectedBg] = rgb(0xf0882e, 0.30f);
    c[ImGuiCol_DragDropTarget] = accentHover;
    c[ImGuiCol_NavCursor] = accent;
    c[ImGuiCol_ModalWindowDimBg] = rgb(0x000000, 0.45f);
}

} // namespace sf::theme
