// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Ui.h"

#include "app/App.h"
#include "app/Shell.h"
#include "app/Theme.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace sf::ui {

namespace {

// The Ctrl-help request for this frame: the last hovered item that asked for help wins.
struct HelpRequest {
    bool active = false;
    std::string title, shortcut, description, path, command, actionId;
    const char* icon = nullptr;
    bool disabled = false;
    std::vector<ParamSpec> params;
    bool hasCommand = false;
};

HelpRequest g_help;
std::string g_shown;
bool g_popupInfo = true;
int g_nextShelf = ShelfNone, g_nextIndex = -1;

ImVec4 rgba(ImU32 c, float alpha = -1.0f) {
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
    if (alpha >= 0.0f) v.w = alpha;
    return v;
}

float uiScale() { return ImGui::GetFontSize() / 15.0f; }

bool itemHoveredForHelp() {
    return ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
}

bool wantHelp() { return g_popupInfo && CtrlHeld(); }

const char* typeName(ParamSpec::Type t) {
    switch (t) {
    case ParamSpec::Type::Bool: return "true/false";
    case ParamSpec::Type::Number: return "number";
    case ParamSpec::Type::String: return "text";
    }
    return "";
}

void takeOrigin(int& shelf, int& index) {
    shelf = g_nextShelf;
    index = g_nextIndex;
    g_nextShelf = ShelfNone;
    g_nextIndex = -1;
}

bool dragSource(App& app, const char* itemId, const char* label, int shelf, int index) {
    if (!app.layout().customize || !CtrlHeld() || !AltHeld()) return false;
    if (!ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) return false;
    UiItemPayload p;
    std::snprintf(p.id, sizeof p.id, "%s", itemId);
    p.fromShelf = shelf;
    p.fromIndex = index;
    ImGui::SetDragDropPayload(kUiItemPayload, &p, sizeof p);
    ImGui::TextUnformatted(label);
    ImGui::TextColored(rgba(color::textDim), shelf >= 0 ? "Drop on a shelf to move, elsewhere to remove" : "Drop on a shelf to add");
    ImGui::EndDragDropSource();
    return true;
}

} // namespace

bool CtrlHeld() {
    const ImGuiIO& io = ImGui::GetIO();
    return io.KeyCtrl || io.KeySuper || (SDL_GetModState() & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
}

bool AltHeld() { return ImGui::GetIO().KeyAlt || (SDL_GetModState() & SDL_KMOD_ALT) != 0; }

void SetNextItemOrigin(int shelf, int index) {
    g_nextShelf = shelf;
    g_nextIndex = index;
}

bool UiItemDragSource(App& app, const char* itemId, const char* label) {
    int shelf, index;
    takeOrigin(shelf, index);
    return dragSource(app, itemId, label, shelf, index);
}

void HelpText(const char* title, const char* shortcut, const char* description, const char* path) {
    if (!wantHelp() || !itemHoveredForHelp()) return;
    g_help = HelpRequest{};
    g_help.active = true;
    g_help.title = title ? title : "";
    g_help.shortcut = shortcut ? shortcut : "";
    g_help.description = description ? description : "";
    g_help.path = path ? path : "";
    g_help.disabled = (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) != 0;
}

bool IsModeAction(std::string_view id) {
    auto starts = [&](std::string_view p) { return id.substr(0, p.size()) == p; };
    if (id == "tool.draw" || id == "tool.move" || id == "tool.scale" || id == "tool.rotate") return true;
    if (id == "draw.sculpt" || id == "draw.paint") return true;
    return starts("brush.") || starts("mask.") || starts("select.");
}

void DrawOnMark(ImDrawList* dl, ImVec2 a, ImVec2 b, bool side) {
    const float w = std::max(1.0f, std::floor(theme::markWidth * uiScale() + 0.5f));
    const float r = ImGui::GetStyle().FrameRounding;
    if (side) dl->AddRectFilled(a, ImVec2(a.x + w, b.y), theme::u32(theme::accent), r, ImDrawFlags_RoundCornersLeft);
    else dl->AddRectFilled(ImVec2(a.x, b.y - w), b, theme::u32(theme::accent), r, ImDrawFlags_RoundCornersBottom);
}

void Segmented(App& app, std::initializer_list<const char*> actionIds, float width) {
    const float total = width > 0.0f ? width : ImGui::GetContentRegionAvail().x;
    const float w = (total - float(actionIds.size() - 1)) / float(actionIds.size());
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(1.0f, ImGui::GetStyle().ItemSpacing.y));
    bool first = true;
    for (const char* id : actionIds) {
        if (!first) ImGui::SameLine();
        first = false;
        ActionButton(app, id, ImVec2(w, 0));
        HelpAction(app, id);
    }
    ImGui::PopStyleVar();
}

