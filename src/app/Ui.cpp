// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Ui.h"

#include "app/App.h"
#include "app/I18n.h"
#include "app/Shell.h"
#include "app/Theme.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

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

// A colour of the Classic look, or the current look's token.
ImU32 lookColor(ImU32 classic, const ImVec4& token) { return theme::look.id == 0 ? classic : theme::u32(token); }

// Hover and focus feedback (Ui.h). Items report where they are through the ImGui item hooks; DrawHover rings the
// hovered and the keyboard-focused one at the end of the frame, in that item's window, on top of what it drew.
constexpr float kHoverSeconds = 0.10f; // the ease-in (80-120 ms)
struct Anim {
    float t = 0.0f; // linear 0-1
    int frame = 0;
};
std::unordered_map<ImGuiID, Anim> g_anims;
bool g_reduceMotion = false, g_animating = false, g_animatingLast = false;
struct Seen {
    ImGuiID id = 0;
    ImRect bb, clip;
    ImGuiWindow* window = nullptr;
    bool labelled = false;
};
std::vector<Seen> g_seen;   // this frame: the items that are or were hovered or focused
std::vector<ImGuiID> g_rings; // the ids with a ring showing or fading
std::vector<ImGuiID> g_noRing;
int g_seenFrame = -1;
int g_ringsShown = 0, g_liftsShown = 0, g_liftsCounting = 0;

float easeOut(float t) { return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t); }

bool focusedByKeyboard(ImGuiID id) {
    const ImGuiContext& g = *GImGui;
    return id && g.NavCursorVisible && g.NavId == id && g.NavWindow;
}

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
    g_help.title = title ? tr(title) : "";
    g_help.shortcut = shortcut ? shortcut : "";
    g_help.description = description ? tr(description) : "";
    g_help.path = path ? path : "";
    g_help.disabled = (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) != 0;
}

bool IsModeAction(std::string_view id) {
    auto starts = [&](std::string_view p) { return id.substr(0, p.size()) == p; };
    if (id == "tool.draw" || id == "tool.move" || id == "tool.scale" || id == "tool.rotate") return true;
    if (id == "draw.sculpt" || id == "draw.paint") return true;
    return starts("brush.") || starts("mask.") || starts("select.");
}

bool IsChoiceAction(std::string_view id) {
    return id == "draw.zadd" || id == "draw.zsub" || id == "material.smooth" || id == "material.flat" ||
           id == "view.background_gradient" || id == "view.background_flat" || id == "view.shading_lit_sphere" ||
           id == "view.shading_pbr" ||
           id.substr(0, 16) == "subtool.shading_" || id.substr(0, 8) == "uv.mode_";
}

void CentredGlyph(ImDrawList* dl, ImFont* font, ImVec2 a, ImVec2 b, ImU32 col, const char* glyph) {
    if (!font) font = ImGui::GetFont();
    const float size = ImGui::GetFontSize() > 0.0f && font == ImGui::GetFont() ? ImGui::GetFontSize() : font->FontSize;
    unsigned int c = 0;
    const int n = ImTextCharFromUtf8(&c, glyph, nullptr);
    const ImFontGlyph* g = n > 0 && glyph[n] == '\0' ? font->FindGlyphNoFallback(ImWchar(c)) : nullptr;
    if (!g || g->X1 <= g->X0 || g->Y1 <= g->Y0) {
        const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, glyph);
        dl->AddText(font, size, ImVec2(std::floor((a.x + b.x - ts.x) * 0.5f), std::floor((a.y + b.y - ts.y) * 0.5f)), col, glyph);
        return;
    }
    // The ink box (X0..X1, Y0..Y1, offsets included) relative to where AddText puts the pen, at this size.
    const float k = size / font->FontSize;
    const float x = (a.x + b.x) * 0.5f - (g->X0 + g->X1) * 0.5f * k, y = (a.y + b.y) * 0.5f - (g->Y0 + g->Y1) * 0.5f * k;
    dl->AddText(font, size, ImVec2(std::round(x), std::round(y)), col, glyph);
}

bool GlyphButton(const char* id, const char* glyph, ImVec2 size) {
    const bool clicked = ImGui::Button(id, size);
    CentredGlyph(ImGui::GetWindowDrawList(), nullptr, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                 ImGui::GetColorU32(ImGuiCol_Text), glyph);
    return clicked;
}

