// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// My Panel (Member Request, November 2026): a panel of your own. Hold Alt and drag a button, a section header or (0.14)
// any one control, a checkbox, slider, choice or readout, from any palette into it; it holds that button, that whole
// section (its controls, live) or that one control. Open it in a tray or a drop-down like any palette (and in a window
// of its own, docs/design/detach-panels.md), or float it at the cursor with a key (ui.my_panel). Alt-drag inside it to
// reorder; drag an entry out, or right-click it, to remove it. What it holds is saved with the layout (UiLayout::myPanel).
//
// A control entry ("control:<scope>/<key>") is drawn by drawing its scope's body, the section (or the palette's own
// controls) the same way a section entry is, in a child window cropped to that one control: the body starts above the
// child by the control's measured offset and a clip rect hides its neighbours. The control is the palette's own widget,
// so it cannot differ from it. ui::HelpText / HelpAction, which follow nearly every control, are the marker that names
// it (the English help title, or the action id) and measures it (Ui.cpp pinLastItem).
#include "app/App.h"

#include "app/Demo.h"
#include "app/I18n.h"
#include "app/Palettes.h"
#include "app/Shell.h"
#include "app/Theme.h"
#include "app/Ui.h"
#include "app/UiLayout.h"

#include <IconsLucide.h>

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace sf {

namespace {

constexpr float kHeaderH = 22.0f; // a section's header row, unscaled

bool contains(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

// The palette a section id belongs to ("tool.geometry" -> "tool"), or nullptr.
const PaletteDef* paletteOfSection(const std::string& sectionId) {
    const PaletteDef* def = findPalette(sectionId.substr(0, sectionId.find('.')));
    return def && std::string_view(def->id) != "mypanel" ? def : nullptr;
}

} // namespace

bool App::myPanelAdd(const std::string& entry, int at) {
    const MyPanelEntry e = myPanelEntry(entry);
    if (!e.valid() || entry.size() >= sizeof(ui::MyPanelPayload::entry)) return false;
    if (e.control ? !paletteOfSection(e.scope()) : e.section ? !paletteOfSection(e.id) : !actions_.find(e.id)) return false;
    auto& list = layout_.myPanel;
    if (const auto it = std::find(list.begin(), list.end(), entry); it != list.end()) { // already there: a move
        const int from = int(it - list.begin());
        list.erase(it);
        if (from < at) --at;
    } else if (list.size() >= kMyPanelMax) {
        status_ = "My Panel is full (64 entries): remove one first";
        return false;
    }
    list.insert(list.begin() + std::clamp(at, 0, int(list.size())), entry);
    noteLayoutChanged();
    return true;
}

// One section's controls without its header: the palette body runs with every item skipped (SkipItems) and only the
// section's own body let through (App::subpalette and tray::Section, Palettes.cpp). Every section of a palette is
// drawn by the same code, here and in the palette, so a control cannot differ between the two.
void App::drawMyPanelSection(const std::string& sectionId) { drawMyPanelScope(sectionId); }

// A scope is a section id ("tool.display") or a palette id ("draw"): the palette's own controls outside every section
// (subpalette() then keeps every section shut).
void App::drawMyPanelScope(const std::string& scope) {
    const PaletteDef* def = paletteOfSection(scope);
    if (!def) return;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const bool wasSkipping = window->SkipItems;
    const bool paletteLevel = scope.find('.') == std::string::npos;
    myPanelSection_ = scope;
    myPanelPaletteLevel_ = paletteLevel;
    tray::scopeSkip(!paletteLevel);
    ImGui::PushID(scope.c_str());
    ui::PinPalette(def->id); // its controls are My Panel entries (Ui.h), here as in the palette
    const bool readOnly = ui::BeginPaletteItems();
    (this->*(def->draw))();
    ui::EndPaletteItems(readOnly);
    ui::PinPalette("");
    ImGui::PopID();
    tray::scopeSkipEnd();
    window->SkipItems = wasSkipping;
    myPanelSection_.clear();
    myPanelPaletteLevel_ = false;
}

// The words of a control entry when the control itself cannot be shown: its help title (English, translated here), or
// the action's label; a repeat's "#2" dropped.
static std::string controlWords(App& app, const MyPanelEntry& e) {
    std::string key = e.key();
    if (const size_t hash = key.rfind('#'); hash != std::string::npos && hash > 0 && key.find_first_not_of("0123456789", hash + 1) == std::string::npos)
        key.resize(hash);
    if (const Action* a = app.actions().find(key)) return trAction(a->id, a->label);
    return tr(key.c_str());
}

std::string App::shelfItemForEntry(const std::string& entry) const {
    const MyPanelEntry e = myPanelEntry(entry);
    if (!e.valid() || e.section) return {};
    if (e.control) return paletteOfSection(e.scope()) ? entry : std::string();
    return actions_.find(e.id) ? e.id : std::string();
}

bool App::drawPinnedControl(const std::string& entry, const MyPanelEntry& e, int host, float layoutWidth, bool fit, float rowH,
                            ImRect& rect, bool& remove) {
    const std::string scope = e.scope(), key = e.key();
    if (!paletteOfSection(scope)) return false;
    const float s = uiScale_;
    const bool panel = host == ui::HostMyPanel;
    const float width = std::max(layoutWidth, 1.0f);
    PinnedControlPlace& place = pinnedControls_[pinnedPlaceKey(host, entry)];
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav; // (nav would scroll it to a hidden neighbour)
    ImGui::PushID(entry.c_str());
    // The first time, and after it went missing (then twice a second, not every frame): lay the body out unseen and find the control.
    if (!place.known && !(place.missing && ImGui::GetTime() - place.measured < 0.5)) {
        const ImVec2 start = ImGui::GetCursorScreenPos();
        place.missing = true;
        place.measured = ImGui::GetTime();
        if (ImGui::BeginChild(("##pinned.measure." + entry).c_str(), ImVec2(width, 1.0f), ImGuiChildFlags_None,
                              flags | ImGuiWindowFlags_NoInputs)) {
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            ui::BeginPinnedControl(scope, key, true, host);
            drawMyPanelScope(scope);
            const ui::PinnedControl r = ui::EndPinnedControl();
            if (r.found) {
                place.known = true;
                place.missing = false;
                place.x = r.min.x - origin.x;
                place.y = r.min.y - origin.y;
                place.w = r.max.x - r.min.x;
                place.h = r.max.y - r.min.y;
                place.lineX = r.lineX - origin.x;
            }
        }
        ImGui::EndChild();
        ImGui::SetCursorScreenPos(start); // (the 1 px child took no room)
    }
    if (!place.known || place.h < 1.0f || place.w < 1.0f) { // hidden in this state (a readout of levels the Part has none of)
        if (!panel && !layout_.customize) { // a shelf: a contextual item, not shown (from the next frame on, not laid out either)
            ImGui::Dummy(ImVec2(0, 0));
            rect = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            ImGui::PopID();
            return true;
        }
        const std::string words = controlWords(*this, e);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = std::max(ImGui::GetFrameHeight(), rowH);
        const float w = panel ? width : std::min(width, ImGui::CalcTextSize(words.c_str()).x + 16.0f * s);
        ImGui::InvisibleButton("##pinned.control.missing", ImVec2(w, h));
        rect = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        ui::MyPanelDragEntry(entry, words.c_str(), panel);
        if (ImGui::BeginPopupContextItem("##pinned.ctx")) {
            if (ImGui::MenuItem(tr(panel ? "Remove from My Panel" : "Remove from shelf"))) remove = true;
            ImGui::EndPopup();
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float ty = std::floor(p.y + (h - ImGui::GetFontSize()) * 0.5f);
        dl->AddText(ImVec2(p.x + 8.0f * s, ty), theme::u32(theme::textDim, 0.7f), words.c_str());
        if (panel) {
            const std::string note = tr("(not shown now)");
            const float nw = ImGui::CalcTextSize(note.c_str()).x;
            dl->AddText(ImVec2(std::max(p.x + 8.0f * s + ImGui::CalcTextSize(words.c_str()).x + 8.0f * s, p.x + width - nw), ty),
                        theme::u32(theme::textDim, 0.5f), note.c_str());
        }
        ui::DelayedTooltip(tr("Its palette does not show this control right now (it depends on the Part or the mode). It comes back when it does."));
        ImGui::PopID();
        return true;
    }
    // The control, live: the body drawn so the control lands at the top of a child as tall as it, cropped to its width.
    // In My Panel one from the middle of a row ("Mask" between Wireframe and Groups) is moved left to where its row starts;
    // on a shelf every control sits flush left. (Every line of the body starts at the child's left edge, so the move left is
    // the child's scroll: its content is told to be that much wider and it is scrolled by as much, every frame; no scrollbar,
    // no wheel. The content width is set every time, so the body is laid out at `width` however wide the item is.)
    const float shift = panel ? std::max(0.0f, place.x - place.lineX) : std::max(0.0f, place.x);
    const float itemW = fit ? std::clamp(place.w + 2.0f * s, 1.0f, width) : width;
    const float itemH = std::max(place.h, rowH);
    const ImVec2 start = ImGui::GetCursorScreenPos();
    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(itemW, itemH)); // the item's room; the child sits in it, centred
    const ImVec2 childMin(start.x, std::floor(start.y + (itemH - place.h) * 0.5f));
    ImGui::SetCursorScreenPos(childMin);
    ImGui::SetNextWindowContentSize(ImVec2(width + shift, 0.0f));
    ImGui::SetNextWindowScroll(ImVec2(shift, 0.0f)); // (every frame: a row that stops being one keeps no stale scroll)
    ui::PinnedControl r;
    // A shelf in Customize Shelves with Ctrl+Alt held: the control is grabbed through a button the shelf lays over it (Shell.cpp
    // drawShelfItem), so its child takes no input, or the pointer would hover the child and the shelf's press never lands.
    const bool inert = !panel && layout_.customize && ui::CtrlHeld() && ui::AltHeld();
    const bool drawn = ImGui::BeginChild(("##pinned.control." + entry).c_str(), ImVec2(itemW, place.h), ImGuiChildFlags_None,
                                         flags | (inert ? ImGuiWindowFlags_NoInputs : 0));
    if (drawn) {
        const ImVec2 origin(childMin.x - shift, childMin.y - place.y);
        const float left = childMin.x + place.x - shift;
        ImGui::PushClipRect(ImVec2(left - 1.0f, childMin.y), ImVec2(left + place.w + 1.0f, childMin.y + place.h), true);
        ImGui::SetCursorScreenPos(origin);
        ui::BeginPinnedControl(scope, key, false, host);
        drawMyPanelScope(scope);
        r = ui::EndPinnedControl();
        ImGui::PopClipRect();
        if (r.found) { // where it is now, for the next frame (a readout grows, a row above it appears)
            place.x = r.min.x - origin.x;
            place.y = r.min.y - origin.y;
            place.w = r.max.x - r.min.x;
            place.h = r.max.y - r.min.y;
            place.lineX = r.lineX - origin.x;
        }
        // A control wider than its host (a long checkbox label on a side shelf, or in a narrow tray): the cut is visible, "..."
        // over its last letters in the host's background, with the full name on hover or Ctrl help as before (cto 11-24: a
        // silent cut reads as a different, wrong label).
        if (r.found && r.max.x > childMin.x + itemW + 1.0f && itemW > 40.0f * s) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const char* dots = "...";
            const float dw = ImGui::CalcTextSize(dots).x, right = childMin.x + itemW, x0 = right - dw - 1.0f * s;
            dl->AddRectFilled(ImVec2(x0 - 2.0f * s, childMin.y), ImVec2(right, childMin.y + place.h),
                              panel ? ImGui::GetColorU32(ImGuiCol_WindowBg) : theme::u32(theme::shellBg));
            dl->AddText(ImVec2(x0, std::floor(childMin.y + (place.h - ImGui::GetFontSize()) * 0.5f)), ImGui::GetColorU32(ImGuiCol_Text), dots);
        }
    }
    ImGui::EndChild();
    ImGui::EndGroup();
    rect = ImRect(start, ImVec2(start.x + itemW, start.y + itemH));
    if (drawn && !r.found) place.known = false; // gone this frame: measured again next frame, the placeholder meanwhile
    if (r.remove) remove = true;
    ImGui::PopID();
    return true;
}

void App::drawPalette_MyPanel() {
    const float s = uiScale_;
    auto& entries = layout_.myPanel;
    ImGui::PushID("mypanel");
    const ImVec2 areaMin = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    std::vector<ImRect> rects(entries.size()); // each entry's first item (a section: its header), for where a drop lands
    std::vector<bool> isSection(entries.size(), false); // ... a row of its own (a section's header, a control)
    std::string remove;                         // an entry the member removed this frame
    bool prevButton = false;                    // the last entry was a button: the next one may share its line
    float lineRight = 0.0f;                     // where that line ends (screen x)
    for (size_t i = 0; i < entries.size(); ++i) {
        const MyPanelEntry e = myPanelEntry(entries[i]);
        if (e.control) {
            bool removeThis = false;
            if (!drawPinnedControl(entries[i], e, ui::HostMyPanel, width, false, 0.0f, rects[i], removeThis)) continue; // a palette this version no longer has
            prevButton = false;
            isSection[i] = true;
            if (removeThis) remove = entries[i];
            continue;
        }
        if (e.section) {
            const PaletteDef* def = paletteOfSection(e.id);
            if (!def) continue; // a section this version no longer has: skipped, kept (the file may be from a newer one)
            prevButton = false;
            isSection[i] = true;
            const bool folded = contains(myPanelFolded_, e.id);
            if (!myPanelTitles_.count(e.id)) { // the first time: draw nothing, only learn its title
                myPanelProbe_ = true;
                drawMyPanelSection(e.id);
                myPanelProbe_ = false;
            }
            const auto title = myPanelTitles_.find(e.id);
            const std::string name = title == myPanelTitles_.end() ? e.id : tr(title->second.c_str());
            // The header: a quiet row like a palette's, title at the left, chevron at the right. Click folds it here only.
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x, h = kHeaderH * s;
            bool clicked = ImGui::InvisibleButton((name + "##mypanel.section." + e.id).c_str(), ImVec2(w, h));
            const bool hovered = ImGui::IsItemHovered();
            rects[i] = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            ui::MyPanelDragSource(e.id.c_str(), true, name.c_str(), true);
            if (clicked && ui::MyPanelDragged()) clicked = false;
            if (ImGui::BeginPopupContextItem("##mypanel.ctx")) {
                if (ImGui::MenuItem(tr("Remove from My Panel"))) remove = entries[i];
                ImGui::EndPopup();
            }
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), theme::u32(hovered ? theme::headerHover : theme::header, folded ? 0.6f : 0.9f),
                              theme::rounding(2.0f * s));
            dl->AddLine(ImVec2(p.x, p.y + h - 0.5f * s), ImVec2(p.x + w, p.y + h - 0.5f * s), theme::u32(theme::sectionRule), 1.0f * s);
            const char* chevron = folded ? ICON_LC_CHEVRON_RIGHT : ICON_LC_CHEVRON_DOWN;
            const float cs = ImGui::GetFontSize() * 0.8f;
            const ImVec2 cz = ImGui::GetFont()->CalcTextSizeA(cs, FLT_MAX, 0.0f, chevron);
            dl->AddText(ImGui::GetFont(), cs, ImVec2(std::floor(p.x + w - h * 0.5f - cz.x * 0.5f), std::floor(p.y + (h - cz.y) * 0.5f)),
                        theme::u32(hovered ? theme::text : theme::textDim), chevron);
            dl->AddText(ImVec2(p.x + 8.0f * s, std::floor(p.y + (h - ImGui::GetFontSize()) * 0.5f)),
                        theme::u32(theme::text, hovered ? 1.0f : 0.85f), name.c_str());
            if (clicked) {
                if (folded) myPanelFolded_.erase(std::remove(myPanelFolded_.begin(), myPanelFolded_.end(), e.id), myPanelFolded_.end());
                else myPanelFolded_.push_back(e.id);
            }
            ImGui::Dummy(ImVec2(0, 2.0f * s));
            if (!folded) drawMyPanelSection(e.id);
            continue;
        }
        const Action* a = actions_.find(e.id);
        if (!a) continue; // an action this version no longer has
        const char* icon = a->icon;
        const std::string label = trAction(a->id, a->label);
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float natural = ImGui::CalcTextSize(((icon ? std::string(icon) + "  " : std::string()) + label).c_str()).x +
                              2.0f * ImGui::GetStyle().FramePadding.x;
        const float bw = std::min(natural, width);
        if (prevButton && lineRight + spacing + bw <= areaMin.x + width + 0.5f) ImGui::SameLine();
        ui::ActionButton(*this, e.id.c_str(), ImVec2(bw, 0), icon ? ui::Look::IconLabel : ui::Look::Label);
        rects[i] = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        lineRight = ImGui::GetItemRectMax().x;
        prevButton = true;
        if (ImGui::BeginPopupContextItem("##mypanel.ctx")) {
            if (ImGui::MenuItem(tr("Remove from My Panel"))) remove = entries[i];
            ImGui::EndPopup();
        }
    }
    if (!remove.empty()) {
        entries.erase(std::remove(entries.begin(), entries.end(), remove), entries.end());
        noteLayoutChanged();
    }
    // Words for an empty panel, and a reminder under a full one.
    {
        ImGui::Dummy(ImVec2(0, 4.0f * s));
        if (smallFont()) ImGui::PushFont(smallFont());
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", tr(entries.empty() ? "Hold Alt and drag a button, a slider, a checkbox or a section's title from any palette "
                                                       "into this panel. Right-click an entry to remove it."
                                                     : "Alt+drag to add or move. Right-click an entry to remove it."));
        ImGui::PopStyleColor();
        if (smallFont()) ImGui::PopFont();
    }
    const ImVec2 end = ImGui::GetCursorScreenPos();
    // (The floating panel sizes itself to this content, so the area must not reach for the window's edge: it would never shrink.)
    const ImRect area(areaMin, ImVec2(areaMin.x + width, std::max(end.y, areaMin.y + 96.0f * s)));

    // The drop: an accent line shows where the entry lands.
    const ImGuiPayload* payload = ImGui::GetDragDropPayload();
    if (payload && payload->IsDataType(ui::kMyPanelPayload) && area.Contains(ImGui::GetIO().MousePos) &&
        ImGui::BeginDragDropTargetCustom(area, ImGui::GetID("##mypanel.drop"))) {
        const ImVec2 m = ImGui::GetIO().MousePos;
        int insert = int(entries.size());
        ImVec2 a(area.Min.x, end.y), b(area.Max.x, end.y);
        for (size_t k = 0; k < entries.size(); ++k) {
            const ImRect& rc = rects[k];
            if (rc.Max.x <= rc.Min.x) continue;
            const bool before = isSection[k] ? m.y < rc.GetCenter().y
                                              : (m.y < rc.Min.y || (m.y < rc.Max.y && m.x < rc.GetCenter().x));
            if (!before) continue;
            insert = int(k);
            a = isSection[k] || m.y < rc.Min.y ? ImVec2(rc.Min.x, rc.Min.y - 2.0f * s) : ImVec2(rc.Min.x - 2.0f * s, rc.Min.y);
            b = isSection[k] || m.y < rc.Min.y ? ImVec2(rc.Max.x, rc.Min.y - 2.0f * s) : ImVec2(rc.Min.x - 2.0f * s, rc.Max.y);
            break;
        }
        ImGui::GetForegroundDrawList()->AddLine(a, b, theme::u32(theme::accent), 2.0f * s);
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(ui::kMyPanelPayload, ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
            ui::MyPanelPayload item;
            std::memcpy(&item, p->Data, std::min(size_t(p->DataSize), sizeof item));
            item.entry[sizeof item.entry - 1] = 0;
            if (myPanelAdd(item.entry, insert)) status_ = "Added to My Panel";
            myPanelDrag_.accepted = true;
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::Dummy(ImVec2(0, std::max(0.0f, area.Max.y - end.y)));
    ImGui::PopID();
}

// A My Panel in a window of its own is another ImGui context: it never sees a drag that began in the main window (the
// press holds the mouse there, so it gets no motion either). The release is the main window's: if the pointer, mouse or
// pen, is over that window then, the entry lands at the end of the panel.
bool App::droppedOnMyPanelWindow() {
    if (!window_) return false;
    int wx = 0, wy = 0;
    SDL_GetWindowPosition(window_, &wx, &wy);
    const ImVec2 m = ImGui::GetIO().MousePos;
    const float gx = float(wx) + m.x, gy = float(wy) + m.y; // the main window's coordinates, on the desktop
    for (const PanelWindow& p : panelWindows_) {
        if (p.id != "palette.mypanel" || !p.window || !p.shown) continue;
        int px = 0, py = 0, pw = 0, ph = 0;
        SDL_GetWindowPosition(p.window, &px, &py);
        SDL_GetWindowSize(p.window, &pw, &ph);
        if (gx >= float(px) && gx < float(px + pw) && gy >= float(py) && gy < float(py + ph)) return true;
    }
    return false;
}

// The end of a drag: an entry dragged out of My Panel and dropped anywhere but on a My Panel is removed (like a shelf
// item). Called once a frame after every window, with the shelves' own finishShelfDrag.
void App::finishMyPanelDrag() {
    const ImGuiPayload* payload = ImGui::GetDragDropPayload();
    if (payload && payload->IsDataType(ui::kMyPanelPayload)) {
        ui::MyPanelPayload item;
        std::memcpy(&item, payload->Data, std::min(size_t(payload->DataSize), sizeof item));
        item.entry[sizeof item.entry - 1] = 0;
        if (!myPanelDrag_.active) myPanelDrag_.accepted = false; // a new drag
        myPanelDrag_.active = true;
        myPanelDrag_.fromPanel = item.fromPanel;
        myPanelDrag_.entry = item.entry;
        return;
    }
    if (!myPanelDrag_.active) return;
    if (!myPanelDrag_.accepted && !myPanelDrag_.fromPanel && droppedOnMyPanelWindow() && myPanelAdd(myPanelDrag_.entry, int(layout_.myPanel.size())))
        status_ = "Added to My Panel";
    if (!myPanelDrag_.accepted && myPanelDrag_.fromPanel) {
        auto& list = layout_.myPanel;
        if (const auto it = std::find(list.begin(), list.end(), myPanelDrag_.entry); it != list.end()) {
            list.erase(it);
            status_ = "Removed from My Panel";
            noteLayoutChanged();
        }
    }
    myPanelDrag_ = {};
}

// The floating My Panel: opens at the cursor on its key, closes on the key, Esc or its close button.
void App::drawMyPanelFloating() {
    if (!myPanelFloating_) return;
    const float s = uiScale_;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    // As tall as what it holds (a few entries: a few rows, not a tall empty window over the model), up to 70 % of the
    // view, then it scrolls; the width stays a palette's.
    const float w = 300.0f * s, hMax = std::min(460.0f * s, vp->Size.y * 0.7f);
    ImGui::SetNextWindowSizeConstraints(ImVec2(w, 0.0f), ImVec2(w, hMax));
    if (myPanelPlace_) { // just opened: the pointer is over its title bar, so the first thing under it is the first entry
        float h = hMax; // (its height is its content's: the last size it had is the best guess for keeping it in view)
        if (const ImGuiWindow* last = ImGui::FindWindowByName("###mypanel.floating")) h = std::min(last->Size.y, hMax);
        ImGui::SetNextWindowPos(ImVec2(std::clamp(mouseX_ - w * 0.5f, vp->Pos.x, vp->Pos.x + std::max(0.0f, vp->Size.x - w)),
                                       std::clamp(mouseY_ - 14.0f * s, vp->Pos.y, vp->Pos.y + std::max(0.0f, vp->Size.y - h))),
                                ImGuiCond_Always);
        ImGui::SetNextWindowFocus();
        myPanelPlace_ = false;
    }
    bool open = true;
    if (ImGui::Begin((std::string(tr("My Panel")) + "###mypanel.floating").c_str(), &open,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
        Demo::pushScope("My Panel");
        drawPalette_MyPanel();
        Demo::popScope();
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsAnyItemActive() &&
            ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            open = false;
    }
    ImGui::End();
    if (!open) myPanelFloating_ = false;
}

void App::registerMyPanelActions() {
    Action a;
    a.id = "ui.my_panel";
    a.label = "My Panel";
    a.icon = ICON_LC_STAR;
    a.palette = "Edit > My Panel";
    a.shortcut = "F4";
    a.description = "Your own panel, floating at the cursor: the buttons, controls and sections you dragged into it (hold Alt and "
                    "drag from any palette). Press the key again, or Esc, to close it. It is also a palette (Edit > My Panel) for "
                    "a tray, and can be moved to a window of its own.";
    a.run = [this] {
        myPanelFloating_ = !myPanelFloating_;
        myPanelPlace_ = myPanelFloating_;
    };
    a.checked = [this] { return myPanelFloating_; };
    actions_.add(std::move(a));
}

} // namespace sf