void HelpAction(App& app, const char* actionId) {
    if (!wantHelp() || !itemHoveredForHelp()) return;
    const Action* a = app.actions().find(actionId);
    if (!a) return;
    g_help = HelpRequest{};
    g_help.active = true;
    g_help.title = a->label;
    g_help.actionId = a->id;
    g_help.icon = a->icon;
    g_help.shortcut = app.actions().shortcutText(a->id);
    g_help.path = a->palette;
    g_help.command = a->command;
    g_help.disabled = a->enabled && !a->enabled();
    g_help.description = a->description;
    if (const Command* c = a->command.empty() ? nullptr : app.commands().find(a->command)) {
        if (g_help.description.empty()) g_help.description = c->description;
        g_help.params = c->params;
        g_help.hasCommand = true;
    }
}

float KeycapsWidth(const std::string& chord) {
    const float s = uiScale(), pad = 5.0f * s, gap = 3.0f * s;
    float w = 0.0f;
    size_t start = 0;
    bool firstChord = true;
    while (start <= chord.size()) {
        const size_t at = chord.find(" / ", start);
        const std::string part = chord.substr(start, at == std::string::npos ? std::string::npos : at - start);
        if (!firstChord) w += ImGui::CalcTextSize("/").x + 2 * gap;
        firstChord = false;
        const auto keys = chordKeys(part);
        for (size_t i = 0; i < keys.size(); ++i) {
            if (i) w += ImGui::CalcTextSize("+").x + 2 * gap;
            w += ImGui::CalcTextSize(keys[i].c_str()).x + 2 * pad;
        }
        if (at == std::string::npos) break;
        start = at + 3;
    }
    return w;
}

float KeycapsAt(ImDrawList* dl, ImVec2 p, const std::string& chord) {
    const float s = uiScale(), pad = 5.0f * s, gap = 3.0f * s;
    const float h = ImGui::GetTextLineHeight() + 2.0f * s;
    const float x0 = p.x;
    auto dimText = [&](const char* t) {
        p.x += gap;
        dl->AddText(ImVec2(p.x, p.y + 1.0f * s), color::textDim, t);
        p.x += ImGui::CalcTextSize(t).x + gap;
    };
    size_t start = 0;
    bool firstChord = true;
    while (start <= chord.size()) {
        const size_t at = chord.find(" / ", start);
        const std::string part = chord.substr(start, at == std::string::npos ? std::string::npos : at - start);
        if (!firstChord) dimText("/");
        firstChord = false;
        const auto keys = chordKeys(part);
        for (size_t i = 0; i < keys.size(); ++i) {
            if (i) dimText("+");
            const ImVec2 ts = ImGui::CalcTextSize(keys[i].c_str());
            const ImVec2 a = p, b(p.x + ts.x + 2 * pad, p.y + h);
            dl->AddRectFilled(a, b, IM_COL32(0x2c, 0x2c, 0x30, 0xff), 3.0f * s);
            dl->AddRect(a, b, IM_COL32(0x4a, 0x4a, 0x50, 0xff), 3.0f * s);
            dl->AddText(ImVec2(a.x + pad, a.y + 1.0f * s), color::accent, keys[i].c_str());
            p.x = b.x;
        }
        if (at == std::string::npos) break;
        start = at + 3;
    }
    return p.x - x0;
}

void Keycaps(const std::string& chord) {
    if (chord.empty()) return;
    const float w = KeycapsAt(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), chord);
    ImGui::Dummy(ImVec2(w, ImGui::GetTextLineHeight() + 2.0f * uiScale()));
    if (ImGui::GetCurrentContext()->LogEnabled) ImGui::LogText(" %s", chord.c_str());
}

void DelayedTooltip(const char* text) {
    if (!text || !*text || CtrlHeld()) return;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay)) ImGui::SetTooltip("%s", text);
}