bool Checkbox(const char* label, bool* v) {
    const bool changed = ImGui::Checkbox(tr(label), v);
    if (!*v) {
        const ImVec2 a = ImGui::GetItemRectMin();
        const float sz = ImGui::GetFrameHeight();
        ImGui::GetWindowDrawList()->AddRect(a, ImVec2(a.x + sz, a.y + sz), theme::u32(theme::checkBorder),
                                            ImGui::GetStyle().FrameRounding, 0, std::max(1.0f, std::floor(uiScale())));
    }
    return changed;
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
    g_help.title = trAction(a->id, a->label);
    g_help.actionId = a->id;
    g_help.icon = a->icon;
    g_help.shortcut = app.actions().shortcutText(a->id);
    g_help.path = a->palette;
    g_help.command = a->command;
    g_help.disabled = a->enabled && !a->enabled();
    g_help.description = tr(a->description);
    if (const Command* c = a->command.empty() ? nullptr : app.commands().find(a->command)) {
        if (g_help.description.empty()) g_help.description = tr(c->description);
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
            dl->AddRectFilled(a, b, lookColor(IM_COL32(0x2c, 0x2c, 0x30, 0xff), theme::frame), theme::rounding(3.0f * s));
            dl->AddRect(a, b, lookColor(IM_COL32(0x4a, 0x4a, 0x50, 0xff), theme::tileBorder), theme::rounding(3.0f * s));
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

float HoverAmount(ImGuiID id, bool on) {
    auto it = g_anims.find(id);
    if (it == g_anims.end()) {
        if (!on) return 0.0f;
        it = g_anims.emplace(id, Anim{}).first;
    }
    Anim& a = it->second;
    const int frame = ImGui::GetFrameCount();
    if (a.frame != frame) {
        a.frame = frame;
        const float step = g_reduceMotion ? 1.0f : std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.05f) / kHoverSeconds;
        a.t = on ? std::min(1.0f, a.t + step) : std::max(0.0f, a.t - step);
    }
    if (a.t > 0.0f && a.t < 1.0f) g_animating = true;
    const float t = easeOut(a.t);
    if (!on && a.t <= 0.0f) g_anims.erase(it);
    return t;
}

bool HoveredOrFocused(ImGuiID id) { return id && (ImGui::GetHoveredID() == id || focusedByKeyboard(id)); }

void NoHoverRing(ImGuiID id) {
    if (id) g_noRing.push_back(id);
}

bool HoverAnimating() { return g_animating || g_animatingLast; }
bool ReduceMotion() { return g_reduceMotion; }
int HoverRingsShown() { return g_ringsShown; }
int HoverLiftsShown() { return g_liftsShown; }

void NoteItem(ImGuiContext* ctx, ImGuiID id, const ImVec2& min, const ImVec2& max) {
    if (!id || !ctx || !ctx->CurrentWindow) return;
    if (g_seenFrame != ctx->FrameCount) {
        g_seenFrame = ctx->FrameCount;
        g_seen.clear();
        g_noRing.clear();
    }
    bool want = id == ctx->HoveredIdPreviousFrame || id == ctx->HoveredId || id == ctx->NavId || id == ctx->ActiveId;
    for (size_t i = 0; !want && i < g_rings.size(); ++i) want = g_rings[i] == id;
    if (!want) return;
    g_seen.push_back({id, ImRect(min, max), ctx->CurrentWindow->ClipRect, ctx->CurrentWindow, false});
}

void NoteItemLabel(ImGuiID id) {
    for (auto it = g_seen.rbegin(); it != g_seen.rend(); ++it)
        if (it->id == id) {
            it->labelled = true;
            return;
        }
}

namespace {

// The look's hover ring round `r` at strength t (0-1): a tinted fill and a crisp border in every look, plus its flavour.
void drawRing(ImDrawList* dl, ImRect r, float t, float rounding, bool fill) {
    const float s = uiScale(), px = std::max(1.0f, std::floor(s + 0.25f));
    const ImVec4& ac = theme::accent;
    auto rect = [&](float grow, const ImVec4& c, float alpha, float w) {
        dl->AddRect(ImVec2(r.Min.x - grow, r.Min.y - grow), ImVec2(r.Max.x + grow, r.Max.y + grow), theme::u32(c, alpha * t),
                    rounding > 0.0f ? rounding + grow : 0.0f, 0, w);
    };
    const int look = theme::look.id;
    // Outlines (Preferences > Interface > Look) sets how loud the ring is: Off none (the fill and label only), Subtle
    // (the default) one faint line, On the look's full flavour; Outline opacity scales the lines.
    const int outlines = theme::look.outlines;
    const float op = theme::look.outlineOpacity;
    if (fill) dl->AddRectFilled(r.Min, r.Max, theme::u32(ac, (look == 2 ? 0.16f : look == 0 ? 0.07f : 0.10f) * t), rounding);
    // (Strokes sit half a stroke inside the rect, so the border is crisp and inside the item.)
    r.Min.x += 0.5f * px, r.Min.y += 0.5f * px, r.Max.x -= 0.5f * px, r.Max.y -= 0.5f * px;
    if (outlines == 0 || op <= 0.0f) return;
    t *= op;
    if (outlines == 1) {
        rect(0.0f, look == 0 ? theme::accentHover : ac, 0.40f, px);
        return;
    }
    switch (look) {
    case 0: // Classic: a subtle warm highlight
        rect(0.0f, theme::accentHover, 0.60f, px);
        break;
    case 2: // Phosphor: the inverse-video glow, light bleeding out of the item
        rect(1.0f * px, ac, 0.34f, px);
        rect(2.0f * px, ac, 0.16f, px);
        rect(3.0f * px, ac, 0.07f, px);
        rect(0.0f, ac, 1.0f, px);
        break;
    case 3: // Blueprint: a cyan line with a soft outer line, like a highlighted drawing stroke
        rect(1.5f * px, ac, 0.28f, 2.0f * px);
        rect(0.0f, ac, 1.0f, px);
        break;
    default: // Forge Terminal: the crisp accent border
        rect(0.0f, ac, 1.0f, std::max(px, std::floor(1.5f * s)));
        break;
    }
}

} // namespace

TileLift BeginTileLift(ImVec2 min, ImVec2 max, float rounding) {
    TileLift l;
    const ImGuiID id = ImGui::GetItemID();
    const bool disabled = (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) != 0;
    NoHoverRing(id);
    l.dl = ImGui::GetWindowDrawList();
    l.min = min, l.max = max;
    l.t = disabled ? 0.0f : HoverAmount(id, ImGui::IsItemHovered() || focusedByKeyboard(id));
    if (l.t > 0.0f && !g_reduceMotion) {
        // A soft shadow under the lifted tile (drawn at its lifted size), then let the tile's own drawing out of the
        // clip by the lift.
        const float s = uiScale(), k = 0.025f * l.t;
        const ImVec2 c((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f), h((max.x - min.x) * (0.5f + k), (max.y - min.y) * (0.5f + k));
        for (int i = 3; i >= 1; --i) {
            const float g = float(i) * 1.5f * s, dy = 2.5f * s * l.t;
            l.dl->AddRectFilled(ImVec2(c.x - h.x - g, c.y - h.y - g + dy), ImVec2(c.x + h.x + g, c.y + h.y + g + dy),
                                IM_COL32(0, 0, 0, int(30.0f * l.t)), theme::rounding(rounding * s) + g);
        }
        const ImVec4 cr = l.dl->_CmdHeader.ClipRect;
        const float m = std::max(h.x, h.y) * 0.06f + 1.0f;
        l.dl->PushClipRect(ImVec2(std::max(cr.x - m, min.x - m), std::max(cr.y - m, min.y - m)),
                           ImVec2(std::min(cr.z + m, max.x + m), std::min(cr.w + m, max.y + m)), false);
        l.clip = true;
    }
    l.vtx = l.dl->VtxBuffer.Size;
    return l;
}

void EndTileLift(const TileLift& l, float rounding) {
    if (!l.dl || l.t <= 0.0f) return;
    ++g_liftsCounting;
    const ImVec2 c((l.min.x + l.max.x) * 0.5f, (l.min.y + l.max.y) * 0.5f);
    const float scale = g_reduceMotion ? 1.0f : 1.0f + 0.05f * l.t;
    if (scale != 1.0f)
        for (int i = l.vtx; i < l.dl->VtxBuffer.Size; ++i) {
            ImVec2& p = l.dl->VtxBuffer[i].pos;
            p = ImVec2(c.x + (p.x - c.x) * scale, c.y + (p.y - c.y) * scale);
        }
    const ImVec2 h((l.max.x - l.min.x) * 0.5f * scale, (l.max.y - l.min.y) * 0.5f * scale);
    const float s = uiScale(), px = std::max(1.0f, std::floor(s + 0.25f));
    // The glow ring just outside the tile (the active tile's orange outline stays inside it).
    const ImRect r(ImVec2(c.x - h.x - px, c.y - h.y - px), ImVec2(c.x + h.x + px, c.y + h.y + px));
    drawRing(l.dl, r, l.t, theme::rounding(rounding * s), false);
    if (theme::look.id != 2 && theme::look.outlines == 2) // a soft second line: the glow (Phosphor's ring brings its own)
        l.dl->AddRect(ImVec2(r.Min.x - 1.5f * px, r.Min.y - 1.5f * px), ImVec2(r.Max.x + 1.5f * px, r.Max.y + 1.5f * px),
                      theme::u32(theme::accent, 0.22f * l.t * theme::look.outlineOpacity), theme::rounding(rounding * s) + 1.5f * px, 0, 2.0f * px);
    if (l.clip) l.dl->PopClipRect();
}

void DrawHover(App& app) {
    g_reduceMotion = app.uiPrefs().reduceMotion;
    g_animatingLast = g_animating;
    g_animating = false;
    g_liftsShown = g_liftsCounting;
    g_liftsCounting = 0;
    g_ringsShown = 0;
    const ImGuiContext& g = *GImGui;
    if (g_seenFrame != g.FrameCount) g_seen.clear(), g_noRing.clear(); // no items this frame
    const ImGuiID hovered = g.HoveredId, focused = g.NavCursorVisible && g.NavWindow ? g.NavId : 0;
    const float fh = ImGui::GetFrameHeight();
    auto find = [&](ImGuiID id) -> const Seen* {
        if (!id || std::find(g_noRing.begin(), g_noRing.end(), id) != g_noRing.end()) return nullptr;
        for (auto it = g_seen.rbegin(); it != g_seen.rend(); ++it)
            if (it->id == id) {
                // Only labelled widgets, and nothing as big as a view (the UV editor, the timeline's track).
                if (!it->labelled || (it->bb.GetWidth() > 16.0f * fh && it->bb.GetHeight() > 4.0f * fh)) return nullptr;
                return &*it;
            }
        return nullptr;
    };
    for (ImGuiID id : {hovered, focused})
        if (find(id) && std::find(g_rings.begin(), g_rings.end(), id) == g_rings.end()) g_rings.push_back(id);
    for (size_t i = 0; i < g_rings.size();) {
        const ImGuiID id = g_rings[i];
        const Seen* at = find(id);
        const bool on = at && (id == hovered || id == focused);
        const float t = HoverAmount(id, on);
        if (!at || t <= 0.0f) { // gone (its window closed, or it draws its own hover now), or faded out
            g_anims.erase(id);
            g_rings.erase(g_rings.begin() + std::ptrdiff_t(i));
            continue;
        }
        ImDrawList* dl = at->window->DrawList;
        const float m = 4.0f * uiScale();
        ImRect clip(at->clip.Min.x - m, at->clip.Min.y - m, at->clip.Max.x + m, at->clip.Max.y + m);
        clip.ClipWith(at->window->OuterRectClipped);
        clip.ClipWith(ImRect(ImVec2(0, 0), g.IO.DisplaySize));
        dl->PushClipRect(clip.Min, clip.Max, false);
        drawRing(dl, at->bb, t, ImGui::GetStyle().FrameRounding, id != g.ActiveId);
        dl->PopClipRect();
        ++g_ringsShown;
        ++i;
    }
    // Forget items that stopped animating long ago (a window closed under the pointer).
    const int frame = ImGui::GetFrameCount();
    for (auto it = g_anims.begin(); it != g_anims.end();)
        it = frame - it->second.frame > 120 ? g_anims.erase(it) : std::next(it);
}

void DelayedTooltip(const char* text) {
    if (!text || !*text || CtrlHeld()) return;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay)) ImGui::SetTooltip("%s", tr(text));
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
    const std::string id = a->id, label = trAction(a->id, a->label); // the visible words in the interface language (I18n.h)
    const char* icon = a->icon;
    const std::function<void()> run = a->run;
    const bool enabled = !a->enabled || a->enabled();
    const bool checked = a->checked && a->checked();
    const float s = uiScale();
    if (look == Look::Icon && !icon) look = Look::Label;
    if (look == Look::IconLabel && !icon) look = Look::Label;
    if (look == Look::Icon && size.x <= 0.0f) size = ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight());
    if (look == Look::Tile && size.x <= 0.0f) size = ImVec2(44.0f * s, 40.0f * s);

    // On: the accent fill for the current mode, a raised grey for the chosen side of a choice (Zadd | Zsub), a
    // raised grey with an accent underline for an option.
    const bool mode = IsModeAction(id), choice = IsChoiceAction(id);
    const std::string itemLabel = label + "##" + id;
    int colors = 0;
    if (checked) {
        ImGui::PushStyleColor(ImGuiCol_Button, mode ? theme::accent : theme::toggleOn);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, mode ? theme::accentHover : theme::toggleOnHover);
        ImGui::PushStyleColor(ImGuiCol_Text, mode ? theme::textOnAccent : theme::textBright);
        colors = 3;
    } else if (look != Look::Tile && enabled) {
        // Hover eases the fill towards the accent-tinted hover and brightens the label (Phosphor: inverse video, the
        // accent fill with a dark label); pressed is darker. (Hovered as of the last frame: the item isn't drawn yet.)
        const float h = HoverAmount(ImGui::GetID(itemLabel.c_str()), HoveredOrFocused(ImGui::GetID(itemLabel.c_str())));
        if (h > 0.0f) {
            auto mix = [h](const ImVec4& a, const ImVec4& b) {
                return ImVec4(a.x + (b.x - a.x) * h, a.y + (b.y - a.y) * h, a.z + (b.z - a.z) * h, a.w + (b.w - a.w) * h);
            };
            const bool inverse = theme::look.id == 2 && theme::look.outlines == 2; // (loud: Outlines On only)
            const ImVec4 fill = mix(ImGui::GetStyleColorVec4(ImGuiCol_Button), inverse ? theme::accent : theme::buttonHover);
            const ImVec4 ink = mix(ImGui::GetStyleColorVec4(ImGuiCol_Text), inverse ? theme::textOnAccent : theme::textHover);
            ImGui::PushStyleColor(ImGuiCol_Button, fill);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, fill);
            ImGui::PushStyleColor(ImGuiCol_Text, ink);
            colors = 3;
        }
    }
    ImGui::BeginDisabled(!enabled);
    bool clicked = false;
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
    if (checked && !mode && !choice && look != Look::Tile)
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
    // Looks with technical numbers draw the label in the monospace font, padded to the same frame height.
    ImFont* numbers = theme::numberFont();
    const float outerSize = ImGui::GetFontSize();
    if (numbers) {
        ImGui::PushFont(numbers);
        const ImVec2 pad = ImGui::GetStyle().FramePadding;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(pad.x, pad.y + (outerSize - ImGui::GetFontSize()) * 0.5f));
    }
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
    const bool changed = ImGui::SliderFloat(id, v, lo, hi, format ? tr(format) : format, flags);
    const bool active = ImGui::IsItemActive();
    const bool disabled = (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) != 0;
    const float hover = disabled ? 0.0f : HoverAmount(ImGui::GetItemID(), ImGui::IsItemHovered() || active || focusedByKeyboard(ImGui::GetItemID()));
    ImGui::PopStyleColor(5);
    if (numbers) {
        ImGui::PopStyleVar();
        ImGui::PopFont();
    }
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
    // The position: a quiet fill and a thin neutral bar along the bottom, the accent only while dragging, so a
    // palette of sliders isn't a stack of orange lines (docs/ui-design.md §10). Nothing at the very start, where a
    // rounded sliver would show as a stray stub.
    const float fillW = std::floor(w * t);
    if (fillW >= 1.0f) {
        const bool full = fillW >= w - 0.5f;
        dl->AddRectFilled(p, ImVec2(p.x + fillW, p.y + h), theme::u32(theme::sliderFill, active ? 1.5f : 1.0f + 0.35f * hover), rounding,
                          full ? ImDrawFlags_RoundCornersAll : ImDrawFlags_RoundCornersLeft);
        // Hover brightens the bar towards the accent and grows a knob at its end (Reduce motion: no growing).
        const float bar = std::max(1.0f, std::floor(2.0f * uiScale()));
        const ImVec4& b0 = theme::sliderBar;
        const ImVec4& b1 = theme::accent;
        const float k = active ? 1.0f : 0.6f * hover;
        const ImU32 barCol = theme::u32(ImVec4(b0.x + (b1.x - b0.x) * k, b0.y + (b1.y - b0.y) * k, b0.z + (b1.z - b0.z) * k, 1.0f));
        dl->AddRectFilled(ImVec2(p.x, p.y + h - bar), ImVec2(p.x + fillW, p.y + h), barCol, std::min(rounding, bar),
                          full ? ImDrawFlags_RoundCornersBottom : ImDrawFlags_RoundCornersBottomLeft);
        if (hover > 0.0f) {
            const float kw = bar * (ReduceMotion() ? 1.5f : 1.0f + 0.6f * hover), kh = bar * (ReduceMotion() ? 3.0f : 1.5f + 2.0f * hover);
            const float x = std::clamp(p.x + fillW, p.x + kw, p.x + w - kw);
            dl->AddRectFilled(ImVec2(x - kw, p.y + h - kh), ImVec2(x + kw, p.y + h), theme::u32(active ? theme::accent : theme::textBright, hover),
                              std::min(rounding, kw));
        }
    }
    dl->ChannelsMerge();
    // The value, exactly, while pointing at it or dragging (the label inside may round it or be clipped).
    if (hover > 0.0f && (ImGui::IsItemHovered() || active) && !CtrlHeld() && format) {
        const char* f = tr(format);
        const char* spec = ImParseFormatFindStart(f);
        if (*spec) {
            char buf[32] = "";
            const char* one = ImParseFormatTrimDecorations(spec, buf, sizeof(buf));
            if (active || ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_NoSharedDelay)) ImGui::SetTooltip(one, double(*v));
        }
    }
    return changed;
}

