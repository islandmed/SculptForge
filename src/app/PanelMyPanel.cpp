// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// My Panel (Member Request, November 2026): a panel of your own. Hold Alt and drag a button or a section header from
// any palette into it; it holds that button, or that whole section (its controls, live). Open it in a tray or a
// drop-down like any palette (and in a window of its own, docs/design/detach-panels.md), or float it at the cursor
// with a key (ui.my_panel). Alt-drag inside it to reorder; drag an entry out, or right-click it, to remove it.
// What it holds is saved with the layout (UiLayout::myPanel). docs/design/my-panel.md.
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
    if (e.section ? !paletteOfSection(e.id) : !actions_.find(e.id)) return false;
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
void App::drawMyPanelSection(const std::string& sectionId) {
    const PaletteDef* def = paletteOfSection(sectionId);
    if (!def) return;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const bool wasSkipping = window->SkipItems;
    myPanelSection_ = sectionId;
    window->SkipItems = true;
    ImGui::PushID(sectionId.c_str());
    (this->*(def->draw))();
    ImGui::PopID();
    window->SkipItems = wasSkipping;
    myPanelSection_.clear();
}

void App::drawPalette_MyPanel() {
    const float s = uiScale_;
    auto& entries = layout_.myPanel;
    ImGui::PushID("mypanel");
    const ImVec2 areaMin = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    std::vector<ImRect> rects(entries.size()); // each entry's first item (a section: its header), for where a drop lands
    std::vector<bool> isSection(entries.size(), false);
    std::string remove;                         // an entry the member removed this frame
    bool prevButton = false;                    // the last entry was a button: the next one may share its line
    float lineRight = 0.0f;                     // where that line ends (screen x)
    for (size_t i = 0; i < entries.size(); ++i) {
        const MyPanelEntry e = myPanelEntry(entries[i]);
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
        ImGui::TextWrapped("%s", tr(entries.empty() ? "Hold Alt and drag a button, or a section's title, from any palette into this panel. "
                                                       "Right-click an entry to remove it."
                                                     : "Alt+drag to add or move. Right-click an entry to remove it."));
        ImGui::PopStyleColor();
        if (smallFont()) ImGui::PopFont();
    }
    const ImVec2 end = ImGui::GetCursorScreenPos();
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
    if (myPanelPlace_) { // just opened: the pointer is over its title bar, so the first thing under it is the first entry
        const float w = 300.0f * s, h = std::min(460.0f * s, vp->Size.y * 0.7f);
        ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImVec2(std::clamp(mouseX_ - w * 0.5f, vp->Pos.x, vp->Pos.x + std::max(0.0f, vp->Size.x - w)),
                                       std::clamp(mouseY_ - 14.0f * s, vp->Pos.y, vp->Pos.y + std::max(0.0f, vp->Size.y - h))),
                                ImGuiCond_Always);
        ImGui::SetNextWindowFocus();
        myPanelPlace_ = false;
    }
    bool open = true;
    if (ImGui::Begin((std::string(tr("My Panel")) + "###mypanel.floating").c_str(), &open, ImGuiWindowFlags_NoCollapse)) {
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
    a.description = "Your own panel, floating at the cursor: the buttons and sections you dragged into it (hold Alt and drag "
                    "from any palette). Press the key again, or Esc, to close it. It is also a palette (Edit > My Panel) for "
                    "a tray, and can be moved to a window of its own.";
    a.run = [this] {
        myPanelFloating_ = !myPanelFloating_;
        myPanelPlace_ = myPanelFloating_;
    };
    a.checked = [this] { return myPanelFloating_; };
    actions_.add(std::move(a));
}

} // namespace sf