bool ActionButton(App& app, const char* actionId, ImVec2 size, Look look) {
    int shelf, index;
    takeOrigin(shelf, index);
    const Action* a = app.actions().find(actionId);
    if (!a) {
        ImGui::BeginDisabled();
        ImGui::Button((std::string("?") + actionId).c_str(), size);
        ImGui::EndDisabled();
        return false;
    }
    // Copy what we need: running the action may add or replace actions.
    const std::string id = a->id, label = a->label;
    const char* icon = a->icon;
    const std::function<void()> run = a->run;
    const bool enabled = !a->enabled || a->enabled();
    const bool checked = a->checked && a->checked();
    const float s = uiScale();
    if (look == Look::Icon && !icon) look = Look::Label;
    if (look == Look::IconLabel && !icon) look = Look::Label;
    if (look == Look::Icon && size.x <= 0.0f) size = ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight());
    if (look == Look::Tile && size.x <= 0.0f) size = ImVec2(44.0f * s, 40.0f * s);

    // On: the accent fill for the current mode, a raised grey with an accent underline for an option.
    const bool mode = IsModeAction(id);
    int colors = 0;
    if (checked) {
        ImGui::PushStyleColor(ImGuiCol_Button, mode ? theme::accent : theme::toggleOn);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, mode ? theme::accentHover : theme::toggleOnHover);
        ImGui::PushStyleColor(ImGuiCol_Text, mode ? theme::textOnAccent : theme::textBright);
        colors = 3;
    }
    ImGui::BeginDisabled(!enabled);
    bool clicked = false;
    const std::string itemLabel = label + "##" + id;
    bool clipped = false;
    if (look == Look::Label) {
        clicked = ImGui::Button(itemLabel.c_str(), size);
        const float textW = ImGui::CalcTextSize(itemLabel.c_str(), nullptr, true).x + 2.0f * ImGui::GetStyle().FramePadding.x;
        clipped = size.x > 0.0f && textW > size.x + 0.5f;
    } else if (look == Look::IconLabel) {
        // Drawn by hand so the item keeps the plain label as its name ("Store Config", not glyph + label).
        const std::string text = std::string(icon) + "  " + label;
        const ImGuiStyle& st = ImGui::GetStyle();
        const ImVec2 ts = ImGui::CalcTextSize(text.c_str());
        const ImVec2 full = ImGui::CalcItemSize(size, ts.x + st.FramePadding.x * 2.0f, ts.y + st.FramePadding.y * 2.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
        clicked = ImGui::Button(itemLabel.c_str(), full);
        ImGui::PopStyleColor();
        const ImRect r(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        ImGui::RenderTextClipped(ImVec2(r.Min.x + st.FramePadding.x, r.Min.y + st.FramePadding.y),
                                 ImVec2(r.Max.x - st.FramePadding.x, r.Max.y - st.FramePadding.y), text.c_str(), nullptr, &ts,
                                 st.ButtonTextAlign, &r);
        clipped = ts.x + st.FramePadding.x * 2.0f > full.x + 0.5f;
    } else if (look == Look::Tile) {
        // Shelf tiles (Shell.cpp): brush thumbnails on the bottom shelf, a large icon over a caption on the
        // right shelf. The item keeps the plain label as its name.
        clicked = drawShelfTile(app, *a, size);
    } else {
        // Icon: the label stays the item's name (for tests and screen readers) but is drawn by hand.
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
        clicked = ImGui::Button(itemLabel.c_str(), size);
        ImGui::PopStyleColor();
        const ImRect r(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImU32 fg = !enabled ? color::textDim : checked ? theme::u32(mode ? theme::textOnAccent : theme::textBright)
                                                              : ImGui::GetColorU32(ImGuiCol_Text);
        if (look == Look::Icon) {
            const ImVec2 is = ImGui::CalcTextSize(icon);
            dl->AddText(ImVec2(std::floor(r.GetCenter().x - is.x * 0.5f), std::floor(r.GetCenter().y - is.y * 0.5f)), fg, icon);
        } else {
            const ImVec2 is = ImGui::CalcTextSize(icon ? icon : "");
            const float small = 0.8f;
            ImFont* font = ImGui::GetFont();
            const float fs = ImGui::GetFontSize() * small;
            const ImVec2 ls = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, label.c_str());
            const float total = (icon ? is.y : 0.0f) + ls.y;
            float y = r.GetCenter().y - total * 0.5f;
            if (icon) {
                dl->AddText(ImVec2(std::floor(r.GetCenter().x - is.x * 0.5f), std::floor(y)), fg, icon);
                y += is.y;
            }
            const float maxW = r.GetWidth() - 2.0f * s;
            clipped = ls.x > maxW;
            const float x = clipped ? r.Min.x + 1.0f * s : r.GetCenter().x - ls.x * 0.5f;
            const ImVec4 clip(r.Min.x + 1.0f * s, r.Min.y, r.Max.x - 1.0f * s, r.Max.y);
            dl->AddText(font, fs, ImVec2(std::floor(x), std::floor(y)), fg, label.c_str(), nullptr, 0.0f, &clip);
        }
        clipped = clipped || look == Look::Icon;
    }
    ImGui::EndDisabled();
    if (colors) ImGui::PopStyleColor(colors);
    if (checked && !mode && look != Look::Tile)
        DrawOnMark(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

    const bool customize = app.layout().customize;
    dragSource(app, id.c_str(), label.c_str(), shelf, index);
    HelpAction(app, id.c_str());
    if (clipped) {
        const std::string chord = app.actions().shortcutText(id);
        DelayedTooltip((chord.empty() ? label : label + "   " + chord).c_str());
    }
    if (!clicked) return false;
    if (CtrlHeld() && AltHeld()) {
        // Ctrl+Alt+click: customize mode drags instead; otherwise it assigns a shortcut (ZBrush).
        if (!customize) app.beginShortcutCapture(id);
        return false;
    }
    if (run) run();
    return true;
}

bool SliderInside(const char* id, float* v, float lo, float hi, const char* format, ImGuiSliderFlags flags) {
    const float w = ImGui::CalcItemWidth(), h = ImGui::GetFrameHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // Frame and fill go underneath the slider (which draws a transparent frame on top).
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(1, 1, 1, 0.04f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(1, 1, 1, 0.06f));
    // No grab over the label: the position shows as the fill and a thin accent bar along the bottom (ZBrush).
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0, 0, 0, 0));
    const bool changed = ImGui::SliderFloat(id, v, lo, hi, format, flags);
    const bool active = ImGui::IsItemActive();
    ImGui::PopStyleColor(5);
    dl->ChannelsSetCurrent(0);
    float t = 0.0f;
    if (hi > lo) {
        if ((flags & ImGuiSliderFlags_Logarithmic) && lo > 0.0f)
            t = std::log(std::max(*v, lo) / lo) / std::log(hi / lo);
        else
            t = (*v - lo) / (hi - lo);
    }
    t = std::clamp(t, 0.0f, 1.0f);
    const float rounding = ImGui::GetStyle().FrameRounding;
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(ImGuiCol_FrameBg), rounding);
    dl->AddRectFilled(p, ImVec2(p.x + w * t, p.y + h), theme::u32(theme::sliderFill, active ? 1.6f : 1.0f), rounding);
    const float bar = std::max(2.0f, std::floor(h * 0.1f));
    dl->AddRectFilled(ImVec2(p.x, p.y + h - bar), ImVec2(p.x + w * t, p.y + h), active ? color::accentHover : color::accent,
                      rounding, ImDrawFlags_RoundCornersBottomLeft);
    dl->ChannelsMerge();
    return changed;
}