namespace {
ImGuiID g_dragScrollWindow = 0; // the panel a drag is scrolling (0: none)
ImGuiMouseButton g_dragScrollButton = ImGuiMouseButton_Left;
bool g_dragScrollMoved = false;
int g_dragScrolls = 0;
float g_dragScrollY = 0.0f;
} // namespace

void DragScrollPanels() {
    ImGuiContext& g = *GImGui;
    const ImGuiIO& io = g.IO;
    if (g_dragScrollWindow) {
        ImGuiWindow* w = ImGui::FindWindowByID(g_dragScrollWindow);
        const ImGuiID id = w ? w->GetID("##dragscroll") : 0;
        if (!w || !io.MouseDown[g_dragScrollButton] || (g.ActiveId != 0 && g.ActiveId != id)) {
            if (w && g.ActiveId == id) ImGui::ClearActiveID();
            if (g_dragScrollMoved) ++g_dragScrolls;
            g_dragScrollWindow = 0;
            return;
        }
        ImGui::KeepAliveID(id); // holds the press: nothing under the pointer reacts, the window doesn't move
        if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f) {
            if (w->ScrollMax.y > 0.0f) ImGui::SetScrollY(w, std::clamp(w->Scroll.y - io.MouseDelta.y, 0.0f, w->ScrollMax.y));
            if (w->ScrollMax.x > 0.0f) ImGui::SetScrollX(w, std::clamp(w->Scroll.x - io.MouseDelta.x, 0.0f, w->ScrollMax.x));
            g_dragScrollMoved = true;
            g_dragScrollY = w->ScrollTarget.y < FLT_MAX ? w->ScrollTarget.y : w->Scroll.y;
        }
        return;
    }
    // A press this frame: left only on empty space (nothing hovered or pressed) of a fixed panel, middle anywhere. The
    // nearest window that scrolls, from the one under the pointer out through its parents (a tray's sections are
    // child windows).
    for (const ImGuiMouseButton b : {ImGuiMouseButton_Left, ImGuiMouseButton_Middle}) {
        if (!ImGui::IsMouseClicked(b) || !g.HoveredWindow) continue;
        if (b == ImGuiMouseButton_Left && (g.HoveredId != 0 || g.ActiveId != 0)) continue;
        if (b == ImGuiMouseButton_Middle && g.ActiveId != 0) continue;
        ImGuiWindow* w = g.HoveredWindow;
        while (w && !(w->ScrollMax.y > 0.0f || w->ScrollMax.x > 0.0f) && (w->Flags & ImGuiWindowFlags_ChildWindow)) w = w->ParentWindow;
        if (!w || !(w->ScrollMax.y > 0.0f || w->ScrollMax.x > 0.0f)) continue;
        if (w->ScrollbarY && io.MousePos.x >= w->InnerRect.Max.x) continue; // on its scroll bar: ImGui's own
        // A left-drag on a floating window (or its title bar) moves the window, as ever: only the fixed panels
        // (trays, palettes, shelves) scroll with it. The middle button never moves windows, so it scrolls any.
        if (b == ImGuiMouseButton_Left && (!(w->RootWindow->Flags & ImGuiWindowFlags_NoMove) ||
                                           w->RootWindow->TitleBarRect().Contains(io.MousePos)))
            continue;
        g_dragScrollWindow = w->ID;
        g_dragScrollButton = b;
        g_dragScrollMoved = false;
        ImGui::SetActiveID(w->GetID("##dragscroll"), w);
        return;
    }
}

int DragScrolls() { return g_dragScrolls; }
float DragScrollY() { return g_dragScrollY; }

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
    ImGui::PushStyleColor(ImGuiCol_WindowBg, rgba(lookColor(IM_COL32(0x18, 0x18, 0x1a, 0xff), theme::shellBg), std::clamp(prefs.helpOpacity, 0.3f, 1.0f)));
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
                        lookColor(IM_COL32(0x34, 0x34, 0x38, 0xff), theme::sectionRule));
            ImGui::Dummy(ImVec2(0, 3.0f * s));
        }
        ImGui::PushTextWrapPos(0.0f);
        if (!alternate) {
            ImGui::PushStyleColor(ImGuiCol_Text, rgba(lookColor(IM_COL32(0xc8, 0xc8, 0xc8, 0xff), theme::text)));
            ImGui::TextUnformatted(r.description.empty() ? tr("No description yet.") : r.description.c_str());
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
                ImGui::PushStyleColor(ImGuiCol_Text, rgba(lookColor(IM_COL32(0xc8, 0xc8, 0xc8, 0xff), theme::text)));
                ImGui::TextUnformatted(p.description.c_str());
                ImGui::PopStyleColor();
                ImGui::Unindent(10.0f * s);
            }
        }
        if (r.disabled) ImGui::TextColored(rgba(color::danger), "%s", tr("Not available right now."));
        ImGui::PopTextWrapPos();
        // The user guide chapter about it: F1 (with Ctrl still held) opens it.
        if (const std::string chapter = UserGuide::chapterFor(r.path, r.actionId); !chapter.empty()) {
            const std::string title = app.guide().chapterTitle(chapter);
            if (!title.empty()) {
                ImGui::Dummy(ImVec2(0, 1.0f * s));
                Keycaps("F1");
                ImGui::SameLine();
                ImGui::TextColored(rgba(color::textDim), tr("Learn more: %s"), title.c_str());
                if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) app.guide().open(chapter);
                r.description += "\nLearn more: " + title;
            }
        }
        // Footer: where it lives (dim) and the script command (mono, right).
        if (!r.path.empty() || !r.command.empty()) {
            ImGui::Dummy(ImVec2(0, 2.0f * s));
            ImGui::SetWindowFontScale(0.82f);
            if (!r.path.empty()) ImGui::TextColored(rgba(color::textDim), "%s", trPath(r.path).c_str());
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