void DrawHelp(App& app) {
    const UiPrefs& prefs = app.uiPrefs();
    g_popupInfo = prefs.popupInfo;
    ImGui::GetStyle().HoverDelayNormal = std::max(0.05f, prefs.tooltipDelay);
    g_shown.clear();
    if (!g_help.active) return;
    HelpRequest r = std::move(g_help);
    g_help = HelpRequest{};
    if (!wantHelp()) return;
    const bool alternate = AltHeld() && r.hasCommand; // Ctrl+Alt: the scripting reference

    const float s = uiScale(), width = 340.0f * s;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    ImVec2 size(width, 120.0f * s);
    if (ImGuiWindow* last = ImGui::FindWindowByName("##sf_help")) size.y = last->Size.y;
    ImVec2 pos(mouse.x + 18.0f * s, mouse.y + 18.0f * s);
    if (pos.x + size.x > vp->Pos.x + vp->Size.x - 4.0f * s) pos.x = mouse.x - 12.0f * s - size.x;
    if (pos.y + size.y > vp->Pos.y + vp->Size.y - 4.0f * s) pos.y = mouse.y - 12.0f * s - size.y;
    pos.x = std::max(pos.x, vp->Pos.x + 4.0f * s);
    pos.y = std::max(pos.y, vp->Pos.y + 4.0f * s);

    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0), ImVec2(width, FLT_MAX));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * s, 9.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f * s);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f * s, 4.0f * s));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, rgba(IM_COL32(0x18, 0x18, 0x1a, 0xff), std::clamp(prefs.helpOpacity, 0.3f, 1.0f)));
    ImGui::PushStyleColor(ImGuiCol_Border, rgba(color::accent, 0.6f));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize;
    if (ImGui::Begin("##sf_help", nullptr, flags)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        // Title row: icon + title (SemiBold), keycaps on the right.
        const float keysW = r.shortcut.empty() ? 0.0f : KeycapsWidth(r.shortcut);
        ImGui::PushFont(app.helpTitleFont());
        const std::string title = (r.icon ? std::string(r.icon) + "  " : std::string()) + r.title +
                                  (alternate ? "  \xc2\xb7  script" : "");
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - keysW - 8.0f * s);
        ImGui::TextUnformatted(title.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        if (!r.shortcut.empty()) {
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - keysW);
            Keycaps(r.shortcut);
        }
        {
            const ImVec2 a = ImGui::GetCursorScreenPos();
            dl->AddLine(ImVec2(a.x, a.y + 1.0f * s), ImVec2(a.x + ImGui::GetContentRegionAvail().x, a.y + 1.0f * s),
                        IM_COL32(0x34, 0x34, 0x38, 0xff));
            ImGui::Dummy(ImVec2(0, 3.0f * s));
        }
        ImGui::PushTextWrapPos(0.0f);
        if (!alternate) {
            ImGui::PushStyleColor(ImGuiCol_Text, rgba(IM_COL32(0xc8, 0xc8, 0xc8, 0xff)));
            ImGui::TextUnformatted(r.description.empty() ? "No description yet." : r.description.c_str());
            ImGui::PopStyleColor();
            if (r.hasCommand && !r.params.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, rgba(color::textDim));
                ImGui::TextUnformatted("Hold Alt for the scripting parameters.");
                ImGui::PopStyleColor();
            }
        } else {
            ImGui::PushFont(app.consoleFont());
            std::string usage = r.command;
            for (const auto& p : r.params)
                usage += p.required ? " " + p.name + "=<" + typeName(p.type) + ">" : " [" + p.name + "=...]";
            ImGui::TextColored(rgba(color::accent), "%s", usage.c_str());
            ImGui::PopFont();
            if (r.params.empty()) ImGui::TextColored(rgba(color::textDim), "No parameters.");
            for (const auto& p : r.params) {
                ImGui::PushFont(app.consoleFont());
                ImGui::TextColored(rgba(color::text), "%s", p.name.c_str());
                ImGui::PopFont();
                ImGui::SameLine();
                ImGui::TextColored(rgba(color::textDim), "%s%s", typeName(p.type), p.required ? ", required" : "");
                ImGui::Indent(10.0f * s);
                ImGui::PushStyleColor(ImGuiCol_Text, rgba(IM_COL32(0xc8, 0xc8, 0xc8, 0xff)));
                ImGui::TextUnformatted(p.description.c_str());
                ImGui::PopStyleColor();
                ImGui::Unindent(10.0f * s);
            }
        }
        if (r.disabled) ImGui::TextColored(rgba(color::danger), "Not available right now.");
        ImGui::PopTextWrapPos();
        // The user guide chapter about it: F1 (with Ctrl still held) opens it.
        if (const std::string chapter = UserGuide::chapterFor(r.path, r.actionId); !chapter.empty()) {
            const std::string title = app.guide().chapterTitle(chapter);
            if (!title.empty()) {
                ImGui::Dummy(ImVec2(0, 1.0f * s));
                Keycaps("F1");
                ImGui::SameLine();
                ImGui::TextColored(rgba(color::textDim), "Learn more: %s", title.c_str());
                if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) app.guide().open(chapter);
                r.description += "\nLearn more: " + title;
            }
        }
        // Footer: where it lives (dim) and the script command (mono, right).
        if (!r.path.empty() || !r.command.empty()) {
            ImGui::Dummy(ImVec2(0, 2.0f * s));
            ImGui::SetWindowFontScale(0.82f);
            if (!r.path.empty()) ImGui::TextColored(rgba(color::textDim), "%s", r.path.c_str());
            ImGui::SetWindowFontScale(1.0f);
            if (!r.command.empty()) {
                ImGui::PushFont(app.consoleFont());
                const float cw = ImGui::CalcTextSize(r.command.c_str()).x;
                if (!r.path.empty()) ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - cw);
                else ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - cw);
                ImGui::TextColored(rgba(color::textDim), "%s", r.command.c_str());
                ImGui::PopFont();
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(4);

    g_shown = r.title + "\n" + r.shortcut + "\n" + r.description + "\n" + r.path + "\n" + r.command;
    for (const auto& p : r.params) g_shown += "\n" + p.name + ": " + p.description;
}

const std::string& HelpShown() { return g_shown; }

} // namespace sf::ui
